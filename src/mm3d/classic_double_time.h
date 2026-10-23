// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>
#include "mm3d/classic_double_time_text.h"
#include "mm3d/classic_localized_text.h"
#include "mm3d/translation.h"

namespace MM3D::ClassicDoubleTime {
using U32=std::uint32_t;
inline std::atomic_bool enabled{false};
constexpr U32 FirstSVC=0xfffa80,Common=0x7751d8;
constexpr std::array<std::array<U32,2>,4> Sites{{
    {0x1d824c,0xe59f1180}, // Song accepted, after native restrictions.
    {0x1d811c,0xe3a00012}, // Native question has opened.
    {0x6042c0,0xe1d016b6}, // Native two-choice confirmation, after input acceptance.
    {0x604520,0xe5950010}, // After native Yes feedback, before song effect.
}};
constexpr std::array<std::array<U32,3>,5> Signatures{{
    {0x1d8104,0x2c,0x1e375d0a}, {0x1d824c,0x13c,0xfc2f58d2},
    {0x604290,0x74,0x721cbf8e}, {0x604510,0xa0,0x9b2a0cb9},
    {0x5b5da0,0x54,0x7d0e2370},
}};
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<Sites.size();++i)
        if(pc==Sites[i][0]&&word==Sites[i][1])return 0xef000000|FirstSVC+i;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for(auto s:Signatures) {
        U32 hash=2166136261;
        for(U32 n=0;n<s[1];n+=4) {
            auto w=m.Read(s[0]+n);if(!w)return false;
            U32 word=*w;
            for(unsigned i=0;i<Sites.size();++i)
                if(s[0]+n==Sites[i][0]&&word==(0xef000000|FirstSVC+i))word=Sites[i][1];
            for(unsigned b=0;b<4;++b)hash=(hash^((word>>(b*8))&255))*16777619;
        }
        if(hash!=s[2])return false;
    }
    return true;
}
struct Destination {unsigned question;U32 time;bool dawn;};
inline std::optional<Destination> Next(unsigned day,U32 time) {
    if(day<1||day>3||time>65535)return {};
    // The cycle day changes at dawn, not at midnight.
    const bool night=time<0x4000||time>=0xc000;
    if(night&&day==3)return {};
    return Destination{(day-1)*2+unsigned(night),night?0x4000u:0xc000u,night};
}
template<class M> void Half(M& m,U32 at,U32 value) {
    if(auto w=m.Read(at&~3u))m.Write(at&~3u,(*w&~(65535u<<((at&2)*8)))|((value&65535)<<((at&2)*8)));
}
template<class M> void Byte(M& m,U32 at,U32 value) {
    if(auto w=m.Read(at&~3u))m.Write(at&~3u,(*w&~(255u<<((at&3)*8)))|((value&255)<<((at&3)*8)));
}
inline bool Pointer(U32 at,U32 size=4){return at>=0x100000&&size<=0x1000000&&at<=0x40000000-size;}
// In-place, same-size substitution preserves native message attributes and
// choice layout. Recognize every owned variant so the next Song of Time lookup
// restores its original question, even after toggling the option off.
template<class M> bool Text(M& m,U32 self,int question) {
    if (Translation::Owns(m, self))
        return question < int(Questions.size()) && Translation::Apply(m, self, 0x1b8a,
            Translation::Kind::Song, question < 0 ? 0 : unsigned(question + 1));
    if(!Pointer(self,20)||question>=int(Questions.size()))return false;
    auto base=m.Read(self+4),entries=m.Read(self+8),size=m.Read(self+12);
    if(!base||!entries||!size||m.Read(self+16)!=20||!Pointer(*base,*size)||*size<16||
       *entries!=*base+16||m.Read(*base)!=0x47534d47||m.Read(*base+4)!=1||!m.Read(*base+12))return false;
    auto original=OriginalText;
    const auto* questions=&Questions;
    const auto language=*m.Read(*base+12);
    if(language!=2) {
        const ClassicLocalizedText::Dialogue* dialogue=nullptr;
        for(const auto& d:ClassicLocalizedText::Dialogues)if(d.language==language)dialogue=&d;
        if(!dialogue)return false;
        original=dialogue->song;questions=&dialogue->questions;
    }
    auto count=m.Read(*base+8);
    if(!count||!*count||*count>20000||16+*count*20>*size)return false;
    U32 lo=0,hi=*count;
    while(lo<hi) {
        const U32 mid=lo+(hi-lo)/2,entry=*entries+mid*20;
        auto key=m.Read(entry);if(!key)return false;
        if((*key&65535)<0x1b8a){lo=mid+1;continue;}
        if((*key&65535)>0x1b8a){hi=mid;continue;}
        auto offset=m.Read(entry+12),length=m.Read(entry+16);
        if(!offset||!length||*length!=original.size()||m.Read(entry+4)!=0x3fffffff||
           (*offset&1)||*offset<16+*count*20||*offset>*size||*length>*size-*offset)return false;
        const U32 start=*base+*offset,end=start+*length;
        const auto desired=question<0?original:(*questions)[question];
        std::vector<char> current(*length);
        struct Write{U32 at,value;};std::vector<Write> writes;
        for(U32 at=start&~3u;at<end;at+=4) {
            auto w=m.Read(at);if(!w)return false;U32 value=*w;
            for(unsigned b=0;b<4;++b)if(at+b>=start&&at+b<end) {
                const auto i=at+b-start;current[i]=char(*w>>(b*8));
                value=(value&~(255u<<(b*8)))|(U32(static_cast<unsigned char>(desired[i]))<<(b*8));
            }
            writes.push_back({at,value});
        }
        const std::string_view old(current.data(),current.size());
        bool known=old==original;
        for(auto text:*questions)known|=old==text;
        if(!known)return false;
        for(auto w:writes)m.Write(w.at,w.value);
        return true;
    }
    return false;
}
struct Hook {
    U32 message_data=0,window=0,play=0;
    std::optional<Destination> target;
    bool opening=false;
    void Reset(){window=play=0;target.reset();opening=false;}
    // Native MessageWindow::Open stores the active ID at +3f4; +3fc is
    // the pending next message and is normally -1 during a choice prompt.
    template<class M> bool Owns(M& m,U32 w) const {
        return target&&w==window&&m.Read(w+0x444)==play&&
            m.Read(w+0x3f4)==0x1b8a&&m.Read(play+0x13c)&&
            (*m.Read(play+0x13c)&65535)==0x402;
    }
    template<class M> void Message(M& m,U32 self,U32 id) {
        message_data=self;
        if(id==0x1b8a)Text(m,self,target&&(opening||Owns(m,window))?int(target->question):-1);
    }
    template<class M,class R> void Handle(M& m,R& r,unsigned site) {
        switch(site) {
        case 0: {
            r[1]=0x66666667; // displaced LDR
            Reset();
            if(!enabled)return;
            auto p=m.Read(r[4]+0x444),day=m.Read(Common+0x14),time=m.Read(Common+0x20);
            if(!p||!day||!time||*day<1||*day>3||
               !m.Read(*p+0x13c)||(*m.Read(*p+0x13c)&65535)!=0x402)return;
            auto next=Next(*day,*time&65535);
            if(!next){r[15]=0x1d82f8;return;} // Native 1b94 message + ocarina cleanup.
            if(!Text(m,message_data,int(next->question)))return;
            window=r[4];play=*p;target=next;opening=true;
            m.Write(window+0x428,0xff);
            r[0]=play;r[1]=0x1b8a;r[2]=0;r[15]=0x1d8114;
            break;
        }
        case 1:
            r[0]=opening?0x14:0x12;
            opening=false;
            break;
        case 2: {
            const auto state=m.Read(r[0]+0x64);
            r[1]=state?(*state>>16):0; // displaced LDRH
            if(!Owns(m,r[4])||r[1]!=0x14)return;
            // Reuse native inverted-song Yes/No cleanup, but never its speed
            // writes. The following Yes hook redirects into the time jump.
            if(m.Read(window+0x430)!=0) {Reset();r[15]=0x604560;}
            else r[15]=0x60451c;
            break;
        }
        case 3:
            r[0]=m.Read(r[5]+0x10).value_or(0);
            if(!Owns(m,r[4]))return;
            // Same state writes as the modern selector's confirm branch.
            // Native JumpingForwardInTime performs the actual clock/day change.
            if(target->dawn)Byte(m,0x7781d8+0x6bf,1);
            Half(m,Common+0x36a2,m.Read(Common+0x20).value_or(0));
            Half(m,Common+0x140f2,target->time);
            Half(m,play+0x8366,0x1a);
            Byte(m,0x7781d8+0x543,0);
            if(auto main=m.Read(0x656b04);main&&Pointer(*main,0x1f0))Byte(m,*main+0x1ef,0);
            r[15]=0x6045a8;Reset();
            break;
        }
    }
};
}
