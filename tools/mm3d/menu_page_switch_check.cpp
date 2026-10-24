// SPDX-License-Identifier: GPL-2.0-or-later
// clang++ -std=c++20 -Isrc tools/mm3d/menu_page_switch_check.cpp -o /tmp/menu-page-switch-check
#include "mm3d/menu_page_switch.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
using namespace MM3D::MenuPageSwitch;
struct Memory {
    std::unordered_map<U32,U32> words;
    std::optional<U32> Read(U32 p) const {
        auto it=words.find(p);return it==words.end()?std::nullopt:std::optional<U32>(it->second);
    }
    void Write(U32 p,U32 v){words[p]=v;}
    void Byte(U32 p,U32 v){auto& w=words[p&~3u];unsigned n=(p&3)*8;w=(w&~(255u<<n))|(v<<n);}
    void Layout(U32 p,const char* name) {
        Write(p+4,p+0x200);Write(p+0x218,p+0x300);
        for(unsigned i=0;;++i){Byte(p+0x300+i,name[i]);if(!name[i])break;}
    }
};
constexpr U32 C=0x200000,UI=0x201000,G=0x202000,I=0x210000,Q=0x220000,
              K=0x250000,P=0x260000,IL=0x230000,QL=0x240000,KL=0x270000,PL=0x280000;
