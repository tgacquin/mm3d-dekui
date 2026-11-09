// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstring>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include "common/file_util.h"
#include "mm3d/qt_user_path.h"
#include "mm3d/titles.h"
#include "mm3d/native_inventory.h"
#include "mm3d/pause_capture.h"
#include "mm3d/item_preview.h"
#include "mm3d/items_map_prompt.h"
#include "mm3d/menu_tabs.h"
#include "mm3d/menu_help.h"

namespace MM3D::InventoryMenu {
struct Options {
    MenuHelp::Options help;
    GearMenu::Options gear;
    bool enabled=true; float x=0.f,y=32.f,scale=.8f;
    float masks_x=0.f,masks_y=0.f,masks_scale=1.f;
    bool shared_slide=true;
    bool high_res_pause=true;
    bool bar_enabled=true; float bar_x=0.f,bar_y=-13.6f,bar_scale=.65f,bar_spacing=64.f;
    float description_x=0.f,description_y=0.f,description_scale=1.f;
    float model_x=0.f,model_y=0.f,model_scale=1.f;
    NativeInventory::ButtonPose map_prompt=ItemsMapPrompt::DefaultPose,gear_button{};
    std::array<NativeInventory::ButtonPose,3> map_prompt_elements=ItemsMapPrompt::DefaultElements;
    bool buttons_enabled=true;
    std::array<NativeInventory::ButtonPose,3> buttons{};
    bool background_enabled=true;
    NativeInventory::ButtonPose background{};
    float shadow_width=1.f,shadow_opacity=1.f;
};
inline void Apply(const Options& o) {
    MenuHelp::Apply(o.help);
    GearMenu::Apply(o.gear);
    MenuTabs::gear_pose.x=o.gear_button.x;
    MenuTabs::gear_pose.y=o.gear_button.y;
    MenuTabs::gear_pose.scale=o.gear_button.scale;
    ItemsMapPrompt::pose.x=o.map_prompt.x;
    ItemsMapPrompt::pose.y=o.map_prompt.y;
    ItemsMapPrompt::pose.scale=o.map_prompt.scale;
    for(unsigned i=0;i<o.map_prompt_elements.size();++i) {
        ItemsMapPrompt::elements[i].x=o.map_prompt_elements[i].x;
        ItemsMapPrompt::elements[i].y=o.map_prompt_elements[i].y;
        ItemsMapPrompt::elements[i].scale=o.map_prompt_elements[i].scale;
    }
    NativeInventory::x.store(o.x); NativeInventory::y.store(o.y);
    NativeInventory::scale.store(o.scale); NativeInventory::enabled.store(o.enabled);
    NativeInventory::masks_x.store(o.masks_x); NativeInventory::masks_y.store(o.masks_y);
    NativeInventory::masks_scale.store(o.masks_scale);
    NativeInventory::shared_slide.store(o.shared_slide);
    PauseCapture::enabled.store(o.high_res_pause);
    NativeInventory::bar_enabled.store(o.bar_enabled);
    NativeInventory::bar_x.store(o.bar_x); NativeInventory::bar_y.store(o.bar_y);
    NativeInventory::bar_scale.store(o.bar_scale); NativeInventory::bar_spacing.store(o.bar_spacing);
    DialogueScale::item_x.store(o.description_x); DialogueScale::item_y.store(o.description_y);
    DialogueScale::item_scale.store(o.description_scale);
    ItemPreview::model_x.store(o.model_x); ItemPreview::model_y.store(o.model_y);
    ItemPreview::model_scale.store(o.model_scale);
    NativeInventory::buttons_enabled.store(o.buttons_enabled);
    NativeInventory::shadow_width.store(o.shadow_width);
    NativeInventory::shadow_opacity.store(o.shadow_opacity);
    NativeInventory::background_enabled.store(o.background_enabled);
    NativeInventory::background_pose.x.store(o.background.x);
    NativeInventory::background_pose.y.store(o.background.y);
    NativeInventory::background_pose.scale.store(o.background.scale);
    for(unsigned i=0;i<o.buttons.size();++i) {
        NativeInventory::buttons[i].x.store(o.buttons[i].x);
        NativeInventory::buttons[i].y.store(o.buttons[i].y);
        NativeInventory::buttons[i].scale.store(o.buttons[i].scale);
    }
}
// Undo only the four widgets edited by the old experiment; preserve other mods.
inline QString RestoreLegacy() {
    const QString path=QtUserPath(FileUtil::UserPath::LoadDir)+
        "mods/"+QString::fromStdString(MM3D::Titles::SelectedHex())+"/romfs/layout/Joker.Main.Play.SubMenu.gar";
    if(!QFileInfo::exists(path+".mm3d-inventory-source") || !QFileInfo::exists(path)) return {};
    QFile base(path+".mm3d-inventory-source"),live(path);
    if(!base.open(QIODevice::ReadOnly)||!live.open(QIODevice::ReadOnly)) return "Cannot restore the previous inventory experiment.";
    const auto original=base.readAll();auto bytes=live.readAll();live.close();
    if(bytes.size()!=original.size()||bytes.size()<0x29000||original.mid(0x18f9c,4)!="MFL "||original.mid(0x25a8c,4)!="MFL ")
        return "Inventory archive changed; previous experiment was not restored.";
    const auto previous=bytes;
    for(unsigned record:{0x19264u,0x19dd0u,0x25b78u,0x2610cu}) {
        for(unsigned i=0;i<68;++i) {
            if((i>=8&&i<16)||(i>=20&&i<28)) continue;
            if(bytes[record+i]!=original[record+i]) return "Inventory widget has other edits; previous experiment was not restored.";
        }
        for(unsigned field:{8u,12u,20u,24u}) std::memcpy(bytes.data()+record+field,original.constData()+record+field,4);
    }
    if(bytes==previous) return {};
    const QString backup=path+".mm3d-inventory-before-live";
    if(!QFileInfo::exists(backup)) {
        QSaveFile saved(backup);
        if(!saved.open(QIODevice::WriteOnly)||saved.write(previous)!=previous.size()||!saved.commit()) return "Cannot back up the previous inventory experiment.";
    }
    QSaveFile saved(path);
    if(!saved.open(QIODevice::WriteOnly)||saved.write(bytes)!=bytes.size()||!saved.commit()) return "Cannot restore the previous inventory experiment.";
    return {};
}
}
