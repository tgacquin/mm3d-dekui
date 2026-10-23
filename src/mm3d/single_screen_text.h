// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <optional>
#include <atomic>
#include <vector>
#include "mm3d/single_screen_text_data.h"
#include "mm3d/stick_controls.h"
#include "mm3d/single_screen_localized_data.h"
#include "mm3d/classic_owl_text_data.h"
#include "mm3d/classic_saving.h"
#include "mm3d/classic_owl_return_text.h"
#include "mm3d/classic_localized_text.h"
#include "mm3d/translation.h"

namespace MM3D::SingleScreenText {
inline std::atomic_bool enabled{true};
using U32=std::uint32_t;
constexpr U32 PC=0x7d1498,Word=0xe5903004,SVC=0xfffe90;
inline U32 Instruction(U32 pc,U32 word){return pc==PC&&word==Word?0xef000000|SVC:word;}
template<class M> bool CodeMatches(M& m) {
    // Restoration's MessageData::Get implementation; retain the outer Odolwa
    // message remap and the original lookup, attributes and language handling.
    constexpr std::array<U32,8> words{Word,0xe92d47f0,0xe593e008,0xe25ee001,
        0x4a000051,0xe1a06000,0xe3a04000,0xe5905008};
    for(unsigned i=0;i<words.size();++i)if(m.Read(PC+i*4)!=words[i])return false;
    return m.Read(0x7d07a4)==0xea00033bu&&m.Read(0x7d079c)==0xe1510003u;
}
inline bool Pointer(U32 p,U32 n=4){return p>=0x100000&&n<=0x1000000&&p<=0x40000000-n;}
template<class M> bool Apply(M& m,U32 self,U32 id) {
    if (Translation::Owns(m, self)) {
        if (id == 0xc01)
            return Translation::Apply(m, self, id, Translation::Kind::Owl,
                ClassicSaving::resume_text_active.load() ? 2 : ClassicSaving::enabled.load() ? 1 : 0);
        return Translation::Apply(m, self, id, Translation::Kind::Controls,
            enabled.load() ? (StickControls::enabled.load() ? 2 : 1) : 0);
    }
    bool active=enabled.load();
    bool return_notice=false;
    Replacement localized{};
    auto return_text=ClassicOwlReturnText;
    auto return_hash=ClassicOwlReturnHash;
    const Replacement* replacement=nullptr;
    for(const auto& r:Replacements)if(r.id==id){replacement=&r;break;}
    if(id==0x0c01) {
        replacement=&ClassicOwlReplacements[0];
        return_notice=ClassicSaving::resume_text_active.load();
        active=ClassicSaving::enabled.load();
    }
    if(!replacement||!Pointer(self,20))return false;
    auto base=m.Read(self+4),entries=m.Read(self+8),size=m.Read(self+12),stride=m.Read(self+16);
    if(!base||!entries||!size||stride!=20||!Pointer(*base,16)||
       *size<16||*size>0x1000000||!Pointer(*base,*size)||*entries!=*base+16||
       m.Read(*base)!=0x47534d47u||m.Read(*base+4)!=1||!m.Read(*base+12))return false;
    const auto language=*m.Read(*base+12);
    if(language!=2) {
        if(id==0xc01) {
            const ClassicLocalizedText::Dialogue* dialogue=nullptr;
            for(const auto& d:ClassicLocalizedText::Dialogues)if(d.language==language)dialogue=&d;
            if(!dialogue)return false;
            localized={0xc01,U32(dialogue->original.size()),dialogue->original_hash,
                dialogue->classic,dialogue->original,dialogue->classic_hash};
            replacement=&localized;return_text=dialogue->returned;return_hash=dialogue->return_hash;
        } else {
            replacement=nullptr;
            if(language==4)for(const auto& r:FrenchReplacements)if(r.id==id){replacement=&r;break;}
            if(language==8)for(const auto& r:SpanishReplacements)if(r.id==id){replacement=&r;break;}
            if(!replacement)return false;
        }
    }
    const Replacement* stick=nullptr;
    if(language==2)for(const auto& r:StickReplacements)if(r.id==id)stick=&r;
    if(language==4)for(const auto& r:StickFrenchReplacements)if(r.id==id)stick=&r;
    if(language==8)for(const auto& r:StickSpanishReplacements)if(r.id==id)stick=&r;
    if(stick&&(stick->size!=replacement->size||stick->hash!=replacement->hash))return false;
    const auto* selected=stick&&StickControls::enabled.load()?stick:replacement;
    auto count=m.Read(*base+8);
    if(!count||!*count||*count>20000||16+*count*20>*size)return false;
    U32 lo=0,hi=*count;
    while(lo<hi) {
        const U32 mid=lo+(hi-lo)/2,entry=*entries+mid*20;
        auto key=m.Read(entry);if(!key)return false;
        const U32 candidate=*key&65535;
        if(candidate<id){lo=mid+1;continue;}
        if(candidate>id){hi=mid;continue;}
        auto offset=m.Read(entry+12),length=m.Read(entry+16);
        if(!offset||!length||*length!=replacement->size||replacement->text.size()>*length||
           (*offset&1)||*offset<16+*count*20||*offset>*size||*length>*size-*offset)return false;
        const auto text=return_notice?return_text:(active?selected->text:replacement->original);
        const U32 start=*base+*offset,end=start+*length;
        std::uint64_t hash=0xcbf29ce484222325ULL;
        struct Write {U32 at,value;};std::vector<Write> writes;
        // Read and validate every destination word before writing anything.
        // Preserve neighboring entries when this text is only halfword-aligned.
        for(U32 at=start&~3u;at<end;at+=4) {
            auto before=m.Read(at);if(!before)return false;
            U32 value=*before;
            for(unsigned byte=0;byte<4;++byte) {
                const U32 pos=at+byte;if(pos<start||pos>=end)continue;
                hash=(hash^((*before>>(byte*8))&255))*0x100000001b3ULL;
                const auto i=pos-start;
                const U32 next=i<text.size()?static_cast<unsigned char>(text[i]):0;
                value=(value&~(255u<<(byte*8)))|(next<<(byte*8));
            }
            writes.push_back({at,value});
        }
        // Native, classic-save and return text can transition in either
        // direction, including save -> title -> resume in the same process.
        if(id==0x0c01) {
            const auto& owl=*replacement;
            if(hash!=owl.hash&&hash!=owl.edited_hash&&hash!=return_hash)return false;
            constexpr U32 NativeFlags=0x00ff0001,ClassicFlags=0x00ff0000;
            const U32 before_flags=hash==owl.edited_hash?ClassicFlags:NativeFlags;
            if(m.Read(entry+8)!=before_flags)return false;
            const U32 after_flags=return_notice||!active?NativeFlags:ClassicFlags;
            const auto desired=return_notice?return_hash:(active?owl.edited_hash:owl.hash);
            if(hash==desired)return true;
            writes.push_back({entry+8,after_flags});
        } else {
            if(hash==(active?selected->edited_hash:replacement->hash))return true;
            if(hash!=replacement->hash&&hash!=replacement->edited_hash&&
               (!stick||hash!=stick->edited_hash))return false;
        }
        for(const auto& w:writes)m.Write(w.at,w.value);
        return true;
    }
    return false;
}
}
