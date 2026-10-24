// SPDX-License-Identifier: GPL-2.0-or-later
#define main inventory_full_main
#include "native_inventory_check.cpp"
#undef main
int main() {
    namespace D=MM3D::DungeonMap;Memory m;D::Hook h;
    const auto layout=m.Layout("CommonBottomBg"),all=m.Widget("all");
    m.Children(layout+8,{all});std::vector<U32> source;
    for(unsigned i=0;i<2;++i) {
        auto n=m.Widget(i?"common_bottom_bg_pane1":"common_bottom_bg_pane0"),g=m.Alloc(0x7c);
        m.words[n+0x28]=g;m.words[g+4]=6;
        m.words[g+0x14]=std::bit_cast<U32>(160.f*i);
        m.words[g+0x20]=std::bit_cast<U32>(160.f);m.words[g+0x24]=std::bit_cast<U32>(240.f);
        m.words[g+0x28]=std::bit_cast<U32>(i?.953125f:.640625f);
        m.words[g+0x30]=std::bit_cast<U32>(i?-.3125f:.3125f);
        m.words[n+0x134]=std::bit_cast<U32>(.6f);
        for(unsigned c=0;c<3;++c)m.words[n+0x128+c*4]=std::bit_cast<U32>(1.f);
        source.push_back(n);
    }
    m.Children(all,source);h.valid=h.dungeon=true;h.pass=1;h.backgrounds[0]=layout;h.routed={layout};
    const auto sp=(m.Alloc(0x14000)+0x14000)&~7u;
    D::Options o;o.top_strip=false;o.panels={{{12,110,.5f},{275,128,.3f}}};D::Apply(o);
    for(float darkness:{0.f,.7f,1.f}) {
        D::panel_darkness=darkness;auto before=m.words;
        auto r=h.PanelRoot(m,layout,sp);assert(r==sp-0x3000&&m.words[r+0x18]==14);
        auto f=[&](U32 p){return std::bit_cast<float>(m.words[p]);};
        for(unsigned i=0;i<8;++i) {
            auto n=m.words[r+0x140+i*4],g=m.words[n+0x28];auto p=o.panels[i/4];
            assert(f(n+0xec)==p.x&&std::abs(f(n+0xfc)-(p.y-(i%4>=2?240*p.scale:0)))<.0001f);
            assert(f(n+0xe0)==p.scale&&f(n+0xf4)==p.scale);
            assert(f(g+0x20)==160&&f(g+0x24)==240); // full texture, both native halves
            assert(f(g+0x30)==(i%2?-.3125f:.3125f));
            assert(f(n+0x128)==1-darkness&&f(n+0x134)==.6f);
        }
        for(unsigned i=8;i<14;++i) {
            auto n=m.words[r+0x140+i*4];assert(f(n+0x128)==0);
            assert(std::abs(f(n+0x134)-.6f*(.32f-.09f*((i-8)%3)))<.0001f);
        }
        assert(h.panel_scratch.End(m)==sp&&m.words==before);
    }
    for(unsigned mode=0;mode<5;++mode) {
        h.dungeon=mode!=0;h.pass=mode==1?3:1;h.routed=mode==2?std::vector<U32>{}:std::vector<U32>{layout};
        D::side_panels=mode!=3;D::panel_darkness=mode==4?NAN:0;
        auto before=m.words;assert(h.PanelRoot(m,layout,sp)==layout+8);
        assert(!h.panel_scratch.End(m)&&m.words==before);
    }
    h.valid=h.dungeon=true;h.pass=1;h.routed={layout};
    for(bool sides:{false,true})for(float scale:{.2f,.3125f,.7f}) {
        o.side_panels=sides;o.top_strip=true;o.top_panel={-15,9,scale};o.panel_darkness=.7f;D::Apply(o);
        const auto before=m.words;auto root=h.PanelRoot(m,layout,sp);
        const unsigned first=sides?14:0;
        assert(root==sp-0x3000&&m.words[root+0x18]==first+8);
        auto f=[&](U32 p){return std::bit_cast<float>(m.words[p]);};
        float previous_right=-15;
        for(unsigned i=0;i<8;++i) {
            auto n=m.words[root+0x140+(first+i)*4],g=m.words[n+0x28];
            const float left=f(n+0xec)+f(g+0x14)*f(n+0xe0);
            assert(std::abs(left-previous_right)<.0001f); // no gaps at any seam
            previous_right=left+f(g+0x20)*f(n+0xe0);
            assert(f(n+0xfc)==9&&f(n+0xe0)==scale&&f(n+0xf4)==scale);
            assert(f(g+0x24)==240&&f(g+0x30)==(i%2?-.3125f:.3125f));
            assert(std::abs(f(n+0x128)-.3f)<.0001f&&f(n+0x134)==.6f);
        }
        assert(std::abs(previous_right-(-15+1280*scale))<.0001f);
        assert(h.panel_scratch.End(m)==sp&&m.words==before);
        h.dungeon=false;assert(h.PanelRoot(m,layout,sp)==layout+8);
        assert(!h.panel_scratch.End(m)&&m.words==before);h.dungeon=true;
    }
    // Split the border around the fixed map window, without nesting the UI.
    // It runs at the start of layout pass 2, before all banner/text widgets.
    auto map=m.Layout("BackGround"),bg_group=m.Widget("backGround"),map_pane=m.Widget("map_base"),map_geo=m.Alloc(0x7c);
    m.Children(map+8,{bg_group});m.Children(bg_group,{map_pane});m.words[map_pane+0x28]=map_geo;
    m.words[map_geo+4]=6;m.words[map_geo+0x20]=std::bit_cast<U32>(320.f);
    m.words[map_geo+0x24]=std::bit_cast<U32>(240.f);
    m.words[map_pane+0x134]=std::bit_cast<U32>(.6f);
    auto menu=m.Layout("MenuMapDungeon"),content=m.Widget("content_and_prompts");
    h.panel=menu;h.backgrounds[1]=map;h.routed={layout,map,menu};h.border_overlay=true;
    o.side_panels=true;o.top_strip=true;o.panels={{{-20,120,.44f},{288,120,.44f}}};
    o.top_panel={-45,0,.529f};D::Apply(o);
    for(auto node:source) {
        m.words[node+0x110]=m.words[node+0x120]=std::bit_cast<U32>(1.f);
        m.words[node+0x114]=m.words[node+0x11c]=0;
    }
    for(float scale:{.75f,1.f,1.1f}) {
        h.frame[D::Background]={40,28,scale};
        auto window=h.MapWindow(m);assert(window&&(*window)[0]==40&&(*window)[1]==40+320*scale&&(*window)[2]==28);
        // Panning and zooming the floor never moves the border.
        h.frame[D::Map]={-400,900,3};assert(h.MapWindow(m)==window);
        std::array<float,22> back_lengths{};
        for(bool front:{false,true}) {
            h.pass=front?2:1;auto before=m.words;
            auto root=h.PanelRoot(m,layout,sp);
            assert(root==sp-0x3000&&m.words[root+0x18]==22);
            for(unsigned i=0;i<22;++i) {
                auto n=m.words[root+0x140+i*4],g=m.words[n+0x28];
                const bool top=i>=14;const unsigned side=i<8?i/4:(i-8)/3,axis=top?1:0;
                auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
                const bool visible=m.words[n+0xd4]&1;
                const float begin=f(n+(axis?0xfc:0xec))+f(g+0x14+axis*4)*f(n+(axis?0xf4:0xe0));
                const float length=visible?f(g+0x20+axis*4)*f(n+(axis?0xf4:0xe0)):0;
                const float edge=(*window)[top?2:side?1:0];
                if(visible) {
                    const bool before_edge=front?(top||side==0):(!top&&side==1);
                    if(before_edge)assert(begin+length<=edge+.0001f);else assert(begin>=edge-.0001f);
                    // Both end points still sample their original texels.
                    const float original_scale=top?o.top_panel.scale:o.panels[side].scale;
                    const float original_origin=axis?o.top_panel.y:o.panels[side].x;
                    for(float t:{0.f,1.f}) {
                        const float uv=f(g+0x28+axis*4)+t*f(g+0x30+axis*4);
                        const float actual=f(n+0x110+axis*16)*uv+f(n+(axis?0x124:0x118));
                        const float original_uv=axis?(begin+t*length-original_origin)/original_scale:
                            f(g+0x28)+(begin+t*length-original_origin-f(g+0x14)*original_scale)/(f(g+0x20)*original_scale)*f(g+0x30);
                        // Shadow bands use a different geometry/UV baseline;
                        // regular panel halves must never stretch their artwork.
                        if(i<8)assert(std::abs(actual-original_uv)<.0001f);
                    }
                }
                if(!front)back_lengths[i]=length;
                else {
                    const float full=top?240*o.top_panel.scale:(i>=8?320.f:160.f)*o.panels[side].scale;
                    assert(std::abs(back_lengths[i]+length-full)<.0001f);
                }
            }
            if(front) {
                const auto border_state=m.words;
                // Map materials must never be attached to the brown layout.
                assert(h.ShadowRoot(m,layout,root,root)==root);
                assert(!h.shadow_scratch.End(m)&&m.words==border_state);
                h.Begin(m,map);
                const auto layered=h.ShadowRoot(m,map,root);
                assert(layered!=map+8&&m.words[layered+0x18]==24);
                const auto children=m.words[layered+0x14];
                assert(m.words[root+0x18]==22); // border remains a separate draw
                for(unsigned side=0;side<2;++side)for(unsigned band=0;band<12;++band) {
                    auto n=m.words[children+(side*12+band)*4];
                    auto f=[&](U32 at){return std::bit_cast<float>(m.words[at]);};
                    const float width=16*scale*MM3D::NativeInventory::shadow_width/12.f;
                    const float expected=side?(*window)[0]-(band+1)*width:(*window)[1]+band*width;
                    assert(std::abs(f(n+0xec)-expected)<.001f);
                    assert(f(n+0xfc)==28&&f(n+0xf4)==scale);
                }
                assert(h.shadow_scratch.End(m)==root);h.End(m);assert(m.words==border_state);
                // Native transforms can retain signed zero from an earlier
                // zoom/minimap pose. Equivalent numeric zeros must draw the
                // same shadow, without requiring a zoom cycle to rewrite them.
                for(float zero:{0.f,-0.f}) {
                    auto signed_state=m.words;
                    m.words[map_pane+0xe4]=std::bit_cast<U32>(zero);
                    m.words[map_pane+0xf0]=std::bit_cast<U32>(zero);
                    const auto original=m.words;
                    const auto shadows=h.ShadowRoot(m,map,root);
                    assert(shadows!=map+8&&m.words[shadows+0x18]==24);
                    assert(h.shadow_scratch.End(m)==root&&m.words==original);
                    m.words=signed_state;
                }
                // Zero width/opacity produces no backdrop or shadow in pass 2.
                for(auto* setting:{&MM3D::NativeInventory::shadow_width,&MM3D::NativeInventory::shadow_opacity}) {
                    const float previous=setting->exchange(0);
                    const auto empty=h.ShadowRoot(m,map,root);
                    assert(empty!=map+8&&m.words[empty+0x18]==0&&m.words[empty+0x28]==0);
                    assert(h.shadow_scratch.End(m)==root&&m.words==border_state);
                    *setting=previous;
                }
            }
            assert(h.panel_scratch.End(m)==sp&&m.words==before);
        }
        h.pass=1;auto before=m.words;
        assert(h.ShadowRoot(m,map,sp)==map+8&&!h.shadow_scratch.End(m)&&m.words==before);
    }
    // The late menu pass must be a complete no-op, including composed prompts.
    h.pass=3;auto before=m.words;
    assert(h.PanelRoot(m,menu,sp,content)==content);assert(!h.panel_scratch.End(m)&&m.words==before);
    assert(h.ShadowRoot(m,menu,sp,content)==content);assert(!h.shadow_scratch.End(m)&&m.words==before);
    h.pass=2;m.words[map_geo+0x20]=0;auto invalid=m.words;
    assert(h.PanelRoot(m,layout,sp)==layout+8);assert(!h.panel_scratch.End(m)&&m.words==invalid);
    m.words=before;
    // Real queue routing: the border goes before even priority-zero header
    // widgets. The existing order and contents of both UI queues are retained.
    auto owner=m.Alloc(0x800),menu_all=m.Widget("all");std::vector<U32> groups;
    for(const char* name:{"icon_touch_direct","cursor_loupe_g","disp_no_map_g","disp_wing_g","item_set_base_g","slide_gauge_g","btn_place_name_g","btn_expansion_g","btn_reduction_g","btn_return_l","bb_map_area_g"})groups.push_back(m.Widget(name,80,60));
    m.Children(menu+8,{menu_all});m.Children(menu_all,groups);
    m.words[owner+0x18]=menu;m.words[owner+0x10]=layout;m.words[owner+0x14]=map;
    auto manager=m.Alloc(0x30);std::array<U32,6> queues{};
    for(unsigned i=0;i<6;++i){queues[i]=m.Alloc(0x848);m.words[manager+8+i*4]=queues[i];}
    auto banner=m.Layout("SubMenuTitle"),labels=m.Layout("Labels"),controls=m.Layout("Controls");
    m.words[queues[2]+4]=banner;m.words[queues[2]+0x804]=1;m.words[queues[2]+0x844]=1;
    m.words[queues[2]+4+0x80]=labels;m.words[queues[2]+0x808]=1;m.words[queues[2]+0x844]=2;
    m.Queue(queues[3],{controls});m.Queue(queues[4],{layout,map});m.Queue(queues[5],{menu});
    D::Apply(o);h.Capture(m,owner);auto native=m.words;h.Pass(m,manager,4);
    assert(h.border_overlay);
    assert(m.words[queues[2]+4]==layout&&m.words[queues[2]+8]==map&&m.words[queues[2]+12]==menu&&m.words[queues[2]+16]==banner);
    assert(m.words[queues[2]+0x804]==4&&m.words[queues[2]+0x844]==5);
    assert(m.words[queues[2]+4+0x80]==labels);
    assert(m.words[queues[3]+4+0x80]==controls&&m.words[queues[3]+8+0x80]==menu);
    for(auto node:groups)for(unsigned off=0;off<0x140;off+=4)assert(m.words[node+off]==native[node+off]);
    // Early pass draws only the red direction-arrow branch; late pass keeps
    // all controls/nameplates but cannot redraw arrows over the description.
    assert(h.arrows_early);
    const auto routed_state=m.words;
    for(unsigned pass:{2u,3u}) {
        h.Pass(m,manager,pass);h.Begin(m,menu);
        for(unsigned i=0;i<groups.size();++i) {
            const bool visible=m.words[groups[i]+0xd4]&1;
            assert(visible==(i!=7&&i!=8&&((pass==2)==(i==0))));
        }
        h.End(m);assert(m.words==routed_state);
    }
    // Previously patched archives keep the inner shadows visible after a zoom
    // cycle. Hide only that branch in enlarged modes, including first entry;
    // preserve overview fades, controls, and all guest state after each draw.
    for(unsigned zoom:{1u,3u,2u,3u,1u,0u,4u})for(bool visible:{false,true}) {
        m.words[0x36e2a0]=0x7781d8;m.words[0x7781d8+0x520]=(zoom<<24)|0x123456;
        m.words[groups[10]+0xd4]=visible?15:14;
        m.words[groups[10]+0x134]=std::bit_cast<U32>(.4f);
        const auto before_zoom=m.words;
        for(unsigned pass:{2u,3u}) {
            h.Pass(m,manager,pass);h.Begin(m,menu);
            for(unsigned i=0;i<groups.size();++i) {
                const bool expected=i!=7&&i!=8&&((pass==2)==(i==0))&&
                    (i!=10||(visible&&zoom!=1&&zoom!=2));
                assert(bool(m.words[groups[i]+0xd4]&1)==expected);
            }
            assert(m.words[groups[10]+0x134]==std::bit_cast<U32>(.4f));
            h.End(m);assert(m.words==before_zoom);
        }
    }
    m.words=routed_state;
    // An already hidden direction group stays hidden, and its native flags
    // survive both draws. No visibility is forced on while closing the menu.
    m.words[groups[0]+0xd4]&=~1u;const auto hidden=m.words;
    for(unsigned pass:{2u,3u}) {
        h.Pass(m,manager,pass);h.Begin(m,menu);
        assert(!(m.words[groups[0]+0xd4]&1));h.End(m);assert(m.words==hidden);
    }
    // Queue saturation cannot partially route a page or lose existing widgets.
    m.words=native;m.words[queues[2]+0x804]=30;m.words[queues[2]+0x844]=31;
    for(unsigned i=0;i<30;++i)m.words[queues[2]+4+i*4]=banner;
    h.Capture(m,owner);auto full=m.words;h.Pass(m,manager,4);assert(!h.border_overlay&&!h.arrows_early&&m.words==full);
    // The first Map draw must select the marker atlas/size for the current
    // zoom even if the cached style still belongs to the gameplay minimap.
    // Scope the override to geometry generation and restore it on every exit.
    {
        Memory draw;D::Hook map_hook;map_hook.valid=true;
        constexpr U32 marker=0x727970+0x834,mode=0x7781d8+0x520;
        draw.words[MM3D::NativeMinimap::Count(6)]=0;
        draw.words[0x36e2a0]=0x7781d8;
        for(bool dungeon:{false,true})for(unsigned zoom:{0u,1u,2u,3u,4u})
            for(unsigned stale:{0u,1u}) {
                map_hook.dungeon=dungeon;
                draw.words[mode]=(zoom<<24)|0x456789;
                draw.words[marker]=(stale<<24)|0x123456;
                const auto initial=draw.words;
                map_hook.BeforeMap(draw);
                const auto expected=dungeon&&zoom>=1&&zoom<=3?unsigned(zoom!=3):stale;
                assert(draw.words[marker]==((expected<<24)|0x123456));
                assert(draw.words[mode]==initial.at(mode)); // never changes zoom
                map_hook.AfterMap(draw);assert(draw.words==initial);
                map_hook.AfterMap(draw);assert(draw.words==initial);
            }
        map_hook.dungeon=true;draw.words[mode]=0x01000000;draw.words[marker]=0x123456;
        const auto initial=draw.words;map_hook.BeforeMap(draw);map_hook.valid=false;
        map_hook.AfterMap(draw);assert(draw.words==initial); // even if routing rejects
        map_hook.valid=true;draw.words.erase(marker);const auto incomplete=draw.words;
        map_hook.BeforeMap(draw);map_hook.AfterMap(draw);assert(draw.words==incomplete);
    }
    // The description is translucent, so shadows must not occupy its plate
    // at all, even if drawn earlier. Use live moved/scaled world rectangles.
    {
        Memory mem;D::Hook hook;
        hook.valid=hook.dungeon=hook.border_overlay=true;hook.pass=2;
        hook.owner=mem.Alloc(0x800);hook.panel=mem.Layout("MenuMapDungeon");
        mem.words[hook.owner+0x18]=hook.panel;
        hook.controller=mem.Alloc(0x14);auto context=mem.Alloc(0x170),state=mem.Alloc(4);
        mem.words[hook.controller+8]=hook.owner;mem.words[hook.controller+4]=context;mem.words[context+0x16c]=state;
        auto desc=mem.Layout("MessageWindow",0,0,0x448),plate=mem.Widget("plate",20,150),geo=mem.Alloc(0x7c);
        mem.words[0x6b36f8+0x2c]=desc;mem.words[desc+0x444]=state;mem.words[desc+0x3f4]=0x17ac;
        mem.Children(desc+8,{plate});mem.words[plate+0x28]=geo;mem.words[plate+0x134]=std::bit_cast<U32>(1.f);
        mem.words[geo+4]=6;mem.words[geo+0x20]=std::bit_cast<U32>(360.f);mem.words[geo+0x24]=std::bit_cast<U32>(70.f);
        auto backdrop=mem.Layout("BackGround"),group=mem.Widget("backGround"),pane=mem.Widget("map_base",40,0),pg=mem.Alloc(0x7c);
        mem.Children(backdrop+8,{group});mem.Children(group,{pane});mem.words[pane+0x28]=pg;
        mem.words[pane+0x134]=std::bit_cast<U32>(1.f);mem.words[pg+4]=6;
        mem.words[pg+0x20]=std::bit_cast<U32>(320.f);mem.words[pg+0x24]=std::bit_cast<U32>(240.f);
        hook.backgrounds[1]=backdrop;hook.routed={backdrop};
        mem.words[0x36e2a0]=0x7781d8;mem.words[0x6890f4]=mem.Alloc(32);
        mem.words[0x6890f4+0x54]=2;mem.words[0x6890f4+0x5c]=0x100;mem.words[0x7781d8+0x520]=1u<<24;
        const auto identity=hook.InterpolationIdentity(mem,hook.panel);assert(identity);
        assert(!hook.InterpolationIdentity(mem,0));
        mem.words[0x6890f4+0x5c]=0x200;assert(hook.InterpolationIdentity(mem,hook.panel)!=identity);
        mem.words[hook.owner+0x78c]=0x100;assert(!hook.InterpolationIdentity(mem,hook.panel));
        mem.words[hook.owner+0x78c]=0;
        const auto stack=(mem.Alloc(0x20000)+0x20000)&~7u;
        for(float dx:{0.f,20.f,350.f})for(float factor:{.5f,1.f,1.4f}) {
            mem.words[plate+0xec]=std::bit_cast<U32>(20.f+dx);mem.words[plate+0xe0]=std::bit_cast<U32>(factor);
            const auto cover=hook.DescriptionBounds(mem);assert(cover&&(*cover)[0]==20+dx&&(*cover)[2]==20+dx+360*factor);
            auto original=mem.words;const auto root=hook.ShadowRoot(mem,backdrop,stack);assert(root==stack-0xc400);
            const auto count=mem.words[root+0x18],children=mem.words[root+0x14];assert(count>0&&count<=96);
            float area=0;
            for(U32 i=0;i<count;++i) {
                const auto n=mem.words[children+i*4],g=mem.words[n+0x28];
                const auto f=[&](U32 at){return std::bit_cast<float>(mem.words.at(at));};
                const float x=f(n+0xec)+f(g+0x14)*f(n+0xe0),y=f(n+0xfc)+f(g+0x18)*f(n+0xf4),
                    w=f(g+0x20)*f(n+0xe0),h=f(g+0x24)*f(n+0xf4);
                assert(x+w<=(*cover)[0]+.001f||x>=(*cover)[2]-.001f||y+h<=(*cover)[1]+.001f||y>=(*cover)[3]-.001f);
                area+=w*h;
            }
            float expected=0;
            for(const D::Hook::Rectangle side: {D::Hook::Rectangle{24,0,40,240},D::Hook::Rectangle{360,0,376,240}})
                for(auto piece:D::Hook::Outside(side,cover))expected+=(piece[2]-piece[0])*(piece[3]-piece[1]);
            assert(std::abs(area-expected)<.1f); // no missing or overlapping strips
            assert(hook.shadow_scratch.End(mem)==stack&&mem.words==original);
        }
        // The actual description can differ from the manager's currently
        // selected window. Track the map's calculated layout, not that slot.
        hook.RememberDescription(mem,desc);assert(hook.description_layout==desc);
        mem.words[0x6b36f8+0x2c]=mem.Layout("MessageWindow",0,0,0x448);
        assert(hook.DescriptionBounds(mem));
        // Tiny off-axis animation terms and mirrored plate bases must not
        // disable the shadow exclusion for the entire description.
        mem.words[plate+0xe4]=std::bit_cast<U32>(.000001f);
        mem.words[plate+0xf0]=std::bit_cast<U32>(.000001f);
        assert(hook.DescriptionBounds(mem));
        mem.words[plate+0xe0]=std::bit_cast<U32>(-1.f);
        assert(hook.DescriptionBounds(mem));
        mem.words[plate+0x134]=0;assert(!hook.DescriptionBounds(mem));
        mem.words[plate+0x134]=std::bit_cast<U32>(1.f);mem.words[desc+0x3f4]=0x1700;
        assert(!hook.DescriptionBounds(mem)); // other messages cannot clip shadows
    }
    {
        // Arrow controls compose with the pre-existing Map pose, scaling about
        // each visual center instead of pulling the four arrows together.
        Memory mem;D::Hook hook;auto root=mem.Widget("icon_touch_direct");
        std::array<U32,4> panes{};std::vector<U32> children;
        for(unsigned i=0;i<4;++i) {
            const auto name=std::string("icon_")+D::ArrowNames[i]+"_direct";
            auto group=mem.Widget(name.c_str()),pane=mem.Widget((name+"_pane").c_str(),20+50*i,30+10*i);
            panes[i]=pane;children.push_back(group);mem.Children(group,{pane});
            auto geo=mem.Alloc(0x7c);mem.words[pane+0x28]=geo;mem.words[geo+4]=6;
            mem.words[geo+0x14]=std::bit_cast<U32>(-4.f);mem.words[geo+0x18]=std::bit_cast<U32>(-6.f);
            mem.words[geo+0x20]=std::bit_cast<U32>(8.f);mem.words[geo+0x24]=std::bit_cast<U32>(12.f);
            mem.words[pane+0x134]=std::bit_cast<U32>(.65f);
        }
        auto touch=mem.Widget("map_touch_icon_rect_n",100,120);children.push_back(touch);mem.Children(root,children);
        const auto original=mem.words;
        D::Options arrows;arrows.poses[D::Map]={41,-9,1.5f};hook.frame=arrows.poses;
        for(float scale:{1.f,.5f,2.f}) {
            arrows.arrow_scale=scale;arrows.arrow_offsets={{{7,-4},{-9,6},{15,11},{-12,-3}}};D::Apply(arrows);
            std::vector<D::Hook::Write> writes;std::vector<U32> seen;
            assert(hook.helper.Transform(mem,root,writes,seen,{1.5f,0,0,1.5f,41,-9}));
            assert(hook.AdjustArrows(mem,root,writes));
            for(auto w:writes)mem.Write(w.address,w.after);
            const auto f=[&](U32 at){return std::bit_cast<float>(mem.words[at]);};
            for(unsigned i=0;i<4;++i) {
                assert(std::abs(f(panes[i]+0xec)-(41+1.5f*(20+50*i)+arrows.arrow_offsets[i][0]))<.0001f);
                assert(std::abs(f(panes[i]+0xfc)-(-9+1.5f*(30+10*i)+arrows.arrow_offsets[i][1]))<.0001f);
                assert(f(panes[i]+0xe0)==1.5f*scale&&f(panes[i]+0xf4)==1.5f*scale);
                assert(f(panes[i]+0x134)==.65f); // native fade and visibility untouched
            }
            assert(f(touch+0xec)==191&&f(touch+0xfc)==171&&f(touch+0xe0)==1.5f);
            for(auto it=writes.rbegin();it!=writes.rend();++it)mem.Write(it->address,it->before);
            assert(mem.words==original);
        }
        arrows.arrow_scale=1;arrows.arrow_offsets={};D::Apply(arrows);
        std::vector<D::Hook::Write> writes;assert(hook.AdjustArrows(mem,root,writes)&&writes.empty());
    }
    D::Apply(D::Options{});
    DungeonMapShadowChecks();NotebookPromptChecks();
    std::cout<<"Dungeon map border layering checks passed\n";
}
