// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/stick_controls.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
using namespace MM3D::StickControls;
struct Memory {
    std::unordered_map<U32,U32> words;
    std::optional<U32> Read(U32 at) const {
        const auto i=words.find(at);return i==words.end()?std::nullopt:std::optional<U32>(i->second);
    }
    void Write(U32 at,U32 value) {words.at(at)=value;}
};
struct Fixture {
    Memory m{{{Scratch,0xabcd0000},{State+8,0},{State+12,0},{State+16,0}}};
    Hook hook;
    std::array<U32,16> r{};
    void Pad(U32 held=0,U32 pressed=0,U32 released=0,U32 native=0) {
        r[2]=native;r[4]=State;r[5]=Scratch;r[15]=PadPC+4;
        m.words[State+8]=held;m.words[State+12]=pressed;m.words[State+16]=released;
        hook.Pad(m,r);
        assert((m.words[Scratch]&0xffff0000)==0xabcd0000);
    }
    Fixture() {Configure(false);host_buttons=0;host_pressed=0;}
    void Enable() {Configure(true);Pad();}
};
int main(int argc,char** argv) {
    assert(Instruction(PadPC,PadWord)==(0xef000000|PadSVC));
    assert(Instruction(MapPC,MapWord)==(0xef000000|MapSVC));
    assert(Instruction(PadPC,PadWord^1)==(PadWord^1));
    {
        Fixture f;f.Pad(ZL|ZR, ZR,ZL,0x0909);
        assert(f.m.words[Scratch]==0xabcd0909&&f.r[15]==PadPC+4);
        assert(f.m.words[State+8]==(ZL|ZR)&&f.m.words[State+12]==ZR&&f.m.words[State+16]==ZL);
        f.hook.Map(f.m,f.r);assert(f.r[15]==0x7d06d4); // previous shortcut route
    }
    for(const U32 trigger:{ZR,ZL}) {
        Fixture f;f.Enable();const U32 action=trigger==ZL?1:2;
        constexpr U32 Others=1|0x400|0x800|0xf0; // A, X, Y and D-pad survive
        f.Pad(trigger|Others,trigger|Others,0,0x0808);
        assert((f.m.words[Scratch]&0xffff)==(0x0808|action|(action<<8)));
        assert(f.m.words[State+8]==Others&&f.m.words[State+12]==Others&&f.r[15]==0x7cfbf0);
        f.Pad(trigger,0);assert((f.m.words[Scratch]&0xffff)==action);
        f.Pad(0,0,trigger);assert(f.m.words[Scratch]==0xabcd0000&&f.m.words[State+16]==0);
    }
    {
        Fixture f;f.Enable();Publish(Right);f.Pad();
        assert((f.m.words[Scratch]&0xffff)==0x0404&&f.m.words[State+8]==ZL&&f.m.words[State+12]==ZL);
        Publish(Right);f.Pad();assert((f.m.words[Scratch]&0xffff)==4&&f.m.words[State+12]==0);
        Publish(0);f.Pad();assert((f.m.words[Scratch]&0xffff)==0&&f.m.words[State+16]==ZL);
        // A complete short click between guest reads is delivered once.
        Publish(Right);Publish(0);f.Pad();assert((f.m.words[Scratch]&0xffff)==0x0404);
        f.Pad();assert((f.m.words[Scratch]&0xffff)==0&&f.m.words[State+16]==ZL);
        Configure(false);Publish(Right);f.Pad(ZL,ZL);
        assert(f.m.words[State+8]==ZL&&(f.m.words[Scratch]&0xffff)==0&&f.r[15]==PadPC+4);
    }
    {
        Fixture f;Publish(Left|Right);Configure(true);f.Pad(ZL|ZR,ZL|ZR);
        assert((f.m.words[Scratch]&0xffff)==0&&f.m.words[State+8]==0&&!f.hook.map_pending);
        f.Pad(ZL|ZR,0);assert((f.m.words[Scratch]&0xffff)==0);
        Publish(0);f.Pad();Publish(Right);f.Pad(ZR,ZR);
        assert((f.m.words[Scratch]&0xffff)==0x0606);
    }
    constexpr U32 Game=0x10000000,UI=0x10020000,Main=0x10030000;
    for(int bad=-1;bad<5;++bad) {
        Fixture f;f.Enable();
        f.m.words.insert({{0x72f5c0,UI},{UI+0x16c,Game},{0x656b04,Main},
            {0x72f5c4,Main},{0x72f5c8,0},{Game+0x13c,0x0402}});
        if(bad==0)f.m.words[0x72f5c4]=Main+4;
        if(bad==1)f.m.words[0x72f5c8]=Main+4;
        if(bad==2)f.m.words[Game+0x13c]=0x0602;
        if(bad==3)f.m.words[UI+0x16c]=Game+4;
        if(bad==4)f.m.words.erase(0x72f5c0);
        MM3D::HudAdjustments::hide_minimap=false;
        Publish(Left);f.Pad();assert(f.hook.map_pending);
        const auto before=f.m.words;f.r[4]=Game;f.hook.Map(f.m,f.r);
        assert(f.m.words==before&&!f.hook.map_pending);
        assert(MM3D::HudAdjustments::hide_minimap==(bad==-1));
        if(bad==-1)assert(f.r[0]==MM3D::MinimapShortcut::Sound&&f.r[14]==0x7d0708&&
                         f.r[15]==MM3D::MinimapShortcut::PlaySound);
        else assert(f.r[15]==0x7d0708);
        f.Pad();f.r[4]=Game;f.hook.Map(f.m,f.r);
        assert(f.r[15]==0x7d0708); // held click cannot repeat
        // A click which was never consumed in a menu expires next pad update.
        Publish(0);f.Pad();Publish(Left);f.Pad();assert(f.hook.map_pending);
        f.Pad();assert(!f.hook.map_pending);
    }
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary);assert(file);Memory code;
        U32 word=0,at=0x100000;
        while(file.read(reinterpret_cast<char*>(&word),4)){code.words[at]=word;at+=4;}
        assert(CodeMatches(code,PadPC)&&CodeMatches(code,MapPC));
        for(const auto range:std::array<std::array<U32,2>,3>{{{0x7cfba8,0x1dc},{0x878430,0x14},{0x7d1604,12}}})
            for(U32 off=0;off<range[1];off+=4) {
                code.words[range[0]+off]^=1;
                assert(!CodeMatches(code,PadPC)&&!CodeMatches(code,MapPC));
                code.words[range[0]+off]^=1;
            }
        code.words[MM3D::MinimapShortcut::PlaySound]^=1;assert(!CodeMatches(code,MapPC));
        // The new item mapping rejoins before all four native usability checks.
        assert(code.Read(0x7cfbf0)==0xe1d530b0u);
    }
    Configure(false);host_buttons=0;host_pressed=0;
    std::cout<<"Stick controls: native fallback, triggers, click edges, mode changes, minimap sound/context and code guards passed\n";
}
