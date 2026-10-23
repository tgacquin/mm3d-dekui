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
        m.words[archive+12]=99;assert(T::Get(m,"Songs")=="Songs");
    }
    NotebookPromptChecks();
    std::cout<<"Localized accents, active language, glyph bounds, stationary ocarina prompts and English presentation checks passed.\n";
}
