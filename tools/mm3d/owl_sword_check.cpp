// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/owl_sword.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <optional>
#include <unordered_map>
using namespace MM3D::OwlSword;
struct Memory {
    std::unordered_map<U32,U32> words;
    std::optional<U32> Read(U32 at){auto i=words.find(at);return i==words.end()?std::nullopt:std::optional<U32>(i->second);}
    void Write(U32 at,U32 w){words[at]=w;}
};
int main(int argc,char** argv) {
    Memory m;constexpr U32 owl=0x9000000,hit=0x9100000;
    for(auto [a,w]:Guards)m.words[a]=w;
    assert(CodeMatches(m));
    for(auto [a,w]:Guards){m.words[a]^=1;assert(!CodeMatches(m));m.words[a]=w;}
    assert(Instruction(TriggerPC,TriggerWord)==(0xef000000|TriggerSVC));
    assert(Instruction(TriggerPC,TriggerWord^1)==(TriggerWord^1));
    assert(Instruction(TriggerPC+4,TriggerWord)==TriggerWord);
    m.words[owl]=0x701b2;m.words[owl+4]=9;m.words[owl+0x1c]=0;
    m.words[0x775324]=0;m.words[0x626c3c]=1;m.words[owl+0x268]=Idle;
    m.words[owl+0x264]=0;m.words[owl+0x21c]=0x20d;m.words[owl+0x248]=hit;
    m.words[owl+0x28c]=0xabcd0501;m.words[owl+0x238]=0xa5021234;
    const auto initial=m.words;
    for(U32 weapon:{2u,8u,32u,128u,256u,512u,0x1000000u}) {
        m.words=initial;m.words[hit]=weapon;enabled=false;
        auto before=m.words;Update(m,owl);assert(m.words==before&&!Locked(m,owl));
        enabled=true;assert(Locked(m,owl));Update(m,owl);
        assert(m.words[owl+0x268]==((weapon&0x1000202)?Activate:Idle));
        assert(m.words[owl+4]==9);assert(m.words[0x775324]==0);
    }
    // Both supported weapons preserve accepted conversations for native Update.
    for(U32 weapon:{2u,512u,0x1000000u}) {
        m.words=initial;m.words[hit]=weapon;m.words[owl+4]|=0x100;
        const auto before=m.words;Update(m,owl);assert(m.words==before);
    }
    // The pre-actor trigger must cancel every stage of Restoration's delayed
    // Talk without damaging its neighboring activation byte or our collider.
    for(bool on:{false,true})for(bool active:{false,true})for(bool feather:{false,true})
    for(U32 timer:{0u,1u,2u,5u,255u}) {
        m.words=initial;enabled=on;m.words[0x775324]=active?1:0;
        m.words[owl+0x1c]=feather?0x200:0;
        m.words[owl+0x28c]=0xabcd0001|(timer<<8);
        std::array<U32,16> r{};
        for(unsigned i=0;i<r.size();++i)r[i]=0x9100000+4*i;
        r[0]=owl;r[15]=TriggerPC+4;
        auto expected_regs=r;auto expected_memory=m.words;
        if(on&&!feather) {
            expected_regs[15]=TriggerReturn;
            expected_memory[owl+0x28c]&=~0xff00u;
            expected_memory[owl+0x238]&=~0x20000u;
        } else expected_regs[3]=owl+0x28c;
        StrikeTrigger(m,r);
        assert(r==expected_regs&&m.words==expected_memory);
    }
    // Hit -> pre-actor trigger -> owl update, repeated over the old five-tick
    // delay. Direct activation starts once, never entering delayed Talk.
    for(bool skip_intro:{false,true})for(U32 weapon:{2u,512u,0x1000000u}) {
        enabled=true;MM3D::OwlIntro::disabled=skip_intro;
        m.words=initial;m.words[hit]=weapon;
        m.words[owl+0x268]=skip_intro?MM3D::OwlIntro::Intro:Idle;
        for(unsigned tick=0;tick<8;++tick) {
            std::array<U32,16> r{};r[0]=owl;
            StrikeTrigger(m,r);assert(r[15]==TriggerReturn);
            assert((m.words[owl+0x28c]&0xff00)==0);
            Update(m,owl);assert(m.words[owl+0x268]==Activate);
            assert(m.words[owl+0x21c]==0xd&&m.words[owl+4]==9);
            // A broken stick/expired hit pointer after starting must be harmless.
            m.words.erase(hit);
        }
    }
    MM3D::OwlIntro::disabled=false;
    // Bad/missing hit data and a statue already talking or saving are ignored.
    for(unsigned scenario=0;scenario<7;++scenario) {
        m.words=initial;m.words[hit]=2;enabled=true;
        if(scenario==0)m.words[owl+0x248]=0;
        if(scenario==1)m.words[owl+0x248]=hit+1;
        if(scenario==2)m.words[owl+0x248]=0xfffffffc;
        if(scenario==3)m.words.erase(hit);
        if(scenario==4)m.words[owl+0x264]=1;
        if(scenario==5)m.words[owl+0x264]=0x01000000;
        if(scenario==6)m.words[owl+0x268]=0x577a8c; // Wings already opening.
        const auto before=m.words;Update(m,owl);assert(m.words==before);
    }
    for(int mode=0;mode<4;++mode) {
        m.words=initial;m.words[hit]=512;enabled=true;
        if(mode==0)m.words[0x775324]=1; // Already activated: ordinary A interaction.
        if(mode==1)m.words[owl+0x1c]=0x200; // Feather statue.
        if(mode==2)m.words[owl]=0x123; // Other actor.
        if(mode==3)m.words[owl+0x1c]=15; // Invalid index.
        const auto before=m.words;assert(!Locked(m,owl));Update(m,owl);assert(m.words==before);
    }
    m.words=initial;m.words[hit]=512;m.words[owl+0x21c]=0x0d;Update(m,owl);
    assert(m.words[owl+0x268]==Idle); // Swinging nearby without a collision is insufficient.
    for(bool classic:{false,true})for(bool sword:{false,true})for(bool feather:{false,true}) {
        m.words=initial;enabled=sword;m.words[owl+0x1c]=feather?0x200:0;
        m.words[owl+4]|=0x100;
        const auto before=m.words;
        assert(FeatherBlocked(m,owl,classic)==(classic&&feather));
        BlockFeatherAcceptance(m,owl,classic);
        auto expected=before;if(classic&&feather)expected[owl+4]&=~0x100u;
        assert(m.words==expected); // Preserve saves, action state and ordinary owls.
    }
    m.words=initial;m.words[owl+0x1c]=0x200;m.words[owl]=0x123;
    assert(!FeatherBlocked(m,owl,true));
    m.words[owl]=0x1b2;m.words.erase(owl+0x1c);
    assert(!FeatherBlocked(m,owl,true));
    assert(!FeatherBlocked(m,owl+1,true));
    if(argc>1){std::ifstream f(argv[1],std::ios::binary);assert(f);for(auto [a,w]:Guards){U32 v=0;f.seekg(a-0x100000);f.read(reinterpret_cast<char*>(&v),4);assert(f&&v==w);}}
    std::cout<<"Owl sword/stick activation, delayed-Talk suppression, accepted conversations, active/feather exemptions, disabled replay and signatures passed.\n";
}
