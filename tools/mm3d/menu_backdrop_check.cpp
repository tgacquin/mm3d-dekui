// SPDX-License-Identifier: GPL-2.0-or-later
#define main page_switch_checks_main
#include "menu_page_switch_check.cpp"
#undef main
#include "mm3d/menu_backdrop.h"
using Backdrop=MM3D::MenuBackdrop::Hook;
constexpr U32 SC=0x72f5bc; // Inline ScreenContext, first word is its vtable.
constexpr U32 BG=0x300000,ALL=0x301000,LEFT=0x302000,RIGHT=0x303000,
 LP=0x304000,RP=0x305000,LG=0x306000,RG=0x307000,GRAPHICS=0x308000,
 MANAGER=0x309000,QUEUE=0x30a000,PLAY=0x30b000,GAME_GRAPHICS=0x30c000;
void Widget(Memory& m,U32 p,const char* name) {
    m.Write(p+0x10,p+0x300);
    for(unsigned i=0;;++i){m.Byte(p+0x300+i,name[i]);if(!name[i])break;}
}
void Children(Memory& m,U32 p,std::initializer_list<U32> children) {
    m.Write(p+0x14,p+0x400);m.Write(p+0x18,children.size());unsigned i=0;
    for(auto c:children)m.Write(p+0x400+4*i++,c);
}
Memory BackgroundFixture(U32 page=Q) {
    auto m=Fixture();
    for(U32 offset:{0u,4u,8u,12u,16u})m.words.erase(C+offset);
    m.Write(SC,0x630000);m.Write(SC+4,UI);m.Write(SC+8,page);
    m.Write(SC+12,0);m.Write(SC+16,0x101);m.Write(G+0x34,0);
    m.Write(0x656b04,PLAY);m.Write(G+0xaac,0x12340001);
    for(unsigned i=0;i<7;++i)m.Write(G+0x11010+i*4,0);
    m.Write(G+0x11010,0x310000);m.Write(G+0x11014,0x311000);
    m.Write(G+0x11018,0x290000);m.Write(G+0x11024,0x00f00190);
    m.Write(0x66889c,GRAPHICS);m.Write(G+0x100,GAME_GRAPHICS);m.Write(GRAPHICS+0x184,0x80000);
    // Live Gear: global capture manager is ready=8, game graphics is a separate
    // object with state=1. Neither equal pointers nor equal states are required.
    m.Write(GAME_GRAPHICS+0x184,0x10000);
    m.Write(0x6b7ba0,BG);m.Layout(BG,"UpCommonBG");
    Widget(m,ALL,"all");Widget(m,LEFT,"picture_left_g");Widget(m,RIGHT,"picture_right_g");
    Widget(m,LP,"picture_left_pane");Widget(m,RP,"picture_right_pane");
    Children(m,BG+8,{ALL});Children(m,ALL,{LEFT,RIGHT});Children(m,LEFT,{LP});Children(m,RIGHT,{RP});
    m.Write(LP+0x28,LG);m.Write(RP+0x28,RG);m.Write(LG+4,4);m.Write(RG+4,4);
    m.Write(LG+0x10,0);m.Write(RG+0x10,0);
    m.Write(MANAGER+8,QUEUE);for(unsigned i=0;i<16;++i)m.Write(QUEUE+0x804+4*i,0);
    m.Write(QUEUE+0x844,0);return m;
}
int main(int argc,char** argv) {
    for(U32 page:{I,K,Q,P}) {
        auto m=BackgroundFixture();m.Write(0x1b03bc,SC);
        const auto visible=[&](){return !Backdrop::HideGameplayMap(m,G);};
        m.Write(SC+8,PLAY);m.Write(SC+12,0);assert(visible());
        // Capturing on recall must already exclude the gameplay minimap,
        // before the destination page becomes current.
        m.Write(SC+12,page);auto before=m.words;
        assert(!visible()&&m.words==before);
        m.Write(SC+8,page);m.Write(SC+12,0);assert(!visible());
        m.Write(SC+12,I);assert(!visible()); // move left from Map to Items
        m.Write(SC+8,I);m.Write(SC+12,0);assert(!visible());
        m.Write(SC+12,PLAY);assert(!visible());
        m.Write(SC+8,PLAY);m.Write(SC+12,0);assert(visible());
        // A second entry, with the gameplay map visible again, follows the
        // same suppression lifetime rather than inheriting an old flag.
        m.Write(SC+12,page);assert(!visible());
        assert(!Backdrop::HideGameplayMap(m,G+4));
        MM3D::GearMenu::enabled=false;assert(visible());MM3D::GearMenu::enabled=true;
        m.words.erase(0x1b03bc);assert(visible());
    }
    {
        auto m=BackgroundFixture(P);m.Write(0x1b03bc,SC);
        m.Byte(P+0x78e,1);assert(!Backdrop::HideGameplayMap(m,G)); // Soaring
        m.Write(SC+8,PLAY);m.Write(SC+12,P);assert(!Backdrop::HideGameplayMap(m,G));
        m.Byte(P+0x78e,0);m.Write(P,0);assert(!Backdrop::HideGameplayMap(m,G));
        m.Write(SC+12,0);assert(!Backdrop::HideGameplayMap(m,G));
    }
    for(bool shared_graphics:{false,true})for(bool keep_scene:{false,true}) {
        // A saved pictograph must not free or replace the scene screenshot.
        // Model both native allocation lifetimes, including the two destructor
        // calls needed on a real exit and only one on a menu tab handoff.
        auto m=BackgroundFixture();Backdrop h;h.Pause(m,G,Q,0xaac,1);
        if(shared_graphics)m.Write(G+0x100,GRAPHICS);
        std::array<U32,7> scene{};
        for(unsigned i=0;i<scene.size();++i)scene[i]=*m.Read(G+0x11010+i*4);
        std::array<U32,16> r{};r[0]=G+0x11010;r[1]=0x320000;r[2]=0x330000;
        r[4]=Q;r[7]=G;r[14]=0x123456;r[15]=0x5a6a20;
        const auto original=r;
        h.GearImageImport(m,r);
        assert(r[14]==0x5a6a20&&r[15]==0x609fa8&&h.HasRetainedCapture());
        for(unsigned i=0;i<14;++i)assert(r[i]==original[i]);
        for(unsigned i=0;i<3;++i)assert(m.Read(G+0x11010+i*4)==0);
        // The import's destructor sees an empty object. Native conversion then
        // owns distinct photo resources and temporarily enters capture state7.
        for(unsigned i=0;i<3;++i)m.Write(G+0x11010+i*4,0x340000+i*0x1000);
        m.Byte(GRAPHICS+0x186,7);h.Tick(m);h.Pass(m,MANAGER,0);
        assert(h.Active(m)&&h.owner==SC&&m.Read(G+0xaac)==0x12340003);
        assert(m.Read(LG+0x10)==scene[2]&&m.Read(RG+0x10)==scene[2]+0x58);
        assert(h.Background(m,G)->textures==scene[2]);
        m.Byte(GRAPHICS+0x186,8);h.Tick(m);
        assert(Backdrop::Source(m,G)->textures==0x342000); // native photo
        assert(h.Background(m,G)->textures==scene[2]); // frozen scene
        // Close releases pause ownership, but retains the scene allocations
        // until the photo's own native destructor has finished.
        h.Release(m);assert(m.Read(G+0xaac)==0x12340001);
        r[0]=G+0x11010;r[4]=G;
        assert(h.CloseGearPhoto(m,r,keep_scene));
        assert(r[14]==0x5a5ff8&&r[15]==0x198864);
        for(unsigned i=0;i<3;++i) {
            assert(m.Read(G+0x11010+i*4)==0x340000+i*0x1000);
            m.Write(G+0x11010+i*4,0); // native destroys only the photo
        }
        m.Byte(GRAPHICS+0x186,0);r[15]=0x5a5ffc;h.FinishGearPhoto(m,r);
        assert(!h.HasRetainedCapture());
        for(unsigned i=0;i<scene.size();++i)assert(m.Read(G+0x11010+i*4)==scene[i]);
        if(keep_scene) {
            assert(r[15]==(shared_graphics?0x5a6000u:0x5a5ffcu));
            if(!shared_graphics)assert(r[0]==GAME_GRAPHICS);
            assert(Byte(m,GRAPHICS+0x186)==8&&Backdrop::Ready(m,G));
            // Reentering Gear can safely retain the same scene again.
            h.Pause(m,G,Q,0xaac,1);r[0]=G+0x11010;r[4]=Q;r[7]=G;
            h.GearImageImport(m,r);assert(h.HasRetainedCapture());
        } else {
            assert(r[15]==0x198864&&r[14]==0x5a5ff8&&r[0]==G+0x11010);
            for(unsigned i=0;i<3;++i)m.Write(G+0x11010+i*4,0); // destroy scene
            r[15]=0x5a5ffc;h.FinishGearPhoto(m,r);
            assert(r[15]==0x5a5ffc&&r[0]==(shared_graphics?GRAPHICS:GAME_GRAPHICS));
            assert(m.Read(G+0x11018)==0&&!Backdrop::Ready(m,G));
        }
    }
    for(unsigned n=0;n<10;++n) {
        auto m=BackgroundFixture();Backdrop h;h.Pause(m,G,Q,0xaac,1);
        std::array<U32,16> r{};r[0]=G+0x11010;r[4]=Q;r[7]=G;
        switch(n) {
        case 0:h.owner=0;break;
        case 1:r[0]+=4;break;
        case 2:r[4]=P;break;
        case 3:r[7]+=4;break;
        case 4:m.Write(SC+8,P);break;
        case 5:m.Write(G+0xaac,1);break;
        case 6:m.Write(G+0x11018,0);break;
        case 7:m.Byte(GRAPHICS+0x186,7);break;
        case 8:MM3D::GearMenu::enabled=false;break;
        case 9:m.words.erase(G+0x11028);break;
        }
        const auto before=m.words;
        h.GearImageImport(m,r);
        assert(r[14]==0x5a6a20&&r[15]==0x609fa8&&m.words==before&&!h.HasRetainedCapture());
        MM3D::GearMenu::enabled=true;
    }
    for(U32 page:{Q,P}) {
        auto m=BackgroundFixture(page);Backdrop h;assert(h.Active(m));
        h.Pause(m,G,page,0xaac,1);assert(m.Read(G+0xaac)==0x12340003);
        assert(m.Read(LG+0x10)==0x290000&&m.Read(RG+0x10)==0x290058);
        h.Pass(m,MANAGER,1);assert(m.Read(QUEUE+0x844)==0);
        h.Pass(m,MANAGER,0);assert(m.Read(QUEUE+4)==BG&&m.Read(QUEUE+0x804)==1&&m.Read(QUEUE+0x844)==1);
        h.Pass(m,MANAGER,0);assert(m.Read(QUEUE+0x844)==1);
        h.Release(m);assert(m.Read(G+0xaac)==0x12340001);
        // Native full pause belongs to the game and is not undone by us.
        h.Pause(m,G,page,0xaac,3);h.Release(m);assert(m.Read(G+0xaac)==0x12340003);
    }
    {
        // A plausible context behind word0 must not substitute for the real
        // inline object. Word0 is the ScreenContext's virtual-method table.
        auto m=BackgroundFixture();Backdrop h;
        for(U32 offset:{4u,8u,12u,16u})m.Write(0x630000+offset,*m.Read(SC+offset));
        m.Write(SC+4,0);auto before=m.words;
        assert(!h.Active(m));h.Pass(m,MANAGER,0);assert(m.words==before);
    }
    for(unsigned n=0;n<10;++n) {
        auto m=BackgroundFixture();Backdrop h;
        switch(n) {
        case 0:m.Write(GRAPHICS+0x184,7<<16);break;
        case 1:m.Write(G+0x11018,0);break;
        case 2:m.Layout(BG,"Other");break;
        case 3:m.Write(ALL+0x18,3);break;
        case 4:m.Write(LEFT+0x18,2);break;
        case 5:m.Write(RP+0x28,LG);break;
        case 6:m.Write(LG+4,3);break;
        case 7:m.Write(SC+8,P);m.Byte(P+0x78e,1);break;
        case 8:m.Write(SC+8,PLAY);break;
        case 9:m.Write(0x66889c,0);break;
        }
        assert(!h.Active(m));auto before=m.words;h.Pass(m,MANAGER,0);assert(m.words==before);
        h.Pause(m,G,*m.Read(SC+8),0xaac,1);assert(m.Read(G+0xaac)==0x12340001);
    }
    {
        auto m=BackgroundFixture();Backdrop h;h.Pause(m,G,Q,0xaac,1);
        m.Write(G+0xaac,0x43210000);h.Release(m);assert(m.Read(G+0xaac)==0x43210000);
        h.Pause(m,G,Q,0xaac,1);MM3D::GearMenu::enabled=false;h.Tick(m);
        assert(m.Read(G+0xaac)==0x43210001);MM3D::GearMenu::enabled=true;
        h.Pause(m,G,Q,0xaac,1);m.Write(UI+0x16c,G+0x40000);
        auto before=m.words;h.Release(m);assert(m.words==before);
    }
    for(U32 page:{Q,P}) {
        auto m=BackgroundFixture(page);Backdrop h;m.Write(SC+8,PLAY);m.Write(SC+12,page);
        std::array<U32,16> r{};r[0]=page;r[1]=SC;r[2]=page==Q?0x5a6d90:0x5998e0;
        assert(h.Prepare(m,r));assert(r[15]==0x1bb320&&r[0]==G+0x11010&&r[14]==0x10fd5c);
        m.Write(GRAPHICS+0x184,1<<16);assert(h.PreparedReady(m,SC,1)==0);
        m.Write(GRAPHICS+0x184,8<<16);assert(h.PreparedReady(m,SC,1)==1&&h.PreparedReady(m,SC,0)==0);
        m.Write(SC+8,page);assert(!h.KeepGearCapture(m,GAME_GRAPHICS,page,G));
        // Replay the native store to the independent per-frame graphics. It
        // enables model depth clearing without restarting the global capture.
        m.Write(GAME_GRAPHICS+0x184,0x55aa);
        MM3D::MenuMemory::Hook recall;
        recall.GearOpenCapture(m,GAME_GRAPHICS,1,page,G);
        assert(m.Read(GAME_GRAPHICS+0x184)==0x155aa);
        assert(m.Read(GRAPHICS+0x184)==0x80000&&h.Active(m));
        m.Write(G+0x100,GRAPHICS);
        assert(h.KeepGearCapture(m,GRAPHICS,page,G)==(page==Q));
        m.Write(G+0x100,GAME_GRAPHICS);
        m.Write(SC+12,0);h.Tick(m);assert(!h.prepare_context);
        // Next reopening must capture again, even with ready-looking old state.
        m.Write(SC+8,PLAY);m.Write(SC+12,page);r[0]=page;r[1]=SC;r[2]=page==Q?0x5a6d90:0x5998e0;
        assert(h.Prepare(m,r));
    }
    {
        auto m=BackgroundFixture(P);Backdrop h;m.Write(SC+8,PLAY);m.Write(SC+12,P);m.Byte(P+0x78e,1);
        std::array<U32,16> r{};r[0]=P;r[1]=SC;r[2]=0x5998e0;assert(!h.Prepare(m,r));
    }
    for(bool keep:{false,true}) {
        auto m=BackgroundFixture(P);Backdrop h;std::array<U32,16> r{};
        r[0]=P;r[1]=SC;r[14]=0x123456;r[15]=0x5970b4;
        h.CloseMap(m,r,keep);assert(r[4]==P);
        if(keep)assert(r[0]==P&&r[1]==SC&&r[14]==0x123456&&r[15]==0x5970b4);
        else assert(r[7]==SC&&r[0]==G+0x11010&&r[14]==0x5970b8&&r[15]==0x198864);
    }
    if(argc>1) {
        std::ifstream f(argv[1],std::ios::binary);assert(f);Memory m;
        std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)),{});
        for(size_t i=0;i+4<=b.size();i+=4)m.Write(0x100000+i,U32(b[i])|U32(b[i+1])<<8|U32(b[i+2])<<16|U32(b[i+3])<<24);
        assert(MM3D::MenuBackdrop::CodeMatches(m));
        for(auto pc:MM3D::MenuBackdrop::PCs) {
            auto w=*m.Read(pc);assert(MM3D::MenuBackdrop::Instruction(pc,w)!=w);
            m.Write(pc,0);assert(!MM3D::MenuBackdrop::CodeMatches(m));m.Write(pc,w);
        }
        m.Write(0x1b03bc,0x72f5c0);assert(!MM3D::MenuBackdrop::CodeMatches(m));
    }
    std::cout<<"Shared menu backdrop checks passed\n";
}
