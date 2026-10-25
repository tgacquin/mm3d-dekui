// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/profiles.h"
#include "mm3d/layout_reset.h"
#include "mm3d/notebook.h"
#include "mm3d/texture_picker.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace MM3D;
static void Check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
int main(int argc,char** argv) try {
    QCoreApplication app(argc,argv); QTemporaryDir directory;
    std::array<std::uint8_t,16> alpha{};
    alpha[5]=1; alpha[10]=255;
    std::array<float,4> region{0,0,1,1};
    Check(Textures::TrimVisible(alpha,4,4,region) && region==std::array<float,4>{.25f,.25f,.75f,.75f},"trim includes alpha=1");
    region={0,0,.25f,.25f}; const auto empty_region=region;
    Check(!Textures::TrimVisible(alpha,4,4,region) && region==empty_region,"empty region unchanged; outside pixels ignored");
    Check(directory.isValid(),"temporary directory");
    QByteArray fade(0x600,'\0');
    fade.replace(0,4,QByteArray("GAR\x02",4)); fade.replace(0x494,4,"MFL ");
    fade.replace(0x4A8,4,QByteArray("\xEC\0\0\0",4));
    fade.replace(0x580,4,QByteArray("\x04\0\x18\0",4)); fade.replace(0x20,8,"FadeMask");
    const float original_width=400.f,original_height=240.f;
    std::memcpy(fade.data()+0x590,&original_width,4); std::memcpy(fade.data()+0x594,&original_height,4);
    const auto fade_source=directory.filePath("Joker.gar"),fade_output=directory.filePath("patched.gar");
    QFile fade_file(fade_source); Check(fade_file.open(QIODevice::WriteOnly),"create synthetic FadeMask");
    fade_file.write(fade); fade_file.close();
    Check(Notebook::PatchFadeArchive(fade_source.toStdString(),fade_output.toStdString(),800.f).ok,"patch verified FadeMask");
    QFile patched(fade_output); Check(patched.open(QIODevice::ReadOnly),"read FadeMask patch");
    auto actual=patched.readAll(); float patched_width{}; std::memcpy(&patched_width,actual.constData()+0x590,4);
    Check(patched_width==800.f,"FadeMask test width");
    std::memcpy(actual.data()+0x590,&original_width,4); Check(actual==fade,"only FadeMask width changes");
    Check(!Notebook::PatchFadeArchive(fade_source.toStdString(),fade_source.toStdString(),800.f).ok,"protect original GAR");
    Check(!Notebook::PatchFadeArchive(fade_source.toStdString(),fade_output.toStdString(),0.f).ok,"reject invalid fade size");
    if(argc>1) {
        const auto real_output=directory.filePath("real-fade-patched.gar");
        Check(Notebook::PatchFadeArchive(argv[1],real_output.toStdString(),800.f).ok,"validate local dump FadeMask");
        QFile original(argv[1]),result(real_output);
        Check(original.open(QIODevice::ReadOnly) && result.open(QIODevice::ReadOnly),"read local dump comparison");
        const auto original_bytes=original.readAll(); auto result_bytes=result.readAll();
        std::memcpy(result_bytes.data()+0x590,original_bytes.constData()+0x590,4);
        Check(result_bytes==original_bytes,"local dump patch changes width only");
    }
    if(argc>2) {
        const auto output=directory.filePath("telop-patched.gar");
        Check(Notebook::PatchTelopArchive(argv[2],output.toStdString(),40.f,.8f).ok,"patch real Telop layout");
        QFile original(argv[2]),result(output);
        Check(original.open(QIODevice::ReadOnly) && result.open(QIODevice::ReadOnly),"read Telop comparison");
        const auto base=original.readAll(); auto bytes=result.readAll();
        Check(base.size()==bytes.size(),"Telop archive size preserved");
        float x{},width{};
        std::memcpy(&x,bytes.constData()+0xCCE8+0x3AC,4);
        std::memcpy(&width,bytes.constData()+0xCCE8+0x3B8,4);
        Check(x<0 && width>426 && width<427,"plate extends across widescreen");
        float sx{},sy{},height{};
        std::memcpy(&sx,bytes.constData()+0xCCE8+0x36C,4);
        std::memcpy(&sy,bytes.constData()+0xCCE8+0x370,4);
        std::memcpy(&height,bytes.constData()+0xCCE8+0x3BC,4);
        Check(sx==.8f && sy==.8f && height==24.f*.8f,"uniform text scale and thinner plate");
        for(auto offset:{0x354,0x3AC,0x3B8,0x36C,0x370,0x3BC})
            std::memcpy(bytes.data()+0xCCE8+offset,base.constData()+0xCCE8+offset,4);
        Check(bytes==base,"Telop patch preserves all other archive bytes");
        Check(!Notebook::PatchTelopArchive(argv[2],argv[2],40.f).ok,"protect Telop source");
    }
    QDir().mkpath(directory.path()+"/settings-backups");
    { QFile snapshot(directory.path()+"/settings-backups/old.json");
      Check(snapshot.open(QIODevice::WriteOnly),"create snapshot fixture");
      snapshot.write("{\"version\":1,\"enabled\":true,\"name\":\"not a profile\"}"); }
    Profiles::Load(directory.path().toStdString());
    Check(Profiles::List().empty(),"settings snapshots excluded from profile registry");
    for(const int layout_version : {1, 2, 3, 4}) {
        QTemporaryDir reset_directory;
        const auto user=reset_directory.path()+"/";
        QDir().mkpath(user+"setup");
        QFile settings(user+"presentation.json");Check(settings.open(QIODevice::WriteOnly),"reset fixture");
        const QByteArray original="{\"version\":1,\"song_list_x\":99,\"mask_present_6_x\":0,\"gear_6_x\":0,\"magic_bar_height\":1.5,\"damage_multiplier\":4,\"custom\":true}";
        settings.write(original);settings.close();
        QFile base(user+"setup/layout-baseline.json");Check(base.open(QIODevice::WriteOnly),"baseline fixture");
        base.write("{\"version\":"+QByteArray::number(layout_version)+",\"defaults\":{\"song_list_x\":-3.9,\"mask_present_6_x\":80.6,\"gear_6_x\":78.7,\"magic_bar_height\":1.68,\"damage_multiplier\":1}}");base.close();
        Check(ResetMenuLayouts(user+"presentation.json",user).isEmpty(),"explicit layout reset");
        Check(settings.open(QIODevice::ReadOnly),"read reset settings");
        auto obj=QJsonDocument::fromJson(settings.readAll()).object();settings.close();
        Check(obj["song_list_x"].toDouble()==-3.9&&obj["damage_multiplier"].toInt()==4&&obj["custom"].toBool(),"reset preserves unrelated preferences");
        Check(obj["mask_present_6_x"].toDouble()==80.6&&obj["gear_6_x"].toDouble()==78.7,
              "explicit reset includes mask description and Gear photo");
        Check(obj["magic_bar_height"].toDouble()==1.5,"menu reset preserves custom HUD height");
        const auto backups=QDir(user+"setup/layout-reset-backups").entryList({"*.json"},QDir::Files);
        Check(backups.size()==1,"reset backed up once");
        QFile backup(user+"setup/layout-reset-backups/"+backups[0]);Check(backup.open(QIODevice::ReadOnly)&&backup.readAll()==original,"reset backup bytes preserved");
    }
    Textures::Start();
    Profiles::Rule logo{64,64,3,std::string(64,'a'),"unit=0 shader=a vertices=12"};
    Profiles::Rule text{128,64,3,std::string(64,'b'),"unit=0 shader=b vertices=6"};
    Notebook::exclusions = {logo};
    Check(!Notebook::Excluded(logo), "panel exclusions inactive outside transparent mode");
    Notebook::transparent_copy = true;
    Check(Notebook::Excluded(logo) && !Notebook::Excluded(text), "only selected background is excluded");
    auto other_draw = logo; other_draw.context += " other";
    Check(!Notebook::Excluded(other_draw), "shared texture in different draw context retained");
    Notebook::transparent_copy = false; Notebook::exclusions.clear();
    Profiles::Observe(0x100,logo); Profiles::Observe(0x200,text);
    auto saved=Profiles::Selection({0x100}); saved.id="title-screen/logo.json"; saved.name="logo"; saved.scale=.75f;
    Check(Profiles::Save(saved).starts_with("Saved"),"save captured profile");
    auto layer=Profiles::Selection({0x200}); layer.id="title-screen/text.json"; layer.name="text";
    layer.presentation=true; layer.crop[3]=.864f; layer.scale=.46f;
    Check(Profiles::Save(layer).starts_with("Saved"),"save arbitrary presentation layer");
    auto preview=Profiles::Selection({0x200}); preview.scale=.3f;
    Profiles::Preview(preview);
    auto matches=Profiles::Match(logo);
    Check(matches.size()==1 && matches[0].scale==.75f,"preview must not modify logo");
    matches=Profiles::Match(text);
    Check(matches.size()==2 && matches[0].id=="@preview" && matches[1].presentation,"independent render modes");
    Profiles::Observe(0x900,text); // Scene relocation changes address, not identity.
    Check(Profiles::Match(text)[0].scale==.3f,"preview survives relocation");
    auto wrong=text; wrong.hash=std::string(64,'c');
    Profiles::Observe(0x200,wrong);
    Check(Profiles::Selection({0x200}).rules[0]==text,"address reuse must not replace snapshot identity");
    Check(Profiles::Match(wrong).empty(),"reject other pixels at old address");
    wrong=text; wrong.context+=" different";
    Check(Profiles::Match(wrong).empty(),"reject wrong draw signature");
    auto same=Profiles::Selection({0x900}); same.presentation=true; same.scale=.6f;
    Profiles::Preview(same);
    Check(Profiles::Match(text).size()==1 && Profiles::Match(text)[0].scale==.6f,"preview overrides only matching mode");
    Profiles::StopPreview();
    Check(Profiles::Match(text)[0].scale==.46f,"stop preview restores saved transform");
    layer.enabled=false; Check(Profiles::Save(layer).starts_with("Saved"),"persist disable");
    Profiles::Load(directory.path().toStdString());
    Check(Profiles::Match(text).empty(),"disabled after reboot");
    Check(Profiles::Match(logo)[0].scale==.75f,"logo persists independently");
    for(auto p:Profiles::List()) if(p.presentation) {
        Check(p.crop[3]==.864f,"crop persists"); p.enabled=true; Profiles::Update(p);
    }
    Check(Profiles::Match(text).size()==1,"reenable without recapture");
    Profiles::Profile empty; empty.id="hud/empty.json";
    Check(!Profiles::Save(empty).starts_with("Saved"),"refuse address-only profile");
    auto atlas=saved; atlas.id="hud/hearts.json"; atlas.name="hearts";
    atlas.atlas_selection=true;
    atlas.atlas_grids.push_back({logo.hash,64,64,8,4,{0,3,17}});
    atlas.atlas_grids[0].rectangles.push_back({.1f,.2f,.5f,.7f});
    Check(Profiles::Save(atlas).starts_with("Saved"),"save multiple atlas cells");
    Profiles::Preview(atlas);
    Check(Profiles::Match(logo).size()==1 && Profiles::Match(logo)[0].atlas_selection,
        "atlas preview retains region restriction");
    Profiles::Load(directory.path().toStdString());
    bool restored=false;
    for(const auto& p:Profiles::List()) if(p.id==atlas.id)
        restored=p.atlas_selection && p.atlas_grids==atlas.atlas_grids;
    Check(restored,"atlas grid and selected cells survive reload");
    Check(Profiles::Match(logo).size()==2,"saved atlas selection matches alongside original profile");
    Profiles::SetDrawTexcoordBounds(.9f,0.f,1.f,.1f);
    Check(Profiles::Match(logo).size()==2,"CPU signal bounds must not skip atlas transforms");
    Profiles::ClearDrawTexcoordBounds();
    atlas.atlas_grids[0].cells.clear();
    Check(Profiles::Save(atlas).starts_with("Saved"),"rectangle-only selection saves");
    atlas.atlas_grids[0].rectangles.clear();
    Check(!Profiles::Save(atlas).starts_with("Saved"),"reject empty cell selection");
    // Generic migration: no special profile filename or hardcoded digest.
    QFile log(directory.path()+"/legacy.log"); Check(log.open(QIODevice::WriteOnly),"log");
    log.write(("key=0x123 sha256="+std::string(64,'d')+" size=32x32 format=3 unit=0 shader=x swizzle=y vertices=6 blend=1 viewport_raw=1,2 feedback=false\n").c_str());log.close();
    QFile legacy(directory.path()+"/title-screen/arbitrary.json"); Check(legacy.open(QIODevice::WriteOnly),"legacy");
    legacy.write("{\"keys\":[\"123\"],\"render_mode\":\"presentation_layer\",\"scale\":0.8}"); legacy.close();
    Profiles::Load(directory.path().toStdString());
    bool migrated=false; for(const auto& p:Profiles::List()) if(p.id=="title-screen/arbitrary.json")
        migrated=p.enabled && p.presentation && p.rules.size()==1 && p.scale==.8f;
    Check(migrated,"generic migration and default enabled");
    std::cout<<"PASS registry: relocation, address reuse, signatures, isolation, preview priority, persistence, disable, migration\n";
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
