// Focused player HUD checks; reuses the native widget-memory fixture.
// clang++ -std=c++20 -Isrc tools/mm3d/player_hud_check.cpp -o /tmp/player-hud-check
#define main native_inventory_checks_main
#include "native_inventory_check.cpp"
#undef main
#include "mm3d/player_hud.h"
namespace P = MM3D::PlayerHud;
namespace H = MM3D::HudAdjustments;
void MenuIsolationChecks() {
    for(const char* name:{"MenuMapDungeon","UpWorldMapHud","UpWorldMapBG","MenuQuestlist",
                         "MenuSchedule","UpSchedule","UpQuestlist","UpEventMap","MessageWindow"}) {
        Memory m;P::Hook hook;const auto layout=m.Layout(name);
        const auto widget=m.Widget("heart_status",18,37),geometry=m.Alloc(0x80);
        m.Children(layout+8,{widget});m.words[widget+0x28]=geometry;m.words[geometry+4]=6;
        m.words[geometry+0x20]=std::bit_cast<U32>(60.f);m.words[geometry+0x24]=std::bit_cast<U32>(20.f);
        const auto original=m.words;
        auto options=H::FreshDefaults();options.overall=4;options.magic_width=2;
        options.opacity=.3f;options.top=7;options.x[H::Hearts]=31;H::Apply(options);
        hook.Begin(m,layout);assert(m.words==original);hook.End(m);assert(m.words==original);
    }
    H::Apply({});
}
void OpacityChecks() {
    for(float opacity:{0.f,.5f,1.f}) {
        Memory m;P::Hook hook;auto hud=m.Layout("Hud2"),group=m.Widget("heart_status");
        auto heart=m.Widget("heart00"),cover=m.Widget("unrelated_cover");
        m.Children(hud+8,{group,cover});m.Children(group,{heart});
        for(U32 n:{group,heart,cover})m.words[n+0x134]=std::bit_cast<U32>(.6f);
        const auto original=m.words;
        H::Options o;o.opacity=opacity;H::Apply(o);hook.Begin(m,hud);
        assert(std::abs(std::bit_cast<float>(m.words[heart+0x134])-.6f*opacity)<.0001f);
        assert(m.words[cover+0x134]==original.at(cover+0x134));
        auto adjusted=m.words;hook.Begin(m,hud);hook.End(m);assert(m.words==adjusted);
        hook.End(m);assert(m.words==original);
        auto menu=m.Layout("MenuItem");auto before=m.words;hook.Begin(m,menu);hook.End(m);assert(m.words==before);
    }
    H::Apply({});
}
void MagicHeightChecks() {
    for(float width:{.5f,1.f,3.f})
    for(float installed:{.5f,1.f,2.f})for(float target:{.5f,1.f,1.5f,2.f})
    for(int height_steps:{-10,0,10})for(int steps:{-10,0,10})for(float fill_width:{0.f,23.f,80.f}) {
        Memory m;P::Hook hook;auto hud=m.Layout("Hud2"),group=m.Widget("magic_gauge_g",10,40);
        m.words[group+0x4c]=m.words[group+0xf4]=std::bit_cast<U32>(installed);
        const auto pane=[&](const char* name,float width) {
            auto n=m.Widget(name,10,40),g=m.Alloc(0x80);m.words[n+0x28]=g;m.words[g+4]=6;
            // Real artwork extends above the group's origin. Height changes
            // must preserve that visible top, not the origin at Y=40.
            m.words[g+0x18]=std::bit_cast<U32>(-4.f);
            m.words[g+0x20]=std::bit_cast<U32>(width);m.words[g+0x24]=std::bit_cast<U32>(9.f);
            m.words[n+0xf4]=std::bit_cast<U32>(installed);m.words[n+0x134]=std::bit_cast<U32>(.6f);return n;
        };
        auto frame=pane("gauge_base",80),fill=pane("gauge_pane",fill_width);
        m.Children(hud+8,{group});m.Children(group,{frame,fill});auto before=m.words;
        H::Options o;o.overall=steps;o.elements[H::Magic]=steps;o.top=3;o.magic_height_steps=height_steps;o.magic_width=width;H::Apply(o);
        P::ApplyMagicHeight(target);hook.Begin(m,hud);const float scale=H::Scale(H::Magic);
        for(auto n:{frame,fill}) {
            assert(std::abs(std::bit_cast<float>(m.words[n+0xf4])-target*H::Factor(height_steps)*scale)<.0001f);
            assert(std::abs(std::bit_cast<float>(m.words[n+0xe0])-scale*width)<.0001f);
            assert(std::abs(std::bit_cast<float>(m.words[n+0xec])-10.f)<.0001f);
            const float top=std::bit_cast<float>(m.words[n+0xfc])-4*target*H::Factor(height_steps)*scale;
            assert(std::abs(top-(43-4*installed))<.0001f);
            assert(m.words[n+0x134]==before.at(n+0x134));
        }
        assert(m.words[group+0x4c]==before.at(group+0x4c));
        auto adjusted=m.words;hook.Begin(m,hud);hook.End(m);assert(m.words==adjusted);
        hook.End(m);assert(m.words==before);
    }
    P::ApplyMagicHeight(0);H::Apply({});
}
void TimerAnchorChecks() {
    Memory m;P::Hook hook;
    const auto quad=[&](const char* name,float x,float y,float w,float h) {
        auto n=m.Widget(name,x,y),g=m.Alloc(0x80);m.words[n+0x28]=g;m.words[g+4]=6;
        m.words[g+0x20]=std::bit_cast<U32>(w);m.words[g+0x24]=std::bit_cast<U32>(h);
        m.words[n+0x134]=std::bit_cast<U32>(.6f);return n;
    };
    auto hud=m.Layout("Hud"),clock=m.Widget("game_timer_g");
    auto decoration=quad("timer_decoration_g",150,210,100,20);
    auto countdown=quad("last_timer_g",170,180,60,15);
    auto timer=m.Widget("eventTimer"),embedded=m.Layout("EventTimer");
    auto digits=quad("digits",160,10,80,20);
    // Off-center inactive artwork must not pull the visible timer sideways.
    auto alternate=quad("alternate_language",40,10,40,20);
    m.words[alternate+0x134]=0;
    m.Children(clock,{decoration});m.words[timer+0x20]=embedded;m.Children(embedded+8,{digits,alternate});
    m.Children(hud+8,{clock,countdown,timer});const auto original=m.words;
    const auto f=[&](U32 n,U32 off){return std::bit_cast<float>(m.words[n+off]);};
    for(int global:{-10,0,10})for(int local:{-10,0,10})for(bool hidden:{false,true}) {
        H::Options o;o.overall=global;o.elements[H::Timer]=local;o.elements[H::Clock]=local;
        o.top=3;o.bottom=7;o.x[H::Timer]=11;o.y[H::Timer]=5;
        o.x[H::Clock]=-4;o.y[H::Clock]=2;H::Apply(o);
        m.words[countdown+0x134]=std::bit_cast<U32>(hidden?0.f:.6f);auto before=m.words;
        hook.Begin(m,hud);const float scale=H::Scale(H::Timer);
        // Timer center remains fixed with explicit position and top padding.
        assert(std::abs(f(digits,0xec)+40*scale-211)<.0001f);
        assert(std::abs(f(digits,0xfc)+10*scale-28)<.0001f);
        assert(std::abs(f(digits,0xe0)-scale)<.0001f);
        // Main clock anchor never depends on countdown visibility or size.
        assert(std::abs(f(decoration,0xec)+50*scale-196)<.0001f);
        assert(std::abs(f(decoration,0xfc)+10*scale-215)<.0001f);
        assert(std::abs(f(decoration,0xfc)-(f(countdown,0xfc)+15*scale)-15*scale)<.0001f);
        assert(m.words[countdown+0x134]==before.at(countdown+0x134));
        auto adjusted=m.words;hook.Begin(m,embedded);hook.End(m);assert(m.words==adjusted);
        hook.End(m);assert(m.words==before);
    }
    m.words=original;H::Apply({});hook.Begin(m,hud);hook.End(m);assert(m.words==original);
}
void TatlAnchorChecks() {
    Memory m;P::Hook hook;
    const auto quad=[&](const char* name,float x,float y,float w,float h) {
        auto n=m.Widget(name,x,y),geo=m.Alloc(0x80);m.words[n+0x28]=geo;m.words[geo+4]=6;
        m.words[geo+0x20]=std::bit_cast<U32>(w);m.words[geo+0x24]=std::bit_cast<U32>(h);
        m.words[n+0x134]=std::bit_cast<U32>(.4f);return n;
    };
    auto hud2=m.Layout("Hud2"),hud=m.Layout("Hud");
    auto hearts=quad("heart_status",10,15,100,20),magic=quad("magic_gauge_g",10,40,80,10);
    auto dpad=quad("hud_dpad",15,60,40,40),tatl=quad("chat_alart",65,70,16,16);
    auto unrelated=quad("unrelated",200,180,20,20);
    m.Children(hud2+8,{hearts,magic,dpad});m.Children(hud+8,{tatl,unrelated});
    const auto original=m.words;
    const auto f=[&](U32 n,U32 off){return std::bit_cast<float>(m.words[n+off]);};
    // Tatl can be submitted before Hud2 is first observed; no invented pivot.
    H::Apply({});hook.Begin(m,hud);hook.End(m);assert(m.words==original);
    hook.Begin(m,hud2);hook.End(m);assert(m.words==original);
    for(bool tatl_first:{false,true})for(int overall:{-10,0,10})for(int local:{-10,0,10})
    for(int above:{0,6}) {
        H::Options o;o.overall=overall;o.elements[H::Dpad]=local;
        o.elements[H::Hearts]=above;o.elements[H::Magic]=above;
        o.top=7;o.x[H::Dpad]=13;o.y[H::Dpad]=-9;H::Apply(o);
        std::array<float,3> pad{},fairy{};
        const auto draw=[&](bool is_tatl) {
            const auto layout=is_tatl?hud:hud2,node=is_tatl?tatl:dpad;
            hook.Begin(m,layout);auto& result=is_tatl?fairy:pad;
            result={f(node,0xec),f(node,0xfc),f(node,0xe0)};
            assert(f(node,0x134)==.4f);
            assert(f(unrelated,0xec)==200&&f(unrelated,0xfc)==180);
            auto adjusted=m.words;hook.Begin(m,layout);hook.End(m);assert(m.words==adjusted);
            hook.End(m);assert(m.words==original);
        };
        draw(tatl_first);draw(!tatl_first);
        const float scale=H::Scale(H::Dpad);
        // Ten native pixels between the pad's right edge and Tatl; same
        // vertical offset and scale, including overall and individual growth.
        assert(std::abs(fairy[0]-(pad[0]+40*scale)-10*scale)<.0001f);
        assert(std::abs(fairy[1]-pad[1]-10*scale)<.0001f);
        assert(std::abs(fairy[2]-scale)<.0001f&&std::abs(pad[2]-scale)<.0001f);
    }
    H::Apply({});hook.Begin(m,hud2);hook.End(m);hook.Begin(m,hud);hook.End(m);
    assert(m.words==original);
}
void PlayerHudChecks() {
    Memory m; P::Hook hook;
    const auto hud=m.Layout("Hud2"),all=m.Widget("all");m.Children(hud+8,{all});
    const auto quad=[&](const char* name,float x,float y,float w,float h) {
        auto n=m.Widget(name,x,y),g=m.Alloc(0x80);m.words[n+0x28]=g;m.words[g+4]=6;
        m.words[g+0x20]=std::bit_cast<U32>(w);m.words[g+0x24]=std::bit_cast<U32>(h);
        m.words[n+0x134]=std::bit_cast<U32>(.4f);return n;
    };
    auto hearts=quad("heart_status",10,15,100,20),magic=quad("magic_gauge_g",10,40,80,10),
         rupees=quad("rupee_g",12,200,32,20),keys=quad("key_g",10,180,20,16),dpad=m.Widget("hud_dpad"),
         buttons=quad("item_icon_set",300,10,80,60),other=quad("unrelated",45,80,40,50);
    auto embedded=m.Layout("Dpad"),dpad_quad=quad("pad",15,60,40,40);
    m.words[dpad+0x20]=embedded;m.Children(embedded+8,{dpad_quad});
    m.Children(all,{hearts,magic,rupees,keys,dpad,buttons,other});
    // Hidden artwork is deliberately far outside the actual button circles.
    auto circle=quad("btn_x_g",300,10,80,60),hidden=quad("inactive_art",900,-200,400,300);
    m.Children(buttons,{circle,hidden});
    // Native number columns align, although the key icon extends farther left.
    auto rupee_digit=quad("rank_a",40,200,4,10),key_digit=quad("num_key",40,180,4,10);
    m.Children(rupees,{rupee_digit});m.Children(keys,{key_digit});
    auto baseline=m.words;
    H::Apply({});hook.Begin(m,hud);assert(m.words==baseline);hook.End(m);assert(m.words==baseline);
    const auto f=[&](U32 n,U32 off){return std::bit_cast<float>(m.words[n+off]);};
    for(int step:{-10,-3,0,2,4,10}) {
        H::Options o;o.overall=step;o.top=7;o.bottom=9;o.elements[H::Magic]=2;H::Apply(o);
        hook.Begin(m,hud);const float scale=H::Factor(step);
        assert(f(hearts,0xec)==10&&f(hearts,0xfc)==22);
        assert(std::abs(f(hearts,0xe0)-scale)<.0001f);
        assert(std::abs(f(magic,0xe0)-scale*1.1f)<.0001f);
        assert(std::abs(f(rupees,0xfc)+20*scale-211)<.0001f&&f(rupees,0xec)==12);
        assert(std::abs(f(key_digit,0xec)-f(rupee_digit,0xec))<.0001f);
        assert(std::abs(f(rupee_digit,0xfc)-f(key_digit,0xfc)-20*scale)<.0001f);
        assert(std::abs(f(buttons,0xec)+80*scale-380)<.0001f&&f(buttons,0xfc)==17);
        assert(std::abs(f(dpad_quad,0xec)-(10+5*scale))<.0001f);
        assert(std::abs(f(dpad_quad,0xfc)-(22+46*scale))<.0001f);
        // The heart-to-magic gap scales with the overall arrangement.
        assert(std::abs(f(magic,0xfc)-(f(hearts,0xfc)+20*scale)-5*scale)<.0001f);
        assert(f(other,0xec)==45&&f(other,0xe0)==1);
        for(auto n:{hearts,magic,rupees,dpad_quad,buttons})assert(f(n,0x134)==.4f);
        auto adjusted=m.words;hook.Begin(m,embedded);assert(m.words==adjusted);hook.End(m);assert(m.words==adjusted);
        hook.End(m);assert(m.words==baseline);
        // Moving the same scale into the editor baseline must preserve every
        // vertex, anchor, gap, count and unrelated widget exactly.
        o.base_scale=step;o.overall=0;H::Apply(o);hook.Begin(m,hud);
        assert(m.words==adjusted);hook.End(m);assert(m.words==baseline);
        o.overall=3;o.x[H::Hearts]=9;H::ResetPlayer(o);
        assert(o.base_scale==2&&o.overall==0&&o.x[H::Hearts]==-3&&o.layout_version==1);
    }
    // The UI version changes readouts only, never renderer geometry.
    auto fresh=H::FreshDefaults(),legacy=fresh;legacy.layout_version=0;
    H::Apply(legacy);hook.Begin(m,hud);const auto authored=m.words;hook.End(m);
    H::Apply(fresh);hook.Begin(m,hud);assert(m.words==authored);hook.End(m);
    const auto ui=H::UiDefaults(fresh,true),old_ui=H::UiDefaults(legacy,true);
    assert(ui.elements==fresh.elements&&ui.x==fresh.x&&ui.y==fresh.y);
    assert(ui.heart_spacing==fresh.heart_spacing&&ui.magic_height_steps==fresh.magic_height_steps);
    assert(fresh.magic_width/ui.magic_width==1.f&&old_ui.magic_width==1.f);
    assert(old_ui.elements==H::Options{}.elements&&H::UiDefaults(fresh,false).x==old_ui.x);
    assert(m.words==baseline);
    H::Options positioned;positioned.overall=4;positioned.top=7;positioned.bottom=9;
    H::Apply(positioned);hook.Begin(m,hud);auto scaled=m.words;hook.End(m);
    for(unsigned i=0;i<H::Count;++i){positioned.x[i]=int(i)+3;positioned.y[i]=-int(i)-2;}
    H::Apply(positioned);hook.Begin(m,hud);
    for(auto [node,part]:std::array<std::pair<U32,H::Part>,5>{{
        {hearts,H::Hearts},{magic,H::Magic},{rupees,H::Rupees},{dpad_quad,H::Dpad},{buttons,H::Buttons}}}) {
        assert(std::abs(f(node,0xec)-std::bit_cast<float>(scaled.at(node+0xec))-positioned.x[part])<.0001f);
        assert(std::abs(f(node,0xfc)-std::bit_cast<float>(scaled.at(node+0xfc))-positioned.y[part])<.0001f);
        assert(m.words[node+0xe0]==scaled.at(node+0xe0));
    }
    hook.End(m);assert(m.words==baseline);
    // Editor baseline persists through user resets and composes with player
    // scale/position/padding for both counters, without touching other widgets.
    for(float editor_scale:{.5f,1.f,2.f})for(int user_scale:{-4,0,5}) {
        P::ApplyRupeeKeys({7,-11,editor_scale});
        H::Options o;o.elements[H::Rupees]=user_scale;o.bottom=3;o.x[H::Rupees]=4;o.y[H::Rupees]=2;H::Apply(o);
        hook.Begin(m,hud);
        const float scale=editor_scale*H::Factor(user_scale);
        for(auto [n,x,y]:std::array<std::tuple<U32,float,float>,2>{{{rupees,12,200},{keys,10,180}}}) {
            assert(std::abs(f(n,0xe0)-scale)<.0001f&&std::abs(f(n,0xf4)-scale)<.0001f);
            assert(std::abs(f(n,0xec)-(23+(x-12)*scale))<.0001f);
            assert(std::abs(f(n,0xfc)-(208+(y-220)*scale))<.0001f);
            assert(f(n,0x134)==.4f);
        }
        assert(std::abs(f(key_digit,0xec)-f(rupee_digit,0xec))<.0001f);
        assert(std::abs(f(rupee_digit,0xfc)-f(key_digit,0xfc)-20*scale)<.0001f);
        assert(f(hearts,0xe0)==1&&f(buttons,0xe0)==1);
        hook.End(m);assert(m.words==baseline);
        H::Apply({});hook.Begin(m,hud);
        assert(f(rupees,0xe0)==editor_scale&&f(keys,0xe0)==editor_scale);
        assert(std::abs(f(rupees,0xec)-19)<.0001f);
        hook.End(m);assert(m.words==baseline);
    }
    P::ApplyRupeeKeys({});H::Apply({});hook.Begin(m,hud);assert(m.words==baseline);hook.End(m);
    H::Options independent;independent.elements[H::Hearts]=10;H::Apply(independent);
    hook.Begin(m,hud);
    assert(std::abs(f(magic,0xfc)-(f(hearts,0xfc)+30)-5)<.0001f);
    assert(std::abs(f(dpad_quad,0xfc)-70)<.0001f);
    hook.End(m);assert(m.words==baseline);
    // Clock stays centered; the minigame counter keeps its upper-right anchor.
    auto primary=m.Layout("Hud"),clock=quad("game_timer_g",170,200,60,20),counter=quad("minigame_g",340,90,40,20);
    m.Children(primary+8,{clock,counter});auto primary_before=m.words;
    H::Options settings;settings.overall=-10;settings.top=6;settings.bottom=8;H::Apply(settings);
    hook.Begin(m,primary);
    assert(f(clock,0xec)+30*.5f==200&&f(clock,0xfc)+10*.5f==202);
    assert(f(counter,0xec)+40*.5f==380&&f(counter,0xfc)==96);
    hook.End(m);assert(m.words==primary_before);
    // A broken tree must not partially apply a player transformation.
    m.words[dpad+0x20]=0xdeadbeec;auto broken=m.words;hook.Begin(m,hud);assert(m.words==broken);hook.End(m);assert(m.words==broken);
    P::Bounds b;b.Add(40,50);b.Add(100,90);
    for(auto anchor:{P::Anchor::TopLeft,P::Anchor::TopRight,P::Anchor::BottomLeft,P::Anchor::Center}) {
        float x=anchor==P::Anchor::TopRight?100:anchor==P::Anchor::Center?70:40;
        float y=anchor==P::Anchor::BottomLeft?90:anchor==P::Anchor::Center?70:50;
        auto a=P::Mapping(b,anchor,.5f,-8);
        assert(x*a.xx+a.tx==x&&y*a.yy+a.ty==y-8);
    }
    assert(H::Factor(-100)==.5f&&H::Factor(100)==1.5f);H::Apply({});
}
void IndividualButtonChecks() {
    constexpr const char* names[]{"icon_item_x_g","icon_item_y_g","icon_item_1_g","icon_item_2_g","a_action_button","b_action_button"};
    constexpr const char* bases[]{"btn_x_g","btn_y_g","btn_corner01_g","btn_corner03_g"};
    for(const char* layout:{"Hud2","HudItemIconSet","Hud"}) {
        Memory m;P::Hook hook;auto hud=m.Layout(layout),group=m.Widget(layout==std::string("Hud")?"action_button_g":"item_icon_set");
        m.Children(hud+8,{group});std::vector<U32> children;std::array<std::vector<U32>,6> members;
        const auto pane=[&](const char* name){auto n=m.Widget(name,300,20),g=m.Alloc(0x80);m.words[n+0x28]=g;m.words[g+4]=6;
            m.words[g+0x20]=std::bit_cast<U32>(16.f);m.words[g+0x24]=std::bit_cast<U32>(16.f);return n;};
        for(unsigned i=0;i<6;++i)if((std::string(layout)=="Hud")== (i>=4)) {
            auto icon=pane(names[i]),label=pane("label");m.Children(icon,{label});children.push_back(icon);members[i]={icon,label};
            if(i<4){auto base=pane(bases[i]);children.push_back(base);members[i].push_back(base);}
        }
        auto other=pane("unrelated");children.push_back(other);m.Children(group,children);auto original=m.words;
        for(bool clicks:{false,true,false})for(int scale:{-5,0,5})for(unsigned selected=0;selected<6;++selected) {
            MM3D::StickControls::Configure(clicks);
            H::Options o;o.overall=scale;o.x[H::Buttons]=7;o.y[H::Buttons]=-3;H::Apply(o);
            hook.Begin(m,hud);auto scaled=m.words;hook.End(m);assert(m.words==original);
            o.button_x[selected]=19;o.button_y[selected]=-11;H::Apply(o);hook.Begin(m,hud);
            for(unsigned i=0;i<6;++i)for(auto n:members[i]) {
                auto f=[&](U32 off){return std::bit_cast<float>(m.words[n+off])-std::bit_cast<float>(scaled.at(n+off));};
                assert(std::abs(f(0xec)-(MM3D::StickControls::ItemLayoutIndex(i,2)==selected?19:0))<.0001f);
                assert(std::abs(f(0xfc)-(MM3D::StickControls::ItemLayoutIndex(i,2)==selected?-11:0))<.0001f);
                assert(m.words[n+0xe0]==scaled.at(n+0xe0));
            }
            assert(m.words[other+0xec]==scaled.at(other+0xec));auto adjusted=m.words;
            hook.Begin(m,hud);assert(m.words==adjusted);hook.End(m);assert(m.words==adjusted);
            hook.End(m);assert(m.words==original);
        }
    }
    H::Apply({});
}
void HideMinimapChecks() {
    namespace N=MM3D::NativeMinimap;
    for(bool pause_capture:{false,true}) {
    Memory m;N::Hook hook;
    for(unsigned q=0;q<8;++q){m.words[N::Count(q)]=0;m.words[N::Pointer(q,0)]=0;m.words[N::Priority(q,0)]=0;}
    auto map=m.Alloc(0x400),radar=m.Alloc(0x400),other=m.Alloc(0x400),top=m.Alloc(0x400);
    m.words[0x6890ec]=map;m.words[0x6890f0]=0;m.words[0x727970+0x828]=radar;
    m.words[N::Count(4)]=1;m.words[N::Pointer(4,0)]=top;m.words[N::Priority(4,0)]=5;
    m.words[N::Count(6)]=1;m.words[N::Pointer(6,0)]=other;m.words[N::Priority(6,0)]=7;
    H::Options o;o.hide_minimap=!pause_capture;H::Apply(o);hook.Begin(m,0,true,0,pause_capture);
    m.words[N::Count(6)]=3;
    m.words[N::Pointer(6,0)]=map;m.words[N::Priority(6,0)]=1;
    m.words[N::Pointer(6,1)]=other;m.words[N::Priority(6,1)]=7;
    m.words[N::Pointer(6,2)]=radar;m.words[N::Priority(6,2)]=9;
    for(U32 object:{map,radar}){m.words[object+0x2c4]=64;m.words[object+0x2c8]=12;}
    auto before=m.words;hook.End(m,0,true,.36f,.85f,400);
    assert(H::hide_minimap==!pause_capture); // transient suppression never changes preferences
    assert(m.words[map+0x2c8]==0&&m.words[radar+0x2c8]==0);
    assert(m.words[N::Count(6)]==1&&m.words[N::Pointer(6,0)]==other&&m.words[N::Priority(6,0)]==7);
    assert(m.words[N::Count(4)]==1&&m.words[N::Pointer(4,0)]==top);
    for(auto [address,value]:before)if(address!=N::Count(6)&&address!=N::Pointer(6,0)&&address!=N::Priority(6,0)&&address!=map+0x2c8&&address!=radar+0x2c8)assert(m.words[address]==value);
    // Hidden gameplay frames must not accumulate overview geometry. Opening
    // the pause map next appends at index zero, with its own marker scale.
    for(unsigned frame=0;frame<3;++frame) {
        hook.Begin(m,0,true,0,pause_capture);
        m.words[N::Count(6)]=3;
        m.words[N::Pointer(6,1)]=map;m.words[N::Priority(6,1)]=1;
        m.words[N::Pointer(6,2)]=radar;m.words[N::Priority(6,2)]=9;
        m.words[map+0x2c8]+=4;m.words[radar+0x2c8]+=8;
        hook.End(m,0,true,.36f,.85f,400);
        assert(m.words[map+0x2c8]==0&&m.words[radar+0x2c8]==0);
    }
    m.words[radar+0x2c8]+=3;assert(m.words[radar+0x2c8]==3);
    // Invalid second object must not partially discard the first object.
    m.words=before;m.words[radar+0x2c8]=65;const auto invalid=m.words;
    hook.armed=true;hook.End(m,0,true,.36f,.85f,400);assert(m.words==invalid);
    hook.Begin(m,0,true);assert(!hook.suppress_draw); // normal gameplay resumes immediately
    H::Apply({});assert(!H::hide_minimap);
    }
}
void PlayerMinimapChecks() {
    namespace N=MM3D::NativeMinimap;
    for(float inset:{0.f,65.f})for(int steps:{-10,0,10})for(int local:{-10,0,10})
    for(float angle:{0.f,.785398163f,1.570796327f})for(float origin:{0.f,-.5f})for(float opacity:{0.f,.5f,1.f})for(float map_opacity:{0.f,.1f,.85f}) {
        Memory m;N::Hook hook;H::Options o;o.opacity=opacity;o.overall=steps;o.elements[H::Minimap]=local;
        o.bottom=13;o.x[H::Minimap]=7;o.y[H::Minimap]=-3;H::Apply(o);
        for(unsigned q=0;q<8;++q){m.words[N::Count(q)]=0;for(unsigned i=0;i<2;++i){m.words[N::Pointer(q,i)]=0;m.words[N::Priority(q,i)]=0;}}
        auto map=m.Alloc(0x3a8),pos=m.Alloc(12),size=m.Alloc(12),rotation=m.Alloc(48);
        auto marker=m.Alloc(0x3a8),marker_pos=m.Alloc(12),marker_size=m.Alloc(12);
        const auto set=[&](U32 at,float v){m.words[at]=std::bit_cast<U32>(v);};
        m.words[0x6890ec]=map;m.words[0x6890f0]=0;m.words[0x727970+0x828]=marker;
        for(auto [object,p,z]:std::array<std::tuple<U32,U32,U32>,2>{{{map,pos,size},{marker,marker_pos,marker_size}}}) {
            m.words[object+0x2c8]=1;m.words[object+0x2c4]=1;m.words[object+0x2b8]=p;m.words[object+0x2c0]=z;
            const auto color=m.Alloc(64);m.words[object+0x398]=color;
            for(unsigned v=0;v<4;++v)set(color+v*16+12,.6f);
            set(p,280-inset);set(p+4,210-inset);set(z,40);set(z+4,30);set(z+8,1);
        }
        m.words[map+0x2bc]=rotation;
        const float c=std::cos(angle),sn=std::sin(angle);
        set(rotation,c);set(rotation+4,-sn);set(rotation+16,sn);set(rotation+20,c);set(rotation+40,1);
        float right=-1e9f,bottom=-1e9f;
        for(unsigned i=0;i<4;++i) {
            const float x=origin+float(i&1),y=origin+float(i>>1);
            set(map+0x2cc+i*12,x);set(map+0x2cc+i*12+4,y);
            right=std::max(right,280-inset+c*x*40-sn*y*30);
            bottom=std::max(bottom,210-inset+sn*x*40+c*y*30);
        }
        // A moving marker beyond the map must not change the map's pivot.
        set(marker_pos,500);set(marker_pos+4,350);
        hook.Begin(m,0,true);m.words[N::Count(6)]=2;m.words[N::Pointer(6,0)]=map;m.words[N::Pointer(6,1)]=marker;
        auto before=m.words;hook.End(m,0,true,.36f,map_opacity,400);
        auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
        for(U32 object:{map,marker})for(unsigned v=0;v<4;++v)
            assert(std::abs(f(m.words[object+0x398]+v*16+12)-.6f*map_opacity*opacity)<.0001f);
        assert(N::moved_quads==2&&m.words[N::Count(6)]==0&&m.words[N::Count(4)]==2);
        float actual_right=-1e9f,actual_bottom=-1e9f;
        for(unsigned i=0;i<4;++i) {
            const float x=origin+float(i&1),y=origin+float(i>>1);
            actual_right=std::max(actual_right,f(pos)+c*x*f(size)-sn*y*f(size+4));
            actual_bottom=std::max(actual_bottom,f(pos+4)+sn*x*f(size)+c*y*f(size+4));
        }
        assert(std::abs(actual_right-(399+(right-320)*.36f))<.0001f);
        assert(std::abs(actual_bottom-(216+(bottom-240)*.36f))<.0001f);
        const float factor=H::Scale(H::Minimap);
        assert(std::abs(f(size)-40*.36f*factor)<.0001f);
        assert(std::abs(f(marker_pos)-f(pos)-(500-(280-inset))*.36f*factor)<.0001f);
        // Invalid rotation data must leave all geometry and queues untouched.
        m.words=before;hook.armed=true;set(rotation,std::numeric_limits<float>::quiet_NaN());
        const auto invalid=m.words;hook.End(m,0,true,.36f,.85f,400);assert(m.words==invalid);
    }
    H::Apply({});
}
void PlayerMagicShadowChecks() {
    namespace B=MM3D::MagicBorder;Memory m;B::Hook h;
    B::Options options;options.thickness=2;options.color={1,.5f,.25f};B::Apply(options);
    auto hud=m.Layout("Hud2"),all=m.Widget("all"),group=m.Widget("magic_gauge_g",10,20);
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
    m.words[group+0x4c]=m.words[group+0xf4]=std::bit_cast<U32>(2.f);
    auto sp=(m.Alloc(0x6000)+0x6000)&~7u;auto before=m.words;
    P::Hook player;
    std::array<float,4> original{};
    for(float width:{.5f,1.f,3.f})for(float height:{.5f,1.f,2.f})for(int step:{0,10}) {
        P::ApplyMagicHeight(height);
        H::Options o;o.overall=step;o.top=step?7:0;o.magic_width=width;H::Apply(o);player.Begin(m,hud);
        const auto root=h.Root(m,hud,sp);assert(root!=hud+8);
        const auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
        for(auto pane:panes)assert(std::abs(f(pane+0x134)-.42f)<.0001f);
        assert(std::abs(f(root+0x200+0x134)-.6f)<.0001f);
        // Both horizontal border strips span the same extended background.
        for(U32 node:{root+0x400,root+0x600}) {
            assert(std::abs(f(node+0xec)-10.f)<.0001f);
            assert(std::abs(f(node+0x140+0x20)-80.f*width*H::Factor(step))<.0001f);
        }
        // No shadow geometry lies inside the now-translucent meter.
        const float bar_bottom=f(panes[1]+0xfc)+9*f(panes[1]+0xf4);
        for(unsigned layer=0;layer<4;++layer) {
            const auto bottom_node=root+0xa00+layer*0x400;
            assert(std::abs(f(bottom_node+0xfc)-bar_bottom)<.0001f);
        }
        const auto node=root+0xa00,geo=node+0x140;
        if(!step)original={f(node+0xec),f(node+0xfc),f(geo+0x20)*f(node+0xe0),f(geo+0x24)*f(node+0xf4)};
        else {
            assert(std::abs(f(node+0xec)-(10+(original[0]-10)*1.5f))<.0001f);
            assert(std::abs(f(node+0xfc)-(20+(original[1]-20)*1.5f+7))<.0001f);
            assert(std::abs(f(geo+0x20)*f(node+0xe0)-original[2]*1.5f)<.0001f);
            assert(std::abs(f(geo+0x24)*f(node+0xf4)-original[3]*1.5f)<.0001f);
        }
        h.End(m);player.End(m);assert(m.words==before);
    }
    P::ApplyMagicHeight(0);H::Apply({});
}
void HeartSpacingChecks() {
    Memory m;P::Hook hook;
    auto hud=m.Layout("Hud2"),group=m.Widget("heart_status");m.Children(hud+8,{group});
    std::vector<U32> hearts,quads;
    for(unsigned i=0;i<20;++i) {
        char name[]="heart00";name[5]='0'+i/10;name[6]='0'+i%10;
        const float x=10+15*(i%10),y=15+13*(i/10);
        auto node=m.Widget(name,x,y),embedded=m.Layout("Heart",x,y),quad=m.Widget("fill",x,y),geo=m.Alloc(0x80);
        m.words[node+0x20]=embedded;m.Children(embedded+8,{quad});m.words[quad+0x28]=geo;
        m.words[geo+4]=6;m.words[geo+0x20]=std::bit_cast<U32>(12.f);m.words[geo+0x24]=std::bit_cast<U32>(12.f);
        m.words[quad+0x134]=std::bit_cast<U32>(.6f);hearts.push_back(node);quads.push_back(quad);
    }
    m.Children(group,hearts);const auto original=m.words;
    for(int size:{0,4})for(int spacing:{-H::HeartSpacingLimit,-10,-1,0,1,10,H::HeartSpacingLimit}) {
        H::Options options;options.overall=size;options.elements[H::Hearts]=size;
        options.heart_spacing=spacing;H::Apply(options);hook.Begin(m,hud);
        const float scale=H::Scale(H::Hearts);
        for(unsigned i=0;i<20;++i) {
            const auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
            const float x=10+15*(i%10)*scale*(1+.005f*spacing);
            assert(std::abs(f(hearts[i]+0xec)-x)<.0001f&&std::abs(f(quads[i]+0xec)-x)<.0001f);
            assert(std::abs(f(quads[i]+0xfc)-(15+13*(i/10)*scale))<.0001f);
            assert(std::abs(f(quads[i]+0xe0)-scale)<.0001f&&f(quads[i]+0x134)==.6f);
        }
        hook.End(m);assert(m.words==original);
    }
    H::Apply({});hook.Begin(m,hud);assert(m.words==original);hook.End(m);
}
void CarrotChecks() {
    Memory m;P::Hook hook;auto hud=m.Layout("Hud2"),group=m.Widget("carrot_status");
    auto sibling=m.Widget("unrelated",20,30);m.Children(hud+8,{group,sibling});
    std::vector<U32> carrots,faces;
    const auto quad=[&](const char* name,float x,float y,float size) {
        auto n=m.Widget(name,x,y),g=m.Alloc(0x80);m.words[n+0x28]=g;m.words[g+4]=6;
        m.words[g+0x20]=m.words[g+0x24]=std::bit_cast<U32>(size);
        m.words[n+0x134]=std::bit_cast<U32>(.4f);return n;
    };
    for(unsigned i=0;i<6;++i) {
        auto n=m.Widget(("carrot0"+std::to_string(i)).c_str()),layout=m.Layout("Carrot");
        m.words[n+0x20]=layout;carrots.push_back(n);
        auto face=quad("carrot_pane",140+20*i,160,20);
        // Oversized refill effects and depleted faces must not move the anchor.
        auto flash=quad("carrot_effect_pane",100+20*i,120,100);
        auto spent=quad("carrot_unuseable_pane",140+20*i,160,20);
        m.Children(layout+8,{face,flash,spent});faces.push_back(face);
        if(i%2)m.words[face+0x134]=0;
    }
    m.Children(group,carrots);auto original=m.words;
    const auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
    for(float editor:{.5f,1.f,2.f})for(int user:{-10,0,10})for(int global:{-5,0,5}) {
        P::ApplyCarrots({7,-11,editor});H::Options o;o.overall=global;
        o.elements[H::Carrots]=user;o.x[H::Carrots]=3;o.y[H::Carrots]=2;o.bottom=4;H::Apply(o);
        hook.Begin(m,hud);const float scale=editor*H::Factor(user)*H::Factor(global);
        for(unsigned i=0;i<6;++i) {
            assert(std::abs(f(faces[i]+0xec)-(210+(140+20*int(i)-200)*scale))<.0001f);
            assert(std::abs(f(faces[i]+0xfc)-(157-10*scale))<.0001f);
            assert(std::abs(f(faces[i]+0xe0)-scale)<.0001f);
            assert(m.words[faces[i]+0x134]==original.at(faces[i]+0x134));
        }
        assert(f(sibling+0xec)==20&&f(sibling+0xfc)==30);
        auto adjusted=m.words;hook.Begin(m,*m.Read(carrots[0]+0x20));assert(m.words==adjusted);hook.End(m);
        hook.End(m);assert(m.words==original);
        H::Apply({});hook.Begin(m,hud);
        assert(std::abs(f(faces[0]+0xec)-(207-60*editor))<.0001f);
        hook.End(m);assert(m.words==original);
    }
    P::ApplyCarrots({});H::Apply({});hook.Begin(m,hud);assert(m.words==original);hook.End(m);
}
int main(){MenuIsolationChecks();BannerChecks();OpacityChecks();MagicHeightChecks();TimerAnchorChecks();TatlAnchorChecks();CarrotChecks();IndividualButtonChecks();HideMinimapChecks();HeartSpacingChecks();PlayerHudChecks();PlayerMagicShadowChecks();PlayerMinimapChecks();SmallGameplayMapChecks();std::cout<<"Player HUD checks passed\n";}
