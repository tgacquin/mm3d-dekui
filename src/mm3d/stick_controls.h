// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include "mm3d/minimap_shortcut.h"

namespace MM3D::StickControls {
using U32=std::uint32_t;
inline std::atomic<bool> enabled{false};
inline std::atomic<U32> revision{},host_buttons{},host_pressed{};
constexpr U32 Left=1,Right=2,ZL=0x4000,ZR=0x8000;
inline void Configure(bool on) {
    if(enabled.exchange(on)!=on) {host_pressed=0;++revision;}
}
// Stored HUD layouts describe physical positions. Swap only the two item
// groups at draw/edit time, leaving installed offsets and all presets intact.
inline unsigned ItemLayoutIndex(unsigned index,unsigned first) {
    return enabled.load()&&(index==first||index==first+1)?first+1-(index-first):index;
}
// Latch short physical clicks between guest frames. These are host-only inputs,
// never additional bits in the emulated 3DS HID/IR shared memory.
inline void Publish(U32 buttons) {
    buttons&=Left|Right;
    const U32 old=host_buttons.exchange(buttons);
    host_pressed.fetch_or(buttons&~old);
}
constexpr U32 PadPC=0x7cfbc0,PadWord=0xe1c520b0,PadSVC=0xfffc85;
constexpr U32 MapPC=MinimapShortcut::RoutePC,MapWord=MinimapShortcut::RouteWord,MapSVC=0xfffc86;
constexpr U32 State=0x72a000,Scratch=0x7f80fc;
inline U32 Instruction(U32 pc,U32 word) {
    if(pc==PadPC&&word==PadWord)return 0xef000000|PadSVC;
    if(pc==MapPC&&word==MapWord)return 0xef000000|MapSVC;
    return word;
}
template<class M> bool CodeMatches(M& m,U32 pc) {
    for(auto [start,size,expected]:std::array<std::array<U32,3>,2>{{
        {0x7cfba8,0x1dc,0xc9103e56},{0x878430,0x14,0xdf590f7c}}}) {
        U32 hash=2166136261u;
        for(U32 off=0;off<size;off+=4) {
            const auto word=m.Read(start+off);if(!word)return false;
            for(unsigned i=0;i<4;++i)hash=(hash^((*word>>(8*i))&255))*16777619u;
        }
        if(hash!=expected)return false;
    }
    if(m.Read(0x7d1604)!=0xe59f0000u || m.Read(0x7d1608)!=0xe12fff1eu ||
       m.Read(0x7d160c)!=0x729000u)return false;
    return pc!=MapPC || MinimapShortcut::CodeMatches(m);
}
struct Hook {
    U32 generation=~0u,blocked{},blocked_clicks{},previous_clicks{};
    bool map_pending{};
    template<class M,class R> void Pad(M& m,R& r) {
        // Displaced STRH r2,[r5]. Its neighbouring bytes belong to other data.
        const auto scratch=m.Read(r[5]);if(!scratch)return;
        const U32 native=(*scratch&0xffff0000u)|(r[2]&0xffff);
        m.Write(r[5],native);
        map_pending=false;
        const U32 clicks=host_buttons.load(),pressed=host_pressed.exchange(0);
        if(!enabled || r[4]!=State || r[5]!=Scratch) {
            generation=~0u;previous_clicks=clicks;return;
        }
        const auto held=m.Read(State+8),fresh=m.Read(State+12),released=m.Read(State+16);
        if(!held||!fresh||!released)return;
        const U32 version=revision.load();
        bool seed=generation!=version;
        if(seed) {generation=version;blocked=*held&(ZL|ZR);blocked_clicks=clicks;previous_clicks=0;}
        blocked&=*held;
        blocked_clicks&=clicks;
        // A quick click released before the guest reads it still lasts one
        // guest frame. Holding a click creates no further press edges.
        const U32 effective=(clicks|(seed?0:pressed))&~blocked_clicks;
        const U32 new_clicks=seed?0:((effective&~previous_clicks)|(pressed&~blocked_clicks));
        const U32 released_clicks=previous_clicks&~effective;
        previous_clicks=effective;
        U32 touch=native&0xffff;
        const auto action=[&](U32 down,U32 down_edge,U32 bit) {
            if(down)touch|=bit;
            if(down_edge)touch|=bit<<8;
        };
        action(*held&ZL&~blocked,*fresh&ZL&~blocked,1); // Item I
        action(*held&ZR&~blocked,*fresh&ZR&~blocked,2); // Item II
        action(effective&Right,new_clicks&Right,4);   // native camera action
        m.Write(Scratch,(native&0xffff0000u)|touch);
        // Consume the physical triggers so ZR cannot also modify A/D-pad or
        // suppress swimming, and ZL cannot also activate camera/arrow actions.
        // Right click inherits the existing contextual ZL input (camera,
        // arrow cycling, swimming) in addition to the native camera flag.
        m.Write(State+8,(*held&~(ZL|ZR))|((effective&Right)?ZL:0));
        m.Write(State+12,(*fresh&~(ZL|ZR))|((new_clicks&Right)?ZL:0));
        m.Write(State+16,(*released&~(ZL|ZR))|((released_clicks&Right)?ZL:0));
        map_pending=(new_clicks&Left)!=0;
        // Rejoin native copying and usability filtering. The unfiltered
        // scratch flags remain available to the item-assignment handlers.
        r[15]=0x7cfbf0;
    }
    template<class M,class R> void Map(M& m,R& r) {
        r[15]=enabled?0x7d0708:0x7d06d4;
        const bool requested=map_pending;map_pending=false;
        if(!enabled||!requested)return;
        const U32 game=r[4];
        const auto ui=m.Read(0x72f5c0),main=m.Read(0x656b04),current=m.Read(0x72f5c4);
        const auto state=m.Read(game+0x13c);
        if(!game||(game&3)||!ui||!*ui||m.Read(*ui+0x16c)!=game||!main||!*main||
           current!=main||m.Read(0x72f5c8)!=0||!state||(*state&0xffff)!=0x0402)return;
        HudAdjustments::ToggleMinimap();
        r[0]=MinimapShortcut::Sound;r[14]=0x7d0708;r[15]=MinimapShortcut::PlaySound;
    }
};
}