struct Route {U32 from,to,button,layout;};
constexpr std::array<Route,4> Routes{{{K,Q,0x200,QL},{Q,K,0x100,KL},{I,P,0x100,PL},{P,I,0x200,IL}}};
Memory Fixture(Route route=Routes[2]) {
    Memory m;m.Write(C+4,UI);m.Write(C+8,route.from);m.Write(C+12,0);m.Write(C+16,0x101);
    m.Write(UI+0x16c,G);m.Byte(G+0x13d,4);m.Write(G+0x34,route.button);
    m.Write(G+0x11018,0x290000);
    m.Write(0x656b10,I);m.Write(0x656b0c,Q);m.Write(0x656b18,K);m.Write(0x656b14,P);
    m.Write(I,ItemsVtable);m.Write(Q,GearVtable);m.Write(K,MasksVtable);m.Write(P,MapVtable);
    m.Byte(I+0x9f,2);m.Byte(K+0x9f,2);m.Byte(Q+0x194,2);m.Write(P+0x78c,0);
    m.Write(I+0xc0,IL);m.Write(Q+0x14,QL);m.Write(K+0xc0,KL);m.Write(P+0x18,PL);
    m.Layout(IL,"MenuItem");m.Layout(QL,"MenuCollect");m.Layout(KL,"MenuMask");m.Layout(PL,"MenuMapDungeon");
    return m;
}
int main(int argc,char** argv) {
    for(auto route:Routes) {
        Memory m=Fixture(route);Hook h;h.Tick(m,C);
        assert(h.PairPending(m,C)&&m.Read(C+12)==route.to&&m.Read(G+0x34)==0);
        assert(!h.FinishLayout(m,route.layout));h.Tick(m,C);
        assert(h.source==route.from&&h.target==route.to);
        m.Write(C+8,route.to);m.Write(C+12,0);
        assert(!h.PairPending(m,C)&&h.FinishLayout(m,route.layout));
        if(route.to==P)assert(m.Read(P+0x750)==5);
        assert(!h.FinishLayout(m,0x300000));
        h.Tick(m,C);assert(h.FinishLayout(m,route.layout));
        h.Tick(m,C);assert(!h.FinishLayout(m,route.layout)&&!h.context);
        const auto before=m.words;h.Tick(m,C);assert(m.words==before);
        m.Write(G+0x34,route.button^0x300);h.Tick(m,C);assert(m.Read(C+12)==route.from);
    }
    // Gear's saved photo can still be converting when the user returns to
    // Masks. A separately retained scene makes both wait states safe to leave.
    for(bool retained:{false,true})for(unsigned state:{0u,1u,2u,3u}) {
        auto m=Fixture(Routes[1]);Hook h;m.Byte(Q+0x194,state);
        m.Write(G+0x11018,0);h.Tick(m,C,retained);
        const bool accepted=state==2||(retained&&state<2);
        assert(h.PairPending(m,C)==accepted);
        if(accepted) {
            assert(m.Read(C+12)==K&&h.preserve_capture==retained);
            assert(h.KeepCapture(m,G+0x11010)==retained);
        }
    }
    // Selecting an item/mask still allows the native shoulder action; route
    // both outer edges, but leave equip/drag/open/close states untouched.
    for(auto route:{Routes[0],Routes[2]})for(unsigned state=0;state<=11;++state) {
        Memory m=Fixture(route);m.Byte(route.from+0x9f,state);Hook h;
        const auto before=m.words;h.Tick(m,C);
        if(state==2||state==3) {
            assert(h.PairPending(m,C)&&m.Read(C+12)==route.to&&m.Read(G+0x34)==0);
            assert(Byte(m,route.from+0x9f)==state);
        } else assert(!h.context&&m.words==before);
    }
    // Native middle pair, outer edges, editing/Soaring, simultaneous buttons,
    // and in-progress transitions all remain untouched.
    for(unsigned n=0;n<16;++n) {
        Memory m=Fixture();Hook h;
        switch(n) {
        case 0:m.Write(G+0x34,0);break;
        case 1:m.Write(G+0x34,0x200);break; // Items -> Masks native
        case 2:m.Write(C+8,K);break; // Masks -> Items native
        case 3:m.Write(C+8,Q);m.Write(G+0x34,0x200);break; // Gear outer edge
        case 4:m.Write(C+8,P);break; // Map outer edge
        case 5:m.Write(G+0x34,0x300);break;
        case 6:m.Write(G+0x34,0x102);break;
        case 7:m.Byte(I+0x9f,4);break;
        case 8:m.Layout(IL,"MenuItemPresen");break;
        case 9:m.Write(C+12,Q);break;
        case 10:m.Byte(C+16,0);break;
        case 11:m.Byte(C+17,0);break;
        case 12:m.Byte(G+0x13d,1);break;
        case 13:m.Write(P,0);break;
        case 14:m=Fixture(Routes[3]);m.Byte(P+0x78e,1);break;
        case 15:m=Fixture(Routes[0]);m.Byte(K+0x9f,4);break;
        }
        const auto before=m.words;h.Tick(m,C);assert(!h.context&&m.words==before);
    }
    {Memory m=Fixture();m.Write(P+0x78c,0xa5010101);Hook h;h.Tick(m,C);assert(m.Read(P+0x78c)==0xa5000101);}
    {Memory m=Fixture();Hook h;h.Tick(m,C);m.Write(C+8,0);m.Write(C+12,0);h.Tick(m,C);assert(!h.context);}
    {Memory m=Fixture();Hook h;h.Tick(m,C);for(int n=0;n<121;++n)h.Tick(m,C);assert(!h.context);}
    // Independently opened world/dungeon maps can return to Items without
    // pretending that a nonexistent screenshot is ready.
    for(U32 outside:{0u,1u}) {
        Memory m=Fixture(Routes[3]);m.Byte(P+0x78d,outside);m.Write(G+0x11018,0);
        Hook h;h.Tick(m,C);assert(h.target==I&&!h.KeepCapture(m,G+0x11010));
    }
    // Real lifecycle ordering: PrepareOpen precedes outgoing Close. Neither
    // may restart/destroy an owned capture during these direct handoffs.
    for(auto route:Routes)for(unsigned site:{3u,4u,5u}) {
        Memory m=Fixture(route);m.Write(G+0x100,0x2a0000);m.Write(0x66889c,0x2a0000);
        Hook h;h.Tick(m,C);
        std::array<U32,16> r{};for(unsigned i=0;i<16;++i)r[i]=0x300000+i*4;
        r[0]=G+0x11010;r[14]=0x123456;r[15]=PCs[site]+4;
        const auto saved=r;const auto memory=m.words;
        assert(h.KeepCapture(m,r[0]));h.CaptureCall(m,r,site);
        assert(r[15]==(site==3?saved[14]:site==5?0x5a6000:PCs[site]+4));
        assert(r[14]==(site==3?saved[14]:PCs[site]+4));
        for(unsigned i=0;i<14;++i)assert(r[i]==saved[i]);
        assert(m.words==memory);
        if(site==5) {
            m.Write(G+0x100,0x2b0000);r=saved;h.CaptureCall(m,r,site);
            assert(r[15]==PCs[site]+4);m.Write(G+0x100,0x2a0000);
        }
        r=saved;r[0]+=4;h.CaptureCall(m,r,site);
        assert(r[15]==(site==3?0x1bb320:0x198864));
        m.Write(C+8,I);m.Write(C+12,K);r=saved;
        assert(!h.KeepCapture(m,r[0]));h.CaptureCall(m,r,site);
        assert(r[15]==(site==3?0x1bb320:0x198864));h.Tick(m,C);assert(!h.context);
        r=saved;h.CaptureCall(m,r,site);assert(r[15]==(site==3?0x1bb320:0x198864));
    }
    MM3D::GearMenu::enabled=true;assert(GearBackdropAlpha()==std::bit_cast<U32>(.75f));
    MM3D::GearMenu::enabled=false;assert(GearBackdropAlpha()==std::bit_cast<U32>(.5f));
    MM3D::GearMenu::enabled=true;
    for(auto route:Routes) {
        Memory m=Fixture(route);Hook h;h.Tick(m,C);
        std::array<U32,16> r{};for(unsigned i=0;i<16;++i)r[i]=0x300000+i*4;
        const auto before=r;const auto memory=m.words;
        h.MapCloseSound(m,r);
        assert(r[14]==PCs[7]+4);
        assert(r[15]==(route.from==P?PCs[7]+4:0x2006e4));
        for(unsigned i=0;i<14;++i)assert(r[i]==before[i]);
        assert(m.words==memory);
        // A real exit (or a stale session) still calls the original sound routine.
        m.Write(C+12,0x2b0000);h.MapCloseSound(m,r);assert(r[15]==0x2006e4);
        h={};h.MapCloseSound(m,r);assert(r[15]==0x2006e4);
    }
    // World-map title and X prompt: only seek the incoming HUD intro, and
    // preserve native zoom/detail, looping, stopped and ordinary map opening.
    for(unsigned scenario=0;scenario<7;++scenario) {
        Memory m=Fixture(Routes[2]);Hook h;h.Tick(m,C);
        constexpr U32 hud=0x2c0000,player=0x2d0000,anim=0x2e0000,name=0x2f0000;
        m.Write(C+8,P);m.Write(C+12,0);m.Write(P+0x1c,hud);m.Layout(hud,"UpWorldMapHud");
        m.Write(hud+0x19c,player);m.Write(hud+0x1a4,anim);
        m.Write(player+12,anim);m.Write(player+8,1);m.Write(player+0x10,0);
        m.Write(anim+8,name);m.Write(anim+12,6);
        const char* text=scenario==1?"map_detail_mask_anim":"in_wmap_hud_anim";
        for(unsigned i=0;;++i){m.Byte(name+i,text[i]);if(!text[i])break;}
        if(scenario==2)m.Write(player+8,0x101);
        if(scenario==3)m.Write(player+8,0);
        if(scenario==4)m.Write(player+12,anim+0x100);
        if(scenario==5)h={};
        if(scenario==6)m.Write(anim+12,0xffffffff);
        const auto frame=m.Read(player+0x10),flags=m.Read(player+8);
        h.FinishLayout(m,hud);
        if(scenario==0) {
            assert(m.Read(player+0x10)==std::bit_cast<U32>(6.f));
            assert(m.Read(player+8)==0x10001);
            // The bounded handoff ends; ordinary frame updates cannot re-seek.
            h.Tick(m,C);h.Tick(m,C);m.Write(player+0x10,0);
            h.FinishLayout(m,hud);assert(m.Read(player+0x10)==0);
        } else {assert(m.Read(player+0x10)==frame&&m.Read(player+8)==flags);}
    }
    // Dungeon entry must reveal the shared parchment, which also supplies
    // the bottom-right prompts' fade. Cover hidden and outgoing initial states.
    for(unsigned scenario=0;scenario<7;++scenario) {
        Memory m=Fixture(Routes[2]);Hook h;h.Tick(m,C);
        constexpr U32 bg=0x2c0000,player=0x2d0000,anim=0x2e0000,name=0x2f0000;
        m.Write(C+8,P);m.Write(C+12,0);m.Write(P+0x14,bg);m.Layout(bg,"BackGround");
        m.Write(bg+0x170,player);m.Write(bg+0x174,anim);
        m.Write(player+12,scenario==0?0:anim+0x100);m.Write(player+8,scenario==1?1:0);
        m.Write(player+0x10,0);m.Write(anim+8,name);m.Write(anim+12,3);
        const char* text=scenario==4?"out_background_anim":"in_background_anim";
        for(unsigned i=0;;++i){m.Byte(name+i,text[i]);if(!text[i])break;}
        if(scenario==2)m.Byte(P+0x78d,1); // world map keeps its own background
        if(scenario==3)m.Byte(P+0x78e,1); // Soaring untouched
        if(scenario==5)h={};
        if(scenario==6)m.Write(anim+12,0xffffffff);
        const auto previous=m.Read(player+12),flags=m.Read(player+8);
        h.FinishLayout(m,bg);
        if(scenario<2) {
            assert(m.Read(player+12)==anim&&m.Read(player+8)==0x10001);
            assert(m.Read(player+0x10)==std::bit_cast<U32>(3.f));
            h.Tick(m,C);h.Tick(m,C);m.Write(player+12,anim+0x100);
            h.FinishLayout(m,bg);assert(m.Read(player+12)==anim+0x100);
        } else {
            assert(m.Read(player+12)==previous&&m.Read(player+8)==flags);
            assert(m.Read(player+0x10)==0);
        }
    }
    Memory code;
    for(unsigned i=0;i<PCs.size();++i)code.Write(PCs[i],Words[i]);
    for(auto [p,w]:Guards)code.Write(p,w);
    assert(CodeMatches(code));
    for(auto [p,w]:code.words){code.Write(p,w^1);assert(!CodeMatches(code));code.Write(p,w);}
    for(unsigned i=0;i<PCs.size();++i){
        assert(Instruction(PCs[i],Words[i])==((Words[i]&0xf0000000)|0x0f000000|(FirstSVC+i)));
        assert(Instruction(PCs[i],Words[i]^1)==(Words[i]^1));
    }
    if(argc>1) {
        std::ifstream f(argv[1],std::ios::binary);assert(f);
        for(auto& [p,w]:code.words){f.seekg(p-0x100000);f.read(reinterpret_cast<char*>(&w),4);assert(f);}
        assert(CodeMatches(code));
    }
    std::cout<<"Gear/Masks/Items/Map routing, capture lifetime, animation scope and code guards passed.\n";
    return 0;
}
