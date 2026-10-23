// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/owl_intro.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
namespace MM3D::OwlSword {
using U32=std::uint32_t;
inline std::atomic<bool> enabled{false};
constexpr U32 UpdatePC=0x4bfabc,UpdateWord=0xe1a04000,OfferPC=0x4bfcac,OfferWord=0xebfc0d5e;
constexpr U32 FirstSVC=0xfffe21,Idle=0x58d8d4,Activate=0x30e86c;
constexpr U32 TriggerPC=0x7d3c4c,TriggerWord=0xe2803fa3,TriggerReturn=0x7d3cbc;
constexpr U32 TriggerSVC=0xfffe27; // 0xfffe23..26 belong to BomberApproach/OwlIntro.
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==UpdatePC&&word==UpdateWord)return 0xef000000|FirstSVC;
    if(pc==OfferPC&&word==OfferWord)return 0xef000000|(FirstSVC+1);
    if(pc==TriggerPC&&word==TriggerWord)return 0xef000000|TriggerSVC;
    return word;
}
inline constexpr std::array<std::pair<U32,U32>,16> Guards{{
    {0x4bfab4,0xe92d4ff0},{UpdatePC,UpdateWord},{OfferPC,OfferWord},
    {0x489574,Idle},{0x4bfdd0,Activate},{0x4bfdbc,0x7752d8},
    {0x4bfdc0,0x626c3c},{0x68cef0,0x01000202},
    // Guard both activation paths together; never install only half the fix.
    {0x7d3c10,0xe92d41f0},{TriggerPC,TriggerWord},{0x7d3c50,0xe1d330d1},
    {0x7d3c60,0xe5d0123a},{0x7d3c78,0xe5c0123a},
    {0x7d3cb0,0xebfff491},{0x7d3cb8,0xe5c4328d},{TriggerReturn,0xe8bd81f0}
}};
template<class M> bool CodeMatches(M& m){for(auto [a,w]:Guards)if(m.Read(a)!=w)return false;return true;}
// Completion also runs after the activation save bit has been set, so Locked
// deliberately cannot be used here. Feather statues retain their native flow.
template<class M> bool QuietCompletion(M& m,U32 actor) {
    if(!enabled||actor<0x100000||actor>0x3ffffd70||(actor&3))return false;
    const auto id=m.Read(actor),params=m.Read(actor+0x1c);
    return id&&(*id&65535)==0x1b2&&params&&!(*params&0x200)&&(*params&15)<10;
}
// Restoration normally waits five ticks then forces Talk after a strike. Our
// direct activation already starts the native cutscene, so that second entry
// must not run (including a timer queued before the option was enabled).
template<class M,class R> void StrikeTrigger(M& m,R& r) {
    const U32 actor=r[0];
    if(QuietCompletion(m,actor)) {
        const auto timer=m.Read(actor+0x28c),element=m.Read(actor+0x238);
        if(timer&&element) {
            m.Write(actor+0x28c,*timer&~0xff00u); // Only activation_timer.
            m.Write(actor+0x238,*element&~0x20000u); // Element AC hit byte mask 0x02.
            // Preserve the base collider hit for Update. The native epilogue
            // restores r4-r8 and PC from the existing stack frame.
            r[15]=TriggerReturn;
            return;
        }
    }
    r[3]=actor+0x28c; // Displaced ADD; leave native timer/Talk logic intact.
}
template<class M> bool Locked(M& m,U32 actor) {
    if(!enabled||actor<0x100000||actor>0x3ffffd70||(actor&3))return false;
    auto id=m.Read(actor),params=m.Read(actor+0x1c),save=m.Read(0x775324);
    if(!id||(*id&65535)!=0x1b2||!params||!save)return false;
    if(*params&0x200)return false; // Feather statues are not soaring owls.
    const unsigned slot=*params&15;if(slot>=10)return false;
    auto bit=m.Read(0x626c3c+slot*4);
    return bit&&*bit&&((*save&65535)&*bit)==0;
}
// Feather checkpoints share the owl actor but carry the 0x200 parameter bit.
// Keep this separate from sword activation: Classic Saving disables feathers
// independently of whether sword activation is enabled.
template<class M> bool FeatherBlocked(M& m,U32 actor,bool classic_saving) {
    if(!classic_saving || actor<0x100000 || actor>0x3ffffd70 || (actor&3))return false;
    const auto id=m.Read(actor),params=m.Read(actor+0x1c);
    return id && (*id&65535)==0x1b2 && params && (*params&0x200);
}
template<class M> void BlockFeatherAcceptance(M& m,U32 actor,bool classic_saving) {
    if(!FeatherBlocked(m,actor,classic_saving))return;
    const auto flags=m.Read(actor+4);
    if(flags && (*flags&0x100))m.Write(actor+4,*flags&~0x100u);
}
template<class M> void Update(M& m,U32 actor) {
    if(!Locked(m,actor))return;
    auto flags=m.Read(actor+4);
    // An already accepted interaction owns player state. Let native Update
    // consume it; erasing TalkInitiated strands Link in a conversation.
    if(!flags||(*flags&0x100))return;
    const auto action=m.Read(actor+0x268);
    if(!action||(*action!=Idle&&!OwlIntro::AllowsSword(*action)))return; // Preserve running cutscenes.
    auto state=m.Read(actor+0x264),collision=m.Read(actor+0x21c),hit=m.Read(actor+0x248);
    if(!state||(*state&0xff0000ff)||!collision||!(*collision&0x200)||!hit||
       *hit<0x100000||*hit>0x3fffffd8||(*hit&3))return;
    // Cylinder AC hit: the incoming element's AT mask distinguishes swords
    // (including Great Fairy's Sword) and sticks from projectiles/explosives.
    auto attack=m.Read(*hit);
    if(attack&&(*attack&0x01000202)) {
        m.Write(actor+0x21c,*collision&~0x200u);
        m.Write(actor+0x268,Activate); // Native wings, sound, cutscene and save bit.
    }
}
}
