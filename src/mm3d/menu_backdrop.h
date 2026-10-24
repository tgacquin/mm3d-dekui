// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/menu_memory.h"

namespace MM3D::MenuBackdrop {
using namespace MenuPageSwitch;
// Native GetScreenContext (0x1b03b4) returns this inline object address.
// Its first word is a vtable, not a pointer to another ScreenContext.
constexpr U32 ScreenContext=0x72f5bc;
constexpr U32 FirstSVC=0xfffe88;
constexpr std::array<U32,7> PCs{0x5a6194,0x59930c,0x599350,0x5a5fc8,0x5970b0,0x5a6a1c,0x5a5ff8};
constexpr std::array<U32,7> Words{0xe18160b7,0xe18100b5,0xe18100b5,0xe1a05000,0xe1a04000,0xeb018d61,0xe5940100};
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)if(pc==PCs[i]&&word==Words[i])return 0xef000000|FirstSVC+i;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    if(!MenuMemory::CodeMatches(m))return false;
    for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=Words[i])return false;
    // Inventory Init borrows the persistent UpCommonBG; Close never frees it.
    for(auto [at,w]:std::array<std::pair<U32,U32>,27>{{
        {0x5bfbd8,0xe5960014},{0x5bfbdc,0xe5840028},{0x5bfbf4,0x6b7b8c},
        {0x5c2554,0xe5940028},{0x5c2558,0xe2812058},
        {0x5f8598,0xe5805010},{0x5f85ac,0xe5804010},{0x59936c,0xaac},
        {0x5970b4,0xe1a07001},{0x5970b8,0xe59f0374},
        {0x198864,0xe92d4070},{0x1988b8,0xe5845008},
        {0x1b03b4,0xe59f0000},{0x1b03b8,0xe12fff1e},{0x1b03bc,ScreenContext},
        // Gear's optional image import destroys the same capture that our
        // shared backdrop owns. Verify both continuations and the destructor.
        {0x5a6a14,0xe2870a11},{0x5a6a18,0xe2800010},
        {0x5a6a20,0xe5c45194},{0x5a6a24,0xe320f000},{0x5a6a28,0xea000001},
        {0x5a6a2c,0xe3a00001},{0x5a6a30,0xe5c40194},
        {0x609fa8,0xe92d4070},{0x609fb8,0xebee3a29},{0x609fd8,0xe3a00006},
        {0x609fe4,0xe1c108b6},{0x609fec,0x66889c}}})
        if(m.Read(at)!=w)return false;
    return true;
}
struct Picture { U32 layout=0,textures=0;std::array<U32,2> geometry{}; };
struct Hook {
    U32 owner=0,game=0,page=0; // Only the extra PauseDraw bit is owned.
    U32 prepare_context=0,prepare_game=0,prepare_source=0,prepare_target=0;
    // Gear's saved pictograph and our scene backdrop normally share a native
    // capture object. Retain the scene's resources while that object loads the
    // photo, then restore or destroy them through the native Close lifecycle.
    U32 retained_game=0,retained_page=0,retained_manager=0,retained_state=0;
    std::array<U32,7> retained_capture{};
    bool closing_photo=false,keep_scene_on_close=false;
    bool HasRetainedCapture() const {return retained_game!=0;}
    static bool Enabled(){return NativeInventory::enabled.load()&&GearMenu::enabled.load();}
    template<class M> static U32 Game(M& m,U32 ctx) {
        if(!Pointer(ctx,20))return 0;
        auto ui=m.Read(ctx+4);auto g=ui&&Pointer(*ui,0x170)?m.Read(*ui+0x16c):std::nullopt;
        return g&&Pointer(*g,0x11030)&&Byte(m,*g+0x13d)==4?*g:0;
    }
    template<class M> static bool Page(M& m,U32 p) {
        return Pointer(p,0x794)&&((m.Read(0x656b0c)==p&&m.Read(p)==GearVtable)||
            (m.Read(0x656b14)==p&&m.Read(p)==MapVtable&&Byte(m,p+0x78e)==0));
    }
    template<class M> static bool HideGameplayMap(M& m,U32 g) {
        // Recalled Map/Gear capture while the gameplay screen is still current.
        // Its lower-screen map draw is normally harmless, but our top-screen
        // relocation would bake it into the shared pause screenshot. Suppress
        // that draw from the pending request through the final pause handoff.
        if(!Enabled()||!g||m.Read(0x1b03bc)!=ScreenContext||Game(m,ScreenContext)!=g)return false;
        const auto current=m.Read(ScreenContext+8),pending=m.Read(ScreenContext+12);
        if(!current||!pending)return false;
        const auto pause_page=[&](U32 p) {
            const auto type=MenuMemory::Hook{}.Page(m,p);
            return type&&(type!=MapVtable||Byte(m,p+0x78e)==0);
        };
        return pause_page(*current)||pause_page(*pending);
    }
    template<class M> static bool Ready(M& m,U32 g) {
        auto graphics=m.Read(0x66889c),t=m.Read(g+0x11018);
        // Native 0x1bdf28 checks the global capture manager. game+0x100 is a
        // different graphics object (live: 0x0ffffd88 vs 0x007892d0), whose byte
        // can remain 1 while this manager's completed screenshot is state8.
        return graphics&&Pointer(*graphics,0x188)&&
            Byte(m,*graphics+0x186)==8&&t&&Pointer(*t,0xb0);
    }
    template<class M> static std::optional<Picture> Source(M& m,U32 g,bool ready=true) {
        if(ready&&!Ready(m,g))return {};
        Picture p;auto l=m.Read(0x6b7b8c+0x14);
        if(!l||!LayoutNamed(m,*l,"UpCommonBG"))return {};
        p.layout=*l;p.textures=m.Read(g+0x11018).value_or(0);
        // Exact native tree: all -> left/right eye -> captured-scene picture.
        auto list=m.Read(*l+8+0x14);
        if(!list||!Pointer(*list)||m.Read(*l+8+0x18)!=1)return {};
        auto all=m.Read(*list);
        if(!all||!NativeInventory::Named(m,*all,"all"))return {};
        auto groups=m.Read(*all+0x14);
        if(!groups||!Pointer(*groups,8)||m.Read(*all+0x18)!=2)return {};
        for(unsigned i=0;i<2;++i) {
            auto group=m.Read(*groups+i*4);if(!group)return {};
            unsigned eye;
            if(NativeInventory::Named(m,*group,"picture_left_g"))eye=0;
            else if(NativeInventory::Named(m,*group,"picture_right_g"))eye=1;
            else return {};
            auto children=m.Read(*group+0x14);
            if(!children||!Pointer(*children)||m.Read(*group+0x18)!=1)return {};
            auto pane=m.Read(*children);
            if(!pane||!NativeInventory::Named(m,*pane,eye?"picture_right_pane":"picture_left_pane"))return {};
            auto geom=m.Read(*pane+0x28);
            if(!geom||!Pointer(*geom,0x14)||p.geometry[eye])return {};
            auto type=m.Read(*geom+4);
            if(!type||(*type&0xffff)<4||(*type&0xffff)>7||!m.Read(*geom+0x10))return {};
            p.geometry[eye]=*geom;
        }
        if(!p.geometry[0]||!p.geometry[1]||p.geometry[0]==p.geometry[1])return {};
        return p;
    }
    template<class M> static void Bind(M& m,const Picture& p) {
        // Same two descriptor writes as native 0x5f857c, no new resources.
        for(unsigned eye=0;eye<2;++eye)m.Write(p.geometry[eye]+0x10,p.textures+eye*0x58);
    }
    template<class M> std::optional<Picture> Background(M& m,U32 g) const {
        if(g!=retained_game)return Source(m,g);
        if(m.Read(ScreenContext+8)!=retained_page)return {};
        auto picture=Source(m,g,false);
        if(picture)picture->textures=retained_capture[2];
        return picture;
    }
    template<class M> bool Active(M& m) const {
        if(!Enabled())return false;
        auto current=m.Read(ScreenContext+8);const U32 g=Game(m,ScreenContext);
        return g&&current&&Page(m,*current)&&Background(m,g).has_value();
    }
    template<class M> void Release(M& m) {
        if(owner&&Game(m,owner)==game&&m.Read(owner+8)==page) {
            auto flags=m.Read(game+0xaac);
            if(flags&&(*flags&0xffff)==3)m.Write(game+0xaac,*flags&~2u);
        }
        owner=game=page=0;
    }
    template<class M> void Pause(M& m,U32 g,U32 p,U32 offset,U32 value) {
        auto flags=m.Read(g+offset);if(!flags)return;
        if(offset==0xaac&&Enabled()&&Game(m,ScreenContext)==g&&m.Read(ScreenContext+8)==p&&Page(m,p)) {
            if(auto picture=Source(m,g)) {
                Bind(m,*picture);
                if(value==1){owner=ScreenContext;game=g;page=p;value=3;}
            }
        }
        m.Write(g+offset,(*flags&0xffff0000u)|(value&0xffff));
    }
    template<class M> void Tick(M& m) {
        if(owner&&!Active(m))Release(m);
        if(prepare_context&&(Game(m,prepare_context)!=prepare_game||
           m.Read(prepare_context+12)!=prepare_target))
            prepare_context=prepare_game=prepare_source=prepare_target=0;
    }
    template<class M,class R> void GearImageImport(M& m,R& r) {
        const bool retain=!HasRetainedCapture()&&owner==ScreenContext&&game==r[7]&&page==r[4]&&
            r[0]==game+0x11010&&m.Read(ScreenContext+8)==page&&
            m.Read(page)==GearVtable&&Game(m,ScreenContext)==game&&
            (m.Read(game+0xaac).value_or(0)&0xffff)==3&&Active(m);
        if(retain) {
            std::array<U32,7> snapshot{};
            bool valid=true;
            for(unsigned i=0;i<snapshot.size();++i) {
                auto word=m.Read(r[0]+i*4);
                if(!word){valid=false;break;}
                snapshot[i]=*word;
            }
            const U32 manager=m.Read(0x66889c).value_or(0);
            const auto state=Pointer(manager,0x188)?m.Read(manager+0x184):std::nullopt;
            if(valid&&state) {
                retained_game=game;retained_page=page;retained_manager=manager;
                retained_state=*state;retained_capture=snapshot;
                // The import's destructor owns only these three allocations.
                // Detach them first; do not copy or alias the photo's buffers.
                for(unsigned i=0;i<3;++i)m.Write(r[0]+i*4,0);
            }
        }
        // Always load the actual pictograph and retain native photo controls.
        r[14]=PCs[5]+4;r[15]=0x609fa8;
    }
    template<class M,class R> bool CloseGearPhoto(M& m,R& r,bool keep_scene) {
        if(!HasRetainedCapture()||r[0]!=retained_game+0x11010||
           Game(m,ScreenContext)!=retained_game||m.Read(ScreenContext+8)!=retained_page)return false;
        closing_photo=true;keep_scene_on_close=keep_scene;
        // Even a tab handoff must destroy the photo before restoring the scene.
        r[14]=PCs[6];r[15]=0x198864;return true;
    }
    template<class M,class R> void FinishGearPhoto(M& m,R& r) {
        if(closing_photo&&r[4]==retained_game&&Game(m,ScreenContext)==retained_game) {
            const U32 capture=retained_game+0x11010;
            for(unsigned i=0;i<retained_capture.size();++i)m.Write(capture+i*4,retained_capture[i]);
            const bool keep=keep_scene_on_close;
            const bool shared_graphics=m.Read(retained_game+0x100)==retained_manager;
            if(keep&&m.Read(0x66889c)==retained_manager) {
                auto state=m.Read(retained_manager+0x184);
                if(state)m.Write(retained_manager+0x184,(*state&0xffffu)|(retained_state&0xffff0000u));
            }
            retained_game=retained_page=retained_manager=retained_state=0;
            retained_capture={};closing_photo=keep_scene_on_close=false;
            if(!keep) {
                // Real menu exit: dispose of the restored scene too, then
                // return here to replay the original graphics-owner load.
                r[0]=capture;r[14]=PCs[6];r[15]=0x198864;return;
            }
            // Match MenuPageSwitch's ordinary handoff: if graphics aliases
            // the capture manager, do not zero its restored ready-state byte.
            if(shared_graphics){r[15]=0x5a6000;return;}
        }
        r[0]=m.Read(r[4]+0x100).value_or(0); // original LDR r0,[r4,#0x100]
    }
    // Dedicated ordinary Map/Gear entry also takes a fresh native capture.
    // Recalled entries are handled first by MenuMemory, tab handoffs keep theirs.
    template<class M,class R> bool Prepare(M& m,R& r) {
        const U32 g=Game(m,r[1]);auto current=m.Read(r[1]+8);
        if(!Enabled()||!g||!current||m.Read(0x656b04)!=current||
           m.Read(r[1]+12)!=r[0]||!Page(m,r[0])||!Source(m,g,false)||
           !((m.Read(r[0])==GearVtable&&r[2]==0x5a6d90)||
             (m.Read(r[0])==MapVtable&&r[2]==0x5998e0)))return false;
        prepare_context=r[1];prepare_game=g;prepare_source=*current;prepare_target=r[0];
        r[14]=MenuMemory::PCs[0]+4;r[0]=g+0x11010;r[1]=0;r[15]=0x1bb320;return true;
    }
    template<class M> U32 PreparedReady(M& m,U32 ctx,U32 value) const {
        if(ctx==prepare_context&&Game(m,ctx)==prepare_game&&
           m.Read(ctx+8)==prepare_source&&m.Read(ctx+12)==prepare_target&&!Ready(m,prepare_game))return 0;
        return value;
    }
    template<class M> bool KeepGearCapture(M& m,U32 graphics,U32 p,U32 g) const {
        // Only suppress a store to the actual capture manager. The separate
        // game graphics byte must receive native state1: its nonzero value
        // enables the depth/stencil clear before menu models (110cf4..110d0c).
        return Enabled()&&m.Read(0x66889c)==graphics&&Game(m,ScreenContext)==g&&m.Read(ScreenContext+8)==p&&
            m.Read(p)==GearVtable&&Page(m,p)&&m.Read(g+0x100)==graphics&&Source(m,g).has_value();
    }
    template<class M,class R> void CloseMap(M& m,R& r,bool keep_capture) {
        Release(m);
        r[4]=r[0]; // original MOV r4,r0, after native PUSH saved the caller's LR
        const U32 g=Game(m,r[1]);
        if(keep_capture||!g||m.Read(r[0])!=MapVtable||!Page(m,r[0])||!Source(m,g))return;
        // Replay MOV r7,r1 before the native capture destructor clobbers r1.
        // Resume at the following LDR; saved LR and callee-saved registers are
        // already on Map Close's own stack, so its normal cleanup still runs.
        r[7]=r[1];r[0]=g+0x11010;r[14]=0x5970b8;r[15]=0x198864;
    }
    template<class M> void Pass(M& m,U32 manager,U32 pass) {
        if(pass!=0||!Active(m)||!Pointer(manager,0x24))return;
        auto picture=Background(m,Game(m,ScreenContext));
        auto queue=m.Read(manager+8);NativeInventory::Hook helper;NativeInventory::Hook::Buckets entries;
        if(!picture||!queue||!helper.ReadQueue(m,*queue,entries))return;
        for(const auto& bucket:entries)
            if(std::find(bucket.begin(),bucket.end(),picture->layout)!=bucket.end())return;
        if(entries[0].size()==32)return;
        Bind(m,*picture);
        // Draw before menu layouts, using the same screen/bucket as Items.
        for(unsigned i=entries[0].size();i>0;--i)m.Write(*queue+4+i*4,entries[0][i-1]);
        m.Write(*queue+4,picture->layout);
        m.Write(*queue+0x804,U32(entries[0].size()+1));
        m.Write(*queue+0x844,*m.Read(*queue+0x844)+1);
    }
};
}
