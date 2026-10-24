// SPDX-License-Identifier: GPL-2.0-or-later
#define main ExistingInventoryChecks
#include "native_inventory_check.cpp"
#undef main
int main() {
    namespace T=MM3D::PromptText;
    assert(T::Codes("Détails")==U"Détails");
    assert(T::Codes("pestaña")==U"pestaña");
    assert(T::Codes("\xc0\x80").empty());
    assert(T::Codes("\xe2\x82").empty());
    for(unsigned language:{2u,4u,8u}) {
        for(auto e:T::Entries)assert(!T::Codes(T::Translate(language,e.english)).empty());
        Memory m;
        const auto archive=m.Alloc(16);m.words[0x6b373c]=archive;
        m.words[archive]=0x47534d47;m.words[archive+4]=1;m.words[archive+12]=language;
        assert(T::Language(m)==language);
        const auto font=m.Alloc(0x14),header=m.Alloc(0x2c),pages=m.Alloc(8);
        std::u32string chars;
        for(auto e:T::Entries){chars+=T::Codes(T::Translate(language,e.english));chars+=T::Codes(e.english);}
        chars+=U"ZRL -+/";
        std::sort(chars.begin(),chars.end());chars.erase(std::unique(chars.begin(),chars.end()),chars.end());
        auto records=m.Alloc(chars.size()*12);
        m.words[font+4]=header;m.words[font+0xc]=pages;m.words[font+0x10]=records;
        m.words[header+8]=8u<<16;m.words[header+12]=12;m.words[header+0x10]=1;m.words[header+0x14]=chars.size();
        m.words[header+0x1c]=4;m.words[header+0x24]=16u<<16;m.words[header+0x28]=16;
        m.words[pages]=0x1000;m.words[pages+4]=256u|(256u<<16);
        for(unsigned i=0;i<chars.size();++i) {
            m.words[records+i*12]=chars[i];m.words[records+i*12+4]=chars[i]==' '?4:10;
            m.words[records+i*12+8]=1u|(((i%16)|((i/16)<<8))<<16);
        }
        const auto layout=m.Layout("MenuOcarina"),icon=m.Widget("icon"),text=m.Widget("text"),ig=m.Alloc(0x7c),tg=m.Alloc(0x30c);
        m.words[icon+0x28]=ig;m.words[text+0x28]=tg;m.words[tg+0x28]=font;
        const auto sp=(m.Alloc(0x6008)+0x6008)&~7u;
        MM3D::OcarinaPrompt::Hook hook;hook.pass=3;hook.panel=layout;hook.icon=icon;hook.text=text;hook.texture=pages;
        hook.native_alpha=1;hook.opened=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        MM3D::OcarinaPrompt::Apply({});
        std::array<U32,4> anchors{};
        for(bool list:{false,true,false}) {
            hook.list_mode=list;const auto before=m.words;
            auto root=hook.Root(m,layout,sp,false);assert(root!=layout+8);
            for(unsigned row=0;row<2;++row) {
                auto children=m.words[root+0x14],n=m.words[children+row*16+4],g=m.words[n+0x28];
                auto label=T::Codes(T::Translate(language,row?(list?"Back":"Cancel"):"Songs"));
                assert(m.words[g+0x300]==label.size());
                const auto start=m.words[g+0x2fc];float pen=0;
                for(unsigned i=0;i<label.size();++i) {
                    auto glyph=MM3D::NotebookFont::Lookup(m,font,label[i],pen);assert(glyph);
                    for(unsigned j=0;j<15;++j)assert(m.words[start+i*60+j*4]==glyph->words[j]);
                    pen+=glyph->advance;
                }
                const float right=std::bit_cast<float>(m.words[n+0xec])+pen*std::bit_cast<float>(m.words[n+0xe0]);
                assert(right<=388.01f);
                for(unsigned kind=0;kind<2;++kind) {
                    auto at=m.words[children+row*16+kind*4]+0xec;
                    if(!anchors[row*2+kind])anchors[row*2+kind]=m.words[at];
                    assert(anchors[row*2+kind]==m.words[at]);
                }
            }
            assert(hook.End(m)==sp&&m.words==before);
        }
        namespace P=MM3D::NotebookPrompt;
        const auto owner=m.Alloc(0x800),pictogram=m.Layout("Pictogram"),all=m.Widget("all"),pict=m.Widget("pict"),anchor=m.Widget("disp_a_icon_n");
        m.Children(pictogram+8,{all});m.Children(all,{pict});
        m.words[owner+0x2a8]=pictogram;m.words[owner+0x290]=anchor;m.words[pictogram+0x180]=anchor;
        m.words[pict+0x28]=ig;m.words[ig+4]=6;m.words[ig+0x10]=pages;
        m.words[P::RegistryLiteral]=P::Registry;m.words[tg+4]=3;
        P::enabled=P::map_enabled=P::schedule_enabled=true;
        P::pose.x=300;P::pose.y=0;P::pose.scale=.6f;
        P::map_pose.x=300;P::map_pose.y=-20;P::map_pose.scale=.6f;
        for(bool schedule:{false,true}) {
            auto panel=m.Layout(schedule?"MenuSchedule":"MenuQuestlist"),a=m.Widget("all"),detail=m.Widget(schedule?"sche_quest_detail":"quest_list_detail");
            m.Children(panel+8,{a});m.Children(a,{detail});m.Children(detail,{text});
            P::Hook notebook;const auto before=m.words;
            auto root=notebook.Build(m,panel,owner,sp,1);assert(root!=panel+8);
            const auto children=m.words[root+0x14];
            for(unsigned i=0;i<8;++i) {
                auto n=m.words[children+i*4],g=m.words[n+0x28];
                if((m.words[g+4]&65535)!=3)continue;
                const auto count=m.words[g+0x300];assert(count>0&&count<=18);
                const auto start=m.words[g+0x2fc];
                const float end=std::bit_cast<float>(m.words[n+0xec])+count*10*std::bit_cast<float>(m.words[n+0xe0]);
                if(language!=2)assert(end<=400.f);
                assert(start+count*60<=sp);
            }
            assert(notebook.End(m)==sp&&m.words==before);
        }
        {
            namespace H=MM3D::MenuHelp;
            auto panel=m.Layout("MenuItem"),all=m.Widget("all"),groups=m.Alloc(8),entries=m.Alloc(48),parts=m.Alloc(0x60);
            m.Children(panel+8,{all});m.Children(all,{icon,text});m.words[all+0x134]=std::bit_cast<U32>(1.f);
            m.words[P::Registry+0x28]=groups;m.words[P::Registry+0x2c]=entries;
            m.words[groups]=12;m.words[entries+44]=parts;m.words[parts+4]=11u<<16;
            MM3D::NativeInventory::Hook inventory;inventory.panel=panel;inventory.pass=3;inventory.routed={panel};
            MM3D::NativeInventory::enabled=true;
            H::Options options;options.horizontal=true;options.pose={150,220,.25f};options.spacing=20;
            H::Apply(options);
            auto stack=(m.Alloc(0x12000)+0x12000)&~7u;
            for(bool clicks:{false,true,false})for(float gap:{20.f,80.f}) {
                MM3D::StickControls::Configure(clicks);
                H::spacing=gap;H::Hook help;const auto before=m.words;
                auto root=help.Root(m,inventory,panel,stack,panel+8);
                assert(root!=panel+8);
                auto children=m.words[root+0x14];U32 text_scale=0;float last_right=150;
                for(unsigned i=0;i<(clicks?20u:28u);i+=2) {
                    auto n=m.words[children+4*i],g=m.words[n+0x28];
                    const float left=std::bit_cast<float>(m.words[n+0xec]);
                    assert(left>=last_right-.01f);
                    float width=16;
                    if((m.words[g+4]&65535)==3) {
                        if(!text_scale)text_scale=m.words[n+0xe0];
                        assert(text_scale==m.words[n+0xe0]);
                        auto count=m.words[g+0x300],records=m.words[g+0x2fc];
                        width=std::bit_cast<float>(m.words[records+(count-1)*60+20])+1+10;
                    }
                    last_right=left+width*std::bit_cast<float>(m.words[n+0xe0]);
                }
                if(language!=2)assert(last_right<=396.01f);
                if(clicks)for(unsigned row=0;row<2;++row) {
                    const auto n=m.words[children+(16+row*2)*4],g=m.words[n+0x28],records=m.words[g+0x2fc];
                    const auto label=T::Codes(H::EquipLabel(language,row));
                    assert(m.words[g+0x300]==label.size());
                    assert(label.substr(0,5)==(row?U"ZR - ":U"ZL - "));
                    float pen=0;
                    for(unsigned i=0;i<label.size();++i) {
                        const auto glyph=MM3D::NotebookFont::Lookup(m,font,label[i],pen);assert(glyph);
                        for(unsigned j=0;j<15;++j)assert(m.words[records+i*60+j*4]==glyph->words[j]);
                        pen+=glyph->advance;
                    }
                }
                assert(help.scratch.End(m)==stack&&m.words==before);
            }
            H::Apply({});
        }
        {
            // Dungeon A zoom uses the same active-language labels and native
            // font/atlas as world-map X zoom, including restored zoom states.
            namespace W=MM3D::WorldMapPrompt;
            MM3D::DungeonMap::Hook map;W::Hook zoom;
            map.valid=map.dungeon=true;map.pass=3;map.owner=m.Alloc(0x800);map.controller=m.Alloc(0x20);
            map.panel=m.Layout("MenuMapDungeon");map.routed={map.panel};
            m.Children(map.panel+8,{icon,text});
            m.words[map.controller+8]=map.owner;m.words[map.owner+0x18]=map.panel;
            map.backgrounds[1]=m.Layout("BackGround");auto bg=m.Widget("backGround"),pane=m.Widget("map_base");
            m.Children(map.backgrounds[1]+8,{bg});m.Children(bg,{pane});m.words[pane+0x134]=std::bit_cast<U32>(.6f);
            m.words[0x1b2764]=0x7781d8;
            MM3D::DungeonMap::Options shared_options;
            shared_options.prompts={true,-17,-32,.7f,1,20};MM3D::DungeonMap::Apply(shared_options);
            std::optional<U32> zoom_icon_x,zoom_text_x;
            for(unsigned mode:{3u,1u,3u,2u,0u,4u}) {
                m.words[0x7781d8+0x520]=mode<<24;const auto before=m.words;
                auto root=zoom.Root(m,map,map.panel,sp,map.panel+8,false);
                if(mode>=1&&mode<=3) {
                    assert(root==sp-0x2400&&m.words[root+0x18]==5);
                    auto children=m.words[root+0x14],button=m.words[children],label=m.words[children+4],geometry=m.words[label+0x28];
                    assert(m.words[children+16]==map.panel+8);
                    assert(std::bit_cast<float>(m.words[m.words[button+0x28]+0x28])==112.f/256);
                    if(!zoom_icon_x){zoom_icon_x=m.words[button+0xec];zoom_text_x=m.words[label+0xec];}
                    assert(m.words[button+0xec]==*zoom_icon_x&&m.words[label+0xec]==*zoom_text_x);
                    assert(std::bit_cast<float>(m.words[button+0xe0])==.7f);
                    assert(std::bit_cast<float>(m.words[label+0x134])==.6f);
                    auto translated=T::Codes(T::Get(m,mode==3?"Zoom in":"Zoom out"));
                    assert(m.words[geometry+0x300]==translated.size());
                    auto records=m.words[geometry+0x2fc];float pen=0;
                    for(unsigned i=0;i<translated.size();++i) {
                        auto glyph=MM3D::NotebookFont::Lookup(m,font,translated[i],pen);assert(glyph);
                        for(unsigned j=0;j<15;++j)assert(m.words[records+i*60+j*4]==glyph->words[j]);
                        pen+=glyph->advance;
                    }
                } else assert(root==map.panel+8);
                zoom.End(m);assert(m.words==before);
            }
            m.words[0x7781d8+0x520]=1u<<24;
            for(unsigned guard=0;guard<5;++guard) {
                const auto normal=m.words;map.pass=guard==0?2:3;
                if(guard==1)m.words[map.owner+0x78c]=0x100; // overworld: no A prompt
                if(guard==2)m.words[map.owner+0x78c]=0x10000; // Soaring
                if(guard==3)m.words[map.controller+8]=0;
                if(guard==4)m.words[pane+0x134]=0; // fade follows map
                const auto before=m.words;
                assert(zoom.Root(m,map,map.panel,sp,map.panel+8,false)==map.panel+8);
                zoom.End(m);assert(m.words==before);m.words=normal;
            }
            // The live Circle Pad hint fades with actual zoom state, preserves
            // the A prompt below it, and restores both scratch allocations.
            const auto stack=(m.Alloc(0x9000)+0x9000)&~7u;
            W::Hook move;
            struct Step { unsigned mode; std::int64_t time; float alpha; };
            for(const auto step:std::array<Step,10>{{
                {1,10000,0},{1,85000,.5f},{2,160000,1},{3,170000,1},
                {3,245000,.5f},{1,245000,.5f},{1,320000,1},{3,330000,1},
                {3,405000,.5f},{3,480000,0}}}) {
                m.words[0x7781d8+0x520]=step.mode<<24;const auto before=m.words;
                const auto root=move.Root(m,map,map.panel,stack,map.panel+8,false,step.time);
                const auto zoom_root=stack-0x2400;
                if(step.alpha==0)assert(root==zoom_root);
                else {
                    assert(root==stack-0x4800);
                    const auto children=m.words[root+0x14],button=m.words[children],label=m.words[children+4];
                    const auto bg=m.words[button+0x28],geometry=m.words[label+0x28];
                    assert(m.words[children+16]==zoom_root);
                    assert(std::bit_cast<float>(m.words[bg+0x28])==208.f/256);
                    assert(std::bit_cast<float>(m.words[bg+0x2c])==16.f/128);
                    assert(std::bit_cast<float>(m.words[button+0xe0])==.7f);
                    assert(std::abs(std::bit_cast<float>(m.words[label+0x134])-.6f*step.alpha)<.0001f);
                    const auto translated=T::Codes(T::Get(m,"Move map"));
                    assert(m.words[geometry+0x300]==translated.size());
                    const auto records=m.words[geometry+0x2fc];float pen=0;
                    for(unsigned i=0;i<translated.size();++i) {
                        const auto glyph=MM3D::NotebookFont::Lookup(m,font,translated[i],pen);assert(glyph);
                        for(unsigned j=0;j<15;++j)assert(m.words[records+i*60+j*4]==glyph->words[j]);
                        pen+=glyph->advance;
                    }
                    assert(records+translated.size()*60<root+0x1600);
                }
                assert(move.End(m)==stack&&m.words==before);
            }
            // All four prompts share global transforms, but each nested root
            // must transform only its own panes (never the already drawn hints).
            using Pose=std::array<float,4>;
            const auto snapshot=[&](MM3D::OcarinaPrompt::Group group) {
                MM3D::DungeonMap::Options options;
                options.prompts={true,13,-7,.9f,1.1f,18};options.prompt_group=group;
                MM3D::DungeonMap::Apply(options);
                m.words[0x7781d8+0x520]=1u<<24;const auto before=m.words;
                const auto pair_root=map.PromptRoot(m,map.panel,stack,false);
                assert(pair_root!=map.panel+8);
                W::Hook all;all.move_fade.Update(map.owner,true,1000);
                const auto root=all.Root(m,map,map.panel,pair_root,pair_root,false,151000);
                assert(root==pair_root-0x4800);
                const auto zoom_root=m.words[m.words[root+0x14]+16];
                assert(m.words[m.words[zoom_root+0x14]+16]==pair_root);
                assert(m.words[m.words[pair_root+0x14]+32]==map.panel+8);
                std::array<Pose,8> result{};unsigned i=0;
                for(U32 r:{root,zoom_root,pair_root}) {
                    const auto children=m.words[r+0x14];
                    for(unsigned row=0;row<(r==pair_root?2u:1u);++row)for(unsigned part=0;part<2;++part) {
                        const auto node=m.words[children+row*16+part*4];
                        const auto shadow=m.words[children+row*16+8+part*4];
                        const float expected_alpha=std::bit_cast<float>(m.words[pane+0x134]);
                        assert(std::abs(std::bit_cast<float>(m.words[node+0x134])-expected_alpha)<.0001f);
                        assert(std::abs(std::bit_cast<float>(m.words[shadow+0x134])-expected_alpha*.8f)<.0001f);
                        for(unsigned c=0;c<3;++c) {
                            assert(std::bit_cast<float>(m.words[node+0x128+c*4])==1);
                            assert(std::bit_cast<float>(m.words[shadow+0x128+c*4])==0);
                        }
                        result[i++]={std::bit_cast<float>(m.words[node+0xec]),std::bit_cast<float>(m.words[node+0xfc]),
                            std::bit_cast<float>(m.words[node+0xe0]),std::bit_cast<float>(m.words[node+0xf4])};
                    }
                }
                assert(all.End(m)==pair_root&&map.map_prompts.End(m)==stack&&m.words==before);
                return result;
            };
            // Dungeon hints stay fully opaque regardless of the separate
            // inventory legend opacity; all four still follow the map fade.
            for(float menu_alpha:{1.f,.6f}) {
                m.words[pane+0x134]=std::bit_cast<U32>(menu_alpha);
                for(float alpha:{.85f,.3f,0.f,1.f}) {
                    MM3D::ItemsMapPrompt::opacity=alpha;
                    snapshot({});
                }
            }
            const auto baseline=snapshot({});
            for(unsigned row=1;row<4;++row) {
                // Same icon and text sizes/left edges, and one shared row gap,
                // even with differently sized localized labels.
                for(unsigned part=0;part<2;++part) {
                    const auto& first=baseline[part];const auto& current=baseline[row*2+part];
                    assert(std::abs(current[0]-first[0])<.001f);
                    assert(std::abs(current[2]-first[2])<.001f);
                    assert(std::abs(current[3]-first[3])<.001f);
                    assert(std::abs(current[1]-first[1]-(row==1?2:row==2?1:3)*18*.9f)<.001f);
                }
            }
            for(const auto group:std::array<MM3D::OcarinaPrompt::Group,3>{{{31,-12,1,1},{0,0,.75f,1},{-21,9,1.5f,1.25f}}}) {
                const auto adjusted=snapshot(group);
                for(unsigned i=0;i<baseline.size();++i) {
                    const bool icon=(i%2)==0;const float factor=icon?group.icon_scale:1;
                    const auto& p=baseline[i];
                    const float px=p[0]+(icon?8*p[2]*(1-factor):0),py=p[1]+(icon?8*p[3]*(1-factor):0);
                    assert(std::abs(adjusted[i][0]-(388+(px-388)*group.scale+group.x))<.001f);
                    assert(std::abs(adjusted[i][1]-(228+(py-228)*group.scale+group.y))<.001f);
                    assert(std::abs(adjusted[i][2]-p[2]*group.scale*factor)<.001f);
                    assert(std::abs(adjusted[i][3]-p[3]*group.scale*factor)<.001f);
                }
            }
            MM3D::ItemsMapPrompt::opacity=1;
            const auto normal_alpha=m.words[pane+0x134];m.words[pane+0x134]=0;
            const auto hidden_before=m.words;
            assert(map.PromptRoot(m,map.panel,stack,false)==map.panel+8);
            W::Hook hidden;
            assert(hidden.Root(m,map,map.panel,stack,map.panel+8,false,151000)==map.panel+8);
            hidden.End(m);map.map_prompts.End(m);assert(m.words==hidden_before);
            m.words[pane+0x134]=normal_alpha;
            MM3D::DungeonMap::Apply({});
            W::ZoomFade fade;
            assert(fade.Update(1,true,1000)==0);
            assert(fade.Update(1,true,151000)==1);
            assert(fade.Update(1,false,151000)==1); // duplicate time doesn't step
            assert(fade.Update(1,false,226000)==.5f);
            assert(fade.Update(2,true,226000)==0); // owner changed
            assert(fade.Update(2,true,376000)==1);
            assert(fade.Update(2,true,1000)==0); // save-state clock rewind
            assert(fade.Update(2,true,151000)==1);
            assert(fade.Update(2,true,1000000)==0); // reopened after a gap
            MM3D::DungeonMap::Apply({});
        }
        m.words[archive+12]=99;assert(T::Get(m,"Songs")=="Songs");
    }
    NotebookPromptChecks();
    std::cout<<"Localized accents, active language, glyph bounds, stationary ocarina prompts and English presentation checks passed.\n";
}
