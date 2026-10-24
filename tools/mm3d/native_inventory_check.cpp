#include "mm3d/cursor_interpolation.h"
// Focused guest-memory checks: clang++ -std=c++20 -Isrc tools/mm3d/native_inventory_check.cpp -o /tmp/inventory-check
#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include "mm3d/native_inventory.h"
#include "mm3d/camera_live.h"
#include "mm3d/item_preview.h"
#include "mm3d/native_notebook.h"
#include "mm3d/native_file_select.h"
#include "mm3d/file_select_input.h"
#include "mm3d/file_select_prompt.h"
#include "mm3d/ocarina_prompt.h"
#include "mm3d/items_map_prompt.h"
#include "mm3d/menu_help.h"
#include "mm3d/menu_tabs.h"
#include "mm3d/cpu_interpolation_capture.h"
#include "mm3d/world_map_prompt.h"
#include "mm3d/magic_border.h"
#include "mm3d/minigame_hud.h"
#include "mm3d/song_list.h"
#include "mm3d/double_time.h"
#include "mm3d/song_reset.h"
#include "mm3d/hud_buttons.h"
#include "mm3d/widescreen_probe.h"
#include "mm3d/banners.h"
#include "mm3d/song_effect.h"
#include "mm3d/dungeon_map.h"
#include "mm3d/inventory_shadow.h"
#include "mm3d/message_layout.h"
#include "mm3d/native_item_present.h"
#include "mm3d/inventory_navigation.h"
#include "mm3d/native_startup.h"
using namespace MM3D::NativeInventory;
struct Memory {
    std::unordered_map<U32,U32> words;
    U32 next=0x1000000;
    std::optional<U32> Read(U32 p) { auto i=words.find(p);return i==words.end()?std::nullopt:std::optional<U32>(i->second); }
    void Write(U32 p,U32 v) { assert(words.contains(p));words[p]=v; }
    U32 Alloc(U32 size) { U32 p=next;next+=(size+3)&~3u;for(U32 i=p;i<next;i+=4)words[i]=0;return p; }
    U32 String(const char* s) {
        const auto n=std::char_traits<char>::length(s)+1;U32 p=Alloc(n);
        for(unsigned i=0;i<n;++i) words[p+(i&~3u)]|=U32(static_cast<unsigned char>(s[i]))<<((i&3)*8);
        return p;
    }
    void WidgetAt(U32 p,const char* name,float px=0,float py=0) {
        words[p+0x10]=String(name);words[p+0x24]=0xffff;words[p+0xd4]=15;
        for(U32 i=0;i<3;++i) words[p+0xe0+i*16+i*4]=std::bit_cast<U32>(1.f);
        words[p+0xec]=std::bit_cast<U32>(px);words[p+0xfc]=std::bit_cast<U32>(py);
    }
    U32 Widget(const char* name,float px=0,float py=0) { auto p=Alloc(0x140);WidgetAt(p,name,px,py);return p; }
    U32 Layout(const char* name,float px=0,float py=0,U32 size=0x200) {
        auto p=Alloc(size),resource=Alloc(0x20);words[p+4]=resource;words[resource+0x18]=String(name);
        WidgetAt(p+8,"root",px,py);
        words[p+0x44]=std::bit_cast<U32>(px);words[p+0x48]=std::bit_cast<U32>(py);return p;
    }
    void Children(U32 p,const std::vector<U32>& nodes) {
        auto list=Alloc(nodes.size()*4);words[p+0x14]=list;words[p+0x18]=nodes.size();
        for(unsigned i=0;i<nodes.size();++i) words[list+i*4]=nodes[i];
    }
    U32 Brown(U32 layout) {
        auto all=Widget("all"),left=Widget("common_bottom_bg_pane0"),right=Widget("common_bottom_bg_pane1");
        Children(layout+8,{all});Children(all,{left,right});
        for(U32 node:{left,right})for(U32 off:{0x128u,0x12cu,0x130u,0x134u})words[node+off]=std::bit_cast<U32>(1.f);
        return right;
    }
    void Queue(U32 p,const std::vector<U32>& nodes) {
        for(unsigned i=0;i<nodes.size();++i)words[p+4+0x80+i*4]=nodes[i];
        words[p+0x808]=nodes.size();words[p+0x844]=nodes.size();
    }
};
void SmallGameplayMapChecks(){
    namespace N=MM3D::NativeMinimap;
    Memory m;N::Hook hook;constexpr U32 state=0x6890f4,mode=0x7786f8;
    auto header=m.Alloc(0x20);m.words[0x36e2a0]=0x7781d8;m.words[state]=header;
    const auto game=m.Alloc(0xf0d8),actor=m.Alloc(0x30),room=m.Alloc(0x728);
    m.words[game+0x20dc]=actor;m.words[header]=room;m.words[header+4]=1;
    m.words[header+0x10]=std::bit_cast<U32>(1.f);
    m.words[actor+0x24]=std::bit_cast<U32>(40.f);m.words[actor+0x2c]=std::bit_cast<U32>(30.f);
    m.words[state+0x54]=0;m.words[state+0x5c]=0;
    m.words[state+0x50]=160|(180u<<16); // zoomed mode's stale player anchor
    m.words[0x727970+0x834]=0x01123456; // large markers plus unrelated packed bytes
    m.words[header+0xc]=0xfff60014; // center20,-10
    m.words[state+0x20]=std::bit_cast<U32>(.5f);
    for(unsigned zoom_mode:{1u,2u,3u})for(float rotation:{0.f,.75f,-1.5f}){
        m.words[mode]=(zoom_mode<<24)|0x123456;
        m.words[state+0x28]=std::bit_cast<U32>(rotation);
        m.words[state+0x1c]=std::bit_cast<U32>(2.f);m.words[state+0x2c]=std::bit_cast<U32>(42.f);m.words[state+0x30]=std::bit_cast<U32>(73.f);
        auto before=m.words;hook.SmallGameplayMap(m,game);
        assert(m.words[mode]==0x03123456&&m.words[state+0x1c]==m.words[state+0x20]);
        assert(std::bit_cast<float>(m.words[state+0x28])==0.f);
        assert(std::bit_cast<float>(m.words[state+0x2c])==20&&std::bit_cast<float>(m.words[state+0x30])==-10);
        assert(m.words[state+0x50]==(170u|(140u<<16))); // player projected onto overview
        assert(m.words[0x727970+0x834]==0x00123456); // small icons, neighboring fields preserved
        hook.RestoreZoom(m);assert(m.words==before);
    }
    // Fixed-room marker and special-scene position scaling follow native rules.
    m.words[mode]=0x02123456;m.words[room]=8|(60u<<16);m.words[room+4]=10;
    auto saved=m.words;hook.SmallGameplayMap(m,game);
    assert(m.words[state+0x50]==(180u|(130u<<16)));hook.RestoreZoom(m);assert(m.words==saved);
    m.words[room]=0;m.words[game+0xf0d4]=0x00010000;
    saved=m.words;hook.SmallGameplayMap(m,game);
    assert(m.words[state+0x50]==(350u|(275u<<16)));hook.RestoreZoom(m);assert(m.words==saved);
    m.words[game+0xf0d4]=0;
    m.words.erase(state+0x28);auto incomplete=m.words;hook.SmallGameplayMap(m,game);assert(m.words==incomplete&&hook.zoom_restore.empty());
    m.words[state+0x28]=std::bit_cast<U32>(.75f);
    m.words[mode]=0x00123456;auto before=m.words;hook.SmallGameplayMap(m,game);assert(m.words==before&&hook.zoom_restore.empty());
    m.words[mode]=0x01123456;m.words.erase(state+0x30);before=m.words;hook.SmallGameplayMap(m,game);assert(m.words==before&&hook.zoom_restore.empty());
}
void FlipPeakChecks(){
    namespace H=MM3D::HudButtons;
    Memory m;H::Hook hook;auto all=m.Widget("action_button_g");std::vector<U32> icons,nodes;
    for(const char* name:{"a_action_button","b_action_button"}){
        auto icon=m.Widget(name),layout=m.Layout("Action"),text=m.Widget("a_action_text",100,60);
        icons.push_back(icon);nodes.push_back(text);m.words[icon+0x20]=layout;m.Children(layout+8,{text});
        m.words[text+0x48]=std::bit_cast<U32>(1.2f);m.words[text+0x4c]=std::bit_cast<U32>(1.1f);
        m.words[text+0xe0]=std::bit_cast<U32>(2.4f);m.words[text+0xf4]=std::bit_cast<U32>(1.1f);
    }
    m.Children(all,icons);const auto before=m.words;
    for(float peak:{1.f,1.1f,1.2f}){
        H::flip_peak=peak;hook.frames.emplace_back();hook.AdjustPulse(m,all);
        for(auto n:nodes){assert(std::abs(std::bit_cast<float>(*m.Read(n+0xe0))-2*peak)<.0001f);
            assert(std::abs(std::bit_cast<float>(*m.Read(n+0xf4))-(1+(peak-1)*.5f))<.0001f);
            assert(std::bit_cast<float>(*m.Read(n+0xec))==100);}
        hook.End(m);assert(m.words==before);
    }
    H::Apply(H::Options{});
}
void HudButtonChecks(){
    namespace H=MM3D::HudButtons;
    Memory m;H::Hook hook;H::Options o;o.buttons[1]={12,-5,1};o.scale=2;
    assert(!H::LoadSlot(o,0));H::SaveSlot(o,0);o.buttons[1]={30,20,1};o.scale=.5f;H::SaveSlot(o,1);
    assert(H::LoadSlot(o,0)&&o.buttons[1].x==12&&o.scale==2);
    H::Apply(o);
    auto layout=m.Layout("HudItemIconSet"),all=m.Widget("all");m.Children(layout+8,{all});
    std::vector<U32> groups;std::array<U32,4> icons{};std::array<std::array<U32,3>,4> contents{};
    const char* names[]{"icon_item_1_g","icon_item_x_g","icon_item_y_g","icon_item_2_g"};
    const char* bases[]{"btn_corner01_g","btn_x_g","btn_y_g","btn_corner03_g"};
    for(unsigned i=0;i<4;++i){icons[i]=m.Widget(names[i],300+i*10,20);groups.push_back(icons[i]);groups.push_back(m.Widget(bases[i]));}
    for(unsigned i=0;i<4;++i) {
        unsigned c=0;for(const char* name:{"letter","item","count"}) {
            contents[i][c]=m.Widget(name,301+i*10+c,21+c);++c;
        }
        m.Children(icons[i],{contents[i][0],contents[i][1],contents[i][2]});
    }
    m.Children(all,groups);auto original=m.words;
    hook.Begin(m,layout);assert(std::bit_cast<float>(*m.Read(icons[1]+0xec))==322);
    assert(std::bit_cast<float>(*m.Read(icons[1]+0xfc))==15);
    assert(std::bit_cast<float>(*m.Read(icons[1]+0xe0))==2);
    assert(*m.Read(icons[0]+0xec)==original.at(icons[0]+0xec));
    assert(std::bit_cast<float>(*m.Read(icons[0]+0xfc))==68);
    assert(std::bit_cast<float>(*m.Read(icons[3]+0xfc))==68);
    assert(std::bit_cast<float>(*m.Read(icons[0]+0xe0))==2);
    auto transformed=m.words;hook.Begin(m,layout);assert(m.words==transformed);hook.End(m);
    hook.End(m);assert(m.words==original);
    auto hud=m.Layout("Hud2"),hudall=m.Widget("all"),buttons=m.Widget("item_button_g"),set=m.Widget("item_icon_set");
    m.Children(hud+8,{hudall});m.Children(hudall,{buttons});m.Children(buttons,{set});m.words[set+0x20]=layout;
    original=m.words;hook.Begin(m,hud);assert(std::bit_cast<float>(*m.Read(icons[1]+0xec))==322);hook.End(m);assert(m.words==original);
    auto actions=m.Layout("Hud"),action_all=m.Widget("all"),action_group=m.Widget("action_button_g");
    auto a=m.Widget("a_action_button",360,30),b=m.Widget("b_action_button",340,50);
    m.Children(actions+8,{action_all});m.Children(action_all,{action_group});m.Children(action_group,{a,b});
    o.buttons[0]={-15,12,1};o.buttons[3]={-20,18,1};H::Apply(o);original=m.words;
    hook.Begin(m,actions);assert(std::bit_cast<float>(*m.Read(a+0xec))==345);
    assert(std::bit_cast<float>(*m.Read(b+0xfc))==68);
    assert(std::bit_cast<float>(*m.Read(a+0xe0))==2&&std::bit_cast<float>(*m.Read(b+0xe0))==2);
    hook.End(m);assert(m.words==original);
    o.diamond={true,200,100,30,35};H::SaveSlot(o,2);o.diamond.spacing=0;
    assert(H::LoadSlot(o,2)&&o.diamond.spacing==30&&o.diamond.rotation==35);
    for(float gap:{0.f,30.f})for(float angle:{0.f,35.f,90.f}){
        o.diamond.spacing=gap;o.diamond.rotation=angle;H::Apply(o);original=m.words;
        hook.Begin(m,layout);
        std::array<std::array<float,2>,4> xy{};
        for(unsigned i=0;i<4;++i){xy[i]={std::bit_cast<float>(*m.Read(icons[i]+0xec)),std::bit_cast<float>(*m.Read(icons[i]+0xfc))};
            assert(std::abs(std::hypot(xy[i][0]-200,xy[i][1]-100)-gap)<.001f);}
        assert(std::abs(std::hypot(xy[0][0]-xy[2][0],xy[0][1]-xy[2][1])-2*gap)<.001f);
        hook.End(m);assert(m.words==original);
    }
    // Both individual layouts and saved diamond layouts exchange complete
    // groups without changing stored coordinates, native data, or X/Y/A/B.
    o.buttons[4]={13,17,1};o.buttons[5]={-23,37,1};
    for(bool diamond:{false,true})for(bool clicks:{false,true,false}) {
        o.diamond.enabled=diamond;o.diamond.spacing=31;o.diamond.rotation=25;
        H::SaveSlot(o,1);const auto saved=o.presets[1];H::Apply(o);
        MM3D::StickControls::Configure(clicks);original=m.words;
        hook.Begin(m,layout);
        for(unsigned n:{0u,3u}) {
            const unsigned index=n==0?4:5,slot=MM3D::StickControls::ItemLayoutIndex(index,4);
            const unsigned source=slot==4?0:3;
            const auto target=diamond?H::DiamondPoint(slot,o.diamond):
                std::array<float,2>{300.f+source*10+o.buttons[slot].x,20+o.buttons[slot].y};
            assert(std::abs(std::bit_cast<float>(*m.Read(icons[n]+0xec))-target[0])<.001f);
            assert(std::abs(std::bit_cast<float>(*m.Read(icons[n]+0xfc))-target[1])<.001f);
            for(unsigned c=0;c<3;++c) {
                assert(std::abs(std::bit_cast<float>(*m.Read(contents[n][c]+0xec))-(target[0]+(1+c)*o.scale))<.001f);
                assert(std::abs(std::bit_cast<float>(*m.Read(contents[n][c]+0xfc))-(target[1]+(1+c)*o.scale))<.001f);
            }
        }
        hook.End(m);assert(m.words==original);
        assert(H::LoadSlot(o,1)&&o.buttons[4].x==saved[4].x&&o.buttons[5].y==saved[5].y);
    }
    MM3D::StickControls::Configure(false);H::Apply(H::Options{});
}
void FullscreenEffectsChecks(){
    namespace W=MM3D::WidescreenProbe;
    W::Stop();W::saved_draws.clear();
    auto bloom=W::TransitionOverlay;bloom.textures=3;bloom.blend=65553;bloom.depth=3905;
    auto blur=bloom;blur.blend=65654;
    W::saved_draws={bloom,blur};
    assert(W::Observe(W::TransitionOverlay,{}));assert(W::Observe(bloom,{}));assert(W::Observe(blur,{}));
    auto other=blur;other.vertices=18;assert(!W::Observe(other,{}));
    W::saved_draws.erase(W::saved_draws.begin()+1);
    assert(W::Observe(bloom,{}));assert(!W::Observe(blur,{}));assert(W::Observe(W::TransitionOverlay,{}));
    W::candidates={{blur,1,{}}};W::selected=0;W::test_until=W::Now()+1000;
    assert(W::Observe(blur,{}));assert(W::Observe(bloom,{}));W::Stop();
    assert(!W::Observe(blur,{}));assert(W::Observe(bloom,{}));
    W::saved_draws.clear();W::candidates.clear();
}
void SongResetChecks(){
    namespace R=MM3D::SongReset;
    Memory m;R::Hook hook;R::Options o;o.poses[R::Items]={10,20,.5f};R::Apply(o);
    auto panel=m.Layout("ReturnToStart"),all=m.Widget("all");m.Children(panel+8,{all});
    std::vector<U32> children;
    for(const char* name:{"item_set_pos_g","item_panel_g","right_base_g","under_set_icon_g","under_set_panel_g",
        "lost_item_base_bin00","lost_item_base_bin01","lost_item_base_bin02","lost_item_base_bin03","lost_item_base_bin04","lost_item_base_bin05","lost_item_base_bin06","lost_item_base_bow"})children.push_back(m.Widget(name,100,100));
    m.Children(all,children);hook.panel=panel;hook.frame=o.poses;hook.pass=3;auto before=m.words;
    hook.Begin(m,panel);assert(std::bit_cast<float>(*m.Read(children[0]+0xec))==60);
    assert(std::bit_cast<float>(*m.Read(children[12]+0xec))==60);
    assert(std::bit_cast<float>(*m.Read(children[2]+0xec))==140);
    hook.End(m);assert(m.words==before);
    auto bg=m.Layout("CommonBottomBg");m.Brown(bg);
    auto manager=m.Alloc(40),bottom=m.Alloc(0x848),top=m.Alloc(0x848),bf=m.Alloc(0x848),bt=m.Alloc(0x848);
    m.words[manager+28]=bottom;m.words[manager+20]=top;m.words[manager+24]=bf;m.words[manager+12]=bt;
    m.Queue(bottom,{panel});m.Queue(bf,{bg});
    const auto old_width=shadow_width.load(),old_opacity=shadow_opacity.load();shadow_width=1;shadow_opacity=1;
    hook.Pass(m,manager,4);assert(hook.panel==panel);assert(*m.Read(bottom+0x844)==0);
    assert(*m.Read(bt+0x804)==26);assert(hook.helper.helper.brown_shadow_steps==24);
    const auto message=m.Layout("MessageWindow"),other=m.Layout("Other");
    assert(hook.IsDescription(m,message));assert(!hook.IsDescription(m,other));
    R::enabled=false;assert(!hook.IsDescription(m,message));R::enabled=true;
    hook.pass=1;before=m.words;
    for(unsigned i=0;i<26;++i){hook.Begin(m,bg);hook.End(m);assert(m.words==before);}
    hook.Pass(m,manager,4);assert(!hook.IsDescription(m,message));
    shadow_width=old_width;shadow_opacity=old_opacity;R::Apply(R::Options{});
}
void BannerChecks(){
    namespace B=MM3D::Banners;
    Memory m;B::Hook hook;B::Options o;
    const auto layout=m.Layout("Telop"),all=m.Widget("all"),text=m.Widget("text"),plate=m.Widget("plate");
    m.Children(layout+8,{all});m.Children(all,{text,plate});
    std::vector<U32> texts,plates,panes;
    for(unsigned i=0;i<2;++i){
        auto t=m.Widget(i?"e_time_telop_text_anim":"e_map_telop_text_anim");texts.push_back(t);
        auto a=m.Widget(i?"timeTelop_anim":"mapTelop_anim");plates.push_back(a);
        auto p=m.Widget(i?"timeTelop_pane00":"mapTelop_pane00");panes.push_back(p);m.Children(a,{p});
        for(U32 off:{0x128u,0x12cu,0x130u,0x134u})m.words[p+off]=std::bit_cast<U32>(.75f);
        o.parts[i].brightness=.64f;o.parts[i].height=1.5f;o.parts[i].text_scale=.8f;
    }
    m.Children(text,texts);m.Children(plate,plates);B::Apply(o);const auto before=m.words;
    hook.Begin(m,layout,true);
    for(unsigned i=0;i<2;++i){const auto p=panes[i];
        assert(std::abs(std::bit_cast<float>(*m.Read(p+0xec))+400.f/30)<.0001f);
        assert(std::abs(std::bit_cast<float>(*m.Read(p+0xe0))*400+std::bit_cast<float>(*m.Read(p+0xec))-413.33333f)<.0001f);
        assert(std::abs(std::bit_cast<float>(*m.Read(p+0x128))-.48f)<.0001f);
        assert(*m.Read(p+0x134)==before.at(p+0x134));
    }
    hook.End(m);assert(m.words==before);
    MM3D::HudAdjustments::Options hud;hud.banner_darkness=.5f;hud.banner_y=24;MM3D::HudAdjustments::Apply(hud);
    hook.Begin(m,layout,false);
    for(unsigned i=0;i<panes.size();++i) {
        const auto pane=panes[i];
        assert(std::abs(std::bit_cast<float>(*m.Read(pane+0x128))-(i?.48f:.24f))<.0001f);
        assert(*m.Read(pane+0x134)==before.at(pane+0x134));
        const float offset=i?0.f:24.f;
        assert(std::abs(std::bit_cast<float>(*m.Read(pane+0xfc))-((i?72.f:66.f)*(1.f-1.5f)+offset))<.0001f);
        assert(std::abs(std::bit_cast<float>(*m.Read(texts[i]+0xfc))-((i?120.f:66.f)*(1.f-.8f)+offset))<.0001f);
    }
    hook.End(m);assert(m.words==before);MM3D::HudAdjustments::Apply({});
    hook.Begin(m,layout,false);assert(std::bit_cast<float>(*m.Read(panes[0]+0xe0))==1);hook.End(m);assert(m.words==before);
    const auto other=m.Layout("Other");const auto unrelated=m.words;hook.Begin(m,other,true);assert(m.words==unrelated);hook.End(m);
    m.words[panes[1]+0x128]=0x7fc00000;const auto invalid=m.words;hook.Begin(m,layout,true);assert(m.words==invalid);hook.End(m);
    B::Apply(B::Options{});
    for(const char* name:{"MenuItem","MenuMask","MenuCollect","MenuQuestlist","MenuSchedule","MenuMapDungeon","UpWorldMapHud"}) {
        Memory q;B::Hook router;const auto manager=q.Alloc(0x24);
        std::array<U32,4> queues;
        for(unsigned i=0;i<4;++i){queues[i]=q.Alloc(0x848);q.words[manager+8+4*i]=queues[i];}
        const auto menu=q.Layout(name),banner=q.Layout("Telop"),back=q.Layout("CommonBottomBg"),scene=q.Layout("UpCommonBG"),other=q.Layout("Other");
        q.Queue(queues[0],{scene,back});q.Queue(queues[3],{menu,banner,other});
        auto original=q.words;router.Pass(q,manager,3);assert(q.words==original);
        router.Pass(q,manager,0);
        assert(*q.Read(queues[0]+0x808)==2&&*q.Read(queues[0]+0x844)==2);
        assert(*q.Read(queues[0]+0x84)==scene&&*q.Read(queues[0]+0x88)==back);
        assert(*q.Read(queues[3]+0x808)==2&&*q.Read(queues[3]+0x844)==2);
        assert(*q.Read(queues[3]+0x84)==menu&&*q.Read(queues[3]+0x88)==other);
        auto routed=q.words;router.Pass(q,manager,0);assert(q.words==routed);
        q.words=original;q.Queue(queues[3],{banner,other});auto gameplay=q.words;
        router.Pass(q,manager,0);assert(q.words==gameplay&&router.owner==banner);
        MM3D::BannerInterpolation::Publish(700,90,router.owner);
        assert(MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
            0xac22b79e29209c01ULL,96,-93,700,90));
        q.Queue(queues[3],{other});router.Pass(q,manager,0);assert(!router.owner);
        MM3D::BannerInterpolation::Publish(701,90,router.owner);
        assert(!MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
            0xac22b79e29209c01ULL,96,-93,701,90));
        q.words=original;q.Queue(queues[0],std::vector<U32>(32,back));auto full=q.words;
        router.Pass(q,manager,0);assert(*q.Read(queues[0]+0x844)==32&&*q.Read(queues[3]+0x844)==2);
        q.words=original;q.words.erase(queues[0]+0x84);auto invalid=q.words;
        router.Pass(q,manager,0);assert(q.words==invalid);
        q.words=original;q.Queue(queues[0],{});q.Queue(queues[2],{banner});q.Queue(queues[3],{menu});
        router.Pass(q,manager,0);
        assert(*q.Read(queues[0]+0x844)==0);
        assert(*q.Read(queues[2]+0x844)==0&&*q.Read(queues[3]+0x844)==1);
        // Also remove banners already in the backdrop queue, preserving
        // other draws and the banner object's state for resumed gameplay.
        q.words=original;q.Queue(queues[0],{scene,banner,back});
        router.Pass(q,manager,0);
        assert(*q.Read(queues[0]+0x844)==2&&*q.Read(queues[0]+0x84)==scene&&*q.Read(queues[0]+0x88)==back);
        for(U32 at=banner;at<banner+0x100;at+=4)
            if(original.contains(at))assert(q.words.at(at)==original.at(at));

    }
}
void DoubleTimeChecks() {
    namespace D=MM3D::DoubleTime;
    for(unsigned day=0;day<3;++day){
        Memory m;D::Hook hook;D::Options o;o.poses[D::Timeline]={60,20,1.5f};D::Apply(o);
        const auto panel=m.Layout("LapTimeSong",0,0,0x240),all=m.Widget("all");
        m.Children(panel+8,{all});m.words[panel+0x220]=day;hook.panel=panel;hook.frame=o.poses;hook.pass=3;
        std::vector<U32> groups;U32 base=0,link=0;
        for(const char* n:{"message_g","link_icon_g","disp_time_g","sche_base_g","popup_window00_g","btn_advance_g","btn_quit_g","touch_rect_center_l","touch_rect_link_l"}){
            auto node=m.Widget(n,10,30);groups.push_back(node);
            if(std::string(n)=="sche_base_g")base=node;
            if(std::string(n)=="link_icon_g")link=node;
        }
        m.Children(all,groups);
        const auto embed=m.Widget("sche_base_l"),layout=m.Layout("ScheBaseBottom"),days=m.Widget("all");
        m.Children(base,{embed});m.words[embed+0x20]=layout;m.Children(layout+8,{days});
        std::vector<U32> cards;for(const char* n:{"sche_base0","sche_base1","sche_base2"})cards.push_back(m.Widget(n));
        m.Children(days,cards);const auto original=m.words;
        hook.Begin(m,panel);
        assert(std::bit_cast<float>(*m.Read(link+0xec))==75.f);
        assert(std::bit_cast<float>(*m.Read(link+0xfc))==65.f);
        for(unsigned i=0;i<3;++i)assert((*m.Read(cards[i]+0xd4)&1)==unsigned(i==day));
        hook.End(m);assert(m.words==original);
        hook.pass=5;hook.Begin(m,panel);assert(m.words==original);hook.End(m);
        hook.pass=3;m.words[panel+0x220]=3;const auto invalid=m.words;
        hook.Begin(m,panel);assert(m.words==invalid);hook.End(m);
        m.words=original;
        const auto manager=m.Alloc(40),bottom=m.Alloc(0x848),top=m.Alloc(0x848),bgfrom=m.Alloc(0x848),bgto=m.Alloc(0x848);
        m.words[manager+28]=bottom;m.words[manager+20]=top;m.words[manager+24]=bgfrom;m.words[manager+12]=bgto;
        const auto bg=m.Layout("CommonBottomBg"),other=m.Layout("Unrelated");
        m.Queue(bottom,{panel,other});m.Queue(bgfrom,{bg});
        hook.Pass(m,manager,4);assert(hook.panel==panel&&hook.background==bg);
        assert(*m.Read(bottom+0x808)==1&&*m.Read(bottom+0x84)==other);
        assert(*m.Read(top+0x840)==1&&*m.Read(top+4+15*0x80)==panel);
        assert(*m.Read(bgto+0x804)==1&&*m.Read(bgto+4)==bg);
        const auto players=m.Alloc(4),player=m.Alloc(0x20),animation=m.Alloc(0x10);
        m.words[panel+0x14c]=players;m.words[panel+0x150]=1;m.words[players]=player;
        m.words[player+0xc]=animation;
        for(bool closing:{false,true}) {
            m.words[animation+8]=m.String(closing?"out_menu_lap_time_song_anim":"in_menu_lap_time_song_anim");
            for(float f:{0.f,1.f,2.f,4.f,8.f}) {
                m.words[player+0x10]=std::bit_cast<U32>(f);
                const float t=std::min(f/4.f,1.f);
                assert(std::abs(hook.ShadeAlpha(m)-.30f*(closing?1-t:t))<.0001f);
            }
        }
        const auto plate=m.Widget("shade_source"),geometry=m.Alloc(0x7c);
        m.words[plate+0x28]=geometry;m.words[geometry+4]=6;m.Children(bg+8,{plate});
        m.Alloc(8);if(m.next&7)m.Alloc(4);
        const auto stack=m.Alloc(0x1000)+0x1000;
        hook.shade_alpha=.30f;hook.pass=1;const auto untouched=m.words;
        const auto root=hook.ShadeRoot(m,bg,bg+8,stack);
        assert(root==stack-0x800);
        assert(m.words[root+0x140]==bg+8&&m.words[root+0x144]==root+0x180);
        assert(std::bit_cast<float>(m.words[root+0x180+0x134])==.30f);
        assert(m.words[root+0x2c0+0x10]==0); // untextured overlay
        assert(hook.shade_scratch.End(m)==stack&&m.words==untouched);
        for(unsigned screen:{3u,5u}) {
            hook.pass=screen;assert(hook.ShadeRoot(m,bg,bg+8,stack)==bg+8);
            hook.shade_scratch.End(m);assert(m.words==untouched);
        }
        hook.pass=1;hook.shade_alpha=0;
        assert(hook.ShadeRoot(m,bg,bg+8,stack)==bg+8);
        hook.shade_scratch.End(m);assert(m.words==untouched);
    }
    D::Apply(D::Options{});
}
void LoadingIconChecks() {
    namespace T=MM3D::NativeStartup;
    Memory m;T::LoadingIconHook hook;T::Apply(T::Options{});
    const auto layout=m.Layout("Throbber",360,200),all=m.Widget("all"),activity=m.Widget("activity_pane",370,210);
    m.Children(layout+8,{all});m.Children(all,{activity});
    const auto original=m.words;
    for(unsigned i=0;i<2;++i) {
        T::Apply(T::Options{});m.words=original;hook.Begin(m,layout);assert(m.words==original);hook.End(m);
        T::Options o;o.loading_icon={20,-10,.5f};T::Apply(o);
        hook.Begin(m,layout);
        assert(std::bit_cast<float>(m.words[activity+0xec])==385.f);
        assert(std::bit_cast<float>(m.words[activity+0xfc])==195.f);
        assert(std::bit_cast<float>(m.words[activity+0xe0])==.5f);
        hook.End(m);assert(m.words==original);
    }
    m.words=original;const auto other=m.Layout("NotThrobber",360,200);const auto before=m.words;
    hook.Begin(m,other);hook.End(m);assert(m.words==before);
    T::Options o;o.loading_icon={0,0,1};T::Apply(o);
}
void PressStartChecks() {
    namespace T=MM3D::NativeStartup;
    {
        using namespace MM3D::ClassicSaving;
        float elapsed=0;
        // Slow guest loading must not expire the cover or restore music gain.
        for(int i=0;i<6000;++i)assert(TitleHandoffAlpha(elapsed,.1f,false)==1.f);
        assert(elapsed==0);
        Memory guest;T::Hook logo_hook;
        logo_hook.skeleton=guest.Alloc(64);logo_hook.matrices=guest.Alloc(13*48);
        for(unsigned i=0;i<8;++i)guest.words[logo_hook.matrices+3*48+i*4]=0;
        exit_fade=ExitFade::In;exit_title_ready=false;
        logo_hook.Matrix(guest,0,3);assert(!exit_title_ready);
        logo_hook.Matrix(guest,logo_hook.skeleton,3);assert(exit_title_ready);
        for(int i=0;i<4;++i)assert(TitleHandoffAlpha(elapsed,.1f,true)==1.f);
        for(int i=0;i<8;++i)TitleHandoffAlpha(elapsed,.1f,true);
        assert(TitleHandoffAlpha(elapsed,.1f,true)==0.f);
        exit_fade=ExitFade::Idle;exit_title_ready=false;
        logo_hook.Matrix(guest,logo_hook.skeleton,3);assert(!exit_title_ready);
    }
    Memory m;T::PressHook hook;T::Apply(T::Options{});
    auto layout=m.Layout("MenuStartup"),all=m.Widget("all");
    auto fx=m.Widget("logo_press_start_fx_g",160,128),text=m.Widget("logo_press_start_g",160,128),home=m.Widget("home_menu_text_g",160,227);
    m.Children(layout+8,{all});m.Children(all,{fx,text,home});
    auto manager=m.Alloc(32),bottom=m.Alloc(0x848),top=m.Alloc(0x848);
    m.words[manager+28]=bottom;m.words[manager+20]=top;m.Queue(bottom,{layout});
    const auto original=m.words;
    for(unsigned repeat=0;repeat<2;++repeat) {
        m.words=original;hook.Pass(m,manager,5);assert(hook.active);
        assert(m.words[top+0x844]==1&&m.words[bottom+0x844]==1);
        const auto queued=m.words;
        hook.Begin(m,layout);assert(!(m.words[fx+0xd4]&1)&&!(m.words[text+0xd4]&1));
        assert(m.words[home+0xd4]==15);hook.End(m);assert(m.words==queued);
        hook.Pass(m,manager,3);hook.Begin(m,layout);
        for(auto node:{fx,text}) {
            assert(std::bit_cast<float>(m.words[node+0xec])==200);
            assert(std::bit_cast<float>(m.words[node+0xfc])==190);
            assert(m.words[node+0x100]==original.at(node+0x100));
        }
        assert(!(m.words[home+0xd4]&1));hook.End(m);assert(m.words==queued);
    }
    // Classic owl handoff hides both prompt branches in every fade phase,
    // restores their flags after drawing, and returns to normal in Idle.
    namespace C=MM3D::ClassicSaving;
    for(auto phase:{C::ExitFade::Out,C::ExitFade::Black,C::ExitFade::In,C::ExitFade::Idle}) {
        C::exit_fade=phase;m.words=original;hook.Pass(m,manager,5);
        const auto queued=m.words;
        hook.Begin(m,layout);
        assert(m.words[home+0xd4]==15);
        assert(!(m.words[fx+0xd4]&1)&&!(m.words[text+0xd4]&1));
        hook.End(m);assert(m.words==queued);
        hook.Pass(m,manager,3);hook.Begin(m,layout);
        for(auto node:{fx,text})assert(bool(m.words[node+0xd4]&1)==(phase==C::ExitFade::Idle));
        hook.End(m);assert(m.words==queued);
    }
    // Actual native queue7 full-screen covers, including the initial black frame.
    m.words=original;
    for(auto [address,word]:T::FadeGuards)m.words[address]=word;
    auto quad=m.Alloc(0x244);
    m.words[T::FadeManager+8]=1;m.words[T::FadeManager+0x338]=quad;
    m.words[T::FadeManager+0x30]=1;m.words[T::FadeManager+0x2f78]=quad;
    m.words[quad+0xc]=std::bit_cast<U32>(320.f);m.words[quad+0x10]=std::bit_cast<U32>(240.f);
    auto pane=m.Widget("logo_press_start_pane",160,128);m.Children(text,{pane});
    for(auto node:{text,fx,pane})for(U32 offset:{0x128u,0x12cu,0x130u,0x134u})
        m.words[node+offset]=std::bit_cast<U32>(.8f);
    const auto fade_original=m.words;
    for(float cover:{1.f,.5f,0.f,.75f,1.f}) {
        m.words=fade_original;m.words[quad+0x16c]=std::bit_cast<U32>(cover);
        hook.Pass(m,manager,5);assert(hook.active);
        const auto queued=m.words;hook.Pass(m,manager,3);hook.Begin(m,layout);
        for(auto node:{text,fx,pane})for(U32 offset:{0x128u,0x12cu,0x130u,0x134u})
            assert(std::bit_cast<float>(m.words[node+offset])==.8f*(1-cover));
        assert(m.words[home+0x134]==queued.at(home+0x134));
        hook.End(m);assert(m.words==queued);
    }
    m.words=fade_original;m.words[quad+0x16c]=std::bit_cast<U32>(1.f);
    m.words[T::FadeManager+0x30]=0;assert(T::BottomVisibility(m)==1); // Stale allocated cover.
    m.words[T::FadeManager+0x30]=1;m.words[quad+0xc]=std::bit_cast<U32>(160.f);
    assert(T::BottomVisibility(m)==1); // Partial dimmer.
    m.words=fade_original;m.words[quad+0x16c]=std::bit_cast<U32>(.5f);
    auto second=m.Alloc(0x244);
    for(U32 offset:{0xcu,0x10u,0x160u,0x164u,0x168u,0x16cu})m.words[second+offset]=m.words[quad+offset];
    m.words[T::FadeManager+8]=2;m.words[T::FadeManager+0x33c]=second;
    m.words[T::FadeManager+0x30]=2;m.words[T::FadeManager+0x2f7c]=second;
    assert(T::BottomVisibility(m)==.25f);
    m.words[0x179c14]^=1;assert(T::BottomVisibility(m)==1);
    T::Options o;o.press_start={20,-30,.5f};T::Apply(o);
    m.words=original;hook.Pass(m,manager,5);hook.Pass(m,manager,3);hook.Begin(m,layout);
    assert(std::bit_cast<float>(m.words[text+0xec])==220);
    assert(std::bit_cast<float>(m.words[text+0xfc])==160);
    assert(std::bit_cast<float>(m.words[text+0xe0])==.5f);hook.End(m);
    m.words=original;m.words[all+0x18]=2;auto malformed=m.words;
    hook.Pass(m,manager,5);assert(!hook.active&&m.words==malformed);
    m.words=original;m.Queue(top,std::vector<U32>(32,home));auto full=m.words;
    hook.Pass(m,manager,5);assert(!hook.active&&m.words==full);
    m.words=original;m.Queue(bottom,{});auto absent=m.words;
    hook.Pass(m,manager,5);assert(!hook.active&&m.words==absent);
    T::Apply(T::Options{});
}
void NotebookPromptChecks() {
    namespace P=MM3D::NotebookPrompt;
    Memory m;
    const auto layout=m.Layout("MenuQuestlist"),owner=m.Alloc(0x800);
    const auto prompt=m.Layout("Pictogram"),all=m.Widget("all"),pict=m.Widget("pict"),anchor=m.Widget("disp_a_icon_n");
    m.Children(prompt+8,{all});m.Children(all,{pict});
    m.words[pict+0xdc]=0x42000100;m.words[pict+0x10c]=std::bit_cast<U32>(-10.f);
    m.words[owner+0x2a8]=prompt;m.words[owner+0x290]=anchor;m.words[prompt+0x180]=anchor;
    const auto geo=m.Alloc(0x7c),parts=m.Alloc(0x60);
    m.words[pict+0x28]=geo;m.words[geo+4]=6;m.words[geo+0x10]=parts+0xc;
    m.words[P::RegistryLiteral]=P::Registry;
    const auto panel_all=m.Widget("all"),detail=m.Widget("quest_list_detail"),text=m.Widget("description");
    m.Children(layout+8,{panel_all});m.Children(panel_all,{detail});m.Children(detail,{text});
    const auto tg=m.Alloc(0x30c),font=m.Alloc(0x14),header=m.Alloc(0x2c),pages=m.Alloc(8);
    std::string chars=std::string(P::Label)+std::string(P::MapLabel)+std::string(P::ScheduleLabel)+std::string(P::AlarmLabel)+std::string(P::EventsLabel)+std::string(P::ScheduleDetailsLabel)+"Song listBackReturnMove cursorItemsChange tabChooseEquip to IIZR +/Zoom inout";std::sort(chars.begin(),chars.end());chars.erase(std::unique(chars.begin(),chars.end()),chars.end());
    const auto records=m.Alloc(chars.size()*12);
    m.words[text+0x28]=tg;m.words[tg+4]=3;m.words[tg+0x28]=font;
    m.words[font+4]=header;m.words[font+0xc]=pages;m.words[font+0x10]=records;
    m.words[header+8]=8u<<16;m.words[header+12]=12;m.words[header+0x10]=1;m.words[header+0x14]=chars.size();
    m.words[header+0x1c]=4; // A8
    m.words[header+0x24]=16u<<16;m.words[header+0x28]=16;
    m.words[pages]=0x1000;m.words[pages+4]=256u|(256u<<16);
    for(unsigned i=0;i<chars.size();++i) {
        m.words[records+i*12]=unsigned(chars[i]);
        m.words[records+i*12+4]=chars[i]==' '?4:8+unsigned(chars[i])%3;
        m.words[records+i*12+8]=1u|(((i%16)|((i/16)<<8))<<16); // bearing1, tiled glyph cells
    }
    const auto stack=m.Alloc(0x6008),sp=(stack+0x6008)&~7u;
    {
        MM3D::SongList::Options banner_options;
        banner_options.banner={20,10,1};banner_options.banner_width=89;banner_options.banner_height=18;
        MM3D::SongList::Apply(banner_options);
        MM3D::SongList::Hook songs;
        songs.panel=m.Layout("MenuMusicList");songs.pass=3;songs.column={20,30,.5f};
        auto group=m.Widget("all"),music=m.Widget("btn_music_g"),title=m.Widget("e_song_title");
        m.words[title+0x28]=tg;m.words[music+0x134]=std::bit_cast<U32>(.75f);
        m.Children(songs.panel+8,{group});m.Children(group,{music,title});m.Children(music,{pict});
        const auto ocarina=m.Layout("MenuOcarina"),oa=m.Widget("all"),message=m.Widget("message_g"),
            text_group=m.Widget("e_message_pane_g"),native_text=m.Widget("e_message_pane");
        songs.header.source_layout=ocarina;
        m.Children(ocarina+8,{oa});m.Children(oa,{message});
        m.Children(text_group,{native_text});m.words[native_text+0x28]=tg;
        std::vector<U32> children{text_group};std::array<U32,3> native_geometries{};
        unsigned pane_index=0;
        for(const char* name:{"bb_message_pane02","bb_message_pane01","bb_message_pane00"}) {
            const auto node=m.Widget(name),g=m.Alloc(0x7c);
            for(U32 off=0;off<0x7c;off+=4)m.words[g+off]=0x10000+off+pane_index;
            m.words[node+0x28]=g;m.words[g+4]=6;
            m.words[g+0x14]=std::bit_cast<U32>(-80.f);
            m.words[g+0x18]=std::bit_cast<U32>(-12.f);
            m.words[g+0x20]=std::bit_cast<U32>(160.f);
            m.words[g+0x24]=std::bit_cast<U32>(24.f);
            children.push_back(node);native_geometries[pane_index++]=g;
        }
        m.Children(message,children);
        for(float slide:{0.f,-70.f}) {
            songs.slide=slide;const auto before=m.words;
            const U32 root=songs.HeaderRoot(m,songs.panel,songs.panel+8,sp);
            assert(root!=songs.panel+8&&m.words[root+0x18]==5);
            assert(m.words[root+0x140]==songs.panel+8);
            assert(m.words[root+0x144]==root+0x1400);
            for(unsigned layer=0;layer<3;++layer)
                assert(m.words[root+0x148+layer*4]==root+0x200+layer*0x200);
            for(unsigned n=0;n<3;++n) {
                const U32 pane=root+0x200+n*0x200,g=m.words[pane+0x28];
                for(U32 off=0;off<0x7c;off+=4)assert(m.words[g+off]==m.words[native_geometries[n]+off]);
                auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
                assert(std::abs(f(pane+0xe0)-89.f/160)<.0001f);
                assert(f(pane+0xf4)==18.f/24);
                assert(std::abs(f(pane+0xec)-(20+slide+44.5f))<.0001f&&std::abs(f(pane+0xfc)-19.f)<.0001f);
                assert(f(pane+0x134)==.75f);
            }
            const U32 text_geometry=m.words[root+0x1400+0x28],records=m.words[text_geometry+0x2fc];
            assert(m.words[text_geometry+0x300]==5);
            float pen=0;unsigned i=0;
            for(char c:std::string("Songs")) {
                auto glyph=MM3D::NotebookFont::Lookup(m,font,c,pen);assert(glyph);
                glyph->words[14]=0;
                for(unsigned channel=0;channel<4;++channel)
                    assert(m.words[text_geometry+0x78+channel*4]==std::bit_cast<U32>(1.f));
                for(unsigned j=0;j<15;++j)assert(m.words[records+i*60+j*4]==glyph->words[j]);
                pen+=glyph->advance;++i;
            }
            assert(songs.header.scratch.End(m)==sp&&m.words==before);
        }
        // Independent banner controls must not follow later grid edits.
        banner_options.banner={40,25,2};MM3D::SongList::Apply(banner_options);
        songs.column={-100,100,1.5f};songs.slide=0;
        {
            const auto before=m.words;
            const auto root=songs.HeaderRoot(m,songs.panel,songs.panel+8,sp);
            assert(root!=songs.panel+8);
            const auto pane=root+0x200;
            assert(std::abs(std::bit_cast<float>(m.words[pane+0xec])-129.f)<.0001f);
            assert(std::abs(std::bit_cast<float>(m.words[pane+0xfc])-43.f)<.0001f);
            assert(std::abs(std::bit_cast<float>(m.words[pane+0xe0])-178.f/160)<.0001f);
            songs.header.scratch.End(m);assert(m.words==before);
        }
        for(unsigned pass:{1u,5u}) {
            songs.pass=pass;const auto before=m.words;
            assert(songs.HeaderRoot(m,songs.panel,songs.panel+8,sp)==songs.panel+8);
            songs.header.scratch.End(m);assert(m.words==before);
        }
        songs.pass=3;songs.header.source_layout=0;
        {
            const auto before=m.words;
            assert(songs.HeaderRoot(m,songs.panel,songs.panel+8,sp)==songs.panel+8);
            songs.header.scratch.End(m);assert(m.words==before);
        }
        songs.header.source_layout=ocarina;
        songs.pass=3;m.words[music+0x134]=0;const auto before=m.words;
        assert(songs.HeaderRoot(m,songs.panel,songs.panel+8,sp)==songs.panel+8);
        songs.header.scratch.End(m);assert(m.words==before);
    }

    {
        namespace O=MM3D::OcarinaPrompt;O::Hook hook;
        auto oc=m.Layout("MenuOcarina"),oi=m.Widget("btn_music_pane"),ot=m.Widget("e_message_pane");
        auto back=m.Widget("btn_quit_g"),music=m.Widget("btn_music_g"),list=m.Alloc(16);
        m.words[oi+0x28]=geo;m.words[ot+0x28]=tg;
        m.words[list]=oi;m.words[list+4]=ot;m.words[list+8]=back;m.words[list+12]=music;
        m.words[oc+0x154]=list;m.words[oc+0x158]=4;
        m.words[back+0x134]=std::bit_cast<U32>(.6f);m.words[music+0x134]=std::bit_cast<U32>(1.f);
        auto groups=m.Alloc(8),entries=m.Alloc(48);
        m.words[P::Registry+0x24]=m.Alloc(8); // Separate layout-name table.
        m.words[P::Registry+0x28]=groups;m.words[P::Registry+0x2c]=entries;
        m.words[groups]=12;m.words[entries+44]=parts;m.words[parts+4]=11u<<16;
        auto manager=m.Alloc(0x30),bottom=m.Alloc(0x848),top=m.Alloc(0x848);
        m.words[manager+28]=bottom;m.words[manager+20]=top;m.Queue(bottom,{oc});
        auto native=m.words;hook.Pass(m,manager,5);
        assert(hook.panel==oc&&O::status==2&&m.words[top+0x844]==1);
        auto routed=m.words;
        // Original lower-screen draw remains untouched.
        assert(hook.Root(m,oc,sp,false)==oc+8);assert(!hook.End(m));assert(m.words==routed);
        hook.Pass(m,manager,3);hook.opened=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        U32 root=hook.Root(m,oc,sp,false);assert(root==sp-0x2400&&O::status==3);
        assert(m.words[root+0x18]==8);
        auto children=m.words[root+0x14];
        for(unsigned row=0;row<2;++row) {
            U32 icon=m.words[children+row*16],label=m.words[children+row*16+4],icon_shadow=m.words[children+row*16+8],shadow=m.words[children+row*16+12];
            assert(m.words[icon_shadow+0x128]==0&&m.words[icon_shadow+0x130]==0);
            assert(std::abs(std::bit_cast<float>(m.words[icon_shadow+0x134])-.48f)<.0001f);
            assert(std::bit_cast<float>(m.words[icon_shadow+0xfc])==std::bit_cast<float>(m.words[icon+0xfc])+1);
            assert(m.words[m.words[icon_shadow+0x28]+0x28]==m.words[m.words[icon+0x28]+0x28]);
            assert(m.words[shadow+0x128]==0&&m.words[shadow+0x12c]==0&&m.words[shadow+0x130]==0);
            assert(std::abs(std::bit_cast<float>(m.words[shadow+0x134])-.48f)<.0001f);
            assert(std::abs(std::bit_cast<float>(m.words[shadow+0xec])-std::bit_cast<float>(m.words[label+0xec])-1)<.0001f);
            assert(std::bit_cast<float>(m.words[shadow+0xfc])==std::bit_cast<float>(m.words[label+0xfc])+1);
            assert(m.words[m.words[shadow+0x28]+0x2fc]==m.words[m.words[label+0x28]+0x2fc]);
            assert(std::bit_cast<float>(m.words[icon+0x134])==.6f);
            assert(std::bit_cast<float>(m.words[label+0x134])==.6f);
            U32 geometry=m.words[icon+0x28];assert(m.words[geometry+0x10]==parts+0xc);
            assert(std::bit_cast<float>(m.words[geometry+0x28])==float(row?128:208)/256);
            assert(std::bit_cast<float>(m.words[label+0xfc])==(row?222:200));
        }
        assert(hook.End(m)==sp&&m.words==routed);
        {
            std::array<U32,8> positions{};
            for(bool list_view:{false,true,false}) {
                hook.list_mode=list_view;
                const auto before=m.words;const auto root=hook.Root(m,oc,sp,false);
                for(unsigned i=0;i<4;++i) {
                    const auto n=m.words[m.words[root+0x14]+(i/2)*16+(i%2)*4];
                    for(unsigned axis=0;axis<2;++axis) {
                        const auto value=m.words[n+(axis?0xfcu:0xecu)];
                        if(!positions[i*2+axis])positions[i*2+axis]=value;
                        assert(positions[i*2+axis]==value);
                    }
                }
                hook.End(m);assert(m.words==before);
            }
        }
        hook.opened=std::chrono::steady_clock::now()-std::chrono::milliseconds(100);
        root=hook.Root(m,oc,sp,false);assert(root!=oc+8);
        float a=std::bit_cast<float>(m.words[root+0x134]);assert(a>=.29f&&a<.5f);
        hook.End(m);assert(m.words==routed);
        {
            const auto saved=m.words;const auto old_hook=hook;
            const auto songs=m.Layout("MenuMusicList"),sa=m.Widget("all"),sm=m.Widget("btn_music_g");
            m.Children(songs+8,{sa});m.Children(sa,{sm});
            m.words[sm+0x134]=std::bit_cast<U32>(.9f);
            m.words[back+0x134]=0;m.words[music+0x134]=0;
            for(unsigned bucket=0;bucket<16;++bucket){m.words[bottom+0x804+bucket*4]=0;m.words[top+0x804+bucket*4]=0;}
            m.Queue(bottom,{});m.Queue(top,{songs});
            hook.Pass(m,manager,5);assert(hook.list_mode&&hook.panel==oc&&hook.native_alpha==.9f);
            hook.Pass(m,manager,3);hook.opened=std::chrono::steady_clock::now()-std::chrono::seconds(1);
            const auto before=m.words;const auto root=hook.Root(m,oc,sp,false);
            assert(root!=oc+8);
            const auto label=m.words[m.words[root+0x14]+20],g=m.words[label+0x28];
            assert(m.words[g+0x300]==4); // Back, even when instrument buttons are hidden.
            hook.End(m);assert(m.words==before);
            m.words=saved;hook=old_hook;
        }
        // Persistent file select pair: both screens, placement, atlas cells,
        // scratch restoration, bottom/gameplay exclusion and Options priority.
        {
            namespace F=MM3D::FileSelectPrompt;
            F::Hook pair;MM3D::NativeFileSelect::Hook files;MM3D::OptionsMenu::Hook options;
            auto file=m.Layout("FileSelect"),option=m.Layout("MenuOption");
            m.Children(file+8,{oi,ot});m.Children(option+8,{oi,ot});
            files.active=true;files.panel=file;files.pass=3;
            F::Options settings;settings.scale=.8f;settings.icon_scale=1.5f;settings.spacing=30;
            F::Apply(settings);
            for(auto panel:{file,option}) {
                if(panel==option){options.panel=option;options.pass=3;}
                auto before=m.words;
                auto root=pair.Root(m,files,options,panel,sp,panel+8,false);
                assert(root==sp-0x2400&&m.words[root+0x18]==9);
                auto children=m.words[root+0x14];assert(m.words[children+32]==panel+8);
                for(unsigned row=0;row<2;++row) {
                    auto icon=m.words[children+row*16],label=m.words[children+row*16+4];
                    auto ig=m.words[icon+0x28],tg=m.words[label+0x28];
                    assert(std::bit_cast<float>(m.words[ig+0x28])==float(row?128:112)/256);
                    assert(std::abs(std::bit_cast<float>(m.words[icon+0xe0])-1.2f)<.0001f);
                    assert(std::abs(std::bit_cast<float>(m.words[label+0xfc])-(228-(row?0:24)-4.8f))<.0001f);
                    assert(m.words[tg+0x300]==(row?4:6));
                }
                assert(pair.End(m)==sp&&m.words==before);
            }
            // Native file-panel cover drives both labels/icons and their shadows,
            // including full disappearance and restoration on the return fade.
            options.panel=0;
            for(float alpha:{1.f,.75f,.25f,0.f,.25f,.75f,1.f}) {
                files.prompt_opacity=alpha;
                const auto before=m.words;
                const auto root=pair.Root(m,files,options,file,sp,file+8,false);
                if(alpha==0)assert(root==file+8);
                else {
                    assert(root==sp-0x2400);
                    const auto children=m.words[root+0x14];
                    for(unsigned i=0;i<8;++i) {
                        const auto node=m.words[children+i*4];
                        const float expected=alpha*(i%4>=2?.8f:1.f);
                        assert(std::abs(std::bit_cast<float>(m.words[node+0x134])-expected)<.0001f);
                    }
                }
                pair.End(m);assert(m.words==before);
            }
            options.panel=option;
            auto before=m.words;
            assert(pair.Root(m,files,options,file,sp,file+8,false)==file+8);pair.End(m);
            options.panel=0;files.pass=5;
            assert(pair.Root(m,files,options,file,sp,file+8,false)==file+8);pair.End(m);
            files.pass=3;files.active=false;
            assert(pair.Root(m,files,options,file,sp,file+8,false)==file+8);pair.End(m);
            assert(m.words==before);F::Apply({});
        }
        // Items-only R/Map prompt composes with an existing draw root.
        {
            namespace I=MM3D::ItemsMapPrompt;
            I::Hook item;Hook inventory;
            I::pose.x=I::pose.y=0;I::pose.scale=1;
            for(auto& e:I::elements){e.x=e.y=0;e.scale=1;}
            const auto arrows=m.Alloc(0x60);m.words[arrows+4]=8u<<16;m.words[entries+32]=arrows;
            auto panel=m.Layout("MenuItem"),all=m.Widget("all"),content=m.Widget("shadow_root");
            m.Children(panel+8,{all});m.Children(all,{oi,ot});m.Children(content,{panel+8});
            m.words[all+0x134]=std::bit_cast<U32>(.6f);
            inventory.inventory_page=true;inventory.panel=panel;inventory.pass=3;inventory.routed={panel};
            // Match the live layout: tabs are textured, description text is
            // in a separately owned MessageWindow, outside the Items tree.
            m.Children(all,{oi});
            const auto description=m.Layout("MessageWindow",0,0,0xa34);m.Children(description+8,{ot});
            const auto no_source=m.words;
            assert(item.Root(m,inventory,panel,sp,content,false)==content);
            assert(!item.prompt.End(m)&&m.words==no_source);
            // No description has drawn: empty first-open and tab changes must
            // still resolve the persistent font, irrespective of message ID.
            const auto owner=m.Alloc(0x170),context=m.Alloc(0x100);
            m.words[owner+0x16c]=context;m.words[description+0x444]=context;
            m.words[0x1c527c]=0xe59f0004;m.words[0x1c5280]=0xe8bd8070;
            m.words[0x1c5288]=0x6b36f8;m.words[0x6b36f8+0x2c]=description;
            m.words[description+8+0x134]=0; // Hidden native description.
            for(U32 id:{0u,0x1701u,0u}) {
                m.words[description+0x3f4]=id;
                item.ResolveDescription(m,owner,panel);
                assert(item.description==description&&item.description_panel==panel);
                const auto empty_before=m.words;
                assert(item.Root(m,inventory,panel,sp,content,false)==sp-0x2400);
                assert(item.prompt.End(m)==sp&&m.words==empty_before);
            }
            // Revalidate after context changes/unloading; never reuse stale data.
            for(U32 bad:{0u,context+4}) {
                m.words[description+0x444]=bad;item.ResolveDescription(m,owner,panel);
                assert(!item.description&&!item.description_panel);
            }
            m.words[description+0x444]=context;
            m.words[0x6b36f8+0x2c]=0;item.ResolveDescription(m,owner,panel);assert(!item.description);
            m.words[0x6b36f8+0x2c]=description;
            m.words[0x1c5288]=0;item.ResolveDescription(m,owner,panel);assert(!item.description);
            m.words[0x1c5288]=0x6b36f8;
            item.ResolveDescription(m,owner,panel);
            const auto before=m.words;
            auto r=item.Root(m,inventory,panel,sp,content,false);
            assert(r==sp-0x2400&&m.words[r+0x18]==7);
            auto children=m.words[r+0x14],icon=m.words[children],label=m.words[children+4];
            assert(m.words[children+24]==content);
            assert(std::bit_cast<float>(m.words[m.words[icon+0x28]+0x28])==192.f/256);
            assert(m.words[m.words[label+0x28]+0x300]==3);
            assert(std::bit_cast<float>(m.words[label+0x134])==.6f);
            const auto arrow=m.words[children+16],arrow_geo=m.words[arrow+0x28];
            assert(m.words[arrow_geo+0x10]==arrows+0xc);
            assert(std::bit_cast<float>(m.words[arrow_geo+0x28])==432.f/512);
            assert(std::bit_cast<float>(m.words[arrow_geo+0x2c])==104.f/128);
            assert(std::bit_cast<float>(m.words[arrow_geo+0x24])==24);
            assert(std::bit_cast<float>(m.words[arrow+0xec])==std::bit_cast<float>(m.words[icon+0xec])+20);
            assert(std::bit_cast<float>(m.words[arrow+0xfc])==198);
            assert(std::bit_cast<float>(m.words[label+0xfc])==224);
            assert(std::bit_cast<float>(m.words[arrow+0x134])==.6f);
            float label_width=0;for(char c:std::string("Map"))label_width+=MM3D::NotebookFont::Lookup(m,font,c,0)->advance;
            const float label_scale=std::bit_cast<float>(m.words[label+0xe0]);
            assert(std::abs(std::bit_cast<float>(m.words[label+0xec])+label_width*label_scale/2-
                            (std::bit_cast<float>(m.words[icon+0xec])+18))<.001f);
            const float px=std::bit_cast<float>(m.words[icon+0xec]);
            assert(item.prompt.End(m)==sp&&m.words==before);
            for(float opacity:{0.f,.5f}) {
                I::opacity=opacity;
                r=item.Root(m,inventory,panel,sp,content,false);
                if(opacity==0)assert(r==content);
                else {
                    auto children=m.words[r+0x14];
                    for(unsigned i=0;i<6;++i) {
                        auto n=m.words[children+i*4];const bool shadow=i==2||i==3||i==5;
                        assert(std::abs(std::bit_cast<float>(m.words[n+0x134])-.3f*(shadow?.8f:1.f))<.0001f);
                    }
                }
                item.prompt.End(m);assert(m.words==before);
            }
            I::opacity=1;
            // Each local pose transforms only its own face/shadow; global
            // scaling carries local offsets without altering sibling elements.
            for(unsigned element=0;element<3;++element) {
                I::pose.x=7;I::pose.y=-3;I::pose.scale=1.5f;
                r=item.Root(m,inventory,panel,sp,content,false);children=m.words[r+0x14];
                const std::array<U32,3> offsets{0,16,4};
                std::array<float,3> xs{},ys{},scales{};
                for(unsigned j=0;j<3;++j) {
                    auto n=m.words[children+offsets[j]];
                    xs[j]=std::bit_cast<float>(m.words[n+0xec]);ys[j]=std::bit_cast<float>(m.words[n+0xfc]);
                    scales[j]=std::bit_cast<float>(m.words[n+0xe0]);
                }
                item.prompt.End(m);
                I::elements[element].x=5;I::elements[element].y=-4;I::elements[element].scale=2;
                r=item.Root(m,inventory,panel,sp,content,false);children=m.words[r+0x14];
                for(unsigned j=0;j<3;++j) {
                    auto n=m.words[children+offsets[j]];
                    assert(std::bit_cast<float>(m.words[n+0xe0])==scales[j]*(j==element?2:1));
                    assert(std::bit_cast<float>(m.words[n+0xf4])==std::bit_cast<float>(m.words[n+0xe0]));
                    if(j!=element) {
                        assert(std::bit_cast<float>(m.words[n+0xec])==xs[j]);
                        assert(std::bit_cast<float>(m.words[n+0xfc])==ys[j]);
                    } else {
                        const float half_width=(j==2?label_width*label_scale/2:8)*1.5f;
                        const float half_height=(j==1?12:j==2?6:8)*1.5f;
                        assert(std::abs(std::bit_cast<float>(m.words[n+0xec])-(xs[j]-half_width+7.5f))<.001f);
                        assert(std::abs(std::bit_cast<float>(m.words[n+0xfc])-(ys[j]-half_height-6))<.001f);
                    }
                }
                assert(item.prompt.End(m)==sp&&m.words==before);
                I::elements[element].x=I::elements[element].y=0;I::elements[element].scale=1;
            }
            I::pose.x=I::pose.y=0;I::pose.scale=1;
            // The inventory shadow already occupies the upper scratch region.
            const U32 shadow_root=sp-0x1800;
            for(U32 n=0;n<0x140;n+=4)m.words[shadow_root+n]=m.words[content+n];
            const auto nested=m.words;
            r=item.Root(m,inventory,panel,shadow_root,shadow_root,false);
            assert(r==shadow_root-0x2400);
            assert(m.words[m.words[r+0x14]+24]==shadow_root);
            assert(item.prompt.End(m)==shadow_root&&m.words==nested);
            m.words=before;
            auto masks=m.Layout("MenuMask");inventory.panel=masks;inventory.routed={masks};
            const auto mask_before=m.words;
            assert(item.Root(m,inventory,masks,sp,masks+8,false)==masks+8);
            assert(!item.prompt.End(m)&&m.words==mask_before);
            m.words=before;inventory.panel=panel;inventory.routed={panel};
            I::pose.x=10;I::pose.y=-5;I::pose.scale=.25f;
            r=item.Root(m,inventory,panel,sp,content,false);children=m.words[r+0x14];icon=m.words[children];
            assert(std::bit_cast<float>(m.words[icon+0xe0])==.25f);
            assert(std::abs(std::bit_cast<float>(m.words[icon+0xec])-(398-(388-px)*.25f))<.001f);
            assert(std::bit_cast<float>(m.words[icon+0xfc])==216.5f);
            item.prompt.End(m);assert(m.words==before);
            I::pose.x=0;I::pose.y=0;I::pose.scale=1;
            inventory.pass=5;
            assert(item.Root(m,inventory,panel,sp,content,false)==content);
            assert(!item.prompt.End(m)&&m.words==before);
            inventory.pass=3;inventory.inventory_page=false;
            assert(item.Root(m,inventory,panel,sp,content,false)==content);
            assert(!item.prompt.End(m)&&m.words==before);
            inventory.inventory_page=true;
            m.words[all+0x134]=0;const auto hidden=m.words;
            assert(item.Root(m,inventory,panel,sp,content,false)==content);
            assert(!item.prompt.End(m)&&m.words==hidden);
        }
        // Both giving menus slide their legends using independent editor poses.
        for(const char* name:{"MenuItemPresen","MenuMaskPresen"}) {
            MM3D::MenuHelp::Hook help;Hook inventory;MM3D::NativeItemPresent::Hook present;
            auto panel=m.Layout(name),all=m.Widget("all"),desc=m.Layout("MessageWindow");
            m.Children(panel+8,{all});m.Children(all,{oi});m.Children(desc+8,{ot});
            m.words[all+0x134]=std::bit_cast<U32>(.6f);
            present.panel=panel;present.pass=3;present.routed={panel};present.transition_x=-7;
            present.mask_menu=std::string_view(name)=="MenuMaskPresen";
            help.source.description=desc;help.source.description_panel=panel;
            const U32 stack=(m.Alloc(0x12000)+0x12000)&~7u;const auto before=m.words;
            for(float scale:{.5f,1.f,2.f})for(float gap:{0.f,13.f}) {
                MM3D::NativeItemPresent::Options o;o.help_pose={32,45,scale};o.help_spacing=gap;
                if(present.mask_menu)MM3D::NativeItemPresent::ApplyMask(o);else MM3D::NativeItemPresent::Apply(o);
                const auto root=help.Root(m,inventory,panel,stack,panel+8,&present);
                assert(root==stack-0x8000&&m.words[root+0x18]==9);
                const auto children=m.words[root+0x14];
                for(unsigned row=0;row<2;++row) {
                    auto n=m.words[children+row*16],g=m.words[n+0x28];
                    assert(std::bit_cast<float>(m.words[g+0x28])==float(row?128:112)/256);
                    assert(std::bit_cast<float>(m.words[n+0xec])==25);
                    assert(std::bit_cast<float>(m.words[n+0xfc])==45+row*(16+gap)*scale);
                    assert(std::bit_cast<float>(m.words[n+0xe0])==scale);
                    assert(std::bit_cast<float>(m.words[n+0x134])==.6f);
                    auto text=m.words[children+(row*4+2)*4],tg=m.words[text+0x28];
                    assert(m.words[tg+0x300]==6);
                    const auto records=m.words[tg+0x2fc];float pen=0;unsigned j=0;
                    for(char c:std::string(row?"Cancel":"Choose")) {
                        auto glyph=MM3D::NotebookFont::Lookup(m,font,c,pen);assert(glyph);
                        for(unsigned k=0;k<15;++k)assert(m.words[records+j*60+k*4]==glyph->words[k]);
                        pen+=glyph->advance;++j;
                    }
                }
                assert(help.scratch.End(m)==stack&&m.words==before);
            }
            present.pass=5;assert(help.Root(m,inventory,panel,stack,panel+8,&present)==panel+8);
            help.scratch.End(m);assert(m.words==before);MM3D::NativeItemPresent::Apply({});MM3D::NativeItemPresent::ApplyMask({});
        }
        // Shared button legend: textured menu panes, separate description font.
        for(const char* name:{"MenuItem","MenuMask","MenuCollect"}) {
            namespace H=MM3D::MenuHelp;
            H::Hook help;Hook inventory;
            auto panel=m.Layout(name),all=m.Widget("all"),desc=m.Layout("MessageWindow",0,0,0xa34);
            m.Children(panel+8,{all});m.Children(all,{oi});m.Children(desc+8,{ot});
            m.words[all+0x134]=std::bit_cast<U32>(.6f);
            inventory.panel=panel;inventory.pass=3;inventory.routed={panel};
            const auto owner=m.Alloc(0x170),context=m.Alloc(0x100);
            m.words[owner+0x16c]=context;m.words[desc+0x444]=context;
            m.words[desc+0x3f4]=0;m.words[desc+8+0x134]=0;
            m.words[0x1c527c]=0xe59f0004;m.words[0x1c5280]=0xe8bd8070;
            m.words[0x1c5288]=0x6b36f8;m.words[0x6b36f8+0x2c]=desc;
            help.source.ResolveDescription(m,owner,panel);
            assert(help.source.description==desc&&help.source.description_panel==panel);
            const U32 stack=(m.Alloc(0x12000)+0x12000)&~7u;
            const auto dim=[&](unsigned child){return std::string_view(name)=="MenuCollect"&&
                ((child>=8&&child<12)||child>=16)?.5f:1.f;};
            const auto before=m.words;
            auto root=help.Root(m,inventory,panel,stack,panel+8);
            assert(root==stack-0x8000&&m.words[root+0x18]==29);
            auto children=m.words[root+0x14];assert(m.words[children+28*4]==panel+8);
            const std::array<unsigned,6> indices{0,4,8,12,18,24},uvs{176,192,112,128,144,160};
            for(unsigned i=0;i<6;++i) {
                auto n=m.words[children+indices[i]*4],g=m.words[n+0x28];
                assert(std::bit_cast<float>(m.words[g+0x28])==float(uvs[i])/256);
                assert(std::bit_cast<float>(m.words[n+0x134])==.6f*dim(indices[i]));
            }
            const std::array<unsigned,8> texts{2,6,10,14,16,20,22,26},lengths{1,10,6,6,4,10,4,11};
            for(unsigned i=0;i<texts.size();++i) {
                auto n=m.words[children+texts[i]*4],g=m.words[n+0x28];
                assert(m.words[g+0x300]==lengths[i]);
            }
            for(unsigned i=0;i<28;++i) {
                auto n=m.words[children+i*4];
                assert(std::abs(std::bit_cast<float>(m.words[n+0x134])-.6f*dim(i)*(i%2?.8f:1.f))<.0001f);
            }
            assert(help.scratch.End(m)==stack&&m.words==before);
            for(float opacity:{0.f,.5f}) {
                MM3D::ItemsMapPrompt::opacity=opacity;
                root=help.Root(m,inventory,panel,stack,panel+8);
                if(opacity==0)assert(root==panel+8);
                else {
                    auto children=m.words[root+0x14];
                    for(unsigned i=0;i<28;++i) {
                        auto n=m.words[children+i*4];
                        assert(std::abs(std::bit_cast<float>(m.words[n+0x134])-.3f*dim(i)*(i%2?.8f:1.f))<.0001f);
                    }
                }
                help.scratch.End(m);assert(m.words==before);
            }
            MM3D::ItemsMapPrompt::opacity=1;
            if(std::string_view(name)=="MenuCollect") {
                const auto page=m.Alloc(0x198);
                m.words[0x656b0c]=page;m.words[page]=0x63a270;
                m.words[page+0x14]=panel;m.words[0x72f5bc+8]=page;
                for(unsigned slot:{4u,5u})for(bool photo:{false,true})
                for(unsigned state:{1u,2u,3u})for(bool active:{false,true}) {
                    m.words[page+0x194]=(slot<<8)|state;
                    m.words[0x7751d8+0x1f8]=photo?0x02000000:0;
                    m.words[0x72f5bc+8]=active?page:0;
                    const auto saved=m.words;
                    auto root=help.Root(m,inventory,panel,stack,panel+8);
                    assert(root==stack-0x8000);
                    auto children=m.words[root+0x14];
                    const bool available=slot==4&&photo&&state==2&&active;
                    for(unsigned i=0;i<28;++i) {
                        auto n=m.words[children+i*4];
                        const float factor=available&&i>=8&&i<12?1.f:dim(i);
                        assert(std::abs(std::bit_cast<float>(m.words[n+0x134])-
                            .6f*factor*(i%2?.8f:1.f))<.0001f);
                    }
                    assert(help.scratch.End(m)==stack&&m.words==saved);
                }
                m.words=before;
            }
            for(bool across:{false,true}) {
                H::Options settings;settings.horizontal=across;settings.spacing=13;settings.button_gap=7;
                settings.button_scale=1.5f;settings.text_scale=.75f;settings.pose={20,35,.5f};H::Apply(settings);
                root=help.Root(m,inventory,panel,stack,panel+8);children=m.words[root+0x14];
                const std::array<unsigned,5> starts{0,8,12,16,22},ends{6,10,14,20,26};
                float expected_x=20;
                for(unsigned row=0;row<5;++row) {
                    auto n=m.words[children+starts[row]*4],last=m.words[children+ends[row]*4];
                    const float actual_x=std::bit_cast<float>(m.words[n+0xec]);
                    assert(std::abs(actual_x-(across?expected_x:20))<.001f);
                    const float center_y=std::bit_cast<float>(m.words[n+0xfc])+(row<3?12:4.5f)*.5f;
                    assert(std::abs(center_y-(35+12*.5f+(across?0:row*37*.5f)))<.001f);
                    float width=0;for(char c:std::string(H::Labels[row]))width+=MM3D::NotebookFont::Lookup(m,font,c,0)->advance;
                    const float text_factor=std::bit_cast<float>(m.words[last+0xe0]);
                    expected_x=std::bit_cast<float>(m.words[last+0xec])+width*text_factor+13*.5f;
                }
                for(unsigned i:indices){auto n=m.words[children+i*4];assert(std::bit_cast<float>(m.words[n+0xe0])==.75f);}
                for(unsigned i:texts){auto n=m.words[children+i*4];assert(std::bit_cast<float>(m.words[n+0xe0])==.5f*.75f*12/16);}
                assert(help.scratch.End(m)==stack&&m.words==before);
            }
            H::Apply(H::Options{});
            MM3D::HudAdjustments::Options user;user.hide_menu_prompts=true;MM3D::HudAdjustments::Apply(user);
            assert(help.Root(m,inventory,panel,stack,panel+8)==panel+8);
            assert(!help.scratch.End(m)&&m.words==before);
            assert(H::enabled&&H::pose.x==280); // User visibility never edits baseline options.
            MM3D::HudAdjustments::Apply({});
            root=help.Root(m,inventory,panel,stack,panel+8);assert(root!=panel+8);
            assert(help.scratch.End(m)==stack&&m.words==before);
            inventory.pass=5;assert(help.Root(m,inventory,panel,stack,panel+8)==panel+8);
            assert(!help.scratch.End(m)&&m.words==before);
            inventory.pass=3;help.source.description=0;
            assert(help.Root(m,inventory,panel,stack,panel+8)==panel+8);
            assert(!help.scratch.End(m)&&m.words==before);
        }
        // Map prompts use the verified neutral D-pad cell and retain the map root.
        {
            MM3D::DungeonMap::Hook map_hook;auto panel=m.Layout("MenuMapDungeon");
            m.Children(panel+8,{oi,ot});auto bg=m.Layout("BackGround"),bg_group=m.Widget("backGround"),map_pane=m.Widget("map_base");
            m.Children(bg+8,{bg_group});m.Children(bg_group,{map_pane});
            m.words[map_pane+0x134]=std::bit_cast<U32>(.6f);
            map_hook.valid=map_hook.dungeon=true;map_hook.pass=3;map_hook.panel=panel;map_hook.routed={panel};map_hook.backgrounds[1]=bg;
            MM3D::DungeonMap::Options map_options;map_options.prompts.spacing=16.1f;
            MM3D::DungeonMap::Apply(map_options);
            // Notebook changes must no longer move or resize dungeon hints.
            P::pose.scale=1.5f;P::icon_scale=1.75f;P::pose.y=80;P::map_pose.y=-40;
            auto before=m.words;auto root=map_hook.PromptRoot(m,panel,sp,true);
            assert(root!=panel+8&&m.words[root+0x18]==9);
            auto children=m.words[root+0x14];assert(m.words[children+32]==panel+8);
            std::array<float,2> original_x{},original_y{};
            for(unsigned row=0;row<2;++row) {
                auto icon=m.words[children+row*16],label=m.words[children+row*16+4];
                auto geo=m.words[icon+0x28],text_geo=m.words[label+0x28];
                assert(std::bit_cast<float>(m.words[geo+0x28])==float(row?128:96)/256);
                assert(std::bit_cast<float>(m.words[geo+0x2c])==(row?0.f:16.f/128));
                assert(m.words[text_geo+0x300]==(row?6u:11u));
                original_x[row]=std::bit_cast<float>(m.words[icon+0xec]);
                original_y[row]=std::bit_cast<float>(m.words[icon+0xfc]);
                assert(std::bit_cast<float>(m.words[icon+0xe0])==1.f);
                assert(std::bit_cast<float>(m.words[icon+0x134])==.6f);
                assert(std::abs(std::bit_cast<float>(m.words[label+0xfc])-(row?222.f:189.8f))<.001f);
            }
            assert(map_hook.map_prompts.scratch.End(m)==sp&&m.words==before);
            map_options.prompts.x=35;map_options.prompts.y=-12;
            MM3D::DungeonMap::Apply(map_options);
            root=map_hook.PromptRoot(m,panel,sp,true);children=m.words[root+0x14];
            for(unsigned row=0;row<2;++row) {
                const auto icon=m.words[children+row*16];
                assert(std::abs(std::bit_cast<float>(m.words[icon+0xec])-original_x[row]-35)<.001f);
                assert(std::abs(std::bit_cast<float>(m.words[icon+0xfc])-original_y[row]+12)<.001f);
            }
            assert(map_hook.map_prompts.End(m)==sp&&m.words==before);
            map_options.prompts.scale=.75f;map_options.prompts.icon_scale=1.25f;
            MM3D::DungeonMap::Apply(map_options);
            root=map_hook.PromptRoot(m,panel,sp,true);children=m.words[root+0x14];
            for(unsigned row=0;row<2;++row) {
                const auto icon=m.words[children+row*16];
                assert(std::bit_cast<float>(m.words[icon+0xe0])==.75f*1.25f);
            }
            assert(map_hook.map_prompts.End(m)==sp&&m.words==before);
            MM3D::DungeonMap::Apply({});
            P::pose.scale=1;P::icon_scale=1;P::pose.y=0;P::map_pose.y=-16.1f;
            map_hook.pass=5;assert(map_hook.PromptRoot(m,panel,sp,true)==panel+8);
            map_hook.map_prompts.scratch.End(m);assert(m.words==before);
            // Mirrored Items navigation uses the selected defaults on both
            // normal map modes, composes below existing prompts, excludes warp.
            namespace I=MM3D::ItemsMapPrompt;
            I::Hook reverse;
            // This hook also runs while no map owner has been captured. Never
            // read owner-relative fields at low unmapped guest addresses.
            struct CheckedMapMemory {
                Memory& source;
                std::optional<U32> Read(U32 at){assert(at!=0x74c);return source.Read(at);}
                void Write(U32 at,U32 value){source.Write(at,value);}
            } checked{m};
            MM3D::DungeonMap::Hook uncaptured;
            const auto idle=m.words;
            assert(reverse.MapRoot(checked,uncaptured,panel,sp,panel+8,false)==panel+8);
            assert(!reverse.prompt.End(m)&&m.words==idle);
            const auto arrows=m.Alloc(0x60);m.words[arrows+4]=8u<<16;m.words[entries+32]=arrows;
            map_hook.owner=m.Alloc(0x800);map_hook.controller=m.Alloc(0x20);
            m.words[map_hook.controller+8]=map_hook.owner;m.words[map_hook.owner+0x18]=panel;
            const auto world=m.Layout("UpWorldMapHud"),all=m.Widget("all");
            m.Children(world+8,{all});m.words[all+0x134]=std::bit_cast<U32>(.6f);
            const auto native_in=m.Widget("disp_text_detail",280,220),native_out=m.Widget("disp_text_whole",280,220),title=m.Widget("place_name_g",20,20);
            m.Children(all,{native_in,native_out,title});
            m.words[map_hook.owner+0x1c]=world;map_hook.pass=3;
            I::pose.x=I::DefaultPose.x;I::pose.y=I::DefaultPose.y;I::pose.scale=I::DefaultPose.scale;
            for(unsigned i=0;i<3;++i){I::elements[i].x=I::DefaultElements[i].x;I::elements[i].y=I::DefaultElements[i].y;I::elements[i].scale=I::DefaultElements[i].scale;}
            for(unsigned outside=0;outside<2;++outside) {
                m.words[map_hook.owner+0x78c]=outside<<8;map_hook.dungeon=!outside;
                map_hook.pass=outside?2:3;
                const U32 target=outside?world:panel;
                const auto saved=m.words;
                for(unsigned wrong_pass:{0u,1u,outside?3u:2u,4u,5u}) {
                    map_hook.pass=wrong_pass;
                    assert(reverse.MapRoot(m,map_hook,target,sp,target+8,false)==target+8);
                    assert(!reverse.prompt.End(m)&&m.words==saved);
                }
                map_hook.pass=outside?2:3;
                U32 composed=outside?target+8:map_hook.PromptRoot(m,panel,sp,false);
                U32 stack=outside?sp:composed;
                auto r=reverse.MapRoot(m,map_hook,target,stack,composed,false);
                assert(r==stack-0x2400&&m.words[r+0x18]==7);
                auto children=m.words[r+0x14],icon=m.words[children],arrow=m.words[children+16],label=m.words[children+4];
                assert(m.words[children+24]==composed);
                assert(std::bit_cast<float>(m.words[m.words[icon+0x28]+0x28])==176.f/256);
                assert(std::bit_cast<float>(m.words[m.words[arrow+0x28]+0x2c])==80.f/128);
                assert(m.words[m.words[label+0x28]+0x300]==5);
                assert(std::bit_cast<float>(m.words[arrow+0xec])<std::bit_cast<float>(m.words[icon+0xec]));
                assert(std::abs(std::bit_cast<float>(m.words[icon+0xec])-11.4f)<.001f);
                assert(std::abs(std::bit_cast<float>(m.words[icon+0xfc])-121.72f)<.001f);
                assert(std::bit_cast<float>(m.words[arrow+0x134])==.6f);
                assert(reverse.prompt.End(m)==stack);
                if(!outside)map_hook.map_prompts.End(m);
                assert(m.words==saved);
                for(float opacity:{0.f,.5f}) {
                    I::opacity=opacity;
                    r=reverse.MapRoot(m,map_hook,target,sp,target+8,false);
                    if(opacity==0)assert(r==target+8);
                    else {
                        auto children=m.words[r+0x14];
                        for(unsigned i=0;i<6;++i) {
                            auto n=m.words[children+i*4];const bool shadow=i==2||i==3||i==5;
                            assert(std::abs(std::bit_cast<float>(m.words[n+0x134])-.3f*(shadow?.8f:1.f))<.0001f);
                        }
                    }
                    reverse.prompt.End(m);assert(m.words==saved);
                }
                I::opacity=1;
                {
                    MM3D::WorldMapPrompt::Hook zoom;
                    for(unsigned state:{0u,1u,0u,2u}) {
                        m.words[target+0x170]=state;const auto zoom_before=m.words;
                        const auto composed=reverse.MapRoot(m,map_hook,target,sp,target+8,false);
                        const auto zoom_stack=composed==target+8?sp:composed;
                        auto z=zoom.Root(m,map_hook,target,zoom_stack,composed,false);
                        if(outside&&state<=1) {
                            assert(z==zoom_stack-0x2400&&m.words[z+0x18]==5);
                            auto children=m.words[z+0x14],icon=m.words[children],text=m.words[children+4],geo=m.words[text+0x28];
                            assert(m.words[children+16]==composed);
                            assert(std::bit_cast<float>(m.words[m.words[icon+0x28]+0x28])==144.f/256);
                            std::string_view label=state?"Zoom out":"Zoom in";
                            assert(m.words[geo+0x300]==label.size());
                            auto records=m.words[geo+0x2fc];float pen=0;
                            for(unsigned i=0;i<label.size();++i) {
                                auto glyph=MM3D::NotebookFont::Lookup(m,font,label[i],pen);assert(glyph);
                                for(unsigned j=0;j<15;++j)assert(m.words[records+i*60+j*4]==glyph->words[j]);
                                pen+=glyph->advance;
                            }
                            assert(std::bit_cast<float>(m.words[text+0x134])==.6f);
                        } else assert(z==composed);
                        zoom.End(m);reverse.prompt.End(m);assert(m.words==zoom_before);
                    }
                    MM3D::WorldMapPrompt::ApplyNative({25,400,.5f});
                    const auto texture_before=m.words;
                    zoom.Root(m,map_hook,target,sp,target+8,false);
                    for(auto n:{native_in,native_out}) {
                        assert(std::bit_cast<float>(m.words[n+0xec])==(outside?305:280));
                        assert(std::bit_cast<float>(m.words[n+0xfc])==(outside?620:220));
                        assert(std::bit_cast<float>(m.words[n+0xe0])==(outside?.5f:1.f));
                    }
                    assert(std::bit_cast<float>(m.words[title+0xec])==20);
                    zoom.End(m);assert(m.words==texture_before);
                    MM3D::WorldMapPrompt::ApplyNative({});
                }
                if(outside) {
                    // The early-game no-map renderer skips UpWorldMapHud but
                    // still submits its shared title in top pass2.
                    const auto header=m.Layout("SubMenuTitle"),header_all=m.Widget("all");
                    m.Children(header+8,{header_all});
                    m.words[header_all+0x134]=std::bit_cast<U32>(.4f);
                    m.words[map_hook.owner+0x20]=header;m.words[map_hook.owner+0x74c]=6;
                    const auto no_map=m.words;
                    auto r=reverse.MapRoot(m,map_hook,header,sp,header+8,false);
                    assert(r==sp-0x2400&&m.words[r+0x18]==7);
                    const auto list=m.words[r+0x14],icon=m.words[list];
                    assert(m.words[list+24]==header+8);
                    assert(std::bit_cast<float>(m.words[m.words[icon+0x28]+0x28])==176.f/256);
                    assert(std::bit_cast<float>(m.words[icon+0x134])==.4f);
                    assert(reverse.prompt.End(m)==sp&&m.words==no_map);
                    assert(reverse.MapRoot(m,map_hook,world,sp,world+8,false)==world+8);
                    assert(!reverse.prompt.End(m)&&m.words==no_map);
                    for(unsigned invalid:{0u,1u,2u}) {
                        m.words=no_map;
                        if(invalid==0)map_hook.pass=5;
                        if(invalid==1)m.words[map_hook.owner+0x78c]|=0x10000;
                        if(invalid==2)m.words[map_hook.controller+8]=0;
                        const auto guarded=m.words;
                        assert(reverse.MapRoot(m,map_hook,header,sp,header+8,false)==header+8);
                        assert(!reverse.prompt.End(m)&&m.words==guarded);map_hook.pass=2;
                    }
                    m.words=no_map;m.words[map_hook.owner+0x74c]=0;
                    const auto normal=m.words;
                    assert(reverse.MapRoot(m,map_hook,header,sp,header+8,false)==header+8);
                    assert(!reverse.prompt.End(m)&&m.words==normal);
                }
                m.words[map_hook.owner+0x78c]|=0x10000;const auto warp=m.words;
                assert(reverse.MapRoot(m,map_hook,target,sp,target+8,false)==target+8);
                assert(!reverse.prompt.End(m)&&m.words==warp);
            }
            m.words=routed;
        }
        // A missing font or unmapped scratch never duplicates the bottom UI above.
        m.words[tg+0x28]=0;auto missing=m.words;
        assert(hook.Root(m,oc,sp,false)==oc+8&&!(m.words[oc+8+0xd4]&1));
        hook.End(m);assert(m.words==missing);m.words=routed;
        m.words=native;O::enabled=false;auto disabled=m.words;hook.Pass(m,manager,5);
        assert(!hook.panel&&m.words==disabled);O::enabled=true;
        m.Queue(bottom,{m.Layout("MenuMusicList")});auto songs=m.words;hook.Pass(m,manager,5);
        assert(!hook.panel&&m.words==songs);
    }
    // Only Event Notes changes Select to X; other notebook pages retain Select.
    for(U32 page:{0u,1u,2u})for(U32 buttons:{0u,1u,4u,0x400u,0x404u,0x800u}) {
        m.words[owner+0xc]=page;m.words[owner+0x40]=layout;
        const U32 cpsr=0xb0000010;
        const auto flags=MM3D::NativeNotebook::MapInputFlags(m,owner,buttons,cpsr);
        assert((flags&0x40000000u)==((buttons&(page==0?0x400u:4u))?0u:0x40000000u));
        assert((flags&~0xc0000000u)==(cpsr&~0xc0000000u));
        assert(!(flags&0x80000000u));
    }
    m.words[owner+0xc]=0;m.words[owner+0x40]=0;
    assert(MM3D::NativeNotebook::MapInputFlags(m,owner,4,0)==0);
    P::schedule_enabled=false;
    P::map_enabled=false;P::enabled=true;P::pose.x=0;P::pose.y=0;P::pose.scale=1;
    P::Hook hook;const auto original=m.words;
    const auto root=hook.Build(m,layout,owner,sp,.5f);
    assert(root==sp-P::ScratchSize&&P::status==3);
    auto children=*m.Read(root+0x14);assert(m.Read(root+0x18)==5);
    assert(m.Read(children+16)==layout+8);
    const auto icon=*m.Read(children),g=*m.Read(icon+0x28);
    assert(m.Read(g+0x10)==parts+0xc&&m.Read(g+0xc)==0x00010101);
    assert(m.Read(icon+0xdc)==0x42000000&&std::bit_cast<float>(*m.Read(icon+0x10c))==-10.f);
    assert(std::bit_cast<float>(*m.Read(g+0x28))==.4375f);
    assert(std::bit_cast<float>(*m.Read(icon+0x134))==.5f);
    const auto label=*m.Read(children+4),text_geo=*m.Read(label+0x28),glyphs=*m.Read(text_geo+0x2fc);
    assert(m.Read(text_geo+4)==3&&m.Read(text_geo+0x300)==P::Label.size());
    assert(std::bit_cast<float>(*m.Read(label+0xe0))==.75f);
    float pen=0;
    for(unsigned i=0;i<P::Label.size();++i) {
        const auto at=glyphs+i*60;const unsigned index=chars.find(P::Label[i]);
        assert(m.Read(at)==header+0x1000&&m.Read(at+4)==0x6756&&m.Read(at+8)==0x1401);
        assert(std::bit_cast<float>(*m.Read(at+20))==pen-1);
        assert(std::bit_cast<float>(*m.Read(at+40))==float(index%16)/16);
        assert(m.Read(at+56)==0xffffffff);
        pen+=P::Label[i]==' '?4:8+unsigned(P::Label[i])%3;
    }
    assert(std::bit_cast<float>(*m.Read(label+0xec))==20.f);
    assert(hook.End(m)==sp&&m.words==original&&hook.frames.empty());
    P::map_enabled=true;
    for(bool a:{true,false})for(float icons:{.5f,1.f,2.f}) {
        P::icon_scale=icons;
        P::enabled=a;P::map_pose.x=10;P::map_pose.y=-20;P::map_pose.scale=1.5f;
        const auto both=hook.Build(m,layout,owner,sp,.5f);
        assert(both!=layout+8&&m.Read(both+0x18)==(a?7u:5u));
        const auto entries=*m.Read(both+0x14),map_text=*m.Read(entries+(a?8:0));
        const auto map_geo=*m.Read(map_text+0x28);
        assert(m.Read(map_geo+0x300)==P::MapLabel.size());
        assert(std::bit_cast<float>(*m.Read(map_text+0xfc))==159.f);
        assert(std::bit_cast<float>(*m.Read(map_text+0x134))==.5f);
        const auto x_icon=*m.Read(entries+(a?12:4)),x_geo=*m.Read(x_icon+0x28);
        assert(m.Read(x_geo+4)==6&&m.Read(x_geo+0x10)==parts+0xc);
        assert(std::bit_cast<float>(*m.Read(x_geo+0x28))==144.f/256.f);
        assert(std::bit_cast<float>(*m.Read(x_geo+0x20))==16.f);
        assert(std::bit_cast<float>(*m.Read(x_icon+0xec))==10.f);
        assert(std::bit_cast<float>(*m.Read(x_icon+0xfc))==168.f-12.f*icons);
        assert(std::bit_cast<float>(*m.Read(x_icon+0xe0))==1.5f*icons);
        assert(std::bit_cast<float>(*m.Read(x_icon+0xf4))==1.5f*icons);
        assert(std::bit_cast<float>(*m.Read(map_text+0xec))==10.f+(16.f*icons+4.f)*1.5f);
        assert(std::bit_cast<float>(*m.Read(x_icon+0x134))==.5f);
        assert(hook.End(m)==sp&&m.words==original);
    }
    // Third prompt fits the same scratch reservation, including all three labels.
    P::schedule_enabled=true;P::enabled=true;P::schedule_y=-40;P::icon_scale=1;
    P::pose.scale=1;P::pose.x=0;
    auto triple=hook.Build(m,layout,owner,sp,.5f);
    assert(m.Read(triple+0x18)==9);
    auto entries3=*m.Read(triple+0x14),schedule_text=*m.Read(entries3+16),y_icon=*m.Read(entries3+20);
    auto y_geo=*m.Read(y_icon+0x28);
    assert(m.Read(*m.Read(schedule_text+0x28)+0x300)==P::ScheduleLabel.size());
    assert(std::bit_cast<float>(*m.Read(y_geo+0x28))==160.f/256.f);
    assert(std::bit_cast<float>(*m.Read(y_icon+0xfc))==160.f);
    assert(std::bit_cast<float>(*m.Read(schedule_text+0xec))==20.f);
    auto return_text=*m.Read(entries3+28),return_geo=*m.Read(return_text+0x28);
    assert(m.Read(return_geo+0x300)==6);
    assert(hook.End(m)==sp&&m.words==original);
    // Schedule reuses the exact Event Notes positions, scales and button cells.
    auto schedule_layout=m.Layout("MenuSchedule"),schedule_all=m.Widget("all"),schedule_detail=m.Widget("sche_quest_detail");
    m.Children(schedule_layout+8,{schedule_all});m.Children(schedule_all,{schedule_detail});m.Children(schedule_detail,{text});
    for(float scale:{.75f,1.f,1.5f})for(float gap:{16.1f,20.f}) {
        P::pose.scale=scale;P::map_pose.scale=scale;P::map_pose.y=-gap;
        auto before=m.words;
        auto event_root=hook.Build(m,layout,owner,sp,.5f);
        const auto event_children=m.words[event_root+0x14];
        std::array<std::array<U32,4>,6> transforms{};
        for(unsigned i=0;i<6;++i)for(unsigned j=0;j<4;++j)
            transforms[i][j]=m.words[m.words[event_children+4*i]+std::array<U32,4>{0xec,0xfc,0xe0,0xf4}[j]];
        hook.End(m);assert(m.words==before);
        MM3D::NativeNotebook::Hook notebook;notebook.active=true;notebook.schedule=true;
        notebook.pass=3;notebook.panel=schedule_layout;notebook.notebook_owner=owner;notebook.detail_opacity=.5f;
        const auto root=notebook.PromptRoot(m,schedule_layout,sp);
        assert(root!=schedule_layout+8&&m.words[root+0x18]==9);
        const auto children=m.words[root+0x14];
        for(unsigned i=0;i<6;++i)for(unsigned j=0;j<4;++j)
            assert((m.words[m.words[children+4*i]+std::array<U32,4>{0xec,0xfc,0xe0,0xf4}[j]]==transforms[i][j]));
        for(auto [slot,label]:std::array<std::pair<unsigned,std::string_view>,4>{{{1,P::ScheduleDetailsLabel},{2,P::AlarmLabel},{4,P::EventsLabel},{7,"Back"}}}) {
            auto geometry=m.words[m.words[children+4*slot]+0x28];
            assert(m.words[geometry+0x300]==label.size());
            auto glyphs=m.words[geometry+0x2fc];
            for(unsigned i=0;i<label.size();++i)
                assert(std::bit_cast<float>(m.words[glyphs+i*60+40])==float(chars.find(label[i])%16)/16);
        }
        auto a_icon=m.words[children],b_icon=m.words[children+24];
        assert(std::abs(std::bit_cast<float>(m.words[b_icon+0xfc])-std::bit_cast<float>(m.words[a_icon+0xfc])-gap*scale)<.001f);
        assert(m.words[b_icon+0xec]==m.words[a_icon+0xec]&&m.words[b_icon+0xe0]==m.words[a_icon+0xe0]);
        assert(std::bit_cast<float>(m.words[m.words[b_icon+0x28]+0x28])==128.f/256.f);
        assert(notebook.prompt_hook.End(m)==sp&&m.words==before);
    }
    m.words[owner+0x44]=schedule_layout;
    const auto selected_event=m.Alloc(8);
    for(unsigned column:{0u,1u,3u,5u})for(bool has_event:{false,true}) {
        m.words[owner+0x10]=column;m.words[owner+0x59c]=has_event?selected_event:0;
        const bool available=column!=0&&has_event;
        assert(P::ScheduleDetailsAvailable(m,owner,schedule_layout)==available);
        auto before=m.words;auto root=hook.Build(m,schedule_layout,owner,sp,.5f);
        assert(root!=schedule_layout+8);auto children=m.words[root+0x14];
        for(unsigned i=0;i<6;++i)
            assert(std::bit_cast<float>(m.words[m.words[children+4*i]+0x134])==(i<2&&!available?.2f:.5f));
        hook.End(m);assert(m.words==before);
    }
    P::pose.scale=1;P::map_pose.scale=1.5f;
    // Both pages follow their scrollbar even with a hidden description.
    // The separate map overlay fade still composes with the page fade.
    for(bool schedule:{false,true}) {
        const auto page=schedule?schedule_layout:layout;
        const auto parent=schedule?schedule_detail:detail;
        auto contents=m.Widget(schedule?"sqd_window00":"qld_window00");
        auto text_group=m.Widget("e_contents_g");m.Children(text_group,{text});
        m.words[text_group+0x134]=std::bit_cast<U32>(1.f);
        m.words[text_group+0xd4]&=~1u; // native exit hides text without fading its alpha
        m.Children(parent,{contents,text_group});
        auto scrollbar=m.Layout("ScrollBar"),bar_all=m.Widget("all");
        m.Children(scrollbar+8,{bar_all});m.words[owner+(schedule?0x5c0:0x78)]=scrollbar;
        m.words[contents+0x134]=0;
        MM3D::NativeNotebook::Hook notebook;notebook.active=true;notebook.schedule=schedule;
        notebook.pass=3;notebook.panel=page;notebook.notebook_owner=owner;notebook.detail_opacity=.5f;
        for(float alpha:{1.f,.75f,.25f,0.f})for(float map_fade:{1.f,.5f}) {
            notebook.detail_opacity=map_fade;
            m.words[bar_all+0x134]=std::bit_cast<U32>(alpha);
            auto before=m.words;auto root=notebook.PromptRoot(m,page,sp);
            if(alpha==0)assert(root==page+8);
            else {
                assert(root!=page+8);auto children=m.words[root+0x14];
                for(unsigned i=0;i<8;++i) {
                    float expected=alpha*map_fade;
                    if(schedule&&i<2&&!P::ScheduleDetailsAvailable(m,owner,page).value_or(true))expected*=.4f;
                    for(U32 off:{0x128u,0x12cu,0x130u,0x134u})
                        assert(std::bit_cast<float>(m.words[m.words[children+4*i]+off])==expected);
                }
            }
            notebook.prompt_hook.End(m);assert(m.words==before);
        }
        m.Children(parent,{text});
        m.words[owner+(schedule?0x5c0:0x78)]=0;
    }
    // Availability follows the selected row/column, and dims both A panes only.
    const auto events=m.Alloc(128*0x12);
    m.words[owner+0x6c]=events;m.words[owner+0x254]=0x00010100;
    const auto setbyte=[&](U32 at,U32 value) {
        auto& word=m.words[at&~3u];const auto shift=(at&3)*8;
        word=(word&~(255u<<shift))|(value<<shift);
    };
    setbyte(events+0x12,2);setbyte(events+0x12+4,7);
    for(auto [at,word]:P::AvailabilityCode)m.words[at]=word;
    for(U32 flags:{0u,0x80u})for(U32 details:{0u,1u}) {
        setbyte(0x7751d8+0x18ac+14,flags);setbyte(0x72f060+0x18+56,details);
        const bool available=details&&!(flags&0x80);
        assert(P::DetailsAvailable(m,owner)==available);
        auto before=m.words;
        const auto root=hook.Build(m,layout,owner,sp,.5f),children=*m.Read(root+0x14);
        for(U32 i=0;i<6;++i)for(U32 a=0;a<4;++a)
            assert(std::bit_cast<float>(*m.Read(*m.Read(children+i*4)+0x128+a*4))==
                   (i<2&&!available?.2f:.5f));
        assert(hook.End(m)==sp&&m.words==before);
    }
    m.words[owner+0x254]=0x0001ff00;assert(!P::DetailsAvailable(m,owner).has_value());
    m.words[owner+0x254]=0x00ff0100;assert(!P::DetailsAvailable(m,owner).has_value());
    // Keep both bright and dim prompts stable through a stale sub-event index,
    // then apply the destination state as soon as its selection becomes valid.
    for(bool source:{false,true})for(bool destination:{false,true}) {
        setbyte(0x7751d8+0x18ac+14,0);
        for(unsigned step=0;step<3;++step) {
            m.words[owner+0x254]=(step==1?0x00050100:0x00010100);
            setbyte(0x72f060+0x18+56,step==0?source:destination);
            const bool expected=step<2?source:destination;
            auto before=m.words;auto root=hook.Build(m,layout,owner,sp,.5f);
            assert(root!=layout+8);auto children=m.words[root+0x14];
            for(unsigned i=0;i<6;++i)
                assert(std::bit_cast<float>(m.words[m.words[children+4*i]+0x134])==(i<2&&!expected?.2f:.5f));
            hook.End(m);assert(m.words==before);
        }
    }
    m.words[owner+0x254]=0x00010100;
    m.words.erase((0x72f060+0x18+56)&~3u);assert(!P::DetailsAvailable(m,owner).has_value());
    m.words[0x1c12d0]^=1;assert(!P::AvailabilityCodeMatches(m));
    assert(!P::DetailsAvailable(m,owner).has_value());
    m.words=original;
    P::schedule_enabled=false;
    for(U32 page:{0u,1u,2u})for(U32 buttons:{0u,4u,0x100u,0x800u,0x900u}) {
        m.words[owner+0xc]=page;m.words[owner+0x40]=layout;
        const auto flags=MM3D::NativeNotebook::ScheduleInputFlags(m,owner,buttons,0xf0000010);
        assert((flags&0x40000000u)==((buttons&0x800u)?0u:0x40000000u));
        assert((flags&~0x40000000u)==0x10000010u);
    }
    m.words=original;
    P::icon_scale=1;
    P::map_enabled=false;P::map_pose.x=0;P::map_pose.y=0;P::map_pose.scale=1;
    for(unsigned mode=0;mode<7;++mode) {
        m.words=original;P::enabled=mode!=0;
        if(mode==1)m.words[tg+0x28]=0;
        if(mode==2)m.words.erase(sp-P::ScratchSize*2);
        if(mode==3)m.words[prompt+0x180]=0;
        if(mode==4)m.words[P::RegistryLiteral]=0;
        if(mode==6)m.words[header+0x14]=0;
        auto before=m.words;
        assert(hook.Build(m,layout,owner,sp,mode==5?0.f:1.f)==layout+8);
        assert(!hook.End(m)&&m.words==before);
    }
    m.words=original;P::enabled=true;
    assert(!MM3D::NotebookFont::Lookup(m,font,'~',0));
    MM3D::NativeNotebook::Hook notebook;notebook.active=true;notebook.panel=layout;notebook.notebook_owner=owner;
    for(auto pass:{2u,3u,5u}) {
        notebook.pass=pass;auto r=notebook.PromptRoot(m,layout,sp);
        assert((r!=layout+8)==(pass==3));notebook.prompt_hook.End(m);assert(m.words==original);
    }
    notebook.schedule=true;notebook.pass=3;
    assert(notebook.PromptRoot(m,layout,sp)==layout+8);notebook.prompt_hook.End(m);
}
void ItemPresentChecks() {
    namespace P=MM3D::NativeItemPresent;
    {
        Memory tm;P::Hook th;
        th.owner=tm.Alloc(0x100);auto player=tm.Alloc(0x18),anim=tm.Alloc(0x18);
        tm.words[th.owner+0x34]=player;tm.words[player+0xc]=anim;
        auto node=tm.Widget("item_panel_g",-96,0);
        tm.words[node+0x3c]=std::bit_cast<U32>(32.f);
        th.frame[P::Confirm]={435.6f,481.4f,1.f};
        th.frame[P::Return]={-512.f,435.6f,1.f};
        th.frame[P::Description].x=9999; // Description cannot affect travel bounds.
        for(bool opening:{true,false})for(float f:{0.f,1.5f,3.f,4.5f,6.f}) {
            tm.words[anim+8]=tm.String(opening?"in_item_presen_anim":"out_item_presen_anim");
            tm.words[player+0x10]=std::bit_cast<U32>(f);th.ReadTransition(tm);
            assert(th.transitioning);
            const float expected=-368*(opening?1-f/6:f/6);
            assert(std::abs(th.transition_x-expected)<.001f);
            std::vector<P::Hook::Write> writes;std::vector<U32> seen;
            assert(th.Move(tm,node,P::Panel,writes,seen));
            auto before=tm.words;for(auto w:writes)tm.Write(w.address,w.after);
            assert(std::abs(std::bit_cast<float>(tm.words[node+0xec])-(32+expected))<.001f);
            for(auto w:writes)tm.Write(w.address,w.before);assert(tm.words==before);
        }
        tm.words[anim+8]=tm.String("out_btn_decide_anim");th.ReadTransition(tm);assert(!th.transitioning);
    }
    {
        Memory im;auto owner=im.Alloc(0x220),layout=im.Layout("MenuItemPresen");im.words[owner+0x28]=layout;
        assert(P::SkipIntro(im,owner));im.words[owner+0x21c]=1;assert(!P::SkipIntro(im,owner));
        im.words[owner+0x21c]=0;P::enabled=false;assert(!P::SkipIntro(im,owner));P::enabled=true;
        im.words[owner+0x28]=im.Layout("MenuItem");assert(!P::SkipIntro(im,owner));
        assert(P::IntroInstruction(P::IntroPC,P::IntroWord)==(0xef000000|P::IntroSVC));
        assert(P::IntroInstruction(P::IntroPC,P::IntroWord^1)==(P::IntroWord^1));
    }
    Memory m;P::Options options;options.poses[P::Background]={-23,99,1.8f};options.poses[P::Panel]={10,20,.5f};
    options.poses[P::Right]={-20,8,.75f};options.poses[P::Prompt]={30,9,1.2f};P::Apply(options);
    const auto owner=m.Alloc(0x300),panel=m.Layout("MenuItemPresen"),all=m.Widget("all"),group=m.Widget("presen_item_collect");
    const auto slots=m.Widget("item_set_pos_g"),grid=m.Widget("item_panel_g",100,60),right=m.Widget("right_base_g",280,20);
    const auto prompt=m.Widget("disp_item_name",160,224),back=m.Widget("btn_return_l"),confirm=m.Widget("btn_decide_g");
    m.Children(panel+8,{all});m.Children(all,{group,prompt,back,confirm});m.Children(group,{slots,grid,right});
    {
        P::Hook pose_hook;
        const auto unrelated=m.Layout("MessageWindow");
        const auto native=m.words;
        for(float alpha:{0.f,.5f,1.f}) {
            m.words[prompt+0x98]=std::bit_cast<U32>(.8f+.2f*alpha);
            m.words[prompt+0x9c]=std::bit_cast<U32>(.8f+.2f*alpha);
            m.words[prompt+0xd0]=std::bit_cast<U32>(alpha);
            const auto before=m.words;
            pose_hook.BeginPromptPose(m,panel);
            auto expected=before;
            for(U32 off:{0x98u,0x9cu,0xd0u})expected[prompt+off]=std::bit_cast<U32>(1.f);
            expected[prompt+0xd4]|=0x04001800;expected[prompt+0xd8]|=0x04001800;
            assert(m.words==expected); // Translation, colors, other groups and clocks untouched.
            pose_hook.BeginPromptPose(m,unrelated);pose_hook.EndPromptPose(m);
            assert(m.words==expected); // Nested layout must not restore the parent early.
            pose_hook.EndPromptPose(m);assert(m.words==before);
        }
        P::enabled=false;auto before=m.words;
        pose_hook.BeginPromptPose(m,panel);pose_hook.EndPromptPose(m);
        assert(m.words==before);P::enabled=true;
        m.words.erase(prompt+0xd8);before=m.words;
        pose_hook.BeginPromptPose(m,panel);pose_hook.EndPromptPose(m);
        assert(m.words==before); // Incomplete input must not partially change the pose.
        m.words=native;
    }
    m.words[owner+0x28]=panel;
    std::vector<U32> entries{panel},nodes;
    for(unsigned i=0;i<20;++i) {
        auto icon=m.Layout("ItemIconButton",10,20),node=m.Widget("slot");
        m.words[owner+0x5c+i*4]=icon;m.words[icon+0x184]=owner+0x24;
        m.words[node+0x20]=icon;entries.push_back(icon);nodes.push_back(node);
    }
    m.Children(slots,nodes);
    const auto cursor=m.Layout("CursorItemCollect",10,20),bg=m.Layout("CommonBottomBg");
    const auto brown_right=m.Brown(bg);
    m.words[owner+8]=cursor;m.words[owner+4]=bg;entries.push_back(cursor);
    const auto manager=m.Alloc(32),q1=m.Alloc(0x848),q3=m.Alloc(0x848),q4=m.Alloc(0x848),q5=m.Alloc(0x848);
    m.words[manager+12]=q1;m.words[manager+20]=q3;m.words[manager+24]=q4;m.words[manager+28]=q5;
    m.Queue(q4,{bg});m.Queue(q5,entries);auto original=m.words;P::Hook h;
    P::enabled=false;h.Pass(m,manager,4);assert(!h.panel&&m.words==original);P::enabled=true;
    h.Pass(m,manager,4);assert(h.panel==panel&&m.words[q4+0x844]==0&&m.words[q1+0x844]==26);
    const auto backdrop=m.words;h.Pass(m,manager,1);
    for(unsigned draw=0;draw<26;++draw) {
        h.Begin(m,bg);
        const auto f=[&](U32 off){return std::bit_cast<float>(m.words[brown_right+off]);};
        assert(f(0xfc)==background_pose.y.load()-(draw%2?240*background_pose.scale.load():0.f));
        if(draw<24) {assert(f(0x128)==0&&f(0x134)>0&&f(0x134)<.5f);}
        else {assert(f(0x128)==1);assert(f(0xec)==-23);assert(f(0xe0)==background_pose.scale.load());}
        h.End(m);assert(m.words==backdrop);
    }
    h.Pass(m,manager,5);assert(m.words[q5+0x844]==0&&m.words[q3+0x844]==22);
    auto message=m.Layout("MessageWindow",0,0,0x450);m.words[message+0x3f4]=0x174c;
    assert(h.IsDescription(m,message));m.words[message+0x3f4]=0x1234;assert(!h.IsDescription(m,message));
    m.words[message+0x3f4]=0x174c;
    std::vector<U32> strip;
    for(unsigned i=0;i<4;++i){const char* names[]={"icon_event00_l","icon_event01_l","icon_event02_l","icon_event03_l"};strip.push_back(m.Widget(names[i],290,29+47*i));}
    auto backing=m.Widget("right_base_pane");strip.push_back(backing);m.Children(right,strip);
    auto queued=m.words;h.Pass(m,manager,3);h.Begin(m,panel);
    assert(std::bit_cast<float>(m.words[grid+0xec])==60);
    assert(std::bit_cast<float>(m.words[right+0xec])==190);
    assert(std::abs(std::bit_cast<float>(m.words[prompt+0xec])-222)<.001f);
    assert(std::bit_cast<float>(m.words[entries[1]+8+0xec])==15);
    h.End(m);assert(m.words==queued);
    h.Begin(m,cursor);assert(std::bit_cast<float>(m.words[cursor+8+0xec])==15);h.End(m);assert(m.words==queued);
    h.frame_horizontal=true;h.Begin(m,panel);
    for(unsigned i=0;i<4;++i) {
        assert(std::bit_cast<float>(m.words[strip[i]+0xec])==-20.f+.75f*(29+47*i));
        assert(std::bit_cast<float>(m.words[strip[i]+0xfc])==29.f);
        assert(std::bit_cast<float>(m.words[strip[i]+0xe0])==.75f);
    }
    assert(std::bit_cast<float>(m.words[backing+0xe4])==.75f);
    h.End(m);assert(m.words==queued);
    h.cursor_part=P::Right;h.cursor_slot=2;h.Begin(m,cursor);
    assert(std::bit_cast<float>(m.words[cursor+8+0xec])==-20.f+.75f*(10+123-290));
    h.End(m);assert(m.words==queued);
    m.words=original;m.words[all+0x18]=3;h.Pass(m,manager,4);auto before=m.words;h.Pass(m,manager,5);assert(m.words==before);
    m.words=original;m.words[owner+0x28]=0;h.Pass(m,manager,4);h.Pass(m,manager,5);assert(m.words[q5+0x844]==22);
    P::Apply(P::Options{});
}
void MaskPresentChecks() {
    namespace P=MM3D::NativeItemPresent;
    Memory m;P::Hook h;P::Options options;
    options.horizontal=true; // Mask column six must stay in the grid.
    options.poses[P::Panel]={10,20,.5f};options.poses[P::Prompt]={30,9,1.2f};
    options.poses[P::Right]={-20,8,.75f};P::ApplyMask(options);
    P::Options item_options;item_options.poses[P::Panel]={100,200,1.5f};P::Apply(item_options);
    const auto owner=m.Alloc(0x300),panel=m.Layout("MenuMaskPresen"),all=m.Widget("all");
    const auto group=m.Widget("presen_mask"),slots=m.Widget("mask_set_pos_g"),grid=m.Widget("mask_panel_g",100,60);
    const auto prompt=m.Widget("disp_mask_name",160,224),label=m.Widget("e_mask_presen_g");
    const auto back=m.Widget("btn_return_l"),confirm=m.Widget("btn_decide_g");
    m.Children(panel+8,{all});m.Children(all,{group,prompt,back,confirm});m.Children(group,{slots,grid});
    m.Children(prompt,{label,m.Widget("bb_mask_name")});m.words[label+0xd4]&=~1u;
    m.words[prompt+0x98]=m.words[prompt+0x9c]=std::bit_cast<U32>(.8f);
    m.words[prompt+0xd0]=std::bit_cast<U32>(.5f);
    m.words[owner+0x28]=panel;m.words[owner+0x18]=0x12340305;
    std::vector<U32> entries{panel},nodes;
    for(unsigned i=0;i<24;++i) {
        const auto icon=m.Layout("ItemIconButton",32+48*(i%6),29+47*(i/6)),node=m.Widget("slot");
        m.words[owner+0x50+4*i]=icon;m.words[icon+0x184]=owner+0x24;
        m.words[node+0x20]=icon;entries.push_back(icon);nodes.push_back(node);
    }
    m.Children(slots,nodes);
    const auto cursor=m.Layout("CursorItemCollect",272,170),touch=m.Layout("CursorItemCollect",272,170);
    const auto bg=m.Layout("CommonBottomBg"),other=m.Layout("Unrelated"),message=m.Layout("MessageWindow",0,0,0x450);
    m.words[message+0x3f4]=0x174c;
    m.words[owner+4]=bg;m.words[owner+8]=cursor;m.words[owner+0xc]=touch;
    entries.push_back(cursor);entries.push_back(touch);entries.push_back(other);
    const auto manager=m.Alloc(32),q1=m.Alloc(0x848),q3=m.Alloc(0x848),q4=m.Alloc(0x848),q5=m.Alloc(0x848);
    m.words[manager+12]=q1;m.words[manager+20]=q3;m.words[manager+24]=q4;m.words[manager+28]=q5;
    m.Queue(q1,{other});m.Queue(q4,{bg});m.Queue(q5,entries);
    const auto original=m.words;
    assert(!P::SkipIntro(m,owner)); // The item-only input patch never affects moon dialogue.
    P::mask_enabled=false;h.Pass(m,manager,4);assert(!h.panel&&m.words==original);P::mask_enabled=true;
    P::enabled=false; // Disabling Give Item must not disable Select Mask.
    h.Pass(m,manager,4);
    assert(h.panel==panel&&h.mask_menu&&h.icon_count==24&&!h.frame_horizontal&&h.cursor_part==P::Panel);
    assert(m.words[q4+0x844]==0&&m.words[q1+0x844]==1); // No full-screen mask backdrop on top.
    h.Pass(m,manager,5);
    assert(m.words[q5+0x844]==1&&m.words[q5+4+0x80]==other&&m.words[q3+0x844]==27);
    assert(h.IsDescription(m,message));
    auto queued=m.words;h.Pass(m,manager,3);h.Begin(m,panel);
    assert(m.words[label+0xd4]&1);
    assert(std::bit_cast<float>(m.words[grid+0xec])==60);
    assert(std::abs(std::bit_cast<float>(m.words[prompt+0xec])-222)<.001f);
    for(unsigned i=0;i<24;++i)
        assert(std::bit_cast<float>(m.words[entries[i+1]+8+0xec])==10+.5f*(32+48*(i%6)));
    h.End(m);assert(m.words==queued);
    for(U32 c:{cursor,touch}) {
        h.Begin(m,c);assert(std::bit_cast<float>(m.words[c+8+0xec])==146);
        h.End(m);assert(m.words==queued);
    }
    h.BeginPromptPose(m,panel);
    for(U32 off:{0x98u,0x9cu,0xd0u})assert(m.words[prompt+off]==std::bit_cast<U32>(1.f));
    assert(m.words[label+0xd4]&1);h.EndPromptPose(m);assert(m.words==queued);
    m.words.erase(label+0xd4);auto broken=m.words;
    h.BeginPromptPose(m,panel);h.EndPromptPose(m);assert(m.words==broken);m.words=queued;

    // The mask player is at +0x30; +0x34 contains an animation resource.
    const auto player=m.Alloc(0x18),anim=m.Alloc(0x18);m.words[owner+0x30]=player;
    m.words[owner+0x34]=anim;m.words[player+0xc]=anim;
    m.words[grid+0x3c]=std::bit_cast<U32>(100.f);m.words[grid+0x40]=std::bit_cast<U32>(60.f);
    float travel=0;
    for(bool opening:{true,false}) {
        m.words[anim+8]=m.String(opening?"in_mask_presen_anim":"out_mask_presen_anim");
        for(float f:{0.f,1.5f,3.f,4.5f,6.f}) {
            m.words[player+0x10]=std::bit_cast<U32>(f);h.ReadTransition(m);
            assert(h.transitioning);
            if(opening&&f==0)travel=-h.transition_x;
            assert(travel>0);
            assert(std::abs(h.transition_x+travel*(opening?1-f/6:f/6))<.001f);
            m.words[grid+0xec]=std::bit_cast<U32>(-96.f); // Native animation must not add a second slide.
            const auto before=m.words;std::vector<P::Hook::Write> writes;std::vector<U32> seen;
            assert(h.Move(m,grid,P::Panel,writes,seen));for(auto w:writes)m.Write(w.address,w.after);
            assert(std::abs(std::bit_cast<float>(m.words[grid+0xec])-(60+h.transition_x))<.001f);
            for(auto w:writes)m.Write(w.address,w.before);assert(m.words==before);
        }
    }
    m.words[anim+8]=m.String("in_item_presen_anim");h.ReadTransition(m);assert(!h.transitioning);
    // Invalid layout/owner/icon data leaves both queues and guest state untouched.
    for(U32 address:{owner+0x28,owner+0x50+23*4,group+0x18}) {
        m.words=original;m.words[address]=0;const auto before=m.words;
        h.Pass(m,manager,4);h.Pass(m,manager,5);assert(!h.panel&&m.words==before);
    }
    P::Apply({});P::ApplyMask({});
}
void InventoryNavigationChecks() {
    namespace N=MM3D::InventoryNavigation;
    Memory m;auto owner=m.Alloc(0x180),sp=m.Alloc(24),layout=m.Layout("MenuItem");
    m.words[owner+0xc0]=layout;m.words[owner+0xa0]=5u<<24;
    for(unsigned i=0;i<5;++i){auto icon=m.Layout("ItemIconButton",32+48*i,32);m.words[owner+0xd0+i*4]=icon;}
    enabled=true;bar_enabled=true;x=0;scale=1;bar_x=0;bar_scale=1;bar_spacing=64;
    const auto move=[&](unsigned c,unsigned r,bool vertical,int delta) {
        m.words[owner+0x18]=0xabcd0000u|c|(r<<8);
        m.words[sp+20]=vertical?(delta<0?0x5c1434:0x5c1464):(delta<0?0x5c149c:0x5c14d4);
        const bool changed=N::Move(m,owner,sp,vertical,U32(delta));
        assert((m.words[owner+0x18]&0xffff0000u)==0xabcd0000u);return changed;
    };
    const unsigned up[]={0,1,1,2,3},down[]={0,1,3,4};
    for(unsigned c=0;c<5;++c){assert(move(c,0,true,-1));assert((m.words[owner+0x18]&65535)==(5u|(up[c]<<8)));}
    for(unsigned r=0;r<4;++r){assert(move(5,r,true,1));assert((m.words[owner+0x18]&65535)==down[r]);
        assert(move(5,r,false,1));assert(((m.words[owner+0x18]>>8)&255)==std::min(r+1,3u));
        assert(move(5,r,false,-1));assert(((m.words[owner+0x18]>>8)&255)==(r?r-1:0));
        assert(move(5,r,true,-1));assert((m.words[owner+0x18]&65535)==(5u|(r<<8)));}
    assert(!move(2,2,true,-1));assert(!move(2,0,true,1));
    assert(move(4,2,false,1));assert((m.words[owner+0x18]&65535)==0x204);
    assert(move(0,2,false,-1));assert((m.words[owner+0x18]&65535)==0x200);
    bar_x=200;assert(move(4,0,true,-1));assert(((m.words[owner+0x18]>>8)&255)==0);bar_x=0;
    bar_enabled=false;assert(!move(0,0,true,-1));bar_enabled=true;
    m.words[owner+0xc0]=m.Layout("MenuMask");assert(!move(0,0,true,-1));
    m.words[owner+0xa0]=6u<<24;masks_x=0;masks_scale=1;
    m.words[owner+0xd0+5*4]=m.Layout("ItemIconButton",272,32);
    const unsigned mask_up[]={0,1,1,2,3,3};
    for(unsigned c=0;c<6;++c) {
        assert(move(c,0,true,-1));assert((m.words[owner+0x18]&65535)==(6u|(mask_up[c]<<8)));
    }
    for(unsigned r=0;r<4;++r) {
        assert(move(6,r,true,1));assert((m.words[owner+0x18]&65535)==down[r]);
        assert(move(6,r,false,1));assert(((m.words[owner+0x18]>>8)&255)==std::min(r+1,3u));
        assert(move(6,r,false,-1));assert(((m.words[owner+0x18]>>8)&255)==(r?r-1:0));
        assert(move(6,r,true,-1));assert((m.words[owner+0x18]&65535)==(6u|(r<<8)));
    }
    assert(move(5,2,false,1));assert((m.words[owner+0x18]&65535)==0x205);
    assert(!move(5,2,true,-1));
    masks_x=-100;masks_scale=.5f;
    assert(move(5,0,true,-1));assert((m.words[owner+0x18]&65535)==6);
    assert(move(6,3,true,1));assert((m.words[owner+0x18]&65535)==5);
    bar_enabled=false;assert(!move(0,0,true,-1));bar_enabled=true;
    masks_x=0;masks_scale=1;m.words[owner+0xc0]=layout;m.words[owner+0xa0]=5u<<24;
    m.words[sp+20]=0;auto before=m.words;assert(!N::Move(m,owner,sp,true,1)&&m.words==before);
    x=0;scale=.8f;bar_scale=.65f;bar_spacing=64;
}
void BottomStripNavigationChecks() {
    namespace N=MM3D::InventoryNavigation;namespace P=MM3D::NativeItemPresent;
    Memory m;auto owner=m.Alloc(0x300),sp=m.Alloc(24);m.words[owner+0x28]=m.Layout("MenuItemPresen");
    P::Apply(P::Options{});P::horizontal=true;
    for(unsigned i=0;i<5;++i)m.words[owner+0x5c+(15+i)*4]=m.Layout("ItemIconButton",29+47*i,180);
    for(unsigned c=0;c<5;++c){m.words[owner+0x18]=0x12340300|c;m.words[sp+20]=0x5bc8c8;assert(N::PresentMove(m,owner,sp,true,1));assert(m.words[owner+0x18]==(0x12340005u|(std::min(c,3u)<<8)));}
    for(unsigned r=0;r<4;++r){m.words[owner+0x18]=5|(r<<8);m.words[sp+20]=0x5bc898;assert(N::PresentMove(m,owner,sp,true,~0u));assert(m.words[owner+0x18]==(0x300|r));
        m.words[owner+0x18]=5|(r<<8);m.words[sp+20]=0x5bc92c;assert(N::PresentMove(m,owner,sp,false,1));assert(m.words[owner+0x18]==(5|(std::min(r+1,3u)<<8)));}
    P::horizontal=false;assert(!N::PresentMove(m,owner,sp,false,1));P::Apply(P::Options{});
    MM3D::GearMenu::Options gear;gear.poses[MM3D::GearMenu::Panel]={0,0,1};gear.poses[MM3D::GearMenu::Right]={-260,220,1};MM3D::GearMenu::Apply(gear);
    m.words[owner+0x14]=m.Layout("MenuCollect");
    for(unsigned i=4;i<=20;++i){m.words[owner+0x34+i*12]=std::bit_cast<U32>(29.f+47*((i-4)%7));m.words[owner+0x38+i*12]=std::bit_cast<U32>(i>=14?180.f:60.f);m.words[0x620364+i*4]=0xffffffff;}
    for(unsigned slot=0;slot<4;++slot){m.words[owner+0x194]=slot<<8;assert(N::GearMove(m,owner,0x10)==std::min(slot+1,3u));assert(N::GearMove(m,owner,0x20)==(slot?slot-1:0));assert(N::GearMove(m,owner,0x80)==slot);
        auto up=N::GearMove(m,owner,0x40);assert(up&&*up>=14&&*up<=20);m.words[owner+0x194]=*up<<8;assert(N::GearMove(m,owner,0x80)==slot);}
    m.words[owner+0x194]=4<<8;assert(!N::GearMove(m,owner,0x80));m.words[0x620364+4*4]=0xffffff00;assert(N::GearMove(m,owner,0x40)==4);
    gear.poses[MM3D::GearMenu::Right].y=-20;MM3D::GearMenu::Apply(gear);
    for(unsigned slot=0;slot<4;++slot){
        m.words[owner+0x194]=slot<<8;
        assert(N::GearMove(m,owner,0x40)==slot);
        assert(N::GearMove(m,owner,0x10)==std::min(slot+1,3u));
        auto down=N::GearMove(m,owner,0x80);assert(down&&*down>=4&&*down<14);
        m.words[owner+0x194]=*down<<8;assert(N::GearMove(m,owner,0x40)==slot);
        assert(N::GearMove(m,owner,0x40000000)==slot);
    }
    MM3D::GearMenu::horizontal=false;assert(!N::GearMove(m,owner,0x40));MM3D::GearMenu::Apply(MM3D::GearMenu::Options{});
}
void InventoryTransitionChecks() {
    Memory m;Hook h;
    enabled=true;bar_enabled=true;shared_slide=true;buttons_enabled=false;background_enabled=true;
    x=10;y=20;scale=.5f;bar_x=10;bar_y=20;bar_scale=.5f;bar_spacing=64;
    background_pose.x=30;background_pose.y=10;background_pose.scale=1.2f;
    auto page=m.Alloc(0x200),panel=m.Layout("MenuItem"),all=m.Widget("all");
    m.Children(panel+8,{all});m.words[page+0xc0]=panel;
    std::vector<U32> branches;
    for(auto name:{"set_frame3_g","set_frame2_g","set_frame1_g","set_frame0_g","item_set_pos_g","btn_item_omen_g","btn_return_l","item_panel_g","right_item_set_l"})
        branches.push_back(m.Widget(name));
    m.Children(all,branches);
    auto strip=m.Widget("panel_base_r0",256,0),label=m.Widget("disp_x_pane",267,67);
    m.Children(branches[8],{strip,label});
    auto widgets=m.Alloc(36);m.words[panel+0x154]=widgets;m.words[panel+0x158]=9;
    for(unsigned i=0;i<9;++i)m.words[widgets+i*4]=branches[i];
    auto player=m.Alloc(0x18),anim=m.Alloc(0x18),players=m.Alloc(4);
    m.words[panel+0x14c]=players;m.words[panel+0x150]=1;m.words[players]=player;
    m.words[player+4]=m.String("leftPaneTransition");m.words[player+0xc]=anim;
    auto in=m.String("in_menu_item_anim"),out=m.String("out_menu_item_anim");
    std::vector<U32> entries{panel},grid,slots;
    for(unsigned i=0;i<20;++i) {auto icon=m.Layout("ItemIconButton",32,28);grid.push_back(icon);entries.push_back(icon);m.words[page+0xd0+4*i]=icon;}
    for(unsigned i=0;i<4;++i) {auto icon=m.Layout("ItemIconButton",Hook::Anchors[i][0],Hook::Anchors[i][1]);slots.push_back(icon);entries.push_back(icon);m.words[page+0x2c+4*i]=icon;}
    auto bg=m.Layout("CommonBottomBg");m.words[page+4]=bg;
    auto manager=m.Alloc(0x30),bottom=m.Alloc(0x848),top=m.Alloc(0x848),bottom_bg=m.Alloc(0x848),top_bg=m.Alloc(0x848);
    m.words[manager+28]=bottom;m.words[manager+20]=top;m.words[manager+24]=bottom_bg;m.words[manager+8]=top_bg;
    m.Queue(bottom,entries);m.Queue(bottom_bg,{bg});
    const auto base=m.words;
    const auto put=[&](U32 at,float v){m.words[at]=std::bit_cast<U32>(v);};
    const auto get=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
    const auto near=[](float a,float b){assert(std::abs(a-b)<.002f);};
    for(bool opening:{true,false})for(float frame:{0.f,.5f,1.f,2.f,3.f,4.f,5.5f,6.f}) {
        m.words=base;m.words[anim+8]=opening?in:out;put(player+0x10,frame);
        float p=std::min(frame/4,1.f),q=std::min(frame/3,1.f);
        float left=-320*(opening?1-p:p),right=80*(opening?1-q:q);
        put(branches[4]+0xec,left);put(branches[7]+0xec,left);put(branches[8]+0xec,right);
        put(strip+0xec,256+right);put(label+0xec,267+right);
        for(auto icon:grid)put(icon+8+0xec,32+left);
        for(unsigned i=0;i<4;++i)put(slots[i]+8+0xec,Hook::Anchors[i][0]+right);
        auto native=m.words;h.Capture(m,page);h.Pass(m,manager,4);
        assert(h.slide_active&&h.routed_background==bg);
        const float t=opening?1-p:p,slide=-574*t;
        near(h.slide_x,slide);
        const float up=-116*t;near(h.bar_slide_y,up);
        assert(m.words[top_bg+0x844]==2);
        h.Pass(m,manager,5);assert(status==2);
        const auto routed=m.words;
        h.Pass(m,manager,0);h.Begin(m,bg);near(get(bg+8+0xec),30+slide);h.End(m);assert(m.words==routed);
        h.Pass(m,manager,3);h.Begin(m,panel);
        near(get(branches[7]+0xec),10+slide);
        // Backing and upright icons cancel native sideways motion and leave upward.
        near(get(strip+0xec),10);near(get(strip+0xfc),52+up);
        near(get(label+0xec),45);near(get(label+0xfc),22+up);
        h.End(m);assert(m.words==routed);
        for(unsigned i=0;i<4;++i) {
            h.Begin(m,slots[i]);near(get(slots[i]+8+0xec),26+32*i);near(get(slots[i]+8+0xfc),36+up);
            h.End(m);assert(m.words==routed);
        }
        h.Begin(m,grid[0]);near(get(grid[0]+8+0xec),26+slide);h.End(m);assert(m.words==routed);
        // Disable restores the previous native trajectories, including strip rotation.
        m.words=native;shared_slide=false;h.Capture(m,page);h.Pass(m,manager,4);h.Pass(m,manager,5);
        assert(!h.slide_active);h.Pass(m,manager,3);h.Begin(m,panel);
        near(get(branches[7]+0xec),10+.5f*left);near(get(strip+0xfc),52-.5f*right);h.End(m);
        shared_slide=true;
    }
    // Header and equipped row share displacement, using the header's native phase.
    m.words=base;m.words[anim+8]=out;put(player+0x10,2);
    auto header=m.Layout("SubMenuTitle"),header_base=m.Widget("menu_title_base",0,0);
    m.Children(header+8,{header_base});
    auto header_widgets=m.Alloc(4);m.words[header_widgets]=header_base;
    m.words[header+0x154]=header_widgets;m.words[header+0x158]=1;
    m.Queue(top_bg,{header});put(header_base+0xfc,-20);
    h.Capture(m,page);h.Pass(m,manager,4);
    assert(h.transition_header==header);near(h.bar_slide_y,-58);
    auto header_before=m.words;h.Pass(m,manager,0);h.Begin(m,header);
    near(get(header_base+0xfc),-58);h.End(m);assert(m.words==header_before);
    // Shadow-only backdrop draws precede both brown tiles and restore the live strip.
    m.words=base;m.words[anim+8]=in;put(player+0x10,4);
    auto shadow_layout=m.Layout("ItemIconSet2"),shadow_all=m.Widget("all");
    auto shadow=m.Widget("shadow_pane01"),other=m.Widget("panel_base_r0");
    m.Children(shadow_layout+8,{shadow_all});m.Children(shadow_all,{other,shadow});
    m.words[branches[8]+0x20]=shadow_layout;
    for(U32 offset:{0x128u,0x12cu,0x130u,0x134u})put(shadow+offset,.8f);
    shadow_width=1.5f;shadow_opacity=.5f;
    const auto shadow_native=m.words;
    h.Capture(m,page);h.Pass(m,manager,4);
    assert(h.shadow_layout==shadow_layout&&m.words[top_bg+0x844]==4);
    assert(m.words[top_bg+4+15*0x80]==shadow_layout);
    assert(m.words[top_bg+4+15*0x80+8]==bg);
    auto shadow_before=m.words;h.Pass(m,manager,0);
    for(unsigned layer=0;layer<2;++layer) {
        h.Begin(m,shadow_layout);
        assert(!(m.words[other+0xd4]&1));assert(m.words[shadow+0xd4]&1);
        near(get(shadow+0xec)+256*get(shadow+0xe0),30+320*1.2f);
        near(get(shadow+0xec)+240*get(shadow+0xe0),30+320*1.2f+16*1.8f);
        near(get(shadow+0xfc),10-layer*240*1.2f);
        near(get(shadow+0x134),.4f);
        h.End(m);assert(m.words==shadow_before);
    }
    // Masks use the same two brown tiles and live strip shadow, with 24 icons.
    m.words=shadow_native;
    m.words[m.words[panel+4]+0x18]=m.String("MenuMask");
    m.words[anim+8]=m.String("in_menu_mask_anim");
    for(unsigned i=20;i<24;++i)m.words[page+0xd0+4*i]=m.Layout("ItemIconButton",32,28);
    h.Capture(m,page);h.Pass(m,manager,4);
    assert(h.inventory_page&&h.slide_active&&h.shadow_layout==shadow_layout);
    assert(m.words[top_bg+0x844]==4&&h.routed_background==bg);
    auto masks_routed=m.words;h.Pass(m,manager,0);
    for(unsigned layer=0;layer<2;++layer) {
        h.Begin(m,bg);near(get(bg+8+0xfc),10-layer*240*1.2f);
        h.End(m);assert(m.words==masks_routed);
    }
    shadow_opacity=0;m.words=base;h.Capture(m,page);h.Pass(m,manager,4);
    assert(!h.shadow_layout&&m.words[top_bg+0x844]==2);
    shadow_width=shadow_opacity=1.f;
    // Guard unsupported animation data and pages; no guest writes during sampling.
    m.words=base;m.words[anim+8]=out;put(player+0x10,2);h.Capture(m,page);
    auto before=m.words;h.ReadTransition(m);assert(h.slide_active&&m.words==before);
    m.words[anim+8]=m.String("unknown");h.ReadTransition(m);assert(!h.slide_active);
    m.words[anim+8]=out;put(player+0x10,std::numeric_limits<float>::quiet_NaN());h.ReadTransition(m);assert(!h.slide_active);
    put(player+0x10,4);bar_enabled=false;h.ReadTransition(m);assert(!h.slide_active);bar_enabled=true;
    h.gear=true;h.ReadTransition(m);assert(!h.slide_active);h.gear=false;
    auto resource=m.words[panel+4];m.words[resource+0x18]=m.String("MenuMask");h.ReadTransition(m);assert(!h.slide_active);
    for(bool opening:{true,false}) {
        m.words[anim+8]=m.String(opening?"in_menu_mask_anim":"out_menu_mask_anim");
        masks_x=320;masks_scale=1.5f;
        for(float f:{0.f,2.f,4.f,6.f}) {
            put(player+0x10,f);auto native=m.words;h.ReadTransition(m);
            assert(h.slide_active&&m.words==native);
            if((opening&&f==0)||(!opening&&f>=4))assert(10+320+320*.5f*1.5f+h.slide_x<=-64);
        }
    }
    masks_x=0;masks_scale=1;
    m.words[resource+0x18]=m.String("MenuItem");m.words[anim+8]=out;
    // Largest GUI poses still completely clear the screen by movement frame4.
    x=320;scale=1.5f;bar_x=320;bar_scale=1.5f;bar_spacing=120;
    background_pose.x=400;background_pose.scale=2;
    h.ReadTransition(m);assert(h.slide_active);
    assert(1200+h.slide_x<=-64&&20+64*1.5f+h.bar_slide_y<=-64);
    x=0;y=32;scale=.8f;bar_x=0;bar_y=20;bar_scale=.65f;bar_spacing=64;
    background_pose.x=0;background_pose.y=0;background_pose.scale=1;buttons_enabled=true;
}
void GearTransitionChecks() {
    Memory m;Hook h;h.gear=true;h.panel=m.Layout("MenuCollect");h.gear_horizontal=true;
    MM3D::GearMenu::Options options;MM3D::GearMenu::Apply(options);
    // This draw snapshot holds only the four 2D groups; model/description
    // poses are handled separately and must not be copied past its end.
    for(unsigned i=0;i<h.gear_frame.size();++i)h.gear_frame.at(i)=options.poses.at(i);
    auto left=m.Widget("left_base_g"),right=m.Widget("right_base_g"),widgets=m.Alloc(8);
    m.words[widgets]=left;m.words[widgets+4]=right;m.words[h.panel+0x154]=widgets;m.words[h.panel+0x158]=2;
    auto player=m.Alloc(0x18),anim=m.Alloc(0x18),players=m.Alloc(4);
    m.words[players]=player;m.words[h.panel+0x14c]=players;m.words[h.panel+0x150]=1;m.words[player+0xc]=anim;
    for(auto name:{"in_menu_collect_anim","out_menu_collect_anim"}) {
        m.words[anim+8]=m.String(name);
        for(float t:{0.f,.25f,.5f,.75f,1.f}) {
            m.words[left+0xec]=std::bit_cast<U32>(-264*t);m.words[right+0xec]=std::bit_cast<U32>(64*t);
            auto before=m.words;h.ReadGearTransition(m);assert(h.gear_slide_active&&m.words==before);
            const auto grid=h.GearTransform(MM3D::GearMenu::Panel),strip=h.GearTransform(MM3D::GearMenu::Right);
            assert(std::abs(grid.tx+grid.xx*(-264*t)-(options.poses[0].x+h.gear_slide_x))<.001f);
            assert(std::abs(strip.tx-(260+options.poses[1].x+h.gear_slide_x))<.001f);
            assert(std::abs(strip.ty+strip.yx*(262+64*t)-(options.poses[1].y+56*options.poses[1].scale))<.001f);
            if(t==1)assert(336+h.gear_slide_x<=-32);
        }
    }
    m.words[anim+8]=m.String("blink_picture_anim");h.ReadGearTransition(m);assert(!h.gear_slide_active&&h.gear_slide_x==0);
    {
        // Reproduce the captured extents: Gear's right edge is 158.63px,
        // while Masks travels 266.9px over eight 60 Hz submissions.
        const std::array<float,8> saved{x.load(),scale.load(),masks_x.load(),masks_scale.load(),
            background_pose.x.load(),background_pose.y.load(),background_pose.scale.load(),shadow_width.load()};
        const bool saved_background=background_enabled.load();
        x=-21.1f;scale=.7f;masks_x=0;masks_scale=1;
        background_enabled=true;background_pose.x=-42.97f;background_pose.y=75.5f;background_pose.scale=.6f;shadow_width=1;
        auto compact=options;compact.poses[MM3D::GearMenu::Panel]={0,32,.5f};
        compact.poses[MM3D::GearMenu::Right]={400,32,.5f};MM3D::GearMenu::Apply(compact);
        const auto reference=HorizontalSlideDistance(true);assert(reference&&std::abs(*reference-266.9f)<.001f);
        namespace I=MM3D::InventoryInterpolation;
        I::History history;I::Values u{};I::Tev tev{};
        u[0]={1,0,-1.f/120,0};u[1]={1,0,0,-1.f/200};
        u[10]={0,3,94,1};u[11]={158.63f,0,0,-100};u[12]={75.5f,0,40,0};u[13]={0,1,0,0};
        u[94]={0,.25f,0,.125f};u[95]={0,.5f,.0625f,0};
        for(unsigned i=0;i<6;++i)tev[i]={i==0?0x0ee00ee0u:i==1?0x0e3f0e3fu:0x0eef0eefu,
            0,i<2?0x10001u:0u,0xffffffffu,0};
        m.words[anim+8]=m.String("in_menu_collect_anim");
        float previous=0;
        for(unsigned step=0;step<4;++step) {
            const float remaining=.75f-.25f*step;
            m.words[left+0xec]=std::bit_cast<U32>(-264*remaining);
            m.words[right+0xec]=std::bit_cast<U32>(64*remaining);
            const auto native=m.words;h.ReadGearTransition(m);
            assert(h.gear_slide_active&&native==m.words);
            u[11][0]=158.63f+h.gear_slide_x;
            const auto midpoint=history.Apply(100+step,{100,200,1,42},u,6,tev);
            if(!step)assert(!midpoint&&u[11][0]<0); // Missing history is entirely offscreen.
            else {
                assert(midpoint);
                // Every displayed step, including entry across X=0, matches
                // Masks' native 60 Hz displacement; no invented prior pose.
                assert(std::abs(midpoint->values[11][0]-previous-*reference/8)<.001f);
                assert(std::abs(u[11][0]-midpoint->values[11][0]-*reference/8)<.001f);
            }
            previous=u[11][0];
        }
        assert(std::abs(previous-158.63f)<.001f); // Same settled position and final guest frame.
        m.words[anim+8]=m.String("out_menu_collect_anim");
        for(unsigned step=0;step<=3;++step) {
            const float remaining=float(step)/3;
            m.words[left+0xec]=std::bit_cast<U32>(-264*remaining);
            m.words[right+0xec]=std::bit_cast<U32>(64*remaining);h.ReadGearTransition(m);
            const float current=158.63f+h.gear_slide_x;
            if(step)assert(std::abs(previous-current-*reference/4)<.001f);
            previous=current;
        }
        assert(previous<0);
        masks_scale=std::numeric_limits<float>::quiet_NaN();h.ReadGearTransition(m);
        assert(!h.gear_slide_active&&h.gear_slide_x==0);
        x=saved[0];scale=saved[1];masks_x=saved[2];masks_scale=saved[3];
        background_pose.x=saved[4];background_pose.y=saved[5];background_pose.scale=saved[6];shadow_width=saved[7];
        background_enabled=saved_background;MM3D::GearMenu::Apply(options);
    }
}
void FileSelectPresentationChecks() {
    namespace F=MM3D::NativeFileSelect;Memory m;F::Hook h;
    h.panel=m.Layout("FileSelect");h.frame[F::Summary]={20,30,.8f};h.frame[F::Files]={100,40,.6f};h.frame[F::Actions]={250,30,.5f};
    auto widgets=m.Alloc(16);m.words[h.panel+0x154]=widgets;m.words[h.panel+0x158]=4;
    for(unsigned i=0;i<4;++i) {
        const char* names[]={"plate_x_n","plate_y_n","plate_z_n","start_g"};
        m.words[widgets+i*4]=m.Widget(names[i],i==3?320:14,0);
    }
    h.ReadPresentation(m);assert(h.presentation&&h.detail_progress==0);
    assert(h.Mapping(F::Summary).tx+400*.8f<=-32);
    assert(h.Mapping(F::Files).tx==100);
    auto player=m.Alloc(0x18),anim=m.Alloc(0x18),players=m.Alloc(4);
    m.words[players]=player;m.words[h.panel+0x14c]=players;m.words[h.panel+0x150]=1;m.words[player+0xc]=anim;
    m.words[anim+8]=m.String("in_menu_fileselect_ani");m.words[player+0x10]=std::bit_cast<U32>(3.f);
    // Native arrival can put stones outside the selection-only bounds or at
    // positions also used by a selected file. Neither should reveal the summary.
    for(float x:{334.f,174.f,14.f,-146.f,-306.f}) {
        for(unsigned i=0;i<3;++i)m.words[m.words[widgets+4*i]+0xec]=std::bit_cast<U32>(x);
        auto before=m.words;h.ReadPresentation(m);assert(m.words==before);
        assert(h.Mapping(F::Summary).tx+400*.8f<=-32);
        assert(h.Mapping(F::Files).tx==100);
    }
    // The first submitted entrance frame can already be at time 1. Preserve
    // the native stagger/endpoints while seeding all three plates offscreen.
    for(unsigned i=0;i<3;++i)m.words[m.words[widgets+4*i]+0xec]=std::bit_cast<U32>(334.f);
    F::entrance_ease=2;
    m.words[player+0x10]=std::bit_cast<U32>(1.f);h.ReadPresentation(m);
    assert(h.entrance_active&&h.entrance_pending&&h.presentation);
    for(float dx:h.stone_slide)assert(h.frame[F::Files].x+14*h.frame[F::Files].scale+dx>413.34f);
    const auto first=h.stone_slide;
    const auto group=m.Widget("null_plate_g");std::vector<U32> roots,plates;
    for(unsigned i=0;i<3;++i) {
        const auto root=m.words[widgets+4*i],plate=m.Layout("FilePlate",334,46+52*i);
        m.words[root+0x20]=plate;roots.push_back(root);plates.push_back(plate);
    }
    m.Children(group,roots);
    const auto native=m.words;std::vector<Hook::Write> writes;std::vector<U32> seen;
    assert(h.Stones(m,group,writes,seen));
    for(auto w:writes)m.Write(w.address,w.after);
    for(auto plate:plates)assert(std::bit_cast<float>(m.words[plate+8+0xec])>413.34f);
    for(auto it=writes.rbegin();it!=writes.rend();++it)m.Write(it->address,it->before);
    assert(m.words==native);
    const auto cursors=m.Widget("cursor_g");std::vector<U32> cursor_nodes;
    for(const char* name:{"cursor_option_g","cursor_copy_del_g","cursor_start_g","cursor_plate_g"})
        cursor_nodes.push_back(m.Widget(name));
    m.Children(cursors,cursor_nodes);writes.clear();seen.clear();
    assert(h.Cursors(m,cursors,writes,seen));
    const U32 cursor_flags=cursor_nodes[3]+0xd4;
    assert(std::any_of(writes.begin(),writes.end(),[&](const auto& w){
        return w.address==cursor_flags && !(w.after&1u);
    }));
    h.ReadPresentation(m);assert(h.stone_slide==first); // Repeated draw doesn't advance time.
    MM3D::FileSelectInterpolation::Publish({100,4,h.panel,{0,0,0},true,first});
    m.words[player+0x10]=std::bit_cast<U32>(2.f);h.ReadPresentation(m);
    assert(h.stone_slide[0]<first[0]&&h.stone_slide[1]==first[1]&&h.stone_slide[2]==first[2]);
    MM3D::FileSelectInterpolation::Publish({101,4,h.panel,{0,0,0},true,h.stone_slide});
    const auto motion=MM3D::FileSelectInterpolation::Motion(101,4,h.panel);
    const float dx=h.stone_slide[0]-first[0];
    assert(std::find(motion.begin(),motion.end(),dx)!=motion.end());
    float previous=h.stone_slide[0],previous_step=first[0]-previous;
    for(float f:{3.f,4.f,5.f,6.f}) {
        m.words[player+0x10]=std::bit_cast<U32>(f);h.ReadPresentation(m);
        const float step=previous-h.stone_slide[0];
        assert(step>=0&&step<previous_step);previous=h.stone_slide[0];previous_step=step;
    }
    assert(h.stone_slide[0]==0&&h.stone_slide[1]>0&&h.stone_slide[2]>0&&h.entrance_pending);
    m.words[player+0x10]=std::bit_cast<U32>(8.f);h.ReadPresentation(m);
    assert(h.stone_slide[0]==0&&h.stone_slide[1]==0&&h.stone_slide[2]>0&&h.entrance_pending);
    m.words[player+0x10]=std::bit_cast<U32>(10.f);h.ReadPresentation(m);
    assert((h.stone_slide==std::array<float,3>{}&&!h.entrance_pending));
    writes.clear();seen.clear();assert(h.Cursors(m,cursors,writes,seen));
    assert(std::none_of(writes.begin(),writes.end(),[&](const auto& w){
        return w.address==cursor_flags && !(w.after&1u);
    }));
    for(float ease:{1.f,3.f}) {
        F::entrance_ease=ease;m.words[player+0x10]=std::bit_cast<U32>(1.f);h.ReadPresentation(m);
        m.words[player+0x10]=std::bit_cast<U32>(3.5f);h.ReadPresentation(m);
        const float expected=first[0]*std::pow(.5f,ease);
        assert(std::abs(h.stone_slide[0]-expected)<.001f);
    }
    F::entrance_ease=2;
    // Gate both digital navigation/selection and analog navigation, and never
    // keep input blocked after completion, a missing layout, or stale state.
    namespace Input=MM3D::FileSelectInput;
    U32 buttons=0xffffffff;short ax=100,ay=-100;
    Input::Observe(true,1000);Input::Filter(buttons,ax,ay,1100);
    assert(!buttons&&!ax&&!ay&&Input::Blocked(1100));
    Input::Observe(false,1101);buttons=17;ax=50;ay=25;Input::Filter(buttons,ax,ay,1102);
    assert(buttons==17&&ax==50&&ay==25&&!Input::Blocked(1102));
    Input::Observe(true,1200);assert(!Input::Blocked(1450));Input::Observe(false);
    for(unsigned i=0;i<3;++i)m.words[m.words[widgets+4*i]+0xec]=std::bit_cast<U32>(14.f);
    for(unsigned slot=0;slot<3;++slot)for(bool entering:{true,false}) {
        m.words[anim+8]=m.String((std::string(entering?"in_f":"out_f")+std::to_string(slot+1)+"_to_start").c_str());
        float previous=entering?0:1;
        for(float f:{0.f,1.5f,3.f,4.5f,6.f}) {
            m.words[player+0x10]=std::bit_cast<U32>(f*(entering&&slot==0?7.f/6.f:1.f));auto before=m.words;h.ReadPresentation(m);assert(m.words==before);
            assert(entering?h.detail_progress>=previous:h.detail_progress<=previous);previous=h.detail_progress;
            if(f==3)assert(h.detail_progress==(entering?.875f:.125f));
        }
        if(entering) {
            assert(h.Mapping(F::Summary).tx==20&&h.actions_slide==0);
            assert(h.Mapping(F::Files).tx+320*.6f<=-32);
        } else assert(h.Mapping(F::Files).tx==100&&h.detail_progress==0);
    }
}
void MaskFlightChecks() {
    Memory m;Hook h;h.panel=m.Layout("MenuMask");h.inventory_owner=m.Alloc(0x180);
    h.frame_x=17;h.frame_y=23;h.frame_scale=.6f;h.row_x=40;h.row_y=12;h.row_scale=.8f;h.row_spacing=55;
    auto icon=m.Layout("ItemIcon");
    const auto put=[&](U32 at,float v){m.words[at]=std::bit_cast<U32>(v);};
    for(unsigned slot=0;slot<4;++slot)for(bool reverse:{false,true}) {
        U32 grid=5|(2<<8),bar=6|(slot<<8);m.words[h.inventory_owner+0x18]=reverse?(bar|(grid<<16)):(grid|(bar<<16));
        for(unsigned i=0;i<2;++i){bool equipped=(i==1)!=reverse;put(h.inventory_owner+0x84+i*12,equipped?292:260);put(h.inventory_owner+0x88+i*12,equipped?28+slot*55:130);}
        for(float t:{0.f,.5f,1.f}) {
            float sx=reverse?292:260,sy=reverse?28+slot*55:130,ex=reverse?260:292,ey=reverse?130:28+slot*55;
            float px=sx+(ex-sx)*t,py=sy+(ey-sy)*t;put(icon+0x44,px);put(icon+0x48,py);put(icon+8+0xec,px);put(icon+8+0xfc,py);
            auto original=m.words;std::vector<Hook::Write> writes;assert(h.Flight(m,icon,writes));for(auto w:writes)m.Write(w.address,w.after);
            float q=reverse?1-t:t;
            float gx=17+.6f*260,gy=23+.6f*130,bx=40+.8f*(32+slot*55),by=12+.8f*32;
            assert(std::abs(std::bit_cast<float>(m.words[icon+8+0xec])-(gx+(bx-gx)*q))<.001f);
            assert(std::abs(std::bit_cast<float>(m.words[icon+8+0xfc])-(gy+(by-gy)*q))<.001f);
            for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);assert(m.words==original);
        }
    }
}
void MinigameHudChecks() {
    namespace H=MM3D::MinigameHud;
    Memory m;H::Hook h;
    auto hud=m.Layout("Hud"),all=m.Widget("all"),group=m.Widget("minigame_g",340,55);
    auto log=m.Widget("log_pane",345,60),hoop=m.Widget("float",345,60),num=m.Widget("num_a",360,60);
    auto timer=m.Widget("game_timer_g",200,220),button=m.Widget("a_action_button",350,30);
    m.Children(hud+8,{all});m.Children(all,{group,timer,button});m.Children(group,{log,hoop,num});
    // Digits use embedded number layouts in the installed HUD.
    auto number=m.Layout("Number"),glyph=m.Widget("glyph",360,60);
    m.Children(number+8,{glyph});m.words[num+0x20]=number;
    auto before=m.words;H::Apply({-30,20,.5f});h.Begin(m,hud);
    auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
    assert(f(group+0xec)==310&&f(group+0xfc)==75);
    assert(f(log+0xec)==312.5f&&f(hoop+0xec)==312.5f&&f(num+0xec)==320);
    assert(f(glyph+0xec)==320&&f(glyph+0xe0)==.5f);
    assert(f(log+0xe0)==.5f&&f(log+0xfc)==77.5f);
    assert(m.words[timer+0xec]==before[timer+0xec]&&m.words[button+0xe0]==before[button+0xe0]);
    auto transformed=m.words;h.Begin(m,hud);assert(m.words==transformed);h.End(m);assert(m.words==transformed);
    h.End(m);assert(m.words==before);
    H::Apply({0,0,0});h.Begin(m,hud);assert(m.words==before);h.End(m);
    H::Apply({});h.Begin(m,hud);h.End(m);assert(m.words==before);
}
void MagicBorderChecks() {
    namespace B=MM3D::MagicBorder;Memory m;B::Hook h;
    B::Options options;options.thickness=2;options.color={1,.5f,.25f};B::Apply(options);
    auto hud=m.Layout("Hud2"),all=m.Widget("all"),group=m.Widget("magic_gauge_g");
    m.Children(hud+8,{all});m.Children(all,{group});
    std::vector<U32> panes;
    for(auto name:{"gauge_base00","gauge_base01_pane","gauge_base02_pane"}) {
        auto node=m.Widget(name,10,20),geo=m.Alloc(0x7c);panes.push_back(node);
        m.words[node+0x28]=geo;m.words[geo+4]=6;m.words[geo+0xc]=0x10101;
        m.words[geo+0x20]=std::bit_cast<U32>(panes.size()==2?80.f:2.f);
        m.words[geo+0x24]=std::bit_cast<U32>(9.f);
        m.words[geo+0x34]=std::bit_cast<U32>(9.f/256);
        m.words[node+0xf4]=std::bit_cast<U32>(2.f);
        m.words[node+0x134]=std::bit_cast<U32>(.6f);
    }
    auto fill=m.Widget("gauge_pane");auto children=panes;children.push_back(fill);m.Children(group,children);
    for(auto node:{hud+8,all,group})m.words[node+0x134]=std::bit_cast<U32>(1.f);
    for(auto [at,value]:std::array<std::pair<U32,U32>,4>{{{0x6561ac,0x0e3f0eef},{0x6561b0,0x804f00c8},{0x6561b4,0},{0x6561b8,0x10000}}})m.words[at]=value;
    auto sp=(m.Alloc(0x6000)+0x6000)&~7u;auto before=m.words;
    const U32 root=h.Root(m,hud,sp);assert(root!=hud+8);
    auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
    assert(m.words[root+0x18]==13&&m.words[root+0x150]==hud+8);
    for(unsigned i=0;i<4;++i) {
        U32 node=root+0x200+i*0x200,geo=node+0x140;
        assert((m.words[geo+0xc]&0x03000000)==0x01000000);
        assert(f(node+0x128)==1&&f(node+0x12c)==.5f&&f(node+0x134)==.6f);
        if(i==0||i==3)assert(f(geo+0x20)==8&&f(geo+0x24)==18);
        else assert(f(geo+0x20)==80&&f(geo+0x24)==4);
    }
    for(unsigned layer=0;layer<4;++layer)for(unsigned side=0;side<2;++side) {
        U32 node=root+0xa00+(layer*2+side)*0x200,geo=node+0x140;
        assert(f(node+0x128)==0&&f(node+0x12c)==0&&f(node+0x130)==0);
        // Shadows stay outside the translucent meter instead of covering its fill.
        assert(side ? f(node+0xec)>=18 : f(node+0xfc)>=38);
        assert(f(geo+0x20)>0&&f(geo+0x24)>0);
        assert(std::abs(f(node+0x134)-.6f*options.shadow_opacity*.4f*(1-layer/5.f))<.001f);
    }
    // Only original frame opacity is adjusted outside scratch; fill is unchanged.
    for(auto [at,value]:before)if(at<sp-0x2400||at>=sp) {
        bool frame_alpha=false;for(auto pane:panes)frame_alpha|=at==pane+0x134;
        if(frame_alpha)assert(std::abs(f(at)-.42f)<.001f);
        else assert(m.words[at]==value);
    }
    assert(h.End(m)==sp&&m.words==before);
    // Re-read animated width and fade every draw; shadow works without the border.
    options.enabled=false;B::Apply(options);
    auto middle=m.words[panes[1]+0x28];
    m.words[middle+0x20]=std::bit_cast<U32>(160.f);
    for(auto pane:panes)m.words[pane+0x134]=std::bit_cast<U32>(.3f);
    auto animated=m.words;auto extended=h.Root(m,hud,sp);
    assert(m.words[extended+0x18]==9&&m.words[extended+0x140]==hud+8);
    assert(f(extended+0xa00+0xfc)>=38.f);
    assert(std::abs(f(extended+0xa00+0x134)-.3f*options.shadow_opacity*.4f)<.001f);
    h.End(m);assert(m.words==animated);
    options.shadow_enabled=false;B::Apply(options);
    const auto transparent_only=h.Root(m,hud,sp);
    assert(m.words[transparent_only+0x18]==1&&m.words[transparent_only+0x140]==hud+8);
    h.End(m);assert(m.words==animated);
    options.enabled=true;B::Apply(options);
    auto border_only=h.Root(m,hud,sp);assert(m.words[border_only+0x18]==5);h.End(m);
    options.shadow_enabled=true;B::Apply(options);m.words=before;
    for(auto pane:panes)m.words[pane+0x134]=0;
    auto invisible=m.words;assert(h.Root(m,hud,sp)==hud+8);h.End(m);assert(m.words==invisible);
    m.words=before;
    m.words[group+0xd4]&=~1u;auto hidden=m.words;
    assert(h.Root(m,hud,sp)==hud+8);h.End(m);assert(m.words==hidden);
    m.words=before;m.words[0x6561ac]=0;
    assert(h.Root(m,hud,sp)==hud+8);h.End(m);
    B::Apply(B::Options{});
}
void SongEffectChecks() {
    namespace E=MM3D::SongEffect;Memory m;
    for(auto l:E::Loads){m.words[l.pc]=l.word;m.words[l.pc+4]=l.next;m.words[l.literal]=0xe59501fc;}
    assert(E::CodeMatches(m));
    for(unsigned i=0;i<E::Loads.size();++i){auto l=E::Loads[i];assert(E::Instruction(l.pc,l.word)==(0xef000000|(E::FirstSVC+i)));assert(E::Instruction(l.pc,l.word^1)==(l.word^1));}
    m.words[E::Loads[0].literal]=0;assert(!E::CodeMatches(m));
    assert(E::Scale(45,false,1)==0x3dcccccd);
    assert(std::abs(std::bit_cast<float>(E::Scale(45,true,1))-.1f*16/15)<.000001f);
    const float corrected=std::bit_cast<float>(E::Scale(65,true,1));
    assert(corrected>.16f&&corrected<.17f);
    assert(std::abs(std::bit_cast<float>(E::Scale(65,true,1.5f))-corrected*1.5f)<.000001f);
    assert(E::Scale(90,true,1)==0x3dcccccd);
    // The zoom UI supports70: coverage must not revert to native size above65.
    float previous=corrected;
    for(float fov:{66.f,67.f,68.f,69.f,70.f}) {
        const float scale=std::bit_cast<float>(E::Scale(fov,true,1));
        assert(scale>previous);previous=scale;
    }
    assert(E::Scale(70.01f,true,1)==0x3dcccccd);
    auto matrix=m.Alloc(48);for(unsigned i=0;i<12;++i)m.words[matrix+4*i]=std::bit_cast<U32>(float(i+1));
    auto before=m.words;assert(E::Expand(m,matrix,65,true,1));
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
        auto at=matrix+16*row+4*col;
        if(col>=2)assert(m.words[at]==before[at]); // depth and camera distance unchanged
        else assert(std::abs(std::bit_cast<float>(m.words[at])-std::bit_cast<float>(before[at])*corrected/.1f)<.00001f);
    }
    m.words=before;assert(E::Expand(m,matrix,45,false,1)&&m.words==before);
    assert(E::Expand(m,matrix,70,true,1));
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col) {
        const auto at=matrix+16*row+4*col;
        if(col>=2)assert(m.words[at]==before[at]);
        else assert(std::abs(std::bit_cast<float>(m.words[at])-
            std::bit_cast<float>(before[at])*previous/.1f)<.00001f);
    }
    m.words.erase(matrix+16);auto invalid=m.words;
    assert(!E::Expand(m,matrix,65,true,1)&&m.words==invalid);

}
void SongListChecks() {
    namespace S=MM3D::SongList;Memory m;S::Hook h;S::Apply(S::Options{});
    {
        MM3D::OcarinaInput::BackButton back;
        assert(back.Filter(2,false)==2); // Instrument view: normal Cancel.
        assert(back.Filter(2,true)==2); // A held Cancel cannot become a new Back.
        assert(back.Filter(0,true)==0);
        assert(back.Filter(2,true)==8); // List: native Songs toggle.
        assert(back.Filter(2,true)==8);
        assert(back.Filter(2,false)==0); // Closing must not also Cancel.
        assert(back.Filter(0,false)==0);
        assert(back.Filter(2,false)==2); // Fresh press exits instrument.
    }

    {
        Memory q;MM3D::SongPanel::Hook panel;
        auto layout=q.Layout("MenuMusicList"),all=q.Widget("all"),staff=q.Widget("four_line_staff_g"),
             edge=q.Widget("four_line_staff_pane02"),geo=q.Alloc(0x7c);
        q.Children(layout+8,{all});q.Children(all,{staff});q.Children(staff,{edge});
        q.words[edge+0x28]=geo;q.words[geo+4]=6;q.words[geo+0x10]=0x123456;
        q.words[geo+0x20]=std::bit_cast<U32>(44.f);q.words[geo+0x24]=std::bit_cast<U32>(86.f);
        for(auto [off,v]:{std::pair{0x28u,68.f/256},std::pair{0x2cu,0.f},std::pair{0x30u,44.f/256},std::pair{0x34u,86.f/256}})
            q.words[geo+off]=std::bit_cast<U32>(v);
        const auto sp=(q.Alloc(0x5000)+0x5000)&~7u;const auto before=q.words;
        const auto root=panel.Root(q,layout,layout+8,sp,10,0,100,240,.8f);
        assert(root!=layout+8&&q.words[root+0x18]==4&&q.words[root+0x140]==layout+8);
        float area=0;
        for(unsigned i=0;i<3;++i) {
            const auto n=q.words[root+0x144+i*4],g=q.words[n+0x28];
            auto f=[&](U32 at){return std::bit_cast<float>(q.words[at]);};
            assert(q.words[g+0x10]==0x123456&&f(n+0x134)==.8f);
            const float u=f(g+0x28),end=u+f(g+0x30);
            assert(std::min(u,end)>=100.f/256&&std::max(u,end)<=112.f/256);
            assert(f(g+0x14)>=10&&f(g+0x18)==0);
            assert(f(g+0x14)+f(g+0x20)<=110&&f(g+0x24)==240);
            assert(f(g+0x2c)==16.f/256&&f(g+0x34)==1.f/256);
            area+=f(g+0x20)*f(g+0x24);
        }
        assert(area==24000);assert(panel.scratch.End(q)==sp&&q.words==before);
        assert(panel.Root(q,layout,layout+8,sp,10,0,100,240,0)==layout+8);
        panel.scratch.End(q);assert(q.words==before);
        {
            const auto before=q.words;
            const auto root=panel.Root(q,layout,layout+8,sp,10,0,100,240,.8f,.5f);
            assert(q.words[root+0x18]==5);
            const auto backing=q.words[root+0x150],g=q.words[backing+0x28];
            assert(q.words[g+0xc]==0x100&&q.words[g+0x10]==0);
            assert(q.words[backing+0x128]==0&&q.words[backing+0x134]==std::bit_cast<U32>(.4f));
            assert(q.words[g+0x20]==std::bit_cast<U32>(100.f)&&q.words[g+0x24]==std::bit_cast<U32>(240.f));
            panel.scratch.End(q);assert(q.words==before);
        }
        for(float strength:{0.f,.5f,1.f})for(float dark:{0.f,.5f}) {
            const auto root=panel.Root(q,layout,layout+8,sp,10,6,100,228,.8f,dark,{.2f,.4f,.6f},strength);
            assert(q.words[root+0x18]==4u+unsigned(dark>0)+unsigned(strength>0));
            assert(q.words[root+0x140]==layout+8); // songs remain above overlay
            if(strength>0) {
                const auto overlay=q.words[root+0x144],g=q.words[overlay+0x28];
                assert(q.words[g+0xc]==0x100&&q.words[g+0x10]==0);
                for(unsigned c=0;c<3;++c)
                    assert(q.words[overlay+0x128+c*4]==std::bit_cast<U32>(std::array<float,3>{.2f,.4f,.6f}[c]));
                assert(q.words[overlay+0x134]==std::bit_cast<U32>(.8f*strength));
                assert(q.words[g+0x14]==std::bit_cast<U32>(13.f));
                assert(q.words[g+0x20]==std::bit_cast<U32>(94.f));
                assert(q.words[g+0x18]==std::bit_cast<U32>(6.f));
                assert(q.words[g+0x24]==std::bit_cast<U32>(228.f));
                const auto texture=q.words[root+0x148];
                assert(q.words[q.words[texture+0x28]+0x10]==0x123456);
            }
            panel.scratch.End(q);assert(q.words==before);
        }
        assert(panel.Root(q,layout,layout+8,sp,10,0,100,240,.8f,0,{1,1,1},NAN)==layout+8);
        panel.scratch.End(q);assert(q.words==before);
        auto music=q.Widget("btn_music_g");q.Children(all,{staff,music});
        q.words[music+0x134]=std::bit_cast<U32>(1.f);
        S::Hook fitted;fitted.panel=layout;fitted.pass=3;fitted.column={20,30,.5f};
        const auto old_left=S::panel_left.load(),old_right=S::panel_right.load(),old_banner=S::banner_x.load();
        S::panel_left=-10.f;S::panel_right=130.f;
        for(float bars:{0.f,6.f,12.f}) {
            fitted.column.x=bars*10;S::banner_x=-bars*20;
            fitted.bar_height=bars;const auto before=q.words;
            const auto root=fitted.PanelRoot(q,layout,layout+8,sp);
            assert(root!=layout+8);
            for(unsigned i=0;i<3;++i) {
                const auto g=q.words[q.words[root+0x144+4*i]+0x28];
                assert(std::bit_cast<float>(q.words[g+0x18])==bars);
                assert(std::bit_cast<float>(q.words[g+0x24])==240-2*bars);
            }
            const auto left=q.words[q.words[root+0x144]+0x28];
            const auto right=q.words[q.words[root+0x14c]+0x28];
            assert(std::bit_cast<float>(q.words[left+0x14])==-10.f);
            assert(std::bit_cast<float>(q.words[right+0x14])+std::bit_cast<float>(q.words[right+0x20])==130.f);
            fitted.backdrop.scratch.End(q);assert(q.words==before);
        }
        S::panel_left=old_left;S::panel_right=old_right;S::banner_x=old_banner;
        q.words[geo+0x20]=0;const auto invalid=q.words;
        assert(panel.Root(q,layout,layout+8,sp,10,0,100,240,1)==layout+8);
        panel.scratch.End(q);assert(q.words==invalid);
    }
    {
        Memory q;S::Hook tint;tint.column={20,30,.5f};
        const auto stone=q.Widget("btn_song_of_time_pane"),geo=q.Alloc(0x7c);
        q.words[stone+0x28]=geo;
        for(unsigned c=0;c<4;++c)q.words[stone+0x128+c*4]=std::bit_cast<U32>(.8f);
        const auto before=q.words;
        S::stone_darkness=.5f;std::vector<MM3D::NativeInventory::Hook::Write> writes;
        assert(tint.Move(q,stone,0,writes,true));
        for(auto w:writes)q.Write(w.address,w.after);
        for(unsigned c=0;c<3;++c)assert(q.words[stone+0x128+c*4]==std::bit_cast<U32>(.4f));
        assert(q.words[stone+0x134]==std::bit_cast<U32>(.8f));
        for(auto it=writes.rbegin();it!=writes.rend();++it)q.Write(it->address,it->before);
        assert(q.words==before);S::stone_darkness=0;
    }
    {
        Memory q;S::Hook capture;
        const auto manager=q.Alloc(0x30),bottom=q.Alloc(0x848),top=q.Alloc(0x848),
            bgfrom=q.Alloc(0x848),bgto=q.Alloc(0x848),ocarina=q.Layout("MenuOcarina");
        q.words[manager+28]=bottom;q.words[manager+20]=top;
        q.words[manager+24]=bgfrom;q.words[manager+12]=bgto;
        q.Queue(bottom,{ocarina});q.Queue(top,{});q.Queue(bgfrom,{});q.Queue(bgto,{});
        capture.Pass(q,manager,4);assert(capture.header.source_layout==ocarina);
        q.Queue(bottom,{});capture.Pass(q,manager,4);
        assert(capture.header.source_layout==ocarina); // Retain across the song-list transition.
    }

    {
        Memory q;S::Hook shadow;shadow.panel=q.Layout("MenuMusicList");shadow.pass=3;
        shadow.column={20,30,.5f};shadow.gap=22;shadow.slide=-8;
        auto all=q.Widget("all"),music=q.Widget("btn_music_g"),pane=q.Widget("btn_song_of_time_pane"),geo=q.Alloc(0x7c);
        q.Children(shadow.panel+8,{all});q.Children(all,{music});q.Children(music,{pane});
        q.words[music+0x134]=std::bit_cast<U32>(.8f);q.words[pane+0x28]=geo;q.words[geo+4]=6;
        q.words[geo+0x14]=std::bit_cast<U32>(9.f);q.words[geo+0x18]=std::bit_cast<U32>(94.f);
        std::vector<U32> buttons{pane};
        for(auto name:S::Names) {
            auto button=q.Widget(name);q.words[button+0x134]=std::bit_cast<U32>(.8f);buttons.push_back(button);
        }
        q.Children(music,buttons);
        const auto sp=(q.Alloc(0xc000)+0xc000)&~7u;auto before=q.words;
        auto root=shadow.ShadowRoot(q,shadow.panel,sp);assert(root!=shadow.panel+8);
        assert(q.words[root+0x18]==40&&q.words[root+0x140]==shadow.panel+8);
        for(unsigned layer=0;layer<3;++layer)for(unsigned slot=0;slot<13;++slot) {
            const U32 n=root+0x200+(layer*13+slot)*0x200;
            auto f=[&](U32 at){return std::bit_cast<float>(q.words[at]);};
            assert(f(n+0x128)==0&&std::abs(f(n+0x134)-.8f*(.32f-.09f*layer))<.0001f);
            assert(f(n+0xec)==12+(S::Column(slot)*62+2.f-9)*.5f);
            assert(f(n+0xfc)==30+22*S::Row(slot)+(4.f+2*layer-94)*.5f);
        }
        assert(shadow.shadow_scratch.End(q)==sp&&q.words==before);
        // Only Time/Healing visible: unknown note slots have no shadow.
        for(unsigned i=3;i<buttons.size();++i)q.words[buttons[i]+0xd4]&=~1u;
        before=q.words;root=shadow.ShadowRoot(q,shadow.panel,sp);
        assert(root!=shadow.panel+8&&q.words[root+0x18]==7);
        for(unsigned i=1;i<7;++i) {
            auto n=q.words[root+0x140+4*i];
            assert(((n-root-0x200)/0x200)%13<2);
        }
        shadow.shadow_scratch.End(q);assert(q.words==before);
        for(unsigned i=1;i<3;++i)q.words[buttons[i]+0x134]=0;
        before=q.words;assert(shadow.ShadowRoot(q,shadow.panel,sp)==shadow.panel+8);
        shadow.shadow_scratch.End(q);assert(q.words==before);
        shadow.pass=5;assert(shadow.ShadowRoot(q,shadow.panel,sp)==shadow.panel+8);
        shadow.shadow_scratch.End(q);assert(q.words==before);
    }

    {
        Memory t;S::Hook transition;transition.panel=t.Layout("MenuMusicList");
        transition.column={24,12,.35f};
        auto players=t.Alloc(4),player=t.Alloc(0x18),anim=t.Alloc(0x18);
        t.words[transition.panel+0x14c]=players;t.words[transition.panel+0x150]=1;
        t.words[players]=player;t.words[player+4]=t.String("main");t.words[player+0xc]=anim;
        for(bool entering:{true,false}) {
            t.words[anim+8]=t.String(entering?"in_menu_music_list_anim":"out_menu_music_list_anim");
            float full=0;
            for(float f:{0.f,2.f,4.f,6.f}) {
                t.words[player+0x10]=std::bit_cast<U32>(f);transition.ReadTransition(t);
                if(entering&&f==0)full=transition.slide;
                if(entering&&f==2)assert(std::abs(transition.slide-full*.5f)<.001f);
                if(entering&&f>=4)assert(transition.slide==0);
                if(!entering&&f==0)assert(transition.slide==0);
                if(!entering&&f>=4)assert(transition.slide< -64);
            }
        }
        t.words[anim+8]=t.String("switch_btn_inst_image_anim");transition.ReadTransition(t);assert(transition.slide==0);
    }

    auto panel=m.Layout("MenuMusicList"),all=m.Widget("all"),music=m.Widget("btn_music_g"),anchors=m.Widget("frame_set_pos_g");
    auto notes=m.Widget("notes"),staff=m.Widget("four_line_staff_g"),back=m.Widget("btn_quit_g"),instrument=m.Widget("btn_inst_g");
    m.Children(panel+8,{all});m.Children(all,{notes,anchors,staff,music,instrument,back});
    std::vector<U32> buttons,positions;
    for(unsigned i=0;i<14;++i){auto button=m.Widget(S::Names[i]),pane=m.Widget("pane");m.Children(button,{pane});buttons.push_back(button);}
    m.Children(music,buttons);
    for(unsigned i=0;i<13;++i){auto a=S::Anchor(i);positions.push_back(m.Widget("anchor",a[0],a[1]));}m.Children(anchors,positions);
    auto bg=m.Layout("CommonBottomBg");m.Brown(bg);
    auto bgall=h.Child(m,bg+8,"all");
    for(const char* name:{"common_bottom_bg_pane0","common_bottom_bg_pane1"}){
        U32 node=h.Child(m,bgall,name),geo=m.Alloc(0x7c);m.words[node+0x28]=geo;
        m.words[geo+4]=6;m.words[geo+0x24]=std::bit_cast<U32>(240.f);m.words[geo+0x34]=std::bit_cast<U32>(1.f);m.words[node+0x120]=std::bit_cast<U32>(1.f);
    }
    auto manager=m.Alloc(0x30),bottom=m.Alloc(0x848),top=m.Alloc(0x848),bgfrom=m.Alloc(0x848),bgto=m.Alloc(0x848);
    auto context=m.Alloc(0x170),game=m.Alloc(0xccb0),bar_state=m.Alloc(4);
    m.words[0x173c18]=0x6a0148;m.words[0x173af4]=0xe1d040d1;
    m.words[0x72f5c0]=context;m.words[context+0x16c]=game;m.words[game+0xccac]=1;
    m.words[0x6a0154]=bar_state;m.words[bar_state]=18u<<8;m.words[0x6a0148]=std::bit_cast<U32>(.75f);
    h.ReadBars(m);assert(h.bar_height==18&&h.bar_alpha==.75f);
    m.words[game+0xccac]=0;h.ReadBars(m);assert(h.bar_height==0);m.words[game+0xccac]=1;
    m.words[manager+28]=bottom;m.words[manager+20]=top;m.words[manager+24]=bgfrom;m.words[manager+12]=bgto;
    m.Queue(bottom,{panel});m.Queue(bgfrom,{bg});auto native_bar=m.Layout("native_bar");m.words[bgto+4]=native_bar;m.words[bgto+0x804]=1;m.words[bgto+0x844]=1;auto original=m.words;
    h.Pass(m,manager,4);assert(h.panel==panel&&h.background==bg&&m.words[top+0x844]==1&&m.words[bgto+0x844]==29);
    assert(m.words[bgto+4]==bg&&m.words[bgto+4+28*4]==native_bar);
    auto routed=m.words;h.Pass(m,manager,3);h.Begin(m,panel);
    for(unsigned i=0;i<14;++i){auto a=S::Anchor(S::Slot(i));float xx=std::bit_cast<float>(m.words[buttons[i]+0xec]);float yy=std::bit_cast<float>(m.words[buttons[i]+0xfc]);
        assert(std::abs(xx+.35f*(a[0]-13)-(24+h.slide+S::Column(S::Slot(i))*62*.35f))<.001f);
        assert(std::abs(yy+.35f*(a[1]-13)-(12+16*S::Row(S::Slot(i))))<.001f);
    }
    assert(!(m.words[back+0xd4]&1));h.End(m);assert(m.words==routed);
    h.Pass(m,manager,5);h.Begin(m,panel);assert(!(m.words[music+0xd4]&1)&&!(m.words[staff+0xd4]&1));assert(m.words[back+0xd4]&1);h.End(m);assert(m.words==routed);
    h.Pass(m,manager,1);
    for(unsigned i=0;i<28;++i){h.Begin(m,bg);
        U32 node=h.Child(m,bgall,"common_bottom_bg_pane1");
        float yy=std::bit_cast<float>(m.words[node+0xf4]),y=std::bit_cast<float>(m.words[node+0xfc]);
        if(i==24||i==25)assert(std::abs(yy-h.helper.frame_background.scale)<.0001f); // Full tiles, never clipped.
        if(i>=26){assert(std::abs(yy*240-18)<.0001f);assert(y==(i==26?0:222));assert(std::bit_cast<float>(m.words[node+0x134])==.75f);}
        h.End(m);assert(m.words==routed);
    }
    // Native animation retracts bars without changing the underlying background.
    for(unsigned height:{12u,3u,0u}){
        m.words[bar_state]=height<<8;h.ReadBars(m);assert(h.bar_height==height);
        std::vector<Hook::Write> writes;assert(h.Bar(m,false,writes));
        auto before=m.words;for(auto w:writes)m.Write(w.address,w.after);
        if(!height)assert(!(m.words[bg+8+0xd4]&1));
        for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);assert(m.words==before);
    }
    m.words=original;S::enabled=false;h.Pass(m,manager,4);assert(!h.panel&&m.words==original);S::enabled=true;
    for(bool full:{false,true})for(unsigned slot=0;slot<13;++slot){
        unsigned current=slot<6?slot:slot==6?(full?6:7):slot+1;
        auto up=S::Selection(current,0x40,full),down=S::Selection(current,0x80,full);
        assert(S::Slot(up)==(slot==12?10:slot<3?slot:slot-3));assert(S::Slot(down)==(slot>=9?12:slot+3));
        assert(S::Slot(S::Selection(current,0x20,full))==(slot==12||slot%3==0?slot:slot-1));
        assert(S::Slot(S::Selection(current,0x10,full))==(slot==12||slot%3==2?slot:slot+1));
    }
}
void DungeonMapShadowChecks() {
    // Without brown panels the original background pass owns the shadow.
    const bool sides=MM3D::DungeonMap::side_panels.exchange(false);
    const bool strip=MM3D::DungeonMap::top_strip.exchange(false);
    Memory m;MM3D::DungeonMap::Hook hook;
    auto map=m.Layout("BackGround"),group=m.Widget("backGround"),pane=m.Widget("map_base",40,12);
    auto brown=m.Layout("CommonBottomBg"),geometry=m.Alloc(0x7c);
    m.Children(map+8,{group});m.Children(group,{pane});m.words[pane+0x28]=geometry;
    m.words[geometry+4]=6;m.words[geometry+0x20]=std::bit_cast<U32>(320.f);
    m.words[geometry+0x24]=std::bit_cast<U32>(240.f);
    hook.valid=hook.dungeon=true;hook.pass=1;hook.backgrounds={brown,map};hook.routed={brown,map};
    const auto sp=(m.Alloc(0x8000)+0x8000)&~7u;
    for(float scale:{.5f,1.f,1.5f})for(float opacity:{.25f,1.f}) {
        m.words[pane+0xe0]=m.words[pane+0xf4]=std::bit_cast<U32>(scale);
        m.words[pane+0x134]=std::bit_cast<U32>(opacity);
        auto before=m.words;auto root=hook.ShadowRoot(m,map,sp);
        assert(root!=map+8&&m.words[root+0x18]==25);
        auto children=m.words[root+0x14];assert(m.words[children]==map+8);
        for(unsigned side=0;side<2;++side) {
        float previous=opacity;
        for(unsigned band=0;band<12;++band) {
            auto node=m.words[children+4+(side*12+band)*4],geo=m.words[node+0x28];
            const float x=std::bit_cast<float>(m.words[node+0xec]);
            const float width=16*scale/12;
            const float expected=side?40-(band+1)*width:40+320*scale+band*width;
            assert(std::abs(x-expected)<.001f);
            if(side)assert(x+width<=40.001f);else assert(x>=40+320*scale-.001f);
            auto opposite=m.words[children+4+(band+(1-side)*12)*4];
            assert(m.words[node+0x134]==m.words[opposite+0x134]);
            assert(m.words[node+0xfc]==m.words[pane+0xfc]&&m.words[node+0xf4]==m.words[pane+0xf4]);
            assert(m.words[geo+0x24]==m.words[geometry+0x24]);
            float alpha=std::bit_cast<float>(m.words[node+0x134]);assert(alpha>0&&alpha<previous);previous=alpha;
        }
        }
        assert(hook.shadow_scratch.End(m)==sp&&m.words==before);
        assert(hook.ShadowRoot(m,brown,sp)==brown+8);hook.shadow_scratch.End(m);assert(m.words==before);
    }
    auto before=m.words;hook.pass=5;
    assert(hook.ShadowRoot(m,map,sp)==map+8);hook.shadow_scratch.End(m);assert(m.words==before);
    hook.pass=1;m.words.erase(sp-0x3400);before=m.words;
    assert(hook.ShadowRoot(m,map,sp)==map+8);hook.shadow_scratch.End(m);assert(m.words==before);
    MM3D::DungeonMap::side_panels=sides;MM3D::DungeonMap::top_strip=strip;
}
void DungeonMapChecks() {
    namespace D=MM3D::DungeonMap;namespace Q=MM3D::NativeMinimap;
    Memory m;D::Hook h;D::Options o;D::Apply(o);
    const auto owner=m.Alloc(0x2100),panel=m.Layout("MenuMapDungeon",0,0,0x300),all=m.Widget("all");
    m.words[owner+0x18]=panel;m.Children(panel+8,{all});
    {
        // Overview movement is suppressed without clearing shared input or
        // modifying cursor coordinates, floor selection, or the A zoom toggle.
        assert(D::FirstSVC+D::PCs.size()<=MM3D::SongEffect::FirstSVC);
        assert(D::Instruction(D::PCs[7],D::Words[7])==0xefffffa7);
        assert(D::Instruction(D::PCs[7],D::Words[7]^1)==(D::Words[7]^1));
        for(U32 zoom:{0u,1u,2u,3u,4u}) {
            m.words[0x7786f8]=(zoom<<24)|0x011234;
            const auto before=m.words;
            assert(D::OverviewCursorNext(m,owner)==(zoom==3?D::OverviewCursorEnd:D::PCs[7]+4));
            assert(D::FilterA(m,owner,0xffffffff)==0xffffffff&&m.words==before);
        }
        m.words[0x7786f8]=0x03011234;
        for(U32 mode:{0x100u,0x10000u,0x200u}) {
            m.words[owner+0x78c]=mode;
            assert(D::OverviewCursorNext(m,owner)==D::PCs[7]+4);
        }
        m.words[owner+0x78c]=0;
        D::enabled=false;assert(D::OverviewCursorNext(m,owner)==D::PCs[7]+4);D::enabled=true;
        assert(D::OverviewCursorNext(m,0)==D::PCs[7]+4);
        m.words.erase(0x7786f8);
        assert(D::OverviewCursorNext(m,owner)==D::PCs[7]+4);
    }
    {
        // Only an accepted A toggle becomes a remembered preference. Native
        // exit restores gameplay mode 2; re-entry must still select mode 3.
        const auto game=m.Alloc(0x170),other_game=m.Alloc(0x170);
        for(auto g:{game,other_game}){m.words[g+0x13c]=0x0402;m.words[g+0x148]=0x44;}
        assert(h.EntryZoom(m,game)==1);
        h.RememberZoom(m,owner,game,3);const auto selected=m.words;
        assert(h.EntryZoom(m,game)==3&&m.words==selected);
        m.words[0x7786f8]=0x02011234; // native exit and other-tab state
        assert(h.EntryZoom(m,game)==3&&h.EntryZoom(m,game)==3);
        m.words[0x7786f8]=0x03011234; // temporary gameplay minimap overview
        assert(h.EntryZoom(m,game)==3);
        h.RememberZoom(m,owner,game,1);assert(h.EntryZoom(m,game)==1);
        h.RememberZoom(m,owner,game,3);assert(h.EntryZoom(m,game)==3);
        for(unsigned invalid:{0u,2u,4u})h.RememberZoom(m,owner,game,invalid);
        assert(h.EntryZoom(m,game)==3);
        for(U32 mode:{0x100u,0x10000u}) { // overworld and soaring cannot overwrite it
            m.words[owner+0x78c]=mode;h.RememberZoom(m,owner,game,1);
            assert(h.EntryZoom(m,game)==3);
        }
        m.words[owner+0x78c]=0;
        assert(h.EntryZoom(m,other_game)==1&&h.EntryZoom(m,0)==1);
        m.words[game+0x148]=0x45;assert(h.EntryZoom(m,game)==1); // unrelated scene
        m.words[game+0x148]=0x44;m.words[game+0x13c]=0x0302;
        assert(h.EntryZoom(m,game)==1);
        m.words[game+0x13c]=0x0402;D::enabled=false;assert(h.EntryZoom(m,game)==1);
        h.RememberZoom(m,owner,game,1);D::enabled=true;assert(h.EntryZoom(m,game)==3);
        h.zoom_choice.reset();
    }
    m.words[owner+0xc]=m.Layout("UpWorldMapBG");m.words[owner+0x1c]=m.Layout("UpWorldMapHud");
    std::vector<U32> groups;
    for(const char* n:{"icon_touch_direct","cursor_loupe_g","disp_no_map_g","disp_wing_g","item_set_base_g","slide_gauge_g","btn_place_name_g","btn_expansion_g","btn_reduction_g","btn_return_l","bb_map_area_g"})groups.push_back(m.Widget(n,80,60));
    m.Children(all,groups);
    auto bg=m.Layout("CommonBottomBg"),base=m.Layout("BackGround"),cursor=m.Layout("CursorItemCollect",80,60);
    m.words[owner+0x10]=bg;m.words[owner+0x14]=base;m.words[owner+0x2c]=cursor;
    auto manager=m.Alloc(0x30);std::array<U32,6> q;
    for(unsigned i=0;i<6;++i){q[i]=m.Alloc(0x848);m.words[manager+8+4*i]=q[i];}
    auto unrelated=m.Layout("Unrelated");m.Queue(q[4],{bg,base});m.Queue(q[5],{panel,cursor,unrelated});
    const auto initial=m.words;
    for(bool dungeon:{false,true}) {
        m.words=initial;m.words[owner+0x78c]=dungeon?0:0x100;
        for(unsigned i=0;i<D::Count;++i)o.poses[i]={float(10+i),float(-20+i),.5f+float(i)*.1f};D::Apply(o);
        h.Capture(m,owner);assert(h.valid&&h.dungeon==dungeon);
        const auto before_skip=m.words;assert(h.SkipWorld(m,owner)==dungeon&&m.words==before_skip);
        // A zoom is native in dungeons and remains disabled on the world map.
        assert(D::FilterA(m,owner,0x403)==(dungeon?0x403u:0x402u));
        h.Pass(m,manager,4);
        assert(m.words[q[4]+0x844]==0&&m.words[q[5]+0x844]==1);
        assert(m.words[q[1]+0x844]==unsigned(dungeon)*2&&m.words[q[3]+0x844]==unsigned(dungeon)*2);
        const auto routed=m.words;
        if(dungeon){
            h.Pass(m,manager,3);h.Begin(m,panel);
            constexpr std::array<unsigned,11> parts{D::Map,D::Loupe,D::Map,D::Map,D::Items,D::Floors,D::Nameplate,D::Zoom,D::Zoom,D::Return,D::Map};
            for(unsigned i=0;i<groups.size();++i){auto p=o.poses[parts[i]];
                assert(std::abs(std::bit_cast<float>(m.words[groups[i]+0xec])-(80*p.scale+p.x))<.001f);
                assert(std::abs(std::bit_cast<float>(m.words[groups[i]+0xfc])-(60*p.scale+p.y))<.001f);}
            h.End(m);assert(m.words==routed);
            h.Begin(m,cursor);assert(std::abs(std::bit_cast<float>(m.words[cursor+8+0xec])-(80*o.poses[D::Items].scale+o.poses[D::Items].x))<.001f);h.End(m);assert(m.words==routed);
        }
    }
    // The same standalone cursor selects either the item strip or floor tab.
    m.words=initial;h.Capture(m,owner);h.Pass(m,manager,4);h.Pass(m,manager,3);
    for(unsigned mode:{0u,2u,0u}){
        m.words[panel+0x2c0]=mode<<8;const auto before=m.words;
        h.Begin(m,cursor);const auto p=o.poses[mode==2?D::Floors:D::Items];
        assert(std::abs(std::bit_cast<float>(m.words[cursor+8+0xec])-(80*p.scale+p.x))<.001f);
        assert(std::abs(std::bit_cast<float>(m.words[cursor+8+0xfc])-(60*p.scale+p.y))<.001f);
        assert(std::abs(std::bit_cast<float>(m.words[cursor+8+0xe0])-p.scale)<.001f);
        h.End(m);assert(m.words==before);
    }
    // Preview and description ownership expires as soon as the active page changes.
    m.words=initial;auto controller=m.Alloc(0x14),context=m.Alloc(0x178),state=m.Alloc(0x200);
    m.words[controller+4]=context;m.words[controller+8]=owner;m.words[context+0x16c]=state;
    auto message=m.Layout("MessageWindow",0,0,0x450);
    m.words[message+0x444]=state;m.words[message+0x3f4]=0x17af;
    h.Capture(m,owner,controller);assert(h.IsDescription(m,message));
    for(U32 id:{0x174cu,0x1234u,0x17b3u}){m.words[message+0x3f4]=id;assert(!h.IsDescription(m,message));}
    m.words[message+0x3f4]=0x17af;m.words[message+0x444]=state+4;assert(!h.IsDescription(m,message));m.words[message+0x444]=state;
    o.poses[D::Model]={3,2,.5f};D::Apply(o);
    const auto matrix=m.Alloc(48);constexpr std::array<float,12> map_model{0,-2,0,0,2,0,0,1.25f,0,0,2,105.8f};
    for(unsigned i=0;i<12;++i)m.words[matrix+i*4]=std::bit_cast<U32>(map_model[i]);
    const auto original_model=m.words;
    h.ModelFrame(m,context);h.preview.ModelBegin(m,context,matrix);
    assert(std::bit_cast<float>(m.words[matrix+4])==-1&&std::bit_cast<float>(m.words[matrix+12])==3);
    assert(std::bit_cast<float>(m.words[matrix+28])==-.75f&&std::bit_cast<float>(m.words[matrix+44])==105.8f);
    h.preview.ModelEnd(m);assert(m.words==original_model);
    for(unsigned bits:{0x100u,0x10000u}){
        m.words[owner+0x78c]=bits;assert(!h.IsDescription(m,message));h.ModelFrame(m,context);assert(!h.preview.drawing);
    }
    m.words[owner+0x78c]=0;m.words[controller+8]=owner+4;
    assert(!h.IsDescription(m,message));h.ModelFrame(m,context);assert(!h.preview.drawing);
    // Special Soaring mode and disabled setting leave both queues and A alone.
    for(unsigned bits:{0x10000u,0x10100u}){
        m.words=initial;m.words[owner+0x78c]=bits;const auto before=m.words;
        h.Capture(m,owner);h.Pass(m,manager,4);assert(!h.valid&&m.words==before&&D::FilterA(m,owner,1)==1&&!h.SkipWorld(m,owner));
    }
    m.words=initial;D::enabled=false;h.Capture(m,owner);h.Pass(m,manager,4);assert(m.words==initial&&D::FilterA(m,owner,1)==1&&!h.SkipWorld(m,owner));D::enabled=true;
    // Bad pane trees fail without partially moving the background or foreground.
    m.words=initial;m.words[all+0x18]=10;h.Capture(m,owner);const auto bad=m.words;h.Pass(m,manager,4);assert(m.words==bad);
    // Native map batches include the floor artwork and radar/actor markers.
    m.words=initial;
    for(unsigned i=0;i<8;++i){m.words[Q::Count(i)]=0;for(unsigned j=0;j<4;++j){m.words[Q::Pointer(i,j)]=0;m.words[Q::Priority(i,j)]=0;}}
    auto batch=m.Alloc(0x3a8),pos=m.Alloc(12),size=m.Alloc(12);
    m.words[batch+0x2b8]=pos;m.words[batch+0x2c0]=size;m.words[batch+0x2c4]=1;m.words[batch+0x2c8]=1;
    m.words[pos]=std::bit_cast<U32>(120.f);m.words[pos+4]=std::bit_cast<U32>(100.f);
    m.words[size]=std::bit_cast<U32>(20.f);m.words[size+4]=std::bit_cast<U32>(30.f);
    m.words[0x6890ec]=batch;m.words[0x6890f0]=0;m.words[0x727970+0x828]=0;
    const auto native=m.words;
    for(bool dungeon:{false,true}){
        m.words=native;m.words[owner+0x78c]=dungeon?0:0x100;
        h.Capture(m,owner);h.BeforeMap(m);assert(h.armed);
        m.words[Q::Count(6)]=1;m.words[Q::Pointer(6,0)]=batch;m.words[Q::Priority(6,0)]=1;
        h.AfterMap(m);assert(m.words[Q::Count(6)]==0&&m.words[Q::Count(4)]==unsigned(dungeon));
        assert(std::bit_cast<float>(m.words[pos])==(dungeon?120*o.poses[D::Map].scale+o.poses[D::Map].x:120));
        assert(std::bit_cast<float>(m.words[size])==(dungeon?20*o.poses[D::Map].scale:20));
    }
    m.words=native;h.Capture(m,owner);h.BeforeMap(m);
    m.words[Q::Count(6)]=1;m.words[Q::Pointer(6,0)]=batch;m.words[Q::Priority(6,0)]=1;m.words[batch+0x2c8]=2;
    const auto invalid=m.words;h.AfterMap(m);assert(m.words==invalid);
    for(unsigned i=0;i<D::PCs.size();++i){auto inst=D::Instruction(D::PCs[i],D::Words[i]);assert((inst>>28)==(D::Words[i]>>28));assert((inst&0xffffff)==D::FirstSVC+i);}
    D::Apply(D::Options{});
}
void InventoryShadowChecks() {
    Memory m;MM3D::InventoryShadow::Hook shadow;Hook inventory;
    inventory.panel=m.Layout("MenuItem");inventory.pass=3;inventory.frame_scale=.5f;inventory.routed={inventory.panel};
    auto a=m.Widget("item_panel_pane00",10,100),b=m.Widget("item_panel_pane01",70,100);
    m.Children(inventory.panel+8,{a,b});
    for(auto node:{a,b}){auto geo=m.Alloc(0x7c);m.words[node+0x28]=geo;m.words[geo+4]=6;m.words[geo+0x20]=std::bit_cast<U32>(120.f);m.words[geo+0x24]=std::bit_cast<U32>(192.f);m.words[node+0xf4]=std::bit_cast<U32>(.5f);m.words[node+0x134]=std::bit_cast<U32>(.8f);}
    const auto sp=(m.Alloc(0x4000)+0x4000)&~7u;const auto original=m.words;
    enabled=true;auto root=shadow.Root(m,inventory,inventory.panel,sp);assert(root!=inventory.panel+8);
    assert(m.words[root+0x18]==7&&m.words[root+0x158]==inventory.panel+8);
    for(unsigned i=0;i<6;++i){auto n=root+0x200+i*0x200;
        assert(std::bit_cast<float>(m.words[n+0xec])==(i%2?70:10));
        assert(std::bit_cast<float>(m.words[n+0xfc])==196+i/2);
        assert(std::abs(std::bit_cast<float>(m.words[n+0x134])-.8f*(.32f-.09f*(i/2)))<.0001f);
        assert(m.words[n+0x128]==0);
        assert(std::bit_cast<float>(m.words[m.words[n+0x28]+0x24])==1.f);}
    assert(shadow.scratch.End(m)==sp&&m.words==original);
    inventory.pass=5;assert(shadow.Root(m,inventory,inventory.panel,sp)==inventory.panel+8);shadow.scratch.End(m);assert(m.words==original);
}
void ExpandedPanelShadowChecks() {
    namespace S=MM3D::InventoryShadow;namespace P=MM3D::NativeItemPresent;
    for(const char* name:{"MenuItem","MenuMask","MenuCollect","MenuItemPresen","MenuMaskPresen"})
    for(bool horizontal:{false,true})for(float scale:{.5f,1.f,1.5f})for(float fade:{0.f,.4f,1.f}) {
        Memory m;S::Hook shadow;Hook inventory;P::Hook present;
        auto panel=m.Layout(name);inventory.panel=present.panel=panel;inventory.pass=present.pass=3;
        inventory.routed=present.routed={panel};
        const bool gear=std::string_view(name)=="MenuCollect",giving_masks=std::string_view(name)=="MenuMaskPresen",
                   giving=giving_masks||std::string_view(name)=="MenuItemPresen",masks=std::string_view(name)=="MenuMask"||giving_masks;
        present.mask_menu=giving_masks;
        const unsigned count=giving?3:2;
        const char* names[3]={gear?"left_base_pane":masks?"mask_panel_pane00":"item_panel_pane00",
                             gear?"right_base_pane":masks?"mask_panel_pane01":"item_panel_pane01",giving_masks?"mask_panel_pane02":"right_base_pane"};
        std::vector<U32> panes;std::array<float,3> left{},bottom{},width{};
        const auto f=[&](U32 at,float v){m.words[at]=std::bit_cast<U32>(v);};
        const auto get=[&](U32 at){return std::bit_cast<float>(m.words.at(at));};
        for(unsigned i=0;i<count;++i) {
            const bool strip=gear?i==1:!giving_masks&&i==2;
            auto node=m.Widget(names[i],20.f+30*i,40.f+20*i),geo=m.Alloc(0x7c);panes.push_back(node);
            m.words[node+0x28]=geo;m.words[geo+4]=6;
            f(node+0x134,fade);f(node+0xe0,scale);f(node+0xf4,scale);
            f(geo+0x14,2);f(geo+0x18,3);f(geo+0x20,strip?56:120);f(geo+0x24,strip?200:96);
            left[i]=20+30*i+2*scale;bottom[i]=40+20*i+(strip?203:99)*scale;width[i]=(strip?56:120)*scale;
            if(strip&&horizontal) {
                f(node+0xe0,0);f(node+0xe4,scale);f(node+0xf0,-scale);f(node+0xf4,0);
                left[i]=20+30*i+3*scale;bottom[i]=40+20*i-2*scale;width[i]=200*scale;
            }
        }
        auto all=m.Widget("all");m.Children(panel+8,{all});m.Children(all,panes);
        const auto sp=(m.Alloc(0x19000)+0x19000)&~7u;const auto before=m.words;
        auto root=shadow.Root(m,inventory,panel,sp,&present);
        if(giving) {
            if(fade==0){assert(root==panel+8);shadow.scratch.End(m);assert(m.words==before);continue;}
            assert(root!=panel+8);
            const unsigned children=m.words[root+0x18];assert(children>24&&children<=97);
            if(giving_masks)assert(children==97); // One finely sampled halo for the whole panel.
            assert(m.words[root+0x140+4*(children-1)]==panel+8);
            const unsigned per_panel=(children-1)/(giving_masks?1:2);
            for(unsigned i=0;i<children-1;++i) {
                const auto n=m.words[root+0x140+4*i],g=m.words[n+0x28];
                const unsigned part=i<per_panel?0:2;
                const float l=giving_masks?std::min({left[0],left[1],left[2]}):part?left[2]:std::min(left[0],left[1]);
                const float r=giving_masks?std::max({left[0]+width[0],left[1]+width[1],left[2]+width[2]}):part?left[2]+width[2]:std::max(left[0]+width[0],left[1]+width[1]);
                const float actual_top=part? (horizontal?bottom[2]-56*scale:80+3*scale):40+3*scale;
                const float bottom_edge=giving_masks?std::max({bottom[0],bottom[1],bottom[2]}):part?bottom[2]:std::max(bottom[0],bottom[1]);
                const float x=get(n+0xec),y=get(n+0xfc),w=get(g+0x20),h=get(g+0x24);
                assert(w>0&&h>0&&get(n+0x134)>0&&get(n+0x134)<=fade*(giving_masks?.6f:.38f));
                assert(x+w<=l+.001f||x>=r-.001f||y+h<=actual_top+.001f||y>=bottom_edge-.001f);
            }
            if(giving_masks) {
                for(unsigned ring=0;ring<24;++ring) {
                    const auto child=[&](unsigned edge){return m.words[root+0x140+4*(ring*4+edge)];};
                    const auto l=child(0),r=child(1),t=child(2),b=child(3);
                    const auto lg=m.words[l+0x28],rg=m.words[r+0x28],tg=m.words[t+0x28],bg=m.words[b+0x28];
                    const float a=get(l+0x134);
                    assert(a==get(r+0x134)&&a==get(t+0x134)&&a==get(b+0x134));
                    if(ring)assert(a<get(m.words[root+0x140+4*((ring-1)*4)]+0x134));
                    assert(std::abs(get(t+0xfc)+get(tg+0x24)-get(l+0xfc))<.0001f);
                    assert(std::abs(get(l+0xfc)+get(lg+0x24)-get(b+0xfc))<.0001f);
                    assert(get(t+0xec)==get(l+0xec)&&get(b+0xec)==get(l+0xec));
                    assert(std::abs(get(tg+0x20)-(get(r+0xec)+get(rg+0x20)-get(l+0xec)))<.0001f);
                    assert(get(tg+0x20)==get(bg+0x20));
                }
            }
        } else {
        assert(root!=panel+8);
        assert(m.words[root+0x18]==3*count+1&&m.words[root+0x140+12*count]==panel+8);
        for(unsigned layer=0;layer<3;++layer)for(unsigned part=0;part<count;++part) {
            auto n=root+0x200+(layer*count+part)*0x200,g=m.words[n+0x28];
            const float edge=(!gear&&part<2)?std::max(bottom[0],bottom[1]):bottom[part];
            assert(std::abs(get(n+0xec)-left[part])<.001f);
            assert(std::abs(get(n+0xfc)-(edge+2*layer*scale))<.001f);
            assert(std::abs(get(g+0x20)-width[part])<.001f&&get(g+0x24)==2*scale);
            const float alpha=std::abs(bottom[part]-edge)<.01f?fade*(.32f-.09f*layer):0;
            assert(std::abs(get(n+0x134)-alpha)<.0001f);
            assert(get(n+0xe0)==1&&get(n+0xf4)==1&&get(n+0xe4)==0&&get(n+0xf0)==0);
        }
        }
        assert(shadow.scratch.End(m)==sp&&m.words==before);
        // Routing and material guards fail without touching guest memory.
        inventory.routed.clear();present.routed.clear();
        assert(shadow.Root(m,inventory,panel,sp,&present)==panel+8);shadow.scratch.End(m);assert(m.words==before);
        inventory.routed=present.routed={panel};m.words[m.words[panes[0]+0x28]+4]=5;
        const auto invalid=m.words;assert(shadow.Root(m,inventory,panel,sp,&present)==panel+8);
        shadow.scratch.End(m);assert(m.words==invalid);
    }
}
void NotebookMixedRowsChecks() {
    // Category headings consume the same vertical space as selectable events.
    for(unsigned total:{9u,10u,20u})for(int start:{-2,0,int(total)-7}) {
        Memory m;MM3D::NotebookRows::Hook hook;Hook helper;
        auto owner=m.Alloc(0x800),layout=m.Layout("UpQuestlist");
        m.words[owner+0x34]=layout;m.words[owner+0x70]=total<<24;
        constexpr std::array<unsigned,9> order{7,5,3,1,0,2,4,6,8};
        std::array<U32,9> anchors{};
        for(unsigned i=0;i<9;++i) {
            anchors[i]=m.Widget("slot",212,51+38.f*i);
            m.words[anchors[i]+0x134]=std::bit_cast<U32>(1.f);
            m.words[owner+0x138+4*order[i]]=anchors[i];
        }
        const int first=std::clamp(start,0,int(total)-9);
        // Visit both ends and reverse direction without rebuilding the window.
        for(int selected:{first+1,first+7,first+8,first+7,first+1}) {
            m.words[owner+0x254]=(unsigned(selected)<<8)|unsigned(start&255);
            hook.Enter(m,owner);hook.Leave(m);
            for(unsigned i=0;i<9;++i) {
                const int rank=start+hook.positions[i]-first;
                m.words[anchors[i]+0x20]=m.Layout(rank==0||rank==2||rank==5?"Category":"QuestPlate");
            }
            auto cursor=m.Layout("CursorQuestPlate",212,89);
            const auto before=m.words;
            std::vector<Hook::Write> writes;
            assert(hook.Expand(m,layout,helper,writes));
            for(auto w:writes)m.Write(w.address,w.after);
            unsigned visible=0;float selected_y=-1;
            for(unsigned i=0;i<9;++i)if(m.words[anchors[i]+0xd4]&1) {
                ++visible;const float y=std::bit_cast<float>(m.words[anchors[i]+0xfc]);
                assert(y>=51&&y<=317);
                if(start+hook.positions[i]==selected)selected_y=y;
            }
            assert(visible==8&&selected_y>=51&&selected_y<=317);
            for(unsigned i=0;i<9;++i)if(start+hook.positions[i]==first) {
                assert(bool(m.words[anchors[i]+0xd4]&1)==(selected!=first+8));
                if(selected==first+1)
                    assert(std::bit_cast<float>(m.words[anchors[i]+0xfc])==51.f);
            }
            for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);
            assert(m.words==before);writes.clear();
            assert(hook.Expand(m,cursor,helper,writes));
            for(auto w:writes)m.Write(w.address,w.after);
            assert(std::bit_cast<float>(m.words[cursor+8+0xfc])==selected_y);
            for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);
            assert(m.words==before);
        }
    }
}
void NotebookScrollbarChecks() {
    // Exercise the complete Events path, including row expansion, track
    // normalization and final screen pose. Testing Resize alone missed the
    // additional 320/296 stretch applied to Events' shorter native track.
    for(float list_scale:{.5f,.69f,1.f}) {
        Memory m;MM3D::NativeNotebook::Hook h;
        auto owner=m.Alloc(0x800),up=m.Layout("UpQuestlist"),all=m.Widget("all");
        auto attachment=m.Widget("scroll_bar_l"),bar=m.Layout("ScrollBar");
        m.Children(up+8,{all});m.words[attachment+0x20]=bar;
        m.words[owner+0x78]=bar;m.words[owner+0x70]=6u<<24;m.words[owner+0x254]=0;
        h.row_window.observed_owner=owner;h.row_window.layout=up;
        std::vector<U32> children{attachment};
        for(unsigned i=0;i<9;++i) {
            auto slot=m.Widget("slot",212,51+38.f*i);children.push_back(slot);
            h.row_window.anchors[i]=slot;
            if(i<6)m.words[slot+0x20]=m.Layout(i==0||i==2?"QuestCategoryPlate":"QuestPlate",212,51+38.f*i);
        }
        m.Children(all,children);
        auto upper=m.Widget("bb_pane00"),middle=m.Widget("bb_pane01"),lower=m.Widget("bb_pane02");
        auto top=m.Widget("bar00",388,41),strip=m.Widget("bar01",388,43),bottom=m.Widget("bar02",388,232);
        m.Children(bar+8,{upper,middle,lower,top,strip,bottom});
        const auto put=[&](U32 at,float value){m.words[at]=std::bit_cast<U32>(value);};
        for(auto pane:{upper,lower}) {
            auto geo=m.Alloc(0x40);m.words[pane+0x28]=geo;
            put(geo+0x14,384);put(geo+0x18,pane==upper?36:228);
            put(geo+0x20,16);put(geo+0x24,8);
        }
        put(strip+0xf4,186);
        h.active=true;h.pass=2;h.list={up};
        h.frame[0]={-47.6f,9.6f,list_scale};h.frame[3]={-7.8f,18.6f,.6f};h.frame[8]={179.6f,0,1};
        auto before=m.words;h.Begin(m,up);
        const auto value=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
        const float track_top=value(upper+0xfc)+36*value(upper+0xf4);
        const float track_bottom=value(lower+0xfc)+236*value(lower+0xf4);
        const float thumb_end=value(bottom+0xfc)-value(bottom+0xf4);
        assert(std::abs(track_top-40.2f)<.001f&&std::abs(track_bottom-232.2f)<.001f);
        assert(std::abs(value(top+0xfc)-43.2f)<.001f&&std::abs(thumb_end-229.2f)<.001f);
        assert(std::abs(value(top+0xf4)-.6f)<.001f&&std::abs(value(bottom+0xf4)-.6f)<.001f);
        h.End(m);assert(m.words==before);
    }
    for(float scale:{.6f,1.f})for(unsigned total:{3u,8u,9u,10u,20u,61u,67u})
        for(float progress:{0.f,.5f,47.f/53.f,48.f/53.f,49.f/53.f,1.f})
        for(float native_height:{1.f,40.f,186.f}) {
            Memory m;MM3D::NotebookRows::Hook hook;Hook helper;
            hook.observed_owner=m.Alloc(0x800);
            constexpr float base=17.f;
            auto bar=m.Layout("ScrollBar",0,base),track=m.Widget("bb_pane01",0,base+44*scale);
            auto lower=m.Widget("bb_pane02",0,base+228*scale),group=m.Widget("bar_g");
            auto top=m.Widget("bar00",388,base+41*scale),middle=m.Widget("bar01",388,base+43*scale);
            auto bottom=m.Widget("bar02",388,base+(46+native_height-1)*scale);
            auto strip=m.Widget("bar01_pane",388,base+43*scale);
            auto cap_pane=m.Widget("bar02_pane",388,base+(46+native_height-1)*scale);
            m.words[hook.observed_owner+0x78]=bar;
            m.Children(bar+8,{track,lower,group});m.Children(group,{top,middle,bottom});
            m.Children(middle,{strip});m.Children(bottom,{cap_pane});
            for(auto node:{bar+8,track,lower,top,middle,bottom})m.words[node+0xf4]=std::bit_cast<U32>(scale);
            m.words[middle+0xf4]=m.words[strip+0xf4]=std::bit_cast<U32>(scale*native_height);
            m.words[cap_pane+0xf4]=std::bit_cast<U32>(scale);
            const auto before=m.words;std::vector<Hook::Write> writes;
            const float coverage=float(std::min(8u,total))/float(total);
            assert(hook.Scrollbar(m,helper,writes,base+332*scale,progress,coverage));
            for(auto w:writes)m.Write(w.address,w.after);
            const auto y=[&](U32 p){return std::bit_cast<float>(m.words[p+0xfc]);};
            const float start=base+41*scale,end=base+327*scale;
            const float height=(end-start)*coverage;
            assert(std::abs(y(top)-(start+progress*((end-start)-height)))<.001f);
            // Native lower cap geometry spans local Y=-3..-1, not its anchor.
            const float cap_top=y(cap_pane)-3*scale,cap_bottom=y(cap_pane)-scale;
            assert(std::abs(cap_bottom-y(top)-height)<.001f);
            assert(y(top)>=start-.001f&&cap_bottom<=end+.001f);
            if(total<=8) {
                // Filled tracks have equal five-unit insets at both ends,
                // including the lower background cap's eight-unit height.
                assert(std::abs((base+332*scale-cap_bottom)-5*scale)<.001f);
                assert(std::abs(y(top)-(base+36*scale)-5*scale)<.001f);
            }
            // Include the captured 61-entry list's four/five/six steps from
            // the bottom. Inner joins overlap even at fractional positions;
            // the strip must stay clear of both rounded outside edges.
            assert(std::abs(y(strip)-y(top)-1.5f*scale)<.001f);
            const float strip_end=y(strip)+std::bit_cast<float>(m.words[strip+0xf4]);
            assert(std::abs(strip_end-cap_top-.5f*scale)<.001f);
            assert(y(strip)>y(top)&&strip_end<cap_bottom);
            // The real layout shader floors positions before projection.
            // Tiny floating-point errors at an exact join can lose a pixel.
            assert(std::floor(y(strip))<=std::floor(y(top)+2.f*scale));
            assert(std::floor(strip_end)>=std::floor(cap_top));
            assert(m.words[top+0xf4]==before.at(top+0xf4)&&m.words[bottom+0xf4]==before.at(bottom+0xf4));
            for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);
            assert(m.words==before);
        }
}
void CutsceneFovChecks() {
    namespace C=MM3D::CameraLive;Memory m;
    auto ctx=m.Alloc(0x178),game=m.Alloc(0xb00),projection=m.Alloc(0x100);
    m.words[0x72f5c0]=ctx;m.words[ctx+0x16c]=game;m.words[game+0x13c]=4<<8;
    m.words[C::CameraPointer]=projection;m.words[projection+8]=400;m.words[projection+16]=240;
    C::desired_fov=65;
    for(unsigned index=0;index<4;++index){
        auto camera=game+0x408+index*0x198;m.words[game+0xa78]=index<<16;m.words[camera+0xa0]=game;
        for(unsigned state:{1u,9u,10u,0x13u,0x11u}){
            m.words[camera+0x154]=state<<16;const bool scripted=state==9||state==10;
            assert(C::ConfiguredFov(m,projection)==(scripted?49.f:65.f));
            assert(C::ConfiguredFov(m,projection+4)==65);
        }
    }
    m.words[game+0xa78]=4<<16;assert(C::ConfiguredFov(m,projection)==65);
    C::desired_fov=45;
}
void CursorInterpolationChecks() {
    namespace L=MM3D::MessageLayout;namespace C=MM3D::CursorInterpolation;
    Memory m;auto layout=m.Layout("CursorItemCollect"),pane=m.Widget("frame_def_pane00");
    m.Children(layout+8,{pane});const auto geo=m.Alloc(0x80);m.words[pane+0x28]=geo;m.words[geo+4]=6;
    const auto put=[&](U32 at,float f){m.words[at]=std::bit_cast<U32>(f);};
    put(layout+8+0x134,1);put(pane+0x134,1);
    for(unsigned r=0;r<3;++r)put(pane+0xe0+r*16+r*4,1);
    put(pane+0xec,103.123f);put(pane+0xfc,50);put(geo+0x20,16);put(geo+0x24,16);
    put(geo+0x28,.25f);put(geo+0x2c,.5f);put(geo+0x30,.125f);put(geo+0x34,.125f);
    put(pane+0x110,1);put(pane+0x120,1);
    const auto before=m.words;const auto panels=L::ReadCursors(m,layout);
    assert(panels.size()==1&&m.words==before);
    C::Values u{};u[10]={0,3,90,1};u[14]={0,3,90,1};
    for(unsigned r=0;r<3;++r){u[11+r]=panels[0].transform[r];u[15+r]=u[11+r];}
    u[15][0]+=30; // Same atlas, different pane in the same batch.
    for(unsigned r=0;r<2;++r)u[90+r]=panels[0].uv[r];
    C::Publish(10,99,panels);assert(C::Mask(10,99,u,12)==1);
    assert(C::Mask(11,99,u,12)==1&&C::Mask(12,99,u,12)==0&&C::Mask(10,100,u,12)==0);
    auto rounded=u;for(auto& row:rounded)for(float& f:row)f=std::bit_cast<float>(std::bit_cast<U32>(f)&~127u);
    assert(C::Mask(10,99,rounded,12)==1);
    auto wrong=u;wrong[90][1]+=.125f;assert(C::Mask(10,99,wrong,12)==0);
    MM3D::InventoryInterpolation::Pose inv;inv.values=u;inv.rows.fill(true);inv.alpha=.75f;
    inv.values[11][0]-=10;inv.values[15][0]-=10;C::Snap(inv,u,1);
    assert(inv.values[11]==u[11]&&inv.values[15][0]==u[15][0]-10&&inv.alpha==.75f);
    MM3D::FileSelectInterpolation::Pose file;file.count=2;file.x={1,2};file.y={3,4};file.alpha=.5f;file.rectangles.emplace();
    C::Snap(file,u,1);assert(file.x[0]==u[11][0]&&file.x[1]==2&&file.y[0]==u[12][0]&&file.y[1]==4&&file.alpha==.5f);
    assert((*file.rectangles)[0]==u[11]&&(*file.rectangles)[1]==u[12]&&(*file.rectangles)[2][0]==0);
    MM3D::ItemSlideInterpolation::Pose item;item.count=2;item.x={1,2};C::Snap(item,u,1);
    assert(item.x[0]==u[11][0]&&item.x[1]==2);
    m.words[pane+0xd4]&=~1u;assert(L::ReadCursors(m,layout).empty());m.words=before;
    m.Children(pane,{pane});assert(L::ReadCursors(m,layout).empty());m.words=before;
    auto unrelated=m.Layout("MenuItem");m.Children(unrelated+8,{pane});assert(L::ReadCursors(m,unrelated).empty());
    auto menu=m.Layout("FileSelect"),group=m.Widget("cursor_g"),art=m.Widget("file_plate");
    m.Children(menu+8,{group,art});m.Children(group,{pane});put(group+0x134,1);
    const auto file_panels=L::ReadCursors(m,menu);assert(file_panels.size()==1&&file_panels[0].source[1]==pane);
    C::Publish(1,100,file_panels);assert(C::Mask(10,99,u,12)==0);
}
void SharedMessageChecks() {
    namespace L=MM3D::MessageLayout;namespace I=MM3D::MessageInterpolation;
    Memory m;auto layout=m.Layout("MessageWindow",0,0,0x500),all=m.Widget("all"),plate=m.Widget("plate"),pane=m.Widget("window_plate");
    m.Children(layout+8,{all});m.Children(all,{plate});m.Children(plate,{pane});
    auto geo=m.Alloc(0x7c);m.words[pane+0x28]=geo;m.words[geo+4]=6;m.words[layout+0x3f4]=42;
    const auto put=[&](U32 at,float f){m.words[at]=std::bit_cast<U32>(f);};
    put(plate+0x134,1);put(pane+0x134,1);
    for(unsigned r=0;r<3;++r)put(pane+0xe0+r*16+r*4,.7f);
    put(pane+0xec,200);put(pane+0xfc,160);put(pane+0x10c,3.5f);
    put(geo+0x14,-200);put(geo+0x18,-50);put(geo+0x20,400);put(geo+0x24,100);
    put(geo+0x28,.25f);put(geo+0x2c,.5f);put(geo+0x30,.5f);put(geo+0x34,.25f);
    put(pane+0x110,1);put(pane+0x120,1);
    const auto before=m.words;const auto panels=L::Read(m,layout);
    assert(panels.size()==1&&m.words==before);
    assert((panels[0].source==I::Source{layout,pane,geo,42}));
    const auto& p=panels[0];
    const std::array<std::array<float,4>,3> expected{{{60,0,0,280},{125,0,70,0},{3.5f,.7f,0,0}}};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
        assert(I::Near(p.transform[r][c],expected[r][c]));
    I::Values a{};a[10]={0,3,94,-93};a[8]={.2f,.9f,.4f,.8f};a[93]={1,.5f,.2f,.6f};
    for(unsigned r=0;r<3;++r)a[11+r]=p.transform[r];
    for(unsigned r=0;r<2;++r)a[94+r]=p.uv[r];
    I::Publish(100,777,panels);
    auto owner=I::SourceFor(100,777,a);assert(owner&&*owner==p.source);
    assert(I::SourceFor(101,777,a)&&!I::SourceFor(102,777,a)&&!I::SourceFor(100,778,a));
    // Native float32 transforms are rounded on upload to the PICA float24
    // mantissa. Ownership must survive that conversion, but not another pane.
    auto rounded=a;
    for(unsigned r:{11u,12u,13u,94u,95u})for(float& f:rounded[r])
        f=std::bit_cast<float>(std::bit_cast<U32>(f)&~127u);
    assert(I::SourceFor(100,777,rounded)==owner);
    assert(!I::Recognized(a)&&I::OwnedPlate(a)); // Unlisted style accepted by native ownership.
    I::History history;const I::Key key{1,2,777};
    assert(!history.Apply(100,key,a,owner));
    auto b=a;b[11][0]+=10;b[11][3]+=40;b[12][2]+=10;b[8][0]+=.1f;b[93][3]+=.2f;
    auto half=history.Apply(101,key,b,owner);
    assert(half&&I::Near((*half)[1][0],65)&&I::Near((*half)[1][3],300)&&I::Near((*half)[4][3],.7f));
    auto unrelated=a;unrelated[11][0]+=1;assert(!I::SourceFor(100,777,unrelated));
    unrelated=a;unrelated[95][1]+=.25f;assert(!I::SourceFor(100,777,unrelated));
    auto changed=p.source;changed[3]++;assert(!history.Apply(102,key,b,changed));
    history.Reset();history.Apply(100,key,a,owner);b=a;b[95][1]+=.25f;
    assert(!history.Apply(101,key,b,owner)); // Atlas regions never blend.
    auto duplicate=panels;duplicate[0].source[1]++;
    I::Publish(100,777,duplicate);assert(!I::SourceFor(100,777,a));
    I::Publish(1,778,panels);assert(!I::SourceFor(100,777,a));
    m.words[plate+0xd4]&=~1u;assert(L::Read(m,layout).empty());m.words=before;
    m.words[geo+4]=3;assert(L::Read(m,layout).empty());m.words=before;
    m.Children(pane,{plate});assert(L::Read(m,layout).empty());m.words=before;
    put(pane+0xec,std::numeric_limits<float>::quiet_NaN());assert(L::Read(m,layout).empty());m.words=before;
    const auto other=m.Layout("Other",0,0,0x500);assert(L::Read(m,other).empty());
    // Indicators are siblings of plate. Their transforms match independently,
    // while text geometry remains excluded from snapshots.
    const auto wait=m.Widget("waitIcon"),arrow=m.Widget("branchArrow"),text=m.Widget("text");
    m.Children(all,{plate,wait,arrow,text});
    for(auto node:{wait,arrow,text}) {
        for(unsigned off=0;off<0x140;off+=4)
            if(off!=0x10)m.words[node+off]=m.words[pane+off];
    }
    const auto glyph=m.Alloc(0x7c);m.words[glyph+4]=3;m.words[text+0x28]=glyph;
    put(wait+0xfc,210);put(arrow+0xec,40);
    const auto icons=L::Read(m,layout);assert(icons.size()==3);
    I::Publish(200,888,icons);
    for(const auto& icon:icons) {
        auto u=a;
        for(unsigned r=0;r<3;++r)u[11+r]=icon.transform[r];
        for(unsigned r=0;r<2;++r)u[94+r]=icon.uv[r];
        assert(I::SourceFor(200,888,u)==icon.source);
    }
    m.words[wait+0xd4]&=~1u;assert(L::Read(m,layout).size()==2);
    auto icon=a;icon[10][3]=1;icon[93]={100,200,300,400};
    I::History icon_history;
    assert(!I::Recognized(icon)&&I::OwnedPlate(icon));
    assert(!icon_history.Apply(200,key,icon,icons[1].source));
    auto next_icon=icon;next_icon[12][0]+=2;next_icon[93][0]=900;
    const auto icon_half=icon_history.Apply(201,key,next_icon,icons[1].source);
    assert(icon_half&&I::Near((*icon_half)[2][0],icon[12][0]+1));
    assert((*icon_half)[4]==next_icon[93]); // Inactive color register is untouched.
    next_icon[94][1]+=.0625f;
    assert(!icon_history.Apply(202,key,next_icon,icons[1].source)); // Sprite changes never blend.
    assert(MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,0xac22b79e29209c01ULL,6,1,200,888));
    assert(!I::Active(202,888)&&!I::Active(200,889));


}

void GearPhotoPoseChecks() {
    Memory m;Hook h;const auto page=m.Alloc(0x200),photo=m.Layout("GearPhoto");
    const auto left=m.Widget("picture_left_pane",120,80),right=m.Widget("picture_right_pane",120,80);
    const auto border=m.Widget("frame_base_pane",116,76),corner=m.Widget("frame_corner",284,164);
    m.Children(photo+8,{left,right,border});m.Children(border,{corner});
    const auto geo=m.Alloc(0x7c);m.words[border+0x28]=geo;m.words[geo+4]=5;
    m.words[geo+0x14]=std::bit_cast<U32>(-104.f);m.words[geo+0x18]=std::bit_cast<U32>(-64.f);
    m.words[geo+0x20]=std::bit_cast<U32>(208.f);m.words[geo+0x24]=std::bit_cast<U32>(128.f);
    m.words[0x656b0c]=page;m.words[0x72f5bc+8]=page;
    m.words[page]=0x63a270;m.words[page+0x10]=photo;
    MM3D::GearMenu::Options options;options.poses[MM3D::GearMenu::Photo]={100,15,.5f};
    MM3D::GearMenu::Apply(options);const auto before=m.words;
    h.Begin(m,photo);
    for(auto node:{left,right,border,corner}) {
        const float x=std::bit_cast<float>(before.at(node+0xec));
        const float y=std::bit_cast<float>(before.at(node+0xfc));
        assert(std::abs(std::bit_cast<float>(*m.Read(node+0xec))-(100+200+.5f*(x-200)))<.001f);
        assert(std::abs(std::bit_cast<float>(*m.Read(node+0xfc))-(15+120+.5f*(y-120)))<.001f);
    }
    h.End(m);assert(m.words==before);
    MM3D::PhotoCapture::layout=photo;
    h.Begin(m,photo,true);
    assert(std::bit_cast<float>(*m.Read(geo+0x20))==207.5f);
    assert(std::bit_cast<float>(*m.Read(geo+0x14))==-103.75f);
    for(auto node:{left,right,border,corner}) {
        const float x=std::bit_cast<float>(before.at(node+0xec));
        assert(std::abs(std::bit_cast<float>(*m.Read(node+0xec))-(300+(8.f/15.f)*(x-200)))<.001f);
    }
    h.End(m);assert(m.words==before);
    // Shutter preview widens the full photo/frame without applying Gear controls.
    MM3D::PhotoCapture::enabled=false;
    h.Begin(m,photo,true);
    assert(std::bit_cast<float>(*m.Read(geo+0x20))==208.f);
    for(auto node:{left,right,border,corner}) {
        const float x=std::bit_cast<float>(before.at(node+0xec));
        assert(std::abs(std::bit_cast<float>(*m.Read(node+0xec))-(300+.5f*(x-200)))<.001f);
    }
    h.End(m);assert(m.words==before);
    MM3D::PhotoCapture::enabled=true;
    m.words[0x72f5bc+8]=0;const auto preview=m.words;
    h.Begin(m,photo,true);
    assert(std::bit_cast<float>(*m.Read(geo+0x20))==207.5f);
    assert(std::bit_cast<float>(*m.Read(geo+0x14))==-103.75f);
    for(auto node:{left,right,border,corner}) {
        const float x=std::bit_cast<float>(preview.at(node+0xec));
        assert(std::abs(std::bit_cast<float>(*m.Read(node+0xec))-(200+(16.f/15.f)*(x-200)))<.001f);
    }
    h.End(m);assert(m.words==preview);
    h.Begin(m,photo,false);h.End(m);assert(m.words==preview);
    MM3D::PhotoCapture::enabled=false;
    h.Begin(m,photo,true);assert(m.words==preview);h.End(m);
    MM3D::PhotoCapture::enabled=true;
    MM3D::PhotoCapture::layout=0;
    // The shared photo layout must retain native placement outside Gear.
    m.words[0x72f5bc+8]=0;const auto outside=m.words;
    h.Begin(m,photo);h.End(m);assert(m.words==outside);
    MM3D::GearMenu::Apply(MM3D::GearMenu::Options{});
}

int main(int argc,char** argv) {
    GearPhotoPoseChecks();
    CursorInterpolationChecks();
    SharedMessageChecks();
    {
        // Captured low/scaled Gear tabs must start hidden but be visible
        // during the first opening step, not only its final one or two steps.
        const float selected_top=219.36f,unselected_top=225.3f;
        const float travel=std::max(MM3D::MenuTabs::SlideClearance(19.7f,.62f,true),
                                    MM3D::MenuTabs::SlideClearance(21.3f,.64f,false));
        assert(selected_top+travel>=241.99f && unselected_top+travel>=242.f);
        assert(selected_top+.75f*travel<240.f);
        assert(unselected_top+.5f*travel<240.f);
        assert(selected_top+travel/3.f<240.f && unselected_top+travel/3.f<240.f);
        // Selected tabs need enough clearance for the lifted face and rim at
        // every supported scale, including a tab moved above the defaults.
        for(float scale:{.25f,1.f,2.f})for(float y:{-40.f,0.f,20.f}) {
            const float top=204.f+y-7.f*scale;
            assert(std::abs(top+MM3D::MenuTabs::SlideClearance(y,scale,true)-242.f)<.001f);
        }
        assert(MM3D::MenuTabs::SlideClearance(100.f,1.f,false)==0.f);
        MM3D::MenuTabs::SlideTiming timing;
        assert(timing.Apply(1,1)==1);
        const auto entering=timing.Apply(1,.5f);
        assert(entering<.5f && entering>.4f);
        assert(timing.Apply(1,.5f)==entering); // Repeated draw keeps direction.
        assert(timing.Apply(1,0)==0);
        const auto exiting=timing.Apply(1,.5f);
        assert(exiting>.5f && exiting<.6f);
        assert(timing.Apply(1,.5f)==exiting);
        assert(timing.Apply(1,1)==1);
        assert(timing.Apply(2,.5f)==entering); // New page starts opening.
    }
    {
        Memory m;Hook hook;
        const auto manager=m.Alloc(0x24);
        std::array<U32,6> queues{};
        for(unsigned i=0;i<6;++i){queues[i]=m.Alloc(0x848);m.words[manager+8+4*i]=queues[i];}
        const auto world=m.Layout("UpWorldMapHud"),background=m.Layout("UpWorldMapBG"),
            title=m.Layout("SubMenuTitle"),gear=m.Layout("MenuCollect");
        m.Queue(queues[2],{world,title});
        const auto before=m.words;
        assert(hook.SubmittedInterpolationOwner(m,manager)==world);
        assert(m.words==before); // Ownership inspection cannot mutate guest state.
        assert(hook.SubmittedInterpolationOwner(m,manager,true)==world);
        MM3D::InventoryInterpolation::Publish(42,3,hook.SubmittedInterpolationOwner(m,manager),
            hook.SubmittedInterpolationOwner(m,manager,true));
        assert(MM3D::InventoryInterpolation::Owner(42,3,true)==world);
        assert(MM3D::InventoryInterpolation::Owner(43,3,true)==world);
        assert(!MM3D::InventoryInterpolation::Owner(44,3,true));
        assert(!MM3D::InventoryInterpolation::Owner(42,4,true));
        assert(MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
            0xac22b79e29209c01ULL,24,1,42,3)); // Map banner retains CPU replay inputs.
        m.Queue(queues[2],{background,title});
        assert(!hook.SubmittedInterpolationOwner(m,manager));
        MM3D::InventoryInterpolation::Publish(43,3,hook.SubmittedInterpolationOwner(m,manager));
        assert(!MM3D::InventoryInterpolation::Owner(43,3,true));
        assert(!MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
            0xac22b79e29209c01ULL,24,1,43,3));
        m.Queue(queues[2],{gear}); // Wrong queue cannot leak into the next queue's scan.
        assert(!hook.SubmittedInterpolationOwner(m,manager));
        m.Queue(queues[2],{world});m.words[queues[2]+0x844]=2;
        assert(!hook.SubmittedInterpolationOwner(m,manager)); // Inconsistent queue rejected.
        m.Queue(queues[2],{});
        m.Queue(queues[3],{gear});
        assert(hook.SubmittedInterpolationOwner(m,manager)==gear);
        assert(!hook.SubmittedInterpolationOwner(m,manager,true));
        m.Queue(queues[3],{});m.Queue(queues[5],{m.Layout("MenuMapDungeon")});
        assert(hook.SubmittedInterpolationOwner(m,manager)!=0);
        assert(!hook.SubmittedInterpolationOwner(m,manager,true));
        m.Queue(queues[5],{});
        const auto double_time=m.Layout("LapTimeSong");
        m.Queue(queues[3],{double_time});
        assert(hook.SubmittedInterpolationOwner(m,manager)==double_time);
        assert(!hook.SubmittedInterpolationOwner(m,manager,true));
        m.Queue(queues[3],{});
        assert(!hook.SubmittedInterpolationOwner(m,manager));
        m.Queue(queues[1],{world});
        assert(!hook.SubmittedInterpolationOwner(m,manager)); // No stale or background-only owner.
    }
    if(argc>1){
        namespace H=MM3D::HudButtons;
        std::ifstream f(argv[1],std::ios::binary);Memory code;
        for(U32 pc=0x7d42c0;pc<=0x7d43dc;pc+=4){f.seekg(pc-0x100000);U32 word=0;f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[pc]=word;}
        assert(H::VisibilityCodeMatches(code));
        for(unsigned i=0;i<H::VisibilityPCs.size();++i){auto pc=H::VisibilityPCs[i],word=*code.Read(pc);
            assert(H::VisibilityInstruction(pc,word)==(i==1||i==4?0xe35c0002:0xe35c0001));
            assert(H::VisibilityInstruction(pc,word^1)==(word^1));}
        assert(H::VisibilityInstruction(0x7d42c8,*code.Read(0x7d42c8))==*code.Read(0x7d42c8));
        code.words[0x7d42f8]^=1;assert(!H::VisibilityCodeMatches(code));
    }

    SmallGameplayMapChecks();
    FlipPeakChecks();
    HudButtonChecks();
    FullscreenEffectsChecks();
    SongResetChecks();
    BannerChecks();
    DoubleTimeChecks();
    CutsceneFovChecks();
    NotebookMixedRowsChecks();
    NotebookScrollbarChecks();
    InventoryShadowChecks();
    ExpandedPanelShadowChecks();
    DungeonMapShadowChecks();
    DungeonMapChecks();
    MaskFlightChecks();
    MinigameHudChecks();
    MagicBorderChecks();
    SongEffectChecks();
    SongListChecks();
    GearTransitionChecks();
    FileSelectPresentationChecks();
    if(argc>1) {
        std::ifstream f(argv[1],std::ios::binary);Memory code;
        for(U32 at: {PCs[0],PCs[1],PCs[2],PCs[3],0x1262f8u,0x59e3f4u}) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        for(auto [at,word]:std::array<std::pair<U32,U32>,4>{{{MM3D::SongList::NavPC,MM3D::SongList::NavWord},{MM3D::SongList::NavPC+4,0x0a000002},{MM3D::SongList::NavPC+8,0xe1a01007},{MM3D::SongList::NavPC+12,0xe1a00004}}}) {
            U32 value=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&value),4);assert(f&&value==word);code.words[at]=value;
        }
        assert(MM3D::SongList::CodeMatches(code));
        for(U32 at:{0x596c24u,0x596cb8u,0x596cbcu,0x594480u,0x596c20u,0x596cb4u,0x594484u,0x36e294u,0x175c30u,0x596cecu,0x596ce8u,0x596ff4u}) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        for(U32 at:MM3D::DungeonMap::PCs) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        for(auto [at,expected]:MM3D::DungeonMap::ZoomGuards) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f&&word==expected);code.words[at]=word;
        }
        for(auto [at,expected]:MM3D::DungeonMap::OverviewCursorGuards) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f&&word==expected);code.words[at]=word;
        }
        assert(MM3D::DungeonMap::CodeMatches(code));
        code.words[MM3D::DungeonMap::PCs[0]]^=1;assert(!MM3D::DungeonMap::CodeMatches(code));

        for(auto load:MM3D::SongEffect::Loads)for(U32 at:{load.pc,load.pc+4,load.literal}) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::SongEffect::CodeMatches(code));
        assert(CodeMatches(code));
        for(auto [at,expected]:MM3D::InventoryNavigation::Guards) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::InventoryNavigation::CodeMatches(code));
        code.words[MM3D::InventoryNavigation::VerticalPC]^=1;assert(!MM3D::InventoryNavigation::CodeMatches(code));
        for(auto [at,expected]:MM3D::NotebookPrompt::AvailabilityCode) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::NotebookPrompt::AvailabilityCodeMatches(code));
        for(auto [at,expected]:MM3D::NativeStartup::FadeGuards) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::NativeStartup::FadeCodeMatches(code));
        for(U32 at: MM3D::ItemPreview::PCs) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        for(U32 at: {0x5c2510u,0x5c27e0u,0x1122e0u,0x1122e8u,0x5a6b48u,0x5a5fccu}) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::ItemPreview::CodeMatches(code));
        for(U32 at:{0x7cf16cu,0x7cf170u,0x7cf174u,0x7cf178u,0x7cf180u,0x7d08f8u}) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::CameraLive::DialogueCameraCodeMatches(code));
        const auto patched=MM3D::CameraLive::DialogueCameraInstruction(0x7cf174,code.words[0x7cf174]);
        assert(patched==0x13530001);
        for(U32 mode=0;mode<64;++mode) {
            const bool before=mode==31||mode-9<=2;
            const bool after=mode==31||mode-9<=(patched&255);
            assert(mode==11?(before&&!after):(before==after));
        }
        code.words[0x7cf16c]^=1;assert(!MM3D::CameraLive::DialogueCameraCodeMatches(code));
        assert(MM3D::CameraLive::DialogueCameraInstruction(0x7cf174,0x13530003)==0x13530003);

        for(U32 at:{0x5bc790u,0x5bc78cu,0x5bc794u,0x5bc7a8u,0x5bc7b0u}) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::NativeItemPresent::IntroCodeMatches(code));
        code.words[0x5bc790]^=1;assert(!MM3D::NativeItemPresent::IntroCodeMatches(code));

        for(U32 at=MM3D::NativeNotebook::PC;at<=MM3D::NativeNotebook::PC+12;at+=4) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::NativeNotebook::CodeMatches(code));
        for(U32 at:{0x7d04b0u,0x7d04b8u,0x7d04c4u,0x7d04c8u,0x7d04ccu,0x7d04d0u,0x7d04ecu,0x7d04f0u}) {
            U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;
        }
        assert(MM3D::NativeNotebook::MapCodeMatches(code));
        assert(MM3D::NativeNotebook::MapInstruction(0x7d04c4,0xe3130004)==0xefffffd2);
        code.words[0x7d04cc]^=1;
        assert(!MM3D::NativeNotebook::MapCodeMatches(code));
        code.words[0x7d04cc]^=1;
    }
    if(argc>1) {
        std::ifstream f(argv[1],std::ios::binary);Memory code;
        auto read=[&](U32 at){U32 word=0;f.seekg(at-0x100000);f.read(reinterpret_cast<char*>(&word),4);assert(f);code.words[at]=word;};
        for(auto p:MM3D::ScheduleRows::Patches)read(p.pc);
        for(U32 at:{0x5abbf8u,0x5abc00u,0x5b24c0u,0x5b24c8u,0x5b2614u,0x5b261cu})read(at);
        assert(MM3D::ScheduleRows::CodeMatches(code));
        code.words[0x5b261c]^=1;assert(!MM3D::ScheduleRows::CodeMatches(code));
    }
    {
        using namespace MM3D::ScheduleRows;
        // Every selection and both ends, including short/empty character lists.
        for(int count=1;count<=28;++count)for(int selected=0;selected<count;++selected)
            for(int requested=-2;requested<=count+2;++requested) {
                int first=Window(requested,selected,count);
                assert(first>=0&&first<=std::max(0,count-7));
                assert(selected>=first&&selected<first+7);
                int attached=0;
                for(int i=0;i<count;++i)if(i>=first&&i<first+7){assert(i-(first-1)>=1&&i-(first-1)<=7);++attached;}
                assert(attached==std::min(count,7));
            }
        Memory m;MM3D::ScheduleRows::Hook h;MM3D::NativeInventory::Hook helper;
        auto owner=m.Alloc(0x800),up=m.Layout("UpSchedule"),cursor=m.Layout("CursorScheFrame");
        auto all=m.Widget("all",37,74);m.words[all+0x40]=std::bit_cast<U32>(80.f);
        m.Children(cursor+8,{all});m.words[owner+0x54c]=all;m.words[owner+0x508]=cursor;m.words[owner+0x38]=up;
        for(unsigned i=0;i<8;++i)for(U32 offset:{0x4d8u,0x550u,0x570u})
            m.words[owner+offset+4*i]=offset==0x550?m.Layout("TextNew"):m.Widget("slot");
        m.words[owner+0xc]=0x1c180201;m.words[owner+0x10]=0x991b01;m.words[owner+0x14]=0x123400ff;
        h.Enter(m,owner);assert(h.owner==owner&&h.first==21&&h.selected==27);
        assert(m.words[owner+0xc]==0x1c150201&&m.words[owner+0x14]==0x123400ff);
        for(unsigned i=1;i<8;++i)assert(std::bit_cast<float>(m.words[m.words[owner+0x4d8+4*i]+0x40])==19.f+40.f*i);
        auto footer=m.Widget("display",0,0),ruler=m.Widget("current_time_pane1",46,54);
        m.Children(up+8,{footer,ruler});
        auto bar=m.Layout("ScrollBar"),middle=m.Widget("bb_pane01"),bottom=m.Widget("bb_pane02");
        auto thumb=m.Widget("bar_g"),top=m.Widget("bar00",388,41),strip=m.Widget("bar01",388,43),last=m.Widget("bar02",388,46);
        m.Children(bar+8,{middle,bottom,thumb});m.Children(thumb,{top,strip,last});m.words[owner+0x5c0]=bar;
        auto before=m.words;std::vector<MM3D::NativeInventory::Hook::Write> writes;
        assert(h.Expand(m,up,helper,writes));for(auto w:writes)m.Write(w.address,w.after);
        assert(std::bit_cast<float>(m.words[footer+0xfc])==120.f);
        assert(std::bit_cast<float>(m.words[bottom+0xfc])==120.f);
        assert(std::bit_cast<float>(m.words[last+0xfc])==352.f); // rendered cap ends one unit above anchor
        for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);assert(m.words==before);
        for(int count:{0,1,7,8,9,10,28})for(int first:{0,std::max(0,count-7)}) {
            h.total=count;h.first=first;writes.clear();
            assert(h.Expand(m,up,helper,writes));for(auto w:writes)m.Write(w.address,w.after);
            const float y=std::bit_cast<float>(m.words[top+0xfc]);
            const float end=std::bit_cast<float>(m.words[last+0xfc])-1.f;
            const float height=310.f*(count>7?7.f/float(count):1.f);
            assert(std::abs(end-y-height)<.001f);
            assert(std::abs(std::bit_cast<float>(m.words[strip+0xfc])+std::bit_cast<float>(m.words[strip+0xf4])-(end-1.5f))<.001f);
            if(first==0)assert(std::abs(y-41.f)<.001f);
            if(first==std::max(0,count-7))assert(std::abs(end-351.f)<.001f);
            for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);
            assert(m.words==before);
        }
        // Recycling a New badge at the bottom of the expanded seven-row list
        // can leave its world matrix at its old/uninitialized origin. Badge
        // validation must never drop the footer/clock/scrollbar adjustments.
        for(int failure:{0,1,2,3}) {
            const auto original=m.words;
            const auto icon=m.words[owner+0x4d8+7*4],badge=m.words[owner+0x550+7*4];
            m.words[icon+0xfc]=std::bit_cast<U32>(299.f);
            if(failure==1)m.words[owner+0x550+7*4]=0;
            if(failure==2)m.words[badge+8+0xfc]=std::bit_cast<U32>(std::numeric_limits<float>::quiet_NaN());
            if(failure==3)m.Children(badge+8,{badge+8}); // failure after staging some writes
            before=m.words;writes.clear();
            assert(h.Expand(m,up,helper,writes));
            assert(m.words==before); // preparation is read-only
            for(auto w:writes)m.Write(w.address,w.after);
            assert(std::bit_cast<float>(m.words[footer+0xfc])==120.f);
            assert(std::abs(std::bit_cast<float>(m.words[ruler+0xf4])-297.f/177.f)<.0001f);
            if(failure==0)assert(std::bit_cast<float>(m.words[badge+8+0xfc])==299.f);
            else for(unsigned off=0xe0;off<0x100;off+=4)assert(m.words[badge+8+off]==before[badge+8+off]);
            for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);
            assert(m.words==before);
            m.words=original;
        }
        h.total=28;h.first=21;
        // Schedule exit: all seven visible slots, including the untracked
        // native buffer slot, move left with badges/seals and restore exactly.
 auto player=m.Alloc(0x20),anim=m.Alloc(0x20);m.words[owner+0x5c4]=player;m.words[player+0xc]=anim;m.words[anim+8]=m.String("out_up_schedule_anim");
 for(float t:{2.f,3.f,4.5f,6.f,8.f,10.f}) {
  for(unsigned i=0;i<8;++i) {
   float x=i<7?400.f*std::clamp((t-float(i+2))/2.f,0.f,1.f):0.f;
   for(U32 off:{0x4d8u,0x550u,0x570u}) {auto p=m.words[owner+off+4*i]+(off==0x550?8:0);m.words[p+0x3c]=std::bit_cast<U32>(0.f);m.words[p+0xec]=std::bit_cast<U32>(x);}
  }
  before=m.words;writes.clear();assert(h.Expand(m,up,helper,writes));for(auto w:writes)m.Write(w.address,w.after);
  for(unsigned i=1;i<8;++i) for(U32 off:{0x4d8u,0x550u,0x570u}) {auto p=m.words[owner+off+4*i]+(off==0x550?8:0);float expected=-400.f*std::clamp((t-float(i+1))/2.f,0.f,1.f);assert(std::abs(std::bit_cast<float>(m.words[p+0xec])-expected)<.001f);}
  for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);assert(m.words==before);
 }
        m.words[owner+0x5c4]=0;
        // Local cursor Y need not describe the rendered Y: native animation
        // bakes offsets into the base transform. Reproduce both directions of
        // the observed one-row error, including initial and last selections.
        for(int first:{0,21})for(int row=0;row<7;++row)for(int day:{0,1,3,5})
            for(float scale:{.6f,1.f})for(float drift:{-40.f,40.f,120.f}) {
                h.first=first;m.words[owner+0x10]=U32((first+row)<<8)|U32(day);
                const auto anchor=m.words[owner+0x4d8+4*(row+1)];
                const float row_y=30.f+(59.f+row*40.f)*scale;
                m.words[anchor+0xfc]=std::bit_cast<U32>(row_y);
                m.words[anchor+0xf4]=std::bit_cast<U32>(scale);
                m.words[all+0x40]=std::bit_cast<U32>(0.f);
                m.words[all+0xfc]=std::bit_cast<U32>(row_y+drift);
                before=m.words;writes.clear();assert(h.Expand(m,cursor,helper,writes));
                for(auto w:writes)m.Write(w.address,w.after);
                const float expected=row_y+(day==0?-3.f:-6.f)*scale;
                assert(std::abs(std::bit_cast<float>(m.words[all+0xfc])-expected)<.0001f);
                assert(m.words[all+0xec]==before[all+0xec]);
                for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);
                assert(m.words==before);
            }
        // Pending top commits once, exact same positions on repeated population.
        m.words[owner+0x14]=0x12340003;m.words[owner+0x10]=0x00000601;h.Enter(m,owner);
        assert(h.first==3&&m.words[owner+0x14]==0x123400ff);before=m.words;h.Enter(m,owner);assert(m.words==before);
        m.words[owner+0xc]=0x00000201;m.words[owner+0x10]=0;h.Enter(m,owner);assert(h.total==0&&h.first==0);
        // Invalid ownership/partial slots must make no writes.
        m.words[owner+0x4dc]=0;before=m.words;h.Enter(m,owner);assert(!h.owner&&m.words==before);
    }
    // Both notebook pages share the Schedule track bounds and retain thumb fractions.
    {
 Memory m;MM3D::NativeNotebook::Hook h; h.frame[0]={-47.6f,9.6f,.69f};h.frame[3]={-7.8f,18.6f,.6f};h.frame[8]={179.6f,0,1};
 for(bool events:{false,true})for(float fraction:{0.f,.5f,1.f}){
  auto l=m.Layout(events?"UpQuestlist":"UpSchedule"),all=m.Widget("all"),bar=m.Widget("scroll_bar_l");m.Children(l+8,{all});m.Children(all,{bar});
  auto a=m.Widget("bb_pane00"),b=m.Widget("bb_pane02"),thumb=m.Widget("bar_g");m.Children(bar,{a,b,thumb});
  auto put=[&](U32 at,float v){m.words[at]=std::bit_cast<U32>(v);};
  for(auto n:{a,b}){auto g=m.Alloc(0x40);m.words[n+0x28]=g;put(g+0x14,384);put(g+0x18,n==a?40:228);put(g+0x20,8);put(g+0x24,4);}
  float extension=events?87.f:120.f;put(b+0xfc,extension);put(thumb+0xec,388);put(thumb+0xfc,40+(192+extension)*fraction);
  auto original=m.words;std::vector<MM3D::NativeInventory::Hook::Write>writes;assert(h.ScrollbarPosition(m,l,writes));for(auto w:writes)m.Write(w.address,w.after);
  auto pose=h.frame[events?0:3];auto value=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
  assert(std::abs(pose.x+pose.scale*(value(a+0xec)+384*value(a+0xe0))-402.2f)<.001f);
  assert(std::abs(pose.y+pose.scale*(value(a+0xfc)+40*value(a+0xf4))-42.6f)<.001f);
  assert(std::abs(pose.y+pose.scale*(value(b+0xfc)+232*value(b+0xf4))-229.8f)<.001f);
  assert(std::abs(pose.y+pose.scale*value(thumb+0xfc)-(42.6f+187.2f*fraction))<.001f);
  for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);assert(m.words==original);
 }
    }
    Memory m;Hook hook;buttons_enabled=false;
    masks_x=7;masks_y=-3;masks_scale=1.2f; // Must not alter any Items checks below.
    auto page=m.Alloc(0x200),panel=m.Layout("MenuItem"),all=m.Widget("all");m.Children(panel+8,{all});
    std::vector<U32> branches;
    for(auto name:{"set_frame3_g","set_frame2_g","set_frame1_g","set_frame0_g","item_set_pos_g","btn_item_omen_g","btn_return_l","item_panel_g","right_item_set_l"})
        branches.push_back(m.Widget(name,100,60));
    m.Children(all,branches);m.words[page+0xc0]=panel;
    std::vector<U32> entries{panel};
    for(unsigned i=0;i<20;++i) {
        auto icon=m.Layout("ItemIconButton",32,28);m.words[page+0xd0+i*4]=icon;entries.push_back(icon);
        auto quantity=m.Widget("quantity",40,40);m.Children(icon+8,{quantity});
    }
    auto cursor=m.Layout("CursorItemCollect",32,28),sidebar_cursor=m.Layout("CursorItemCollect",280,28);
    m.words[page+8]=cursor;m.words[page+12]=sidebar_cursor;
    auto manager=m.Alloc(0x30),bottom=m.Alloc(0x848),top=m.Alloc(0x848);
    m.words[manager+28]=bottom;m.words[manager+20]=top;m.Queue(bottom,entries);
    // Native selection cursors use separate priority buckets from item icons.
    m.words[bottom+4+6*0x80]=cursor;m.words[bottom+0x804+6*4]=1;
    m.words[bottom+4+5*0x80]=sidebar_cursor;m.words[bottom+0x804+5*4]=1;
    m.words[bottom+0x844]+=2;
    hook.Capture(m,page);assert(hook.panel==panel);
    x=10;y=20;scale=.5f;const auto original=m.words;
    enabled=false;hook.Pass(m,manager,5);assert(m.words==original);
    enabled=true;hook.Pass(m,manager,5);assert(status==2&&routed_icons==20);
    assert(m.words[bottom+0x844]==2&&m.words[top+0x844]==22);
    assert(m.words[bottom+0x804+5*4]==1&&m.words[bottom+0x804+6*4]==0);
    assert(m.words[top+4+6*0x80]==cursor);
    const auto routed_memory=m.words;
    hook.Begin(m,panel);assert(m.words[branches[7]+0xd4]==14&&m.words[branches[8]+0xd4]==15);
    hook.End(m);assert(m.words==routed_memory);
    hook.Pass(m,manager,3);
    hook.Begin(m,cursor);assert(std::bit_cast<float>(m.words[cursor+8+0xec])==26.f);
    assert(std::bit_cast<float>(m.words[cursor+8+0xe0])==.5f);
    hook.End(m);assert(m.words==routed_memory);
    hook.Pass(m,manager,3);hook.Begin(m,panel);
    assert(m.words[branches[8]+0xd4]==14&&m.words[branches[7]+0xd4]==15);
    assert(std::bit_cast<float>(m.words[branches[7]+0xec])==60.f);
    assert(std::bit_cast<float>(m.words[branches[7]+0xfc])==50.f);
    hook.End(m);assert(m.words==routed_memory);
    hook.Begin(m,entries[1]);assert(std::bit_cast<float>(m.words[entries[1]+8+0xec])==26.f);
    hook.End(m);assert(m.words==routed_memory);
    // Moving focus to the sidebar retains both cursors on the original screen.
    m.words=original;m.words[cursor+0x44]=std::bit_cast<U32>(280.f);
    hook.Pass(m,manager,5);assert(status==2&&m.words[top+0x844]==21&&m.words[bottom+0x844]==3);
    // Insufficient native queue capacity must leave all original entries intact.
    m.words=original;auto unrelated=m.Layout("Other");m.Queue(top,std::vector<U32>(12,unrelated));
    const auto full=m.words;hook.Pass(m,manager,5);assert(status==3&&m.words==full);
    // An invalid render tree must not move icons off the original screen.
    m.words=original;m.words[all+0x18]=500;const auto invalid=m.words;
    hook.Pass(m,manager,5);assert(status==4&&m.words==invalid);
    // Horizontal sidebar: rotate background only, retain upright slots and cursor.
    m.words=original;
    std::array<U32,4> slots{};
    for(unsigned i=0;i<4;++i) {
        slots[i]=m.Layout("ItemIconButton",Hook::Anchors[i][0],Hook::Anchors[i][1]);
        m.words[page+0x2c+i*4]=slots[i];entries.push_back(slots[i]);
    }
    m.Queue(bottom,entries);m.words[bottom+0x844]+=2;
    m.words[sidebar_cursor+0x44]=std::bit_cast<U32>(292.f);
    m.words[sidebar_cursor+8+0xec]=std::bit_cast<U32>(292.f);
    auto background=m.Widget("panel_base_r0",256,0);
    auto frame=m.Widget("btn_x_g",276,78),label=m.Widget("disp_x_pane",267,67);
    m.Children(branches[8],{background,frame,label});
    hook.Capture(m,page);assert(hook.bar_ready);
    bar_enabled=true;bar_x=10;bar_y=20;bar_scale=.5f;bar_spacing=64;
    const auto row_original=m.words;
    hook.Pass(m,manager,5);assert(status==2);
    assert(m.words[top+0x844]==27&&m.words[bottom+0x844]==1);
    const auto row_routed=m.words;
    hook.Begin(m,panel);assert(m.words[branches[8]+0xd4]==14);hook.End(m);
    hook.Pass(m,manager,3);hook.Begin(m,panel);
    const auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
    assert(f(background+0xe0)==0&&f(background+0xf0)==-.5f);
    assert(f(background+0xec)==10&&f(background+0xfc)==52);
    assert(f(frame+0xe0)==.5f&&f(frame+0xe4)==0&&f(frame+0xf0)==0);
    assert(f(frame+0xec)==49.5f&&f(frame+0xfc)==27.5f);
    assert(f(label+0xec)==45&&f(label+0xfc)==22);
    hook.End(m);assert(m.words==row_routed);
    for(unsigned i=0;i<4;++i) {
        hook.Begin(m,slots[i]);assert(f(slots[i]+8+0xec)==26+i*32&&f(slots[i]+8+0xfc)==36);
        hook.End(m);assert(m.words==row_routed);
    }
    hook.Begin(m,sidebar_cursor);assert(f(sidebar_cursor+8+0xec)==26&&f(sidebar_cursor+8+0xfc)==36);
    hook.End(m);assert(m.words==row_routed);
    // Two independently queued equip-swap icons follow both visual endpoints.
    m.words=row_original;
    auto fly=entries[1],swap=slots[1];
    m.words[page+0xb4]=fly;m.words[page+0xb8]=swap;
    m.words[page+0x18]=0x00000105; // primary strip slot1, alternate grid col0/row0
    for(auto [at,value]:{std::pair{0x84u,292.f},{0x88u,95.f},{0x90u,32.f},{0x94u,28.f}})m.words[page+at]=std::bit_cast<U32>(value);
    m.words[bottom+4+7*0x80]=fly;m.words[bottom+8+7*0x80]=swap;m.words[bottom+0x804+7*4]=2;m.words[bottom+0x844]+=2;
    hook.Pass(m,manager,5);assert(status==2&&m.words[bottom+0x844]==1&&m.words[top+0x844]==27);
    hook.Pass(m,manager,3);
    for(float t:{0.f,.25f,.5f,1.f})for(auto icon:{fly,swap}) {
        const float px=292+t*(32-292),py=95+t*(28-95);
        m.words[icon+0x44]=m.words[icon+8+0xec]=std::bit_cast<U32>(px);
        m.words[icon+0x48]=m.words[icon+8+0xfc]=std::bit_cast<U32>(py);
        auto before=m.words;hook.Begin(m,icon);
        assert(std::abs(f(icon+8+0xec)-(58+t*(26-58)))<.001f);
        assert(std::abs(f(icon+8+0xfc)-(36+t*(34-36)))<.001f);
        hook.End(m);assert(m.words==before);
    }
    // A then X/Y uses a separate +6c ItemIcon copied into +b4 by 5c116c.
    // It is neither a grid/slot wrapper nor an embedded icon.
    for(unsigned slot:{1u,2u}) {
        m.words=row_original;
        auto button_fly=m.Layout("ItemIcon",32,28);
        m.words[page+0x6c]=button_fly;m.words[page+0xb4]=button_fly;m.words[page+0xb8]=0;
        m.words[page+0x18]=5|(slot<<8);
        const float ex=Hook::Anchors[slot][0],ey=Hook::Anchors[slot][1];
        for(auto [at,value]:{std::pair{0x84u,ex},{0x88u,ey},{0x90u,32.f},{0x94u,28.f}})
            m.words[page+at]=std::bit_cast<U32>(value);
        m.words[bottom+4+7*0x80]=button_fly;m.words[bottom+0x804+7*4]=1;++m.words[bottom+0x844];
        hook.Pass(m,manager,5);
        assert(status==2&&m.words[bottom+0x844]==1&&m.words[top+0x844]==28);
        assert(m.words[top+4+7*0x80]==button_fly);
        hook.Pass(m,manager,3);
        for(float t:{0.f,.25f,.5f,1.f}) {
            m.words[button_fly+0x44]=m.words[button_fly+8+0xec]=std::bit_cast<U32>(32+t*(ex-32));
            m.words[button_fly+0x48]=m.words[button_fly+8+0xfc]=std::bit_cast<U32>(28+t*(ey-28));
            auto before=m.words;hook.Begin(m,button_fly);
            assert(std::abs(f(button_fly+8+0xec)-(26+t*slot*32))<.001f);
            assert(std::abs(f(button_fly+8+0xfc)-(34+t*2))<.001f);
            hook.End(m);assert(m.words==before);
        }
    }
    m.words=row_original;bar_enabled=false;hook.Pass(m,manager,5);
    assert(status==2&&m.words[top+0x844]==22&&m.words[bottom+0x844]==6);
    // Independently transform the menu buttons, their backing art and nested Return layout.
    m.words=row_original;
    auto masks_button=m.Widget("btn_omen_l"),items_button=m.Widget("btn_item_l");
    auto masks_face=m.Widget("pressed",66,208),items_face=m.Widget("pressed",154,204);
    auto masks_back=m.Widget("bb_item_omen_pane00",64,206),items_back=m.Widget("bb_item_omen_pane01",160,206);
    m.Children(masks_button,{masks_face});m.Children(items_button,{items_face});
    m.Children(branches[5],{masks_button,items_button,items_back,masks_back});
    auto return_layout=m.Layout("Return",0,202);
    m.words[branches[6]+0x20]=return_layout;
    buttons_enabled=true;
    buttons[0].x=10;buttons[0].y=-20;buttons[0].scale=.75f;
    buttons[1].x=20;buttons[1].y=-10;buttons[1].scale=.5f;
    buttons[2].x=100;buttons[2].y=-30;buttons[2].scale=1.25f;
    const auto before_buttons=m.words;
    hook.Pass(m,manager,5);assert(status==2);
    const auto buttons_routed=m.words;
    hook.Begin(m,panel);
    assert(m.words[branches[5]+0xd4]==14&&m.words[branches[6]+0xd4]==14);
    hook.End(m);assert(m.words==buttons_routed);
    hook.Pass(m,manager,3);hook.Begin(m,panel);
    assert(m.words[branches[5]+0xd4]==15&&m.words[branches[6]+0xd4]==15);
    assert(f(masks_face+0xec)==85&&f(masks_face+0xfc)==197&&f(masks_face+0xe0)==.5f);
    assert(f(masks_back+0xec)==84&&f(masks_back+0xfc)==196);
    assert(f(items_face+0xec)==254&&f(items_face+0xfc)==174&&f(items_face+0xe0)==1.25f);
    assert(f(items_back+0xec)==261.5f&&f(items_back+0xfc)==176.5f);
    assert(f(return_layout+8+0xec)==10&&f(return_layout+8+0xfc)==182&&f(return_layout+8+0xe0)==.75f);
    hook.End(m);assert(m.words==buttons_routed);
    // Delayed native button motion is replaced by the grid phase, at both endpoints
    // and mid-transition; distinct button scales must cancel native Y correctly.
    for(float phase:{0.f,.25f,.5f,1.f})for(float native_y:{0.f,20.f,40.f}) {
        m.words=buttons_routed;
        for(auto node:{branches[5],masks_button,items_button,masks_back,items_back,masks_face,items_face})
            m.words[node+0xfc]=std::bit_cast<U32>(f(node+0xfc)+native_y);
        m.words[branches[5]+0xfc]=std::bit_cast<U32>(native_y);
        const auto before=m.words;
        hook.slide_active=true;hook.slide_remaining=phase;
        hook.Begin(m,panel);
        const float down=254*phase;
        assert(std::abs(f(masks_face+0xfc)-(197+down))<.001f);
        assert(std::abs(f(items_face+0xfc)-(174+down))<.001f);
        hook.End(m);assert(m.words==before);
    }
    hook.slide_active=false;hook.slide_remaining=0;
    // Unknown button trees fail before moving the grid or buttons between queues.
    m.words=before_buttons;m.words[branches[5]+0x18]=3;
    const auto invalid_buttons=m.words;hook.Pass(m,manager,5);
    assert(status==4&&m.words==invalid_buttons);
    m.words=before_buttons;buttons_enabled=false;hook.Pass(m,manager,5);
    assert(status==2);hook.Begin(m,panel);
    assert(m.words[branches[5]+0xd4]==15&&m.words[branches[6]+0xd4]==15);hook.End(m);
    // Shared brown backdrop moves from bottom pass 4 to early top pass 0,
    // only while this Items panel is actually submitted.
    m.words=original;
    auto bg=m.Layout("CommonBottomBg"),left=m.Widget("common_bottom_bg_pane0",0,0),right=m.Widget("common_bottom_bg_pane1",160,0);
    m.Children(bg+8,{left,right});m.words[page+4]=bg;
    auto bottom_bg=m.Alloc(0x848),top_bg=m.Alloc(0x848);
    m.words[manager+24]=bottom_bg;m.words[manager+8]=top_bg;
    m.Queue(bottom_bg,{bg});auto screenshot=m.Layout("CommonUpBg");m.Queue(top_bg,{screenshot});
    hook.Capture(m,page);assert(hook.background==bg);
    background_enabled=true;background_pose.x=12;background_pose.y=30;background_pose.scale=.75f;
    const auto bg_original=m.words;
    hook.Pass(m,manager,4);assert(hook.routed_background==bg);
    assert(m.words[bottom_bg+0x844]==0&&m.words[top_bg+0x844]==3);
    assert(m.words[top_bg+4+15*0x80]==bg&&m.words[top_bg+4+0x80]==screenshot);
    hook.Pass(m,manager,5);hook.Pass(m,manager,0);
    const auto bg_and_grid=m.words;
    hook.Begin(m,bg);assert(f(left+0xec)==12&&f(left+0xfc)==30);
    assert(f(right+0xec)==132&&f(right+0xfc)==30&&f(right+0xe0)==.75f);
    hook.End(m);assert(m.words==bg_and_grid);
    hook.Begin(m,bg);assert(f(left+0xec)==12&&f(left+0xfc)==30-240*.75f);
    hook.End(m);assert(m.words==bg_and_grid);
    // Closing Items can recycle its panel address for Map. Even with that
    // address submitted again, the shared brown backdrop must remain native.
    m.words=bg_original;
    const auto map_resource=m.Alloc(0x20);
    m.words[map_resource+0x18]=m.String("MenuMap");
    m.words[panel+4]=map_resource;
    const auto map_frame=m.words;
    hook.Pass(m,manager,4);
    assert(!hook.routed_background&&!hook.shadow_layout&&m.words==map_frame);
    hook.Pass(m,manager,5);
    assert(hook.routed.empty()&&m.words==map_frame);
    hook.Pass(m,manager,0);hook.Begin(m,bg);hook.End(m);
    assert(m.words==map_frame);
    m.words=bg_original;background_enabled=false;hook.Pass(m,manager,4);
    assert(!hook.routed_background&&m.words==bg_original);
    background_enabled=true;m.Queue(bottom,{});m.words[bottom+0x844]=2; // only the two cursors remain
    const auto no_items=m.words;hook.Pass(m,manager,4);assert(!hook.routed_background&&m.words==no_items);
    m.words=bg_original;m.words[top_bg+0x804+15*4]=32;m.words[top_bg+0x844]+=32;
    for(unsigned i=0;i<32;++i)m.words[top_bg+4+15*0x80+i*4]=screenshot;
    const auto bg_full=m.words;hook.Pass(m,manager,4);assert(!hook.routed_background&&m.words==bg_full);
    m.words=bg_original;m.words[bg+8+0x18]=500;
    const auto bg_invalid=m.words;hook.Pass(m,manager,4);assert(!hook.routed_background&&m.words==bg_invalid);
    m.words=bg_original;
    // Widen the captured-scene backdrop equally at both edges, without touching
    // height, alpha/visibility, ordinary layouts or the non-widescreen path.
    auto capture=m.Layout("UpCommonBG"),capture_all=m.Widget("all");
    auto left_group=m.Widget("picture_left_g"),right_group=m.Widget("picture_right_g");
    auto left_picture=m.Widget("picture_left_pane"),right_picture=m.Widget("picture_right_pane");
    m.Children(capture+8,{capture_all});m.Children(capture_all,{left_group,right_group});
    m.Children(left_group,{left_picture});m.Children(right_group,{right_picture});
    m.words[right_group+0xd4]=14; // native right-eye visibility stays hidden
    const auto capture_original=m.words;
    hook.Pass(m,manager,0);hook.Begin(m,capture,false);hook.End(m);assert(m.words==capture_original);
    hook.Begin(m,capture,true);
    for(U32 pane:{left_picture,right_picture}) {
        assert(std::abs(f(pane+0xec)+13.333333f)<.0001f);
        assert(std::abs(400*f(pane+0xe0)+f(pane+0xec)-413.333333f)<.0001f);
        assert(f(pane+0xf4)==1&&f(pane+0xfc)==0);
    }
    assert(m.words[right_group+0xd4]==14);
    hook.End(m);assert(m.words==capture_original);
    hook.Pass(m,manager,3);hook.Begin(m,capture,true);hook.End(m);assert(m.words==capture_original);
    // Scope previews to Items, with a shared model anchor for multipart items.
    MM3D::ItemPreview::Hook preview;
    auto controller=m.Alloc(16),owner=m.Alloc(0x180),context=m.Alloc(0x200);
    m.words[controller+4]=owner;m.words[owner+0x16c]=context;
    preview.Enter(m,page,controller);assert(preview.page==page&&preview.owner==owner);
    auto message=m.Layout("MessageWindow",0,0,0xa34),composition=m.Widget("all");
    m.Children(message+8,{composition});m.words[composition+8]=message+8;
    auto plate=m.Widget("plate"),text=m.Widget("text"),front=m.Widget("front");
    m.Children(composition,{front,text,plate});
    m.words[message+0x444]=context;m.words[message+0x3f4]=0x174c;
    assert(preview.Description(m,message));
    m.words[message+0x3f4]=0x1234;assert(!preview.Description(m,message));
    m.words[message+0x3f4]=0x174c;m.words[message+0x444]=context+4;
    assert(!preview.Description(m,message));m.words[message+0x444]=context;
    const auto bits=[](float f){return std::bit_cast<U32>(f);};
    m.words[composition+0x8c]=bits(200);m.words[composition+0x90]=bits(180);
    m.words[composition+0x98]=m.words[composition+0x9c]=bits(1);
    m.words[plate+0x98]=bits(1);
    MM3D::DialogueScale::scale=.9f;MM3D::DialogueScale::y_offset=12;
    MM3D::DialogueScale::background_width=1.4f;
    MM3D::DialogueScale::item_scale=.5f;MM3D::DialogueScale::item_x=60;MM3D::DialogueScale::item_y=-30;
    MM3D::DialogueScale::Hook description;
    description.Begin(m,message,preview.Description(m,message));
    assert(f(composition+0x98)==.5f&&f(composition+0x8c)==260&&f(composition+0x90)==150);
    assert(f(plate+0x98)==1.4f); // descriptions share the dialogue background width
    description.End(m,message);
    assert(f(composition+0x98)==1&&f(composition+0x8c)==200&&f(composition+0x90)==180);
    const MM3D::GearMenu::Pose present_pose{-40,25,.75f};
    description.Begin(m,message,false,false,&present_pose);
    assert(f(composition+0x98)==.75f&&f(composition+0x8c)==160&&f(composition+0x90)==205);
    assert(f(plate+0x98)==1.4f);description.End(m,message);
    assert(f(composition+0x98)==1&&f(composition+0x8c)==200&&f(composition+0x90)==180);
    description.Begin(m,message,true);
    assert(f(composition+0x98)==.5f&&f(composition+0x8c)==260&&f(composition+0x90)==150);
    description.End(m,message);
    // Dialogue, Items/Masks, Gear, and explicit description poses (Maps,
    // item-giving, song reset) share width while retaining their own text scale.
    for(float width:{1.f,1.25f,2.f})for(unsigned mode=0;mode<4;++mode) {
        MM3D::DialogueScale::background_width=width;
        description.Begin(m,message,mode==1||mode==2,mode==2,mode==3?&present_pose:nullptr);
        assert(f(plate+0x98)==width);
        assert(f(text+0x98)==0&&f(front+0x98)==0); // no glyph/foreground stretching
        description.End(m,message);assert(f(plate+0x98)==1.f);
    }
    MM3D::DialogueScale::background_width=1.4f;
    auto matrix=m.Alloc(48);
    std::array<float,12> native{0,-2,0,0,2,0,0,1.25f,0,0,2,105.8f};
    for(unsigned i=0;i<12;++i)m.words[matrix+i*4]=bits(native[i]);
    MM3D::ItemPreview::model_scale=.5f;MM3D::ItemPreview::model_x=3;MM3D::ItemPreview::model_y=2;
    auto before_model=m.words;
    preview.ModelFrame(owner);preview.ModelBegin(m,owner,matrix);
    assert(f(matrix+4)==-1&&f(matrix+16)==1); // rotation retained
    assert(f(matrix+12)==3&&f(matrix+28)==-.75f&&f(matrix+44)==105.8f);
    preview.ModelEnd(m);assert(m.words==before_model);
    m.words[matrix+12]=bits(2);m.words[matrix+28]=bits(3.25f);
    before_model=m.words;preview.ModelBegin(m,owner,matrix);
    assert(f(matrix+12)==4&&f(matrix+28)==.25f); // second part shares first pivot
    preview.ModelEnd(m);assert(m.words==before_model);
    preview.ModelFrame(owner+4);preview.ModelBegin(m,owner+4,matrix);preview.ModelEnd(m);
    assert(m.words==before_model);
    preview.Leave(page);assert(!preview.Description(m,message));
    preview.ModelFrame(owner);preview.ModelBegin(m,owner,matrix);preview.ModelEnd(m);
    assert(m.words==before_model);
    description.Begin(m,message,preview.Description(m,message));
    assert(f(composition+0x98)==.9f&&f(composition+0x90)==192&&f(plate+0x98)==1.4f);
    description.End(m,message);
    auto masks=m.Layout("MenuMask");m.words[page+0xc0]=masks;
    preview.Enter(m,page,controller);assert(preview.page==page&&preview.owner==owner);
    preview.ModelFrame(owner);before_model=m.words;preview.ModelBegin(m,owner,matrix);
    assert(f(matrix+4)==-1&&f(matrix+12)==5);
    preview.ModelEnd(m);assert(m.words==before_model);
    assert(preview.Description(m,message));
    auto other=m.Layout("OtherMenu");m.words[page+0xc0]=other;
    preview.Enter(m,page,controller);assert(!preview.page);

    // Masks owns 24 icons, distinct panel branches, and the same cursor layouts.
    // In particular, selection of the last four masks must follow the grid.
    Memory mm;Hook mh;buttons_enabled=false;bar_enabled=false;
    auto mp=mm.Alloc(0x200),ml=mm.Layout("MenuMask"),ma=mm.Widget("all");
    mm.Children(ml+8,{ma});mm.words[mp+0xc0]=ml;
    std::vector<U32> mb;
    for(auto name:{"set_frame3_g","set_frame2_g","set_frame1_g","set_frame0_g","mask_set_pos_g","btn_item_omen_g","btn_return_l","mask_panel_g","right_item_set_l"})
        mb.push_back(mm.Widget(name,100,60));
    mm.Children(ma,mb);
    std::vector<U32> mi{ml};
    for(unsigned i=0;i<24;++i) {
        auto icon=mm.Layout("ItemIconButton",20.f+i*4,28);
        mm.words[mp+0xd0+i*4]=icon;mi.push_back(icon);
    }
    auto mc=mm.Layout("CursorItemCollect",112,28);mm.words[mp+8]=mc;
    auto mgr=mm.Alloc(0x30),bot=mm.Alloc(0x848),up=mm.Alloc(0x848);
    mm.words[mgr+28]=bot;mm.words[mgr+20]=up;mm.Queue(bot,mi);
    mm.words[bot+4+6*0x80]=mc;mm.words[bot+0x804+6*4]=1;mm.words[bot+0x844]++;
    mh.Capture(mm,mp);assert(mh.panel==ml&&mh.icons[23]==mi.back());
    x=10;y=20;scale=.5f;mh.Pass(mm,mgr,5);
    assert(status==2&&routed_icons==24&&mm.words[up+0x844]==26&&mm.words[bot+0x844]==1);
    auto mask_routed=mm.words;mh.Begin(mm,ml);
    assert(mm.words[mb[7]+0xd4]==14);mh.End(mm);assert(mm.words==mask_routed);
    mh.Pass(mm,mgr,3);mh.Begin(mm,ml);
    assert(std::abs(std::bit_cast<float>(mm.words[mb[7]+0xec])-77.f)<.001f);
    assert(std::abs(std::bit_cast<float>(mm.words[mb[7]+0xfc])-53.f)<.001f);
    assert(std::bit_cast<float>(mm.words[mb[4]+0xe0])==.6f);
    mh.End(mm);assert(mm.words==mask_routed);
    for(U32 layout:{mc,mi.back()}) {
        mh.Begin(mm,layout);assert(std::abs(std::bit_cast<float>(mm.words[layout+8+0xec])-84.2f)<.001f);
        mh.End(mm);assert(mm.words==mask_routed);
    }
    // A missing 24th icon rejects capture rather than leaving a partial page.
    mm.words[mp+0xd0+23*4]=0;mh.Capture(mm,mp);assert(!mh.panel);

    // Gear's icons are embedded in two branches, not separately submitted icons.
    MM3D::GearMenu::Options go;
    go.poses[MM3D::GearMenu::Background]={-19,99,1.8f}; // Legacy Gear pose must not override any shared background coordinate.
    go.horizontal=false;
    go.poses[MM3D::GearMenu::Panel]={10,20,.5f};
    go.poses[MM3D::GearMenu::Right]={-30,15,.75f};
    go.poses[MM3D::GearMenu::Model]={4,2,.5f};
    go.poses[MM3D::GearMenu::Description]={20,-10,.6f};
    MM3D::GearMenu::Apply(go);
    Memory gm;Hook gh;
    auto gp=gm.Alloc(0x200),gl=gm.Layout("MenuCollect"),ga=gm.Widget("all");
    gm.Children(gl+8,{ga});gm.words[gp+0x14]=gl;
    std::vector<U32> gb;
    for(auto name:{"btn_return_top_g","left_base_g","right_base_g","btn_option_g","btn_return_l"}) gb.push_back(gm.Widget(name));
    gm.Children(ga,gb);
    auto anchor_node=gm.Widget("icon_sword_l",180,29),embedded_icon=gm.Layout("ItemIconButton",180,29);
    gm.words[anchor_node+0x20]=embedded_icon;gm.Children(gb[1],{anchor_node});
    auto event_node=gm.Widget("icon_event00_l",290,29);gm.Children(gb[2],{event_node});
    auto gc=gm.Layout("CursorItemCollect",290,29);gm.words[gp+0x130]=gc;
    auto gbg=gm.Layout("CommonBottomBg");auto gright=gm.Brown(gbg);gm.words[gp+0x18]=gbg;
    auto gmanager=gm.Alloc(0x30),g5=gm.Alloc(0x848),g3=gm.Alloc(0x848),g4=gm.Alloc(0x848),g0=gm.Alloc(0x848);
    gm.words[gmanager+28]=g5;gm.words[gmanager+20]=g3;gm.words[gmanager+24]=g4;gm.words[gmanager+8]=g0;
    auto g1=gm.Alloc(0x848);gm.words[gmanager+12]=g1;
    gm.Queue(g5,{gl,gc});gm.Queue(g4,{gbg});
    gh.CaptureGear(gm,gp);assert(gh.gear&&gh.panel==gl&&gh.background==gbg);
    const auto gear_original=gm.words;
    MM3D::GearMenu::enabled=false;gh.Pass(gm,gmanager,4);gh.Pass(gm,gmanager,5);assert(gm.words==gear_original);
    MM3D::GearMenu::enabled=true;gh.Pass(gm,gmanager,4);gh.Pass(gm,gmanager,5);
    assert(gh.routed_background==gbg&&gm.words[g3+0x844]==2&&gm.words[g4+0x844]==0);
    assert(gm.words[g0+0x844]==0&&gm.words[g1+0x844]==26&&gh.background_pass==1);
    auto before_bg=gm.words;gh.Pass(gm,gmanager,1);
    for(unsigned draw=0;draw<26;++draw) {
        gh.Begin(gm,gbg);
        assert(std::bit_cast<float>(gm.words[gright+0xfc])==background_pose.y.load()-(draw%2?240*background_pose.scale.load():0.f));
        if(draw>=24)assert(std::bit_cast<float>(gm.words[gright+0xec])==background_pose.x.load());
        if(draw<24)assert(gm.words[gright+0x128]==0);
        gh.End(gm);assert(gm.words==before_bg);
    }
    {
        const auto saved_hook=gh;
        gm.words=gear_original;gh.gear_slide_x=-600;
        gh.BackgroundPass(gm,gmanager);
        assert(gh.routed_background==gbg);
        assert(std::abs(gh.frame_background.x-(std::clamp(background_pose.x.load(),-400.f,400.f)-600))<.001f);
        gh=saved_hook;gm.words=before_bg;
    }
    gh.pass=5; // Inspect the already-routed bottom emission without submitting twice.
    auto gear_routed=gm.words;gh.Begin(gm,gl);assert(gm.words[gb[1]+0xd4]==14);
    gh.End(gm);assert(gm.words==gear_routed);
    gh.Pass(gm,gmanager,3);gh.Begin(gm,gl);
    assert(std::bit_cast<float>(gm.words[embedded_icon+8+0xec])==100.f);
    assert(std::bit_cast<float>(gm.words[event_node+0xec])==252.5f);
    gh.End(gm);assert(gm.words==gear_routed);
    gh.Begin(gm,gc);assert(std::bit_cast<float>(gm.words[gc+8+0xec])==252.5f);
    gh.End(gm);assert(gm.words==gear_routed);
    gm.words=gear_original;gm.words[gp+0x194]=4u<<8;gh.Pass(gm,gmanager,5);gh.Pass(gm,gmanager,3);
    gh.Begin(gm,gc);assert(std::bit_cast<float>(gm.words[gc+8+0xec])==155.f);gh.End(gm);
    // Rotate only strip artwork. Four embedded slot roots and cursor stay upright.
    gm.words=gear_original;
    std::vector<U32> strip;
    for(unsigned i=0;i<4;++i) {
        const auto name="icon_event0"+std::to_string(i)+"_l";
        strip.push_back(gm.Widget(name.c_str(),290,29+47*i));
    }
    auto strip_bg=gm.Widget("right_base_pane",262,0);strip.push_back(strip_bg);gm.Children(gb[2],strip);
    MM3D::GearMenu::horizontal=true;gh.CaptureGear(gm,gp);gh.Pass(gm,gmanager,5);
    assert(status==2);auto horizontal_memory=gm.words;
    gh.Pass(gm,gmanager,3);gh.Begin(gm,gl);
    assert(std::bit_cast<float>(gm.words[strip_bg+0xe0])==0);
    assert(std::bit_cast<float>(gm.words[strip_bg+0xe4])==.75f);
    for(unsigned i=0;i<4;++i) {
        assert(std::bit_cast<float>(gm.words[strip[i]+0xe0])==.75f);
        assert(std::bit_cast<float>(gm.words[strip[i]+0xe4])==0);
        assert(std::bit_cast<float>(gm.words[strip[i]+0xec])==230.f+.75f*(29+47*i));
        assert(std::bit_cast<float>(gm.words[strip[i]+0xfc])==36.f);
    }
    gh.End(gm);assert(gm.words==horizontal_memory);
    gh.Begin(gm,gc);assert(std::bit_cast<float>(gm.words[gc+8+0xec])==251.75f);
    assert(std::bit_cast<float>(gm.words[gc+8+0xfc])==36.f);
    assert(std::bit_cast<float>(gm.words[gc+8+0xe4])==0);gh.End(gm);assert(gm.words==horizontal_memory);
    // Gear preview scope uses the shared Items/Masks model and description controls.
    auto gear_layout=m.Layout("MenuCollect");m.words[page+0x14]=gear_layout;
    preview.EnterGear(m,page,controller);assert(preview.gear&&preview.owner==owner);
    before_model=m.words;preview.ModelFrame(owner);preview.ModelBegin(m,owner,matrix);
    assert(f(matrix+12)==5&&f(matrix+4)==-1);preview.ModelEnd(m);assert(m.words==before_model);
    assert(preview.Description(m,message));
    const auto before_gear_description=m.words;
    description.Begin(m,message,true,true);
    assert(f(composition+0x8c)==260&&f(composition+0x90)==150&&f(composition+0x98)==.5f);
    description.End(m,message);assert(m.words==before_gear_description);
    preview.Leave(page);assert(!preview.Description(m,message));
    // Notebook relocates only description/bar branches; rows and categories
    // already on top receive the same list transform as its frame and cursor.
    Memory nm;MM3D::NativeNotebook::Hook nh;MM3D::NativeNotebook::Options no;
    no.poses={{{20,30,.5f},{10,20,.5f},{40,5,.75f}}};MM3D::NativeNotebook::Apply(no);
    auto np=nm.Alloc(0x800),nl=nm.Layout("MenuQuestlist"),na=nm.Widget("all");
    nm.words[np+0x40]=nl;nm.Children(nl+8,{na});std::vector<U32> nb;
    for(auto name:{"attention_effect_g","touch_rect_window00_l","event_item_g","quest_list_detail","btn_return_l","btn_map_g","line_base_g","bb_btn_schedule_g"})nb.push_back(nm.Widget(name,100,60));
    nm.Children(na,nb);
    const auto add_n=[&](U32 off,const char* name){auto l=nm.Layout(name,100,60);nm.words[np+off]=l;return l;};
    auto up_list=add_n(0x34,"UpQuestlist");std::vector<U32> rows{up_list};
    for(unsigned i=0;i<9;++i)rows.push_back(add_n(0x7c+4*i,"QuestPlate"));
    for(unsigned i=0;i<3;++i)rows.push_back(add_n(0x1c8+4*i,"QuestCategoryPlate"));
    rows.push_back(add_n(0x1d4,"CursorQuestPlate"));
    std::vector<U32> bottom_entries{nl};
    for(unsigned i=0;i<6;++i)bottom_entries.push_back(add_n(0x278+4*i,"ChrIconButton"));
    bottom_entries.push_back(add_n(0x350,"CursorEventCharSeal"));
    auto nmanager=nm.Alloc(0x30),n5=nm.Alloc(0x848),n3=nm.Alloc(0x848);
    nm.words[nmanager+28]=n5;nm.words[nmanager+20]=n3;nm.Queue(n5,bottom_entries);
    nh.Capture(nm,np);assert(nh.panel==nl&&nh.list.size()==14&&nh.bar.size()==7);
    auto n_original=nm.words;MM3D::NativeNotebook::enabled=false;nh.Pass(nm,nmanager,5);assert(nm.words==n_original);
    MM3D::NativeNotebook::enabled=true;nh.Pass(nm,nmanager,5);
    assert(nh.active&&nm.words[n5+0x844]==1&&nm.words[n3+0x844]==8);
    auto n_routed=nm.words;nh.Begin(nm,nl);
    assert(nm.words[nb[3]+0xd4]==14&&nm.words[nb[4]+0xd4]==15);nh.End(nm);assert(nm.words==n_routed);
    nh.Pass(nm,nmanager,3);nh.Begin(nm,nl);
    assert(nm.words[nb[4]+0xd4]==14&&std::bit_cast<float>(nm.words[nb[3]+0xec])==60);
    assert(std::bit_cast<float>(nm.words[nb[6]+0xec])==60);nh.End(nm);assert(nm.words==n_routed);
    nh.Begin(nm,bottom_entries[1]);assert(std::bit_cast<float>(nm.words[bottom_entries[1]+8+0xec])==60);nh.End(nm);
    nh.Pass(nm,nmanager,2);
    for(auto row:rows){nh.Begin(nm,row);assert(std::bit_cast<float>(nm.words[row+8+0xec])==70);nh.End(nm);}
    assert(nm.words==n_routed);
    nm.words=n_original;nm.words[na+0x18]=7;auto n_invalid=nm.words;nh.Pass(nm,nmanager,5);assert(!nh.active&&nm.words==n_invalid);
    nm.words=n_original;nm.words[n5+0x808]=0;nm.words[n5+0x844]=0;nh.Pass(nm,nmanager,5);assert(!nh.active);
    nm.words=n_original;
    auto sp=nm.Layout("MenuSchedule"),sa=nm.Widget("all");nm.Children(sp+8,{sa});
    std::vector<U32> sb;
    for(auto name:{"current_time01","elf_seal_free","elf_seal_fix","char_seal_effect","char_seal_g",
                  "alarm_set_anim_g","event_time_g","popup_window00_g","black_screen_g","sche_base_anim",
                  "sche_quest_detail","under_tub_g","touch_rect_alarm_l","touch_rect_window00_l","touch_rect_center_l"})
        sb.push_back(nm.Widget(name,100,60));
    nm.Children(sa,sb);
    auto su=nm.Layout("UpSchedule",100,60),sc=nm.Layout("CursorScheFrame",100,60);
    auto pointer=nm.Layout("CursorPointer",100,60),touch=nm.Layout("CursorTouch",100,60);
    nm.words[np+0x44]=sp;nm.words[np+0x38]=su;nm.words[np+0x508]=sc;
    nm.words[np+0x514]=pointer;nm.words[np+0x6b0]=touch;
    nm.Queue(n5,{sp,touch});nm.Queue(n3,{});
    auto n2=nm.Alloc(0x848);nm.words[nmanager+16]=n2;nm.Queue(n2,{su,sc,pointer});
    no.poses[3]={10,20,.5f};no.poses[4]={200,30,.5f};MM3D::NativeNotebook::Apply(no);
    nh.Capture(nm,0);assert(!nh.panel&&!nh.schedule_panel);nh.Pass(nm,nmanager,5);
    assert(nh.active&&nh.schedule&&nm.words[n5+0x844]==1&&nm.words[n3+0x844]==2);
    assert(nh.frame[4].scale==nh.frame[1].scale);
    assert(nh.frame[4].x==nh.frame[1].x);
    assert(nh.frame[4].y==nh.frame[1].y);
    auto schedule_memory=nm.words;
    nh.Begin(nm,sp);assert(nm.words[sb[10]+0xd4]==14&&nm.words[sb[11]+0xd4]==15);nh.End(nm);
    nh.Pass(nm,nmanager,3);nh.Begin(nm,sp);
    assert(std::bit_cast<float>(nm.words[sb[10]+0xec])==60);
    assert(std::bit_cast<float>(nm.words[sb[9]+0xec])==60);
    assert(nm.words[sb[11]+0xd4]==14);nh.End(nm);
    nh.Pass(nm,nmanager,2);
    for(auto layout:{su,sc,pointer}){nh.Begin(nm,layout);assert(std::bit_cast<float>(nm.words[layout+8+0xec])==60);nh.End(nm);}
    assert(nm.words==schedule_memory);
    // Fixed Schedule detail: normalize all three days, clip the neighboring
    // panes and crop cross-day bars without changing shared pane resources.
    {
        auto attachment=nm.Widget("sche_base_anim_null"),pane=nm.Widget("event_bar");
        auto geometry=nm.Alloc(0x80);nm.words[geometry+4]=4;
        auto put=[&](U32 at,float v){nm.words[at]=std::bit_cast<U32>(v);};
        auto value=[&](U32 at){return std::bit_cast<float>(nm.words[at]);};
        nm.words[pane+0x28]=geometry;put(geometry+0x14,0);put(geometry+0x20,876);
        put(geometry+0x28,.1f);put(geometry+0x30,.8f);put(pane+0x110,1);put(pane+0x120,1);
        nm.Children(sb[9],{attachment});nm.Children(attachment,{pane});
        nm.words[sp+0x23c]=attachment;put(sa+0xec,0);
        for(unsigned col:{1u,2u,3u,4u,5u})for(float drift:{0.f,-83.f,32.f}) {
            nm.words[np+0x10]=col;const float slide=-292.f*((col-1)/2)+drift;
            put(sb[9]+0xec,slide);put(pane+0xec,15+slide);
            put(sb[0]+0xec,100+slide+292.f*((col-1)/2));
            auto before=nm.words;std::vector<Hook::Write> writes;
            assert(MM3D::ScheduleDetail::Prepare(nm,sp,np,nh.helper,writes));assert(nm.words==before);
            for(auto w:writes)nm.Write(w.address,w.after);
            assert(std::abs(value(pane+0xec)-15)<.0001f);
            assert(std::abs(value(sb[0]+0xec)-100)<.0001f);
            assert(std::abs(value(pane+0xe0)*876-290)<.0001f);
            const float expected=.1f+292.f*((col-1)/2)/876.f*.8f;
            assert(std::abs(value(pane+0x110)*.1f+value(pane+0x118)-expected)<.0001f);
            for(auto i=writes.rbegin();i!=writes.rend();++i)nm.Write(i->address,i->before);
            assert(nm.words==before);
        }
        // Both fairy variants belong to a day of the full timeline. Keep
        // only that day's seal visible; restore all draw-time changes exactly.
        for(unsigned i:{1u,2u})nm.words[sb[i]+0x20]=nm.Layout("ChatSeal");
        for(unsigned selected_day=0;selected_day<3;++selected_day)
            for(unsigned fairy_day=0;fairy_day<3;++fairy_day) {
                nm.words[np+0x10]=1+2*selected_day;
                put(sb[9]+0xec,-292.f*selected_day);
                for(unsigned i:{1u,2u}) {
                    put(sb[i]+0xec,0.f); // stationary attachment
                    put(nm.words[sb[i]+0x20]+8+0xec,100.f+292.f*fairy_day-292.f*selected_day);
                }
                auto before=nm.words;std::vector<Hook::Write> fairy_writes;
                assert(MM3D::ScheduleDetail::Prepare(nm,sp,np,nh.helper,fairy_writes));
                for(auto w:fairy_writes)nm.Write(w.address,w.after);
                for(unsigned i:{1u,2u})assert(bool(nm.words[sb[i]+0xd4]&1)==(selected_day==fairy_day));
                for(auto i=fairy_writes.rbegin();i!=fairy_writes.rend();++i)nm.Write(i->address,i->before);
                assert(nm.words==before);
            }
        std::vector<Hook::Write> writes;put(pane+0xec,-500);put(geometry+0x20,100);
        assert(MM3D::ScheduleDetail::Clip(nm,pane,15,305,writes));
        for(auto w:writes)nm.Write(w.address,w.after);assert(!(nm.words[pane+0xd4]&1));
        for(auto i=writes.rbegin();i!=writes.rend();++i)nm.Write(i->address,i->before);
        nm.words[np+0x10]=0;writes.clear();auto portrait_before=nm.words;
        assert(MM3D::ScheduleDetail::Prepare(nm,sp,np,nh.helper,writes));
        for(auto w:writes)nm.Write(w.address,w.after);
        assert(!(nm.words[sb[9]+0xd4]&1)&&!(nm.words[sb[0]+0xd4]&1));
        for(auto i=writes.rbegin();i!=writes.rend();++i)nm.Write(i->address,i->before);
        assert(nm.words==portrait_before);
        nm.words[np+0x10]=3;nm.words[pane+0x28]=0x123400;auto before=nm.words;
        assert(!MM3D::ScheduleDetail::Prepare(nm,sp,np,nh.helper,writes));assert(nm.words==before);
    }
    {
        struct FadeMemory : Memory {
            unsigned invalid_reads=0;
            std::optional<U32> Read(U32 at) {
                if(!Pointer(at))++invalid_reads;
                return Memory::Read(at);
            }
        } fade;
        MM3D::NativeNotebook::Hook h;
        for(U32 owner:{0u,3u,0xfffffffcu}) {
            h.notebook_owner=owner;h.detail_opacity=0;
            h.MapFade(fade);
            assert(h.detail_opacity==1 && fade.invalid_reads==0);
        }
        auto owner=fade.Alloc(0x800),map=fade.Layout("UpEventMap"),all=fade.Widget("all");
        auto board=fade.Widget("blackboard_g"),content=fade.Widget("event_map_g"),player=fade.Alloc(0x20);
        fade.Children(map+8,{all});fade.Children(all,{board,content});
        fade.words[owner+0x54]=map;fade.words[owner+0x58]=player;fade.words[owner+0x4c]=1;h.notebook_owner=owner;
        auto details=fade.Widget("details"),child=fade.Widget("icon");fade.Children(details,{child});
        for(auto node:{details,child})for(U32 off:{0x128u,0x12cu,0x130u,0x134u})fade.words[node+off]=std::bit_cast<U32>(.8f);
        for(float alpha:{0.f,.25f,1.f,.5f,0.f}) {
            fade.words[board+0x134]=std::bit_cast<U32>(alpha);h.MapFade(fade);
            assert(h.detail_opacity==1-alpha);auto before=fade.words;
            std::vector<Hook::Write> writes;std::vector<U32> seen;
            assert(h.DetailTransform(fade,details,writes,seen,{1,0,0,1,0,0}));
            for(auto w:writes)fade.Write(w.address,w.after);
            assert(std::abs(std::bit_cast<float>(fade.words[child+0x134])-.8f*(1-alpha))<.0001f);
            for(auto i=writes.rbegin();i!=writes.rend();++i)fade.Write(i->address,i->before);
            assert(fade.words==before);
        }
        fade.words[owner+0x4c]=255;fade.words[board+0x134]=std::bit_cast<U32>(1.f);
        h.MapFade(fade);assert(h.detail_opacity==1); // stale hidden-map alpha ignored
        fade.words[player+8]=1;h.MapFade(fade);assert(h.detail_opacity==0); // closing still fades
        fade.words[owner+0x58]=0;h.MapFade(fade);
        assert(h.detail_opacity==1 && fade.invalid_reads==0);
    }
    {
        Memory m;MM3D::NativeNotebook::Hook h;h.frame[10]={240,50,.5f};
        auto popup=m.Widget("popup_window00_g",160,112),box=m.Widget("popup_window00",160,112);
        auto pane=m.Widget("box_corner",180,132),text=m.Widget("e_popup_g",160,112);
        m.Children(popup,{box,text});m.Children(box,{pane});
        auto before=m.words;std::vector<Hook::Write> writes;std::vector<U32> seen;
        MM3D::NativeNotebook::popup_width=2;MM3D::NativeNotebook::popup_height=3;
        assert(h.Popup(m,popup,writes,seen));assert(m.words==before);
        for(auto w:writes)m.Write(w.address,w.after);
        assert(std::bit_cast<float>(m.words[pane+0xec])==340);
        assert(std::bit_cast<float>(m.words[pane+0xfc])==136);
        assert(std::bit_cast<float>(m.words[text+0xe0])==.5f);
        for(auto i=writes.rbegin();i!=writes.rend();++i)m.Write(i->address,i->before);
        assert(m.words==before);
        MM3D::NativeNotebook::popup_width=1;MM3D::NativeNotebook::popup_height=1;
    }
    Memory fm;MM3D::NativeFileSelect::Hook fh;MM3D::NativeFileSelect::Options fo;
    fo.poses={{{50,10,.5f},{10,20,.5f},{30,40,.75f},{80,15,.6f}}};MM3D::NativeFileSelect::Apply(fo);
    auto fl=fm.Layout("FileSelect"),fa=fm.Widget("all");fm.Children(fl+8,{fa});std::vector<U32> fb;
    for(auto name:{"cursor_g","black_screen","popup_window_g","message_g","delete_g","copy_g","start_g","plate_detail_g","null_plate_g","btn_return_l"})fb.push_back(fm.Widget(name,100,60));
    fm.Children(fa,fb);std::vector<U32> fc;
    for(auto name:{"cursor_option_g","cursor_copy_del_g","cursor_start_g","cursor_plate_g"})fc.push_back(fm.Widget(name,100,60));
    fm.Children(fb[0],fc);
    std::vector<U32> stones;
    for(unsigned i=0;i<3;++i) {
        auto node=fm.Widget("plate",100,50+60*i),stone=fm.Layout("FilePlate",100,50+60*i);
        fm.words[node+0x20]=stone;stones.push_back(stone);
        if(i==0)fm.Children(fb[8],{node});
        else {auto old=fm.words[fb[8]+0x14];std::vector<U32> ns;
            for(unsigned j=0;j<i;++j)ns.push_back(fm.words[old+4*j]);ns.push_back(node);fm.Children(fb[8],ns);}
    }
    auto fs=fm.Layout("UpPlateDetail",100,60),fmanager=fm.Alloc(0x30),f5=fm.Alloc(0x848),f3=fm.Alloc(0x848),f2=fm.Alloc(0x848);
    fm.words[fmanager+28]=f5;fm.words[fmanager+20]=f3;fm.words[fmanager+16]=f2;
    fm.Queue(f5,{fl});fm.Queue(f2,{fs});auto f_original=fm.words;
    MM3D::NativeFileSelect::enabled=false;fh.Pass(fm,fmanager,5);assert(!fh.active&&fm.words==f_original);
    MM3D::NativeFileSelect::enabled=true;fh.Pass(fm,fmanager,5);assert(fh.active&&fm.words[f5+0x844]==1&&fm.words[f3+0x844]==2);
    assert(fm.words[f3+4+0x80]==fl&&fm.words[f3+8+0x80]==fs);
    assert(fm.words[f2+0x844]==0&&fm.words[f2+0x808]==0);
    auto f_routed=fm.words;fh.Begin(fm,fl);
    assert(fm.words[fb[9]+0xd4]==15&&fm.words[fb[8]+0xd4]==14);fh.End(fm);assert(fm.words==f_routed);
    fh.Pass(fm,fmanager,3);fh.Begin(fm,fl);
    assert(fm.words[fb[9]+0xd4]==14&&fm.words[fb[1]+0xd4]==14);
    assert(std::bit_cast<float>(fm.words[fb[3]+0xec])==105);
    assert(std::bit_cast<float>(fm.words[fb[6]+0xec])==140);
    assert(std::bit_cast<float>(fm.words[fc[3]+0xec])==60);
    assert(std::bit_cast<float>(fm.words[fc[0]+0xec])==140);
    for(auto stone:stones)assert(std::bit_cast<float>(fm.words[stone+8+0xec])==60);
    fh.End(fm);assert(fm.words==f_routed);
    const auto cursor_base=fm.Widget("cursor_base"),cursor_pane=fm.Widget("corner"),cursor_rect=fm.Alloc(0x80);
    fm.Children(fc[3],{cursor_base});fm.Children(cursor_base,{cursor_pane});fm.words[cursor_pane+0x28]=cursor_rect;
    fm.words[cursor_rect+0x20]=std::bit_cast<U32>(8.f);fm.words[cursor_rect+0x24]=std::bit_cast<U32>(8.f);
    f_routed=fm.words;
    fh.frame_spacing=30;
    for(unsigned selected=0;selected<3;++selected) {
        fm.words=f_routed;fm.words[fc[3]+0xfc]=0;
        fm.words[cursor_pane+0xfc]=std::bit_cast<U32>(48.f+60*selected);
        const auto before=fm.words;fh.Begin(fm,fl);
        for(unsigned i=0;i<3;++i)
            assert(std::bit_cast<float>(fm.words[stones[i]+8+0xfc])==20+.5f*(50+90*i));
        assert(std::bit_cast<float>(fm.words[cursor_pane+0xfc])==20+.5f*(48+90*selected));
        fh.End(fm);assert(fm.words==before);
    }
    fh.frame_spacing=0;fm.words=f_routed;
    fh.Pass(fm,fmanager,3);fh.Begin(fm,fs);assert(std::bit_cast<float>(fm.words[fs+8+0xec])==100*fh.frame[MM3D::NativeFileSelect::Summary].scale+fh.Mapping(MM3D::NativeFileSelect::Summary).tx);
    assert(fh.Mapping(MM3D::NativeFileSelect::Summary).tx+400*fh.frame[MM3D::NativeFileSelect::Summary].scale<=-32);
    fh.End(fm);assert(fm.words==f_routed);
    // Relocated stones, prompt/actions and cursors follow the native exit cover.
    fm.words=f_original;
    namespace Startup=MM3D::NativeStartup;
    for(auto [at,word]:Startup::FadeGuards)fm.words[at]=word;
    auto cover=fm.Alloc(0x244);
    fm.words[Startup::FadeManager+8]=1;fm.words[Startup::FadeManager+0x338]=cover;
    fm.words[Startup::FadeManager+0x30]=1;fm.words[Startup::FadeManager+0x2f78]=cover;
    fm.words[cover+0xc]=std::bit_cast<U32>(320.f);fm.words[cover+0x10]=std::bit_cast<U32>(240.f);
    std::vector<U32> faded{fb[3],fb[4],fb[5],fb[6],fb[8]};
    faded.insert(faded.end(),fc.begin(),fc.end());
    for(auto stone:stones)faded.push_back(stone+8);
    for(auto node:faded)for(U32 off:{0x128u,0x12cu,0x130u,0x134u})fm.words[node+off]=std::bit_cast<U32>(.8f);
    const auto file_fade=fm.words;
    for(float alpha:{0.f,.5f,1.f,.25f}) {
        fm.words=file_fade;fm.words[cover+0x16c]=std::bit_cast<U32>(alpha);
        fh.Pass(fm,fmanager,5);assert(fh.active);auto before=fm.words;
        fh.Begin(fm,fl);for(auto node:faded)assert(fm.words[node+0x134]==before.at(node+0x134));
        fh.End(fm);assert(fm.words==before);
        fh.Pass(fm,fmanager,3);fh.Begin(fm,fl);
        for(auto node:faded)for(U32 off:{0x128u,0x12cu,0x130u,0x134u})
            assert(std::bit_cast<float>(fm.words[node+off])==.8f*(1-alpha));
        assert(fm.words[fb[9]+0x134]==before.at(fb[9]+0x134));
        assert(fm.words[fs+8+0x134]==before.at(fs+8+0x134));
        fh.End(fm);assert(fm.words==before);
    }
    // File selection uses the layout's animated black_screen, independently
    // of the global fade queue (the original regression missed this path).
    for(bool shown:{false,true})for(float alpha:{0.f,.5f,1.f,.25f}) {
        fm.words=file_fade;fm.words[Startup::FadeManager+0x30]=0;
        fm.words[fb[1]+0xd4]=shown?15:14;
        for(U32 off:{0x128u,0x12cu,0x130u,0x134u})fm.words[fb[1]+off]=std::bit_cast<U32>(alpha);
        fh.Pass(fm,fmanager,5);assert(fh.active);auto before=fm.words;
        fh.Pass(fm,fmanager,3);fh.Begin(fm,fl);
        for(auto node:faded)for(U32 off:{0x128u,0x12cu,0x130u,0x134u})
            assert(std::bit_cast<float>(fm.words[node+off])==.8f*(shown?1-alpha:1.f));
        fh.End(fm);assert(fm.words==before);
    }
    fm.words=f_original;fm.words[fa+0x18]=9;auto f_invalid=fm.words;fh.Pass(fm,fmanager,5);assert(!fh.active&&fm.words==f_invalid);
    fm.words=f_original;auto unrelated_file=fm.Layout("Other");fm.Queue(f3,std::vector<U32>(32,unrelated_file));auto f_full=fm.words;
    fh.Pass(fm,fmanager,5);assert(!fh.active&&fm.words==f_full);
    // One free slot cannot fit both the stones and the summary: no partial routing.
    fm.words=f_original;fm.Queue(f3,std::vector<U32>(31,unrelated_file));f_full=fm.words;
    fh.Pass(fm,fmanager,5);assert(!fh.active&&fm.words==f_full);
    // Preserve unrelated detail/top entries and put the summary after the stones.
    fm.words=f_original;fm.Queue(f2,{unrelated_file,fs,unrelated_file});fm.Queue(f3,{unrelated_file});
    fh.Pass(fm,fmanager,5);assert(fh.active);
    assert(fm.words[f2+0x844]==2&&fm.words[f2+4+0x80]==unrelated_file&&fm.words[f2+8+0x80]==unrelated_file);
    assert(fm.words[f3+4+0x80]==unrelated_file&&fm.words[f3+8+0x80]==fl&&fm.words[f3+12+0x80]==fs);
    fm.words=f_original;fm.words[f5+0x808]=0;fm.words[f5+0x844]=0;fh.Pass(fm,fmanager,5);assert(!fh.active);
    fm.words=f_original;
    auto popup=fm.Layout("PopupWindow"),pa=fm.Widget("all");fm.Children(popup+8,{pa});
    std::vector<U32> parts;
    for(auto name:{"cursor_g","window_touch_range","icon_a_btn","activity_indicator","e_message_g",
                   "btn_delete_g","btn_no_g","btn_yes_g","btn_cancel_g","btn_quit_g","btn_overwrite_g","window_g","black_screen_g"})
        parts.push_back(fm.Widget(name,100,60));
    fm.Children(pa,parts);fm.Queue(f5,{fl,popup});
    fo.poses[MM3D::NativeFileSelect::Popup]={40,10,.5f};MM3D::NativeFileSelect::Apply(fo);
    fh.Pass(fm,fmanager,5);assert(fh.active&&fm.words[f3+0x844]==3);
    assert(fm.words[f3+4+15*0x80]==popup);
    assert(fm.words[f3+4+0x80]==fl&&fm.words[f3+8+0x80]==fs&&fm.words[f2+0x844]==0);
    auto popup_routed=fm.words;
    fh.Begin(fm,popup);assert(fm.words[popup+8+0xd4]==14);fh.End(fm);assert(fm.words==popup_routed);
    fh.Pass(fm,fmanager,3);fh.Begin(fm,popup);
    for(unsigned i=0;i<12;++i)assert(std::bit_cast<float>(fm.words[parts[i]+0xec])==90);
    assert(std::bit_cast<float>(fm.words[parts.back()+0xec])==125);
    fh.End(fm);assert(fm.words==popup_routed);
    fh.Begin(fm,popup,true);
    assert(std::abs(std::bit_cast<float>(fm.words[parts.back()+0xec])-120.f)<.001f);
    fh.End(fm);assert(fm.words==popup_routed);
    // At the highest priority, the three relocated layouts share one bucket.
    fm.words=popup_routed;fm.Queue(f3,{});fm.words[f3+0x804+15*4]=0;fm.words[f3+0x844]=0;
    fm.Queue(f2,{fs});fm.Queue(f5,{});
    fm.words[f5+4+15*0x80]=fl;fm.words[f5+8+15*0x80]=popup;
    fm.words[f5+0x804+15*4]=2;fm.words[f5+0x844]=2;
    const auto highest=fm.words;
    fh.Pass(fm,fmanager,5);assert(fh.active);
    assert(fm.words[f3+4+15*0x80]==fl&&fm.words[f3+8+15*0x80]==fs&&fm.words[f3+12+15*0x80]==popup);
    fm.words=highest;
    for(unsigned i=0;i<30;++i)fm.words[f3+4+15*0x80+4*i]=unrelated_file;
    fm.words[f3+0x804+15*4]=30;fm.words[f3+0x844]=30;
    const auto insufficient=fm.words;fh.Pass(fm,fmanager,5);assert(!fh.active&&fm.words==insufficient);
    // A missing destination must not leave one queue partially modified.
    fm.words=highest;fm.words.erase(f3+12+15*0x80);
    const auto unreadable=fm.words;fh.Pass(fm,fmanager,5);assert(!fh.active&&fm.words==unreadable);
    auto backdrop=nm.Layout("CommonUpBg",0,0);auto backdrop_before=nm.words;
    nh.Pass(nm,nmanager,2);nh.Begin(nm,backdrop,true);
    assert(std::abs(std::bit_cast<float>(nm.words[backdrop+8+0xec])+13.333333f)<.001f);
    nh.End(nm);assert(nm.words==backdrop_before);
    nh.Begin(nm,backdrop,false);assert(nm.words==backdrop_before);nh.End(nm);
    MM3D::NotebookRows::Hook rw;Memory rm;
    auto ro=rm.Alloc(0x800),ru=rm.Layout("UpQuestlist");rm.words[ro+0x34]=ru;
    rm.words[ro+0x254]=0x112201fe;rm.words[ro+0x70]=20u<<24;
    constexpr std::array<unsigned,9> order{7,5,3,1,0,2,4,6,8};
    std::array<U32,9> anchors{};
    for(unsigned i=0;i<9;++i){anchors[i]=rm.Widget("slot",212,-25+38.f*i);rm.words[anchors[i]+0x20]=rm.Layout("QuestPlate",212,-25+38.f*i);rm.words[ro+0x138+4*order[i]]=anchors[i];}
    for(auto anchor:anchors)rm.words[anchor+0x134]=std::bit_cast<U32>(1.f);
    auto rb=rm.words;rw.Enter(rm,ro);assert(rw.padding==2&&rm.words[ro+0x254]==0x11220100);
    for(unsigned i=0;i<9;++i)assert(rm.words[ro+0x138+4*order[i]]==anchors[(i+2)%9]);
    rw.Leave(rm);assert(rm.words==rb);
    std::vector<MM3D::NativeInventory::Hook::Write> extra;MM3D::NativeInventory::Hook rh;
    // Preserve native staggered travel; attached art
    // and selected cursor follow, with no horizontal reflection of the art.
    for(unsigned i=0;i<9;++i) {
        rm.words[anchors[i]+0xec]=std::bit_cast<U32>(212.f+20.f*i);
        rm.words[rm.words[anchors[i]+0x20]+8+0xec]=std::bit_cast<U32>(212.f+20.f*i);
    }
    auto slide_before=rm.words;
    assert(rw.Expand(rm,ru,rh,extra));for(auto w:extra)rm.Write(w.address,w.after);
    for(unsigned i=0;i<9;++i) {
        assert(std::bit_cast<float>(rm.words[anchors[i]+0xec])==212.f-20.f*(i==0?8:i==8?0:i));
        assert(std::bit_cast<float>(rm.words[rm.words[anchors[i]+0x20]+8+0xec])==212.f-20.f*(i==0?8:i==8?0:i));
        assert(std::bit_cast<float>(rm.words[anchors[i]+0xe0])==1.f);
    }
    for(auto i=extra.rbegin();i!=extra.rend();++i)rm.Write(i->address,i->before);
    assert(rm.words==slide_before);
    auto slide_cursor=rm.Layout("CursorQuestPlate",272,89);
    extra.clear();assert(rw.Expand(rm,slide_cursor,rh,extra));
    for(auto w:extra)rm.Write(w.address,w.after);
    // Native selection1 - start(-2) = position3, held by anchor3.
    assert(std::bit_cast<float>(rm.words[slide_cursor+8+0xec])==152.f);
    rm.words=rb;extra.clear();
    const U32 buffer_child=rm.words[anchors[0]+0x20]+8;
    rm.words[anchors[0]+0x134]=std::bit_cast<U32>(1.f);
    rm.words[buffer_child+0x134]=std::bit_cast<U32>(.75f);
    assert(rw.Expand(rm,ru,rh,extra)); // remember visible subtree opacity
    rm.words[anchors[0]+0x134]=0;rm.words[buffer_child+0x134]=0;
    rm.words[anchors[7]+0xec]=std::bit_cast<U32>(400.f);
    auto row_all=rm.Widget("all"),buffer_group=rm.Widget("null_plate07");
    std::vector<U32> native_groups{buffer_group};
    for(unsigned i=1;i<13;++i)native_groups.push_back(rm.Widget("other"));
    rm.Children(ru+8,{row_all});rm.Children(row_all,native_groups);
    rm.words[row_all+0x134]=std::bit_cast<U32>(.8f);
    rm.words[buffer_group+0x134]=0;
    auto closing_rows=rm.words;extra.clear();
    assert(rw.Expand(rm,ru,rh,extra));for(auto w:extra)rm.Write(w.address,w.after);
    assert(std::abs(std::bit_cast<float>(rm.words[anchors[0]+0xec])-24.f)<.001f);
    assert(std::bit_cast<float>(rm.words[buffer_child+0x134])==.75f);
    assert(std::bit_cast<float>(rm.words[buffer_group+0x134])==.8f);
    for(auto i=extra.rbegin();i!=extra.rend();++i)rm.Write(i->address,i->before);
    assert(rm.words==closing_rows);
    extra.clear();assert(rw.Expand(rm,buffer_child-8,rh,extra));
    for(auto w:extra)rm.Write(w.address,w.after);
    assert(std::bit_cast<float>(rm.words[buffer_child+0x134])==.75f);
    for(auto i=extra.rbegin();i!=extra.rend();++i)rm.Write(i->address,i->before);
    assert(rm.words==closing_rows);
    // Visible-rank exit order survives recycled slots after scrolling. Drive
    // the actual seven native tracks at mid-exit and their final frame.
    const auto initial_positions=rw.positions;
    for(unsigned rotation:{0u,1u,4u})for(float frame:{8.5f,11.f}) {
        rm.words=rb;rm.words[anchors[0]+0x134]=0;
        for(unsigned i=0;i<9;++i)rw.positions[i]=int((i+rotation)%9);
        rm.words[ro+0x254]=0; // selected first row; hide final buffer event
        for(unsigned i=1;i<=7;++i)
            rm.words[anchors[i]+0xec]=std::bit_cast<U32>(212.f+388.f*std::clamp((frame-float(i+1))/3.f,0.f,1.f));
        auto before_exit=rm.words;extra.clear();
        assert(rw.Expand(rm,ru,rh,extra));for(auto w:extra)rm.Write(w.address,w.after);
        std::array<float,8> xs{};
        for(unsigned i=0;i<9;++i)if(rw.positions[i]<8) {
            const auto rank=rw.positions[i];
            xs[rank]=std::bit_cast<float>(rm.words[anchors[i]+0xec]);
            const float expected=212.f-388.f*std::clamp((frame-(2.f+6.f*rank/7.f))/3.f,0.f,1.f);
            assert(std::abs(xs[rank]-expected)<.001f);
        }
        if(frame<11)assert(xs[5]<xs[6]&&xs[6]<xs[7]);
        else for(float x:xs)assert(std::abs(x+176.f)<.001f);
        for(auto i=extra.rbegin();i!=extra.rend();++i)rm.Write(i->address,i->before);
        assert(rm.words==before_exit);
    }
    rw.positions=initial_positions;
    rm.words=rb;extra.clear();
    assert(rw.Expand(rm,ru,rh,extra));for(auto w:extra)rm.Write(w.address,w.after);
    assert(std::bit_cast<float>(rm.words[anchors[0]+0xfc])==317);
    assert(!(rm.words[anchors[1]+0xd4]&1));
    for(auto i=extra.rbegin();i!=extra.rend();++i)rm.Write(i->address,i->before);assert(rm.words==rb);
    // At the end of down-scroll the first two anchors coincide. Expansion
    // must retain spacing and must not fail (formerly caused native-size flashes).
    rm.words[anchors[1]+0xfc]=std::bit_cast<U32>(-25.f);
    rm.words[anchors[4]+0xfc]=std::bit_cast<U32>(89.f);
    auto loaded=rm.Layout("QuestPlate");rm.words[anchors[8]+0x20]=loaded;
    rm.words[anchors[8]+0xd4]&=~1u;
    auto animated=rm.words;extra.clear();assert(rw.Expand(rm,ru,rh,extra));
    for(auto w:extra)rm.Write(w.address,w.after);
    assert(std::bit_cast<float>(rm.words[anchors[0]+0xfc])==317);
    assert(!(rm.words[anchors[1]+0xd4]&1));
    assert(rm.words[anchors[8]+0xd4]&1u);
    for(auto i=extra.rbegin();i!=extra.rend();++i)rm.Write(i->address,i->before);assert(rm.words==animated);
    rm.words=rb;
    rm.words[ro+0x254]=0x1122130d; // selected19, native start13, total20
    auto end_before=rm.words;rw.Enter(rm,ro);
    assert((rm.words[ro+0x254]&255)==11);
    rw.Leave(rm);assert(rm.words==end_before);
    assert(rw.positions[7]==-2&&rw.positions[8]==-1);
    extra.clear();assert(rw.Expand(rm,ru,rh,extra));
    for(auto w:extra)rm.Write(w.address,w.after);
    unsigned visible_rows=0;for(auto node:anchors)visible_rows+=bool(rm.words[node+0xd4]&1);
    assert(visible_rows==8);assert(rm.words[anchors[6]+0xd4]&1); // selected last stays visible
    for(auto i=extra.rbegin();i!=extra.rend();++i)rm.Write(i->address,i->before);
    assert(rm.words==end_before);
    rm.words[ro+0x254]=0x1122120d; // up one: last event must remain visible
    extra.clear();assert(rw.Expand(rm,ru,rh,extra));
    for(auto w:extra)rm.Write(w.address,w.after);
    assert(rm.words[anchors[6]+0xd4]&1);
    auto cursor_layout=rm.Layout("CursorQuestPlate",212,89);
    extra.clear();assert(rw.Expand(rm,cursor_layout,rh,extra));
    for(auto w:extra)rm.Write(w.address,w.after);
    assert(std::bit_cast<float>(rm.words[cursor_layout+8+0xfc])==279);
    rm.words=rb;
    MM3D::NotebookRows::enabled=false;rw.Enter(rm,ro);assert(!rw.padding&&rm.words==rb);MM3D::NotebookRows::enabled=true;
    MM3D::NotebookRows::enabled=true;
    assert(rw.ImmediateScroll(rm,ro,0x27,0x123400));
    assert(!rw.ImmediateScroll(rm,ro,0x26,0x123400));
    assert(!rw.ImmediateScroll(rm,ro,0x27,0));
    MM3D::NotebookRows::enabled=false;assert(!rw.ImmediateScroll(rm,ro,0x27,0x123400));
    MM3D::NotebookRows::enabled=true;
    LoadingIconChecks();
    PressStartChecks();
    NotebookPromptChecks();
    ItemPresentChecks();
    MaskPresentChecks();
    InventoryNavigationChecks();
    BottomStripNavigationChecks();
    InventoryTransitionChecks();
    std::cout<<"Native menu signatures, routing, transforms/restoration and guarded paths passed.\n";
    return 0;
}
