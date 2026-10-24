#include <QJsonArray>
// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cmath>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include "mm3d/settings.h"
#include "mm3d/actor_draw_distance.h"
namespace MM3D {
bool Settings::Load(const QString& path) {
    QFile file(path);
    // A missing file is a normal first launch. An existing unreadable or
    // invalid file must never be replaced by defaults after a failed load.
    save_blocked = file.exists();
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    if (object.value("version").toInt() != 1) return false;
    const auto value = [&](const char* key, float fallback, float lo, float hi) {
        const double number = object.value(key).toDouble(fallback);
        return std::isfinite(number) ? static_cast<float>(std::clamp(number, double(lo), double(hi))) : fallback;
    };
    for(const auto& f:SubmenuHeader::Fields)
        submenu_header.*f.member=value(f.key,SubmenuHeader::Options{}.*f.member,f.lo,f.hi);
    if(object.contains("inventory_native")) {
        inventory_menu.enabled=object.value("inventory_native").toBool(true);
        inventory_menu.x=value("inventory_panel_x",0.f,-320.f,320.f);
        inventory_menu.y=value("inventory_panel_y",32.f,-240.f,240.f);
        inventory_menu.scale=value("inventory_panel_scale",.8f,.5f,1.5f);
    }
    inventory_menu.masks_x=value("inventory_masks_x",0.f,-320.f,320.f);
    notebook_live.schedule_prompt=object.value("notebook_schedule_prompt").toBool(true);
    notebook_live.schedule_prompt_y=value("notebook_schedule_prompt_y",-40,-240,240);
    notebook_live.map_prompt=object.value("notebook_map_prompt").toBool(true);
    notebook_live.map_prompt_y=value("notebook_map_prompt_y",-20,-240,240);
    notebook_live.event_prompt=object.value("notebook_event_prompt").toBool(true);
    notebook_live.prompt_x=value("notebook_buttons_x",0,-400,400);
    notebook_live.prompt_y=value("notebook_prompt_y",0,-240,240);
    notebook_live.prompt_scale=value("notebook_buttons_scale",1,.5f,2);
    notebook_live.prompt_icon_scale=value("notebook_buttons_icon_scale",1,.5f,2);
    if(!object.contains("notebook_buttons_x") && notebook_live.map_prompt_y==notebook_live.prompt_y)
        notebook_live.map_prompt_y=std::max(-240.f,notebook_live.prompt_y-20.f);
    notebook_live.popup_width=value("notebook_popup_width",1.f,.5f,3.f);
    notebook_live.popup_height=value("notebook_popup_height",1.f,.5f,3.f);
    notebook_live.expanded_rows=object.value("notebook_expanded_rows").toBool(true);
    notebook_live.enabled=object.value("notebook_native").toBool(true);
    world_map_native_zoom={value("world_map_native_zoom_x",0,-800,800),value("world_map_native_zoom_y",0,-480,480),
        value("world_map_native_zoom_scale",1,.1f,3)};
    world_map_zoom_prompt={value("world_map_zoom_prompt_x",0,-400,400),value("world_map_zoom_prompt_y",0,-240,240),
        value("world_map_zoom_prompt_scale",.8f,.25f,2)};
    dungeon_map_move_prompt={value("dungeon_map_move_prompt_x",0,-400,400),
        value("dungeon_map_move_prompt_y",-44,-240,240),value("dungeon_map_move_prompt_scale",.69f,.25f,2)};
    dungeon_map_zoom_prompt={value("dungeon_map_zoom_prompt_x",0,-400,400),
        value("dungeon_map_zoom_prompt_y",-28,-240,240),value("dungeon_map_zoom_prompt_scale",.69f,.25f,2)};
    // The old dungeon hints borrowed Notebook scale/icon scale and the
    // difference between its two Y values. Notebook X never affected them.
    // Fall back only for missing keys; saving makes the map values independent.
    dungeon_map.prompts={true,value("dungeon_map_prompts_x",0,-400,400),
        value("dungeon_map_prompts_y",0,-240,240),
        value("dungeon_map_prompts_scale",notebook_live.prompt_scale,.5f,2),
        value("dungeon_map_prompts_icon_scale",notebook_live.prompt_icon_scale,.5f,2),
        value("dungeon_map_prompts_spacing",notebook_live.prompt_y-notebook_live.map_prompt_y,-480,480)};
    // New global adjustments start neutral; retain every existing local pose.
    dungeon_map.prompt_group={value("dungeon_map_buttons_x",0,-400,400),
        value("dungeon_map_buttons_y",0,-240,240),value("dungeon_map_buttons_scale",1,.5f,2),
        value("dungeon_map_buttons_icon_scale",1,.5f,2)};
    dungeon_map.enabled=object.value("dungeon_map_top").toBool(true);
    dungeon_map.side_panels=object.value("dungeon_map_side_panels").toBool(true);
    dungeon_map.top_strip=object.value("dungeon_map_top_strip").toBool(true);
    dungeon_map.top_panel={value("dungeon_map_top_panel_x",0,-1024,1024),value("dungeon_map_top_panel_y",0,-480,480),
        value("dungeon_map_top_panel_scale",.3125f,.1f,3)};
    dungeon_map.panel_darkness=value("dungeon_map_panel_darkness",0,0,1);
    dungeon_map.arrow_scale=value("dungeon_map_arrows_scale",1,.1f,3);
    for(unsigned i=0;i<4;++i) {
        const auto key=QString("dungeon_map_arrow_%1_").arg(DungeonMap::ArrowNames[i]);
        dungeon_map.arrow_offsets[i]={value((key+"x").toUtf8().constData(),0,-800,800),
            value((key+"y").toUtf8().constData(),0,-480,480)};
    }
    for(unsigned i=0;i<2;++i) {
        const auto key=QString("dungeon_map_panel_%1_").arg(i);const auto d=DungeonMap::Options{}.panels[i];
        dungeon_map.panels[i]={value((key+"x").toUtf8().constData(),d.x,-512,512),
            value((key+"y").toUtf8().constData(),d.y,-480,480),value((key+"scale").toUtf8().constData(),d.scale,.1f,3)};
    }
    for(unsigned i=0;i<DungeonMap::Count;++i) {
        const auto key=QString("dungeon_map_%1_").arg(i);
        auto& p=dungeon_map.poses[i];
        const bool model=i==DungeonMap::Model,description=i==DungeonMap::Description;
        p={value((key+"x").toUtf8().constData(),model||description?0:40,model?-20:description?-320:-512,model?20:description?320:512),
           value((key+"y").toUtf8().constData(),0,model?-20:-240,model?20:240),
           value((key+"scale").toUtf8().constData(),1,model||description?.25f:.1f,model||description?2.f:3.f)};
    }
    banners_migrated=object.value("banners_native").toBool(false);
    for(unsigned i=0;i<2;++i){const auto key=QString("banner_%1_").arg(i);auto& p=banners.parts[i];
        p.height=value((key+"height").toUtf8().constData(),1,.25f,3);
        p.y=value((key+"y").toUtf8().constData(),0,-240,240);
        p.text_scale=value((key+"text_scale").toUtf8().constData(),1,.25f,3);
        p.text_x=value((key+"text_x").toUtf8().constData(),i?200:40,-400,400);
        p.text_y=value((key+"text_y").toUtf8().constData(),0,-240,240);
        p.brightness=value((key+"brightness").toUtf8().constData(),1,0,1);
    }
    const auto read_diamond=[&](const QString& key,HudButtons::Diamond& d){
        d.enabled=object.value(key+"enabled").toBool(false);
        d.x=value((key+"x").toUtf8().constData(),350,-512,512);d.y=value((key+"y").toUtf8().constData(),55,-240,240);
        d.spacing=value((key+"spacing").toUtf8().constData(),24,0,160);d.rotation=value((key+"rotation").toUtf8().constData(),0,-180,180);};
    read_diamond("hud_diamond_",hud_buttons.diamond);
    for(unsigned i=0;i<3;++i)read_diamond(QString("hud_button_layout_%1_diamond_").arg(i+1),hud_buttons.preset_diamonds[i]);
    const auto read_buttons=[&](const QString& prefix,HudButtons::Layout& layout){
        for(unsigned i=0;i<HudButtons::Count;++i){const auto key=prefix+QString::number(i)+"_";
            layout[i]={value((key+"x").toUtf8().constData(),0,-512,512),value((key+"y").toUtf8().constData(),HudButtons::Default()[i].y,-240,240),value((key+"scale").toUtf8().constData(),1,.1f,3)};}};
    read_buttons("hud_button_",hud_buttons.buttons);
    hud_buttons.flip_peak=value("hud_buttons_flip_peak",1.2f,1,1.2f);
    hud_buttons.scale=value("hud_buttons_scale",hud_buttons.buttons[1].scale,.1f,3);
    for(unsigned i=0;i<3;++i){const auto key=QString("hud_button_layout_%1_").arg(i+1);
        hud_buttons.saved[i]=object.value(key+"saved").toBool(false);read_buttons(key,hud_buttons.presets[i]);
        hud_buttons.preset_flip_peaks[i]=value((key+"flip_peak").toUtf8().constData(),1.2f,1,1.2f);
        hud_buttons.preset_scales[i]=value((key+"scale").toUtf8().constData(),hud_buttons.presets[i][1].scale,.1f,3);}
    song_reset.enabled=object.value("song_reset_top").toBool(true);
    song_reset.background_x=value("song_reset_background_x",40,-512,512);
    for(unsigned i=0;i<SongReset::Count;++i){const auto key=QString("song_reset_%1_").arg(i);
        song_reset.poses[i]={value((key+"x").toUtf8().constData(),SongReset::Options{}.poses[i].x,-512,512),value((key+"y").toUtf8().constData(),0,-240,240),value((key+"scale").toUtf8().constData(),1,.1f,3)};}
    double_time.enabled=object.value("double_time_top").toBool(true);
    double_time.current_day_only=object.value("double_time_current_day").toBool(true);
    for(unsigned i=0;i<DoubleTime::Count;++i){
        const auto key=QString("double_time_%1_").arg(i);
        double_time.poses[i]={value((key+"x").toUtf8().constData(),40,-512,512),value((key+"y").toUtf8().constData(),0,-240,240),value((key+"scale").toUtf8().constData(),1,.1f,3)};
    }
    song_list.enabled=object.value("song_list_top").toBool(true);
    song_list.background_x=value("song_list_background_x",-230.f,-512.f,512.f);
    song_list.spacing=value("song_list_spacing",16.f,0.f,60.f);
    song_list.panel_color={value("song_panel_color_r",1.f,0.f,1.f),value("song_panel_color_g",1.f,0.f,1.f),value("song_panel_color_b",1.f,0.f,1.f)};
    song_list.panel_color_opacity=value("song_panel_color_opacity",0.f,0.f,1.f);
    song_list.panel_darkness=value("song_panel_darkness",0.f,0.f,1.f);
    song_list.stone_darkness=value("song_stone_darkness",0.f,0.f,1.f);
    song_list.column_spacing=value("song_list_column_spacing",62.f,40.f,180.f);
    song_list.songs={value("song_list_x",24.f,-512.f,512.f),value("song_list_y",12.f,-240.f,240.f),value("song_list_scale",.35f,.1f,2.f)};
    // Seed independent banner placement from the old grid-derived position once.
    const float song_banner_height=std::clamp(song_list.songs.y-2.f,10.f,18.f);
    song_list.banner={value("song_banner_x",song_list.songs.x,-512.f,512.f),
        value("song_banner_y",song_list.songs.y-song_banner_height-2.f,-240.f,240.f),
        value("song_banner_scale",1.f,.1f,3.f)};
    song_list.banner_width=value("song_banner_width",(2*song_list.column_spacing+54)*song_list.songs.scale,2.f,600.f);
    song_list.banner_height=value("song_banner_height",song_banner_height,1.f,40.f);
    // Preserve the old fitted bounds on migration, then save independent edges.
    song_list.panel_left=value("song_panel_left_x",std::clamp(std::min(song_list.songs.x,song_list.banner.x)-4.f,-512.f,496.f),-512.f,496.f);
    song_list.panel_right=value("song_panel_right_x",std::clamp(std::max(song_list.songs.x+(2*song_list.column_spacing+54)*song_list.songs.scale,
        song_list.banner.x+song_list.banner_width*song_list.banner.scale)+4.f,song_list.panel_left+16.f,512.f),song_list.panel_left+16.f,512.f);
    song_list.staff={value("song_staff_x",90.f,-512.f,512.f),value("song_staff_y",0.f,-240.f,240.f),value("song_staff_scale",.8f,.1f,2.f)};
    ocarina_prompt.enabled=object.value("ocarina_prompts").toBool(true);
    ocarina_prompt.x=value("ocarina_prompt_x",0.f,-400.f,400.f);
    ocarina_prompt.y=value("ocarina_prompt_y",0.f,-240.f,240.f);
    ocarina_prompt.scale=value("ocarina_prompt_scale",1.f,.5f,2.f);
    ocarina_prompt.spacing=value("ocarina_prompt_spacing",22.f,0.f,60.f);
    ocarina_prompt.icon_scale=value("ocarina_icon_scale",1.f,.5f,2.f);
    file_select_prompt.enabled=object.value("file_select_prompts").toBool(true);
    file_select_prompt.x=value("file_select_prompt_x",0.0f,-400.0f,400.0f);
    file_select_prompt.y=value("file_select_prompt_y",0.0f,-240.0f,240.0f);
    file_select_prompt.scale=value("file_select_prompt_scale",1.0f,0.5f,2.0f);
    file_select_prompt.spacing=value("file_select_prompt_spacing",22.0f,0.0f,60.0f);
    file_select_prompt.icon_scale=value("file_select_prompt_icon_scale",1.0f,0.5f,2.0f);
    file_select.stone_spacing=value("file_select_stone_spacing",0.f,0.f,80.f);
    file_select.entrance_ease=value("file_select_entrance_ease",2.f,1.f,3.f);
    file_select.enabled=object.value("file_select_native").toBool(true);
    file_select.header.x=value("file_select_header_x",0.f,-512.f,512.f);
    file_select.header.y=value("file_select_header_y",0.f,-512.f,512.f);
    file_select.header.scale_x=value("file_select_header_scale_x",1.f,.01f,3.f);
    file_select.header.scale_y=value("file_select_header_scale_y",1.f,.01f,3.f);
    file_select.title.x=value("file_select_title_x",0.f,-512.f,512.f);
    file_select.title.y=value("file_select_title_y",0.f,-512.f,512.f);
    file_select.title.scale=value("file_select_title_scale",1.f,.01f,3.f);
    disable_tatl_reminders=object.value("disable_tatl_reminders").toBool(false);
    disable_bomber_approach=object.value("disable_bomber_approach").toBool(false);
    sword_activate_owls=object.value("sword_activate_owls").toBool(false);
    classic_saving=object.value("classic_saving").toBool(false);
    classic_double_time=object.value("classic_double_time").toBool(false);
    startup.logo.x=value("startup_logo_x",0,-400,400);
    startup.logo.y=value("startup_logo_y",0,-240,240);
    startup.logo.scale=value("startup_logo_scale",1,.1f,3);
    startup.copyright.x=value("startup_copyright_x",0,-400,400);
    startup.copyright.y=value("startup_copyright_y",0,-240,240);
    startup.copyright.scale=value("startup_copyright_scale",1,.1f,3);
    startup.press_start.x=value("startup_press_start_x",0,-400,400);
    startup.press_start.y=value("startup_press_start_y",0,-240,240);
    startup.press_start.scale=value("startup_press_start_scale",1,.1f,3);
    startup.loading_icon.x=value("startup_loading_icon_x",0,-400,400);
    startup.loading_icon.y=value("startup_loading_icon_y",0,-240,240);
    startup.loading_icon.scale=value("startup_loading_icon_scale",1,.1f,3);
    item_present.enabled=object.value("item_present_native").toBool(true);
    item_present.help_pose={value("item_present_help_x",280,-512,512),value("item_present_help_y",180,-512,512),value("item_present_help_scale",.8f,.1f,2)};
    item_present.help_spacing=value("item_present_help_spacing",6,0,100);
    item_present.horizontal=object.value("item_present_horizontal").toBool(false);
    for(unsigned i=0;i<NativeItemPresent::Count;++i) {
        const QString key=QString("item_present_%1_").arg(i);auto& p=item_present.poses[i];
        p.x=value((key+"x").toUtf8().constData(),p.x,-512.f,512.f);
        if(i==NativeItemPresent::Background)continue;
        p.y=value((key+"y").toUtf8().constData(),p.y,-512.f,512.f);
        p.scale=value((key+"scale").toUtf8().constData(),p.scale,.1f,2.f);
    }
    // Seed the independent moon-mask controls from the former shared placement.
    // Preserve the description's previous global dialogue pose on first load.
    mask_present=item_present;
    mask_present.horizontal=false;
    mask_present.poses[NativeItemPresent::Description]={0,value("dialogue_y",0,-120,120),value("dialogue_scale",1,.5f,1)};
    mask_present.enabled=object.value("mask_present_native").toBool(mask_present.enabled);
    mask_present.help_pose={value("mask_present_help_x",mask_present.help_pose.x,-512,512),
        value("mask_present_help_y",mask_present.help_pose.y,-512,512),value("mask_present_help_scale",mask_present.help_pose.scale,.1f,2)};
    mask_present.help_spacing=value("mask_present_help_spacing",mask_present.help_spacing,0,100);
    for(unsigned i=0;i<NativeItemPresent::Count;++i) {
        if(i==NativeItemPresent::Background||i==NativeItemPresent::Right)continue;
        const QString key=QString("mask_present_%1_").arg(i);auto& p=mask_present.poses[i];
        p.x=value((key+"x").toUtf8().constData(),p.x,-512,512);
        p.y=value((key+"y").toUtf8().constData(),p.y,-512,512);
        p.scale=value((key+"scale").toUtf8().constData(),p.scale,.1f,2);
    }
    for(unsigned i=0;i<NativeFileSelect::Count;++i) {
        const QString key=QString("file_select_%1_").arg(i);auto& p=file_select.poses[i];
        p.x=value((key+"x").toUtf8().constData(),p.x,-512.f,512.f);
        p.y=value((key+"y").toUtf8().constData(),p.y,-512.f,512.f);
        p.scale=value((key+"scale").toUtf8().constData(),p.scale,.1f,2.f);
    }
    for(unsigned i=0;i<NativeNotebook::Count;++i) {
        const QString key=QString("notebook_live_%1_").arg(i);auto& p=notebook_live.poses[i];
        p.x=value((key+"x").toUtf8().constData(),p.x,-512.f,512.f);
        p.y=value((key+"y").toUtf8().constData(),p.y,-512.f,512.f);
        p.scale=value((key+"scale").toUtf8().constData(),p.scale,.1f,2.f);
    }
    inventory_menu.gear.enabled=object.value("gear_enabled").toBool(true);
    inventory_menu.gear.horizontal=object.value("gear_horizontal").toBool(true);
    for(unsigned i=0;i<GearMenu::Count;++i) {
        if(i==GearMenu::Background||i==GearMenu::Model||i==GearMenu::Description)continue; // Shared inventory settings.
        const QString key=QString("gear_%1_").arg(i);
        auto& p=inventory_menu.gear.poses[i];
        const auto def=GearMenu::Options{}.poses[i];
        const bool model=i==GearMenu::Model;
        p.x=value((key+"x").toUtf8().constData(),def.x,model?-20.f:-320.f,model?20.f:320.f);
        if(i==GearMenu::Background)continue;
        p.y=value((key+"y").toUtf8().constData(),def.y,model?-20.f:-240.f,model?20.f:240.f);
        p.scale=value((key+"scale").toUtf8().constData(),def.scale,.25f,2.f);
    }
    inventory_menu.masks_y=value("inventory_masks_y",0.f,-240.f,240.f);
    inventory_menu.masks_scale=value("inventory_masks_scale",1.f,.5f,1.5f);
    inventory_menu.high_res_pause=object.value("inventory_high_res_pause").toBool(true);
    inventory_menu.shared_slide=object.value("inventory_shared_slide").toBool(true);
    inventory_menu.bar_enabled=object.value("inventory_bar_enabled").toBool(true);
    inventory_menu.bar_scale=value("inventory_bar_scale",.65f,.35f,1.5f);
    inventory_menu.bar_spacing=value("inventory_bar_spacing",64.f,40.f,120.f);
    inventory_menu.bar_x=value("inventory_bar_x",inventory_menu.x,-320.f,320.f);
    inventory_menu.bar_y=value("inventory_bar_y",inventory_menu.y-64.f*inventory_menu.bar_scale-4.f,-240.f,240.f);
    inventory_menu.description_x=value("inventory_description_x",0.f,-320.f,320.f);
    inventory_menu.description_y=value("inventory_description_y",0.f,-240.f,240.f);
    inventory_menu.description_scale=value("inventory_description_scale",1.f,.25f,2.f);
    inventory_menu.model_x=value("inventory_model_x",0.f,-20.f,20.f);
    inventory_menu.model_y=value("inventory_model_y",0.f,-20.f,20.f);
    inventory_menu.model_scale=value("inventory_model_scale",1.f,.25f,2.f);
    inventory_menu.gear_button.x=value("inventory_button_gear_x",0.f,-400.f,400.f);
    inventory_menu.gear_button.y=value("inventory_button_gear_y",0.f,-240.f,240.f);
    inventory_menu.gear_button.scale=value("inventory_button_gear_scale",1.f,.25f,2.f);
    inventory_menu.help.enabled=object.value("inventory_help_enabled").toBool(true);
    inventory_menu.help.horizontal=object.value("inventory_help_horizontal").toBool(false);
    inventory_menu.help.spacing=value("inventory_help_spacing",6,0,100);
    inventory_menu.help.button_gap=value("inventory_help_button_gap",4,0,40);
    inventory_menu.help.button_scale=value("inventory_help_button_scale",1,.25f,3);
    inventory_menu.help.text_scale=value("inventory_help_text_scale",1,.25f,3);
    inventory_menu.help.opacity=value("inventory_prompt_opacity",1,0,1);
    inventory_menu.help.pose={value("inventory_help_0_x",280,-512,512),value("inventory_help_0_y",148,-240,240),
        value("inventory_help_0_scale",.8f,.1f,2)};
    inventory_menu.map_prompt.x=value("inventory_map_prompt_x",ItemsMapPrompt::DefaultPose.x,-400.f,400.f);
    inventory_menu.map_prompt.y=value("inventory_map_prompt_y",ItemsMapPrompt::DefaultPose.y,-240.f,240.f);
    inventory_menu.map_prompt.scale=value("inventory_map_prompt_scale",ItemsMapPrompt::DefaultPose.scale,.25f,2.f);
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"r","arrow","text"};
        const auto prefix=QString("inventory_map_prompt_%1_").arg(names[i]);
        auto& p=inventory_menu.map_prompt_elements[i];
        p.x=value((prefix+"x").toUtf8().constData(),ItemsMapPrompt::DefaultElements[i].x,-400.f,400.f);
        p.y=value((prefix+"y").toUtf8().constData(),ItemsMapPrompt::DefaultElements[i].y,-240.f,240.f);
        p.scale=value((prefix+"scale").toUtf8().constData(),ItemsMapPrompt::DefaultElements[i].scale,.25f,4.f);
    }
    // Older settings mirrored the inventory prompt; carry that exact layout
    // forward until the map prompt receives its own saved controls.
    map_items_prompt.pose={value("map_items_prompt_x",-inventory_menu.map_prompt.x,-400,400),
        value("map_items_prompt_y",inventory_menu.map_prompt.y,-240,240),
        value("map_items_prompt_scale",inventory_menu.map_prompt.scale,.25f,2.f)};
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"l","arrow","text"};
        const auto prefix=QString("map_items_prompt_%1_").arg(names[i]);
        const auto& old=inventory_menu.map_prompt_elements[i];auto& p=map_items_prompt.elements[i];
        p.x=value((prefix+"x").toUtf8().constData(),-old.x,-400,400);
        p.y=value((prefix+"y").toUtf8().constData(),old.y,-240,240);
        p.scale=value((prefix+"scale").toUtf8().constData(),old.scale,.25f,4.f);
    }
    inventory_menu.buttons_enabled=object.value("inventory_buttons_enabled").toBool(true);
    inventory_menu.shadow_width=value("brown_shadow_width",value("inventory_shadow_width",1.f,0.f,3.f),0.f,3.f);
    inventory_menu.shadow_opacity=value("brown_shadow_opacity",value("inventory_shadow_opacity",1.f,0.f,1.f),0.f,1.f);
    inventory_menu.background_enabled=object.value("inventory_background_enabled").toBool(true);
    inventory_menu.background.x=value("inventory_background_x",0.f,-400.f,400.f);
    inventory_menu.background.y=value("brown_background_y",value("inventory_background_y",0.f,-240.f,240.f),-240.f,240.f);
    inventory_menu.background.scale=value("brown_background_scale",value("inventory_background_scale",1.f,.25f,2.f),.25f,2.f);
    for(unsigned i=0;i<inventory_menu.buttons.size();++i) {
        const char* names[]={"back","masks","items"};
        const auto prefix=QString("inventory_button_%1_").arg(names[i]);
        auto& b=inventory_menu.buttons[i];
        b.x=value((prefix+"x").toUtf8().constData(),0.f,-400.f,400.f);
        b.y=value((prefix+"y").toUtf8().constData(),0.f,-240.f,240.f);
        b.scale=value((prefix+"scale").toUtf8().constData(),1.f,.25f,2.f);
    }
    player_hud.layout_version=object.value("player_hud_layout_version").toInt(0)==1?1:0;
    player_hud.opacity=value("player_hud_opacity",1.f,0.f,1.f);
    player_hud.banner_darkness=value("player_hud_banner_darkness",0.f,0.f,1.f);
    player_hud.banner_y=int(value("player_hud_banner_y",0.f,-240.f,240.f));
    player_hud.hide_minimap=object.value("player_hud_hide_minimap").toBool(false);
    for(unsigned i=0;i<6;++i) {
        const auto key=QString("player_hud_button_%1_").arg(HudAdjustments::ButtonKeys[i]);
        player_hud.button_x[i]=int(value((key+"x").toUtf8().constData(),0,-400,400));
        player_hud.button_y[i]=int(value((key+"y").toUtf8().constData(),0,-240,240));
    }
    player_hud.hide_menu_prompts=object.value("player_hud_hide_menu_prompts").toBool(false);
    player_hud.heart_spacing=int(value("player_hud_heart_spacing",0,
        -HudAdjustments::HeartSpacingLimit,HudAdjustments::HeartSpacingLimit));
    player_hud.magic_height_steps=int(value("player_hud_magic_height",0,-10,10));
    player_hud.magic_width=value("player_hud_magic_width",1.f,.5f,3.f);
    // Existing profiles already encode their chosen size in player_hud_scale.
    // Do not apply the new-install baseline on top of it.
    player_hud.base_scale=int(value("player_hud_base_scale",0,-10,10));
    player_hud.overall=int(value("player_hud_scale",0,-10,10));
    player_hud.top=int(value("player_hud_top_padding",0,0,40));
    player_hud.bottom=int(value("player_hud_bottom_padding",0,0,40));
    for(unsigned i=0;i<HudAdjustments::Count;++i) {
        const auto key=QString("player_hud_%1_scale").arg(HudAdjustments::Keys[i]).toUtf8();
        player_hud.elements[i]=int(value(key.constData(),0,-10,10));
        const auto prefix=QString("player_hud_%1_").arg(HudAdjustments::Keys[i]);
        player_hud.x[i]=int(value((prefix+"x").toUtf8().constData(),0,-400,400));
        player_hud.y[i]=int(value((prefix+"y").toUtf8().constData(),0,-240,240));
    }
    // One-time migration of every older audio preference to Balanced. Keep the
    // stored mode IDs stable; the marker preserves later Accurate HLE choices.
    const auto audio_mode=object.value("hle_audio_accuracy");
    const int requested_mode=audio_mode.toInt(-1);
    hle_audio_accuracy=object.value("hle_audio_mode_version")==QJsonValue(1) &&
        audio_mode.isDouble() && audio_mode.toDouble()==requested_mode &&
        requested_mode>=1 && requested_mode<=2?requested_mode:1;
    mute_music=object.value("mute_music").toBool(false);
    disable_low_health_beep=object.value("disable_low_health_beep").toBool(false);
    disable_heart_drops=object.value("disable_heart_drops").toBool(false);
    disable_rupee_drops=object.value("disable_rupee_drops").toBool(false);
    disable_fairy_drops=object.value("disable_fairy_drops").toBool(false);
    disable_magic_drops=object.value("disable_magic_drops").toBool(false);
    disable_ammo_pickups=object.value("disable_ammo_pickups").toBool(false);
    lose_rupees_on_death=object.value("lose_rupees_on_death").toBool(false);
    world_ends_on_death=object.value("world_ends_on_death").toBool(false);
    advance_six_hours_on_death=object.value("advance_six_hours_on_death").toBool(false);
    fast_time=object.value("fast_time").toBool(false);
    prop_draw_distance=ActorDrawDistance::Clamp(float(object.value("prop_draw_distance").toDouble(1.)));
    actor_draw_distance=ActorDrawDistance::Clamp(float(object.value("actor_draw_distance").toDouble(1.)));
    weaker_enhanced_defense=object.value("weaker_enhanced_defense").toBool(false);
    damage_multiplier=int(value("damage_multiplier",1.f,1.f,16.f));
    dialogue_scale=value("dialogue_scale",1.f,.5f,1.f);
    dialogue_y=value("dialogue_y",0.f,-120.f,120.f);
    dialogue_background_width=value("dialogue_background_width",1.f,1.f,2.f);
    dialogue_indicator_x=value("dialogue_indicator_x",0.f,-200.f,200.f);
    dialogue_indicator_y=value("dialogue_indicator_y",0.f,-64.f,64.f);
    double_prices = object.value("double_prices").toBool(false);
    price_multiplier = std::clamp(object.value("price_multiplier").toInt(2),2,3);
    // Existing profiles without this setting keep the original controls.
    stick_click_controls = object.value("stick_click_controls").toBool(false);
    single_screen_text = object.value("single_screen_text").toBool(true);
    fix_bloom = object.value("fix_bloom").toBool(true);
    disable_fog = object.value("disable_fog").toBool(false);
    hd_pictograph_photos = object.value("hd_pictograph_photos").toBool(true);
    ambient_occlusion = AmbientOcclusion::Sanitize({
        object.value("ambient_occlusion").toBool(false),
        float(object.value("ambient_occlusion_strength").toDouble(.6)),
        float(object.value("ambient_occlusion_radius").toDouble(40))});
    depth_of_field = DepthOfField::Sanitize({
        object.value("depth_of_field").toBool(false),
        float(object.value("depth_of_field_focus").toDouble(600)),
        float(object.value("depth_of_field_transition").toDouble(1200)),
        float(object.value("depth_of_field_strength").toDouble(1.5))});
    interpolation_60fps = object.value("interpolation_60fps").toBool(true);
    interpolation_cpu_vertices = object.value("interpolation_cpu_vertices").toBool(true);
    red_potion_hearts = std::clamp(object.value("red_potion_hearts").toInt(20),1,20);
    blue_potion_hearts = std::clamp(object.value("blue_potion_hearts").toInt(20),1,20);
    bottled_fairy_hearts = std::clamp(object.value("bottled_fairy_hearts").toInt(10),1,20);
    show_performance = object.value("show_performance").toBool(false);
    show_startup_settings_hint = object.value("show_startup_settings_hint").toBool(true);
    indicator = object.value("indicator").toBool(true);
    widescreen = object.value("widescreen").toBool(false);
    camera_zoom_out = value("camera_zoom_out", 1.f, 1.f, 1.5f);
    game_camera_fov = value("game_camera_fov", 65.f, 45.f, 70.f);
    cutscene_camera_fov = value("cutscene_camera_fov", 49.f, 45.f, 70.f);
    restoration_hud_scale = value("restoration_hud_scale", 1.f, .5f, 1.5f);
    const OptionsMenu::Options options_defaults;
    options_menu.x=value("options_menu_x",options_defaults.x,-400.f,400.f);
    options_menu.y=value("options_menu_y",options_defaults.y,-240.f,240.f);
    options_menu.scale=value("options_menu_scale",options_defaults.scale,.1f,2.f);
    options_menu.spacing=value("options_menu_spacing",options_defaults.spacing,0.f,120.f);
    options_menu.description.x=value("options_description_x",0.f,-320.f,320.f);
    options_menu.description.y=value("options_description_y",0.f,-240.f,240.f);
    options_menu.description.scale=value("options_description_scale",1.f,.25f,2.f);
    carrots.x=value("carrots_x",0.f,-400.f,400.f);
    carrots.y=value("carrots_y",0.f,-240.f,240.f);
    carrots.scale=value("carrots_scale",1.f,.25f,3.f);
    rupee_keys.x=value("rupee_keys_x",0.f,-400.f,400.f);
    rupee_keys.y=value("rupee_keys_y",0.f,-240.f,240.f);
    rupee_keys.scale=value("rupee_keys_scale",1.f,.25f,3.f);
    minigame_hud.x=value("minigame_hud_x",0.f,-400.f,400.f);
    minigame_hud.y=value("minigame_hud_y",0.f,-240.f,240.f);
    minigame_hud.scale=value("minigame_hud_scale",1.f,.25f,3.f);
    magic_border.enabled=object.value("magic_border_enabled").toBool(true);
    magic_border.shadow_enabled=object.value("magic_shadow_enabled").toBool(true);
    magic_border.shadow_size=value("magic_shadow_size",2.5f,0.f,8.f);
    magic_border.shadow_opacity=value("magic_shadow_opacity",.8f,0.f,1.f);
    magic_border.thickness=value("magic_border_thickness",1.f,1.f,3.f);
    magic_border.color={value("magic_border_r",1.f,0.f,1.f),value("magic_border_g",1.f,0.f,1.f),value("magic_border_b",1.f,0.f,1.f)};
    magic_bar_y = value("magic_bar_y", 0.f, -64.f, 64.f);
    magic_bar_height = value("magic_bar_height", 1.f, .5f, 2.f);
    dpad_y = value("dpad_y", 0.f, -120.f, 120.f);
    dpad_offset_x=value("dpad_offset_x",0.f,-400.f,400.f);
    dpad_offset_y=value("dpad_offset_y",0.f,-240.f,240.f);
    minimap = object.value("minimap").toBool(true);
    minimap_scale = value("minimap_scale", .36f, .2f, .6f);
    minimap_opacity = value("minimap_opacity", .85f, 0.f, 1.f);
    minimap_right_margin = value("minimap_right_margin", 8.f, -64.f, 120.f);
    minimap_bottom_margin = value("minimap_bottom_margin", 8.f, -64.f, 120.f);
    fullscreen_draws.clear();
    auto draws=object.value("fullscreen_draws").toArray();
    // Older versions saved one rule. An explicit new empty array stays empty.
    if(!object.contains("fullscreen_draws") && object.value("fullscreen_draw").isObject())
        draws.append(object.value("fullscreen_draw"));
    for(const auto& entry:draws){
    if(fullscreen_draws.size()>=128)break;
    const auto draw=entry.toObject();
    bool shader_ok=false, swizzle_ok=false;
    WidescreenProbe::Draw rule;
    rule.shader=draw.value("shader").toString().toULongLong(&shader_ok,16);
    rule.swizzle=draw.value("swizzle").toString().toULongLong(&swizzle_ok,16);
    bool valid=shader_ok && swizzle_ok;
    const auto read=[&](const char* key,unsigned& out) {
        const double n=draw.value(key).toDouble(-1);
        if(!std::isfinite(n) || n<0 || n>4294967295.0 || std::floor(n)!=n) valid=false;
        else out=static_cast<unsigned>(n);
    };
    read("vertices",rule.vertices); read("width",rule.width); read("height",rule.height);
    read("textures",rule.textures); read("blend",rule.blend); read("depth",rule.depth);
    if(valid && rule.vertices>0 && rule.vertices<=24 && rule.width>0 && rule.height>0)
        if(rule!=WidescreenProbe::TransitionOverlay && std::find(fullscreen_draws.begin(),fullscreen_draws.end(),rule)==fullscreen_draws.end())
            fullscreen_draws.push_back(rule);
    }
    scale = value("scale", 1, .5f, 2.f);
    opacity = value("opacity", .9f, .2f, 1.f);
    x = value("x", .02f, 0.f, .75f);
    y = value("y", .02f, 0.f, .9f);
    save_blocked = false;
    return true;
}
bool Settings::Save(const QString& path) const {
    if (save_blocked) return false;
    QSaveFile file(path); // atomic replacement; no direct-write fallback
    if (!file.open(QIODevice::WriteOnly)) return false;
    QJsonObject object{{"version", 1}, {"accurate_hle_audio",hle_audio_accuracy==2}, {"hle_audio_accuracy",hle_audio_accuracy}, {"hle_audio_mode_version",1}, {"damage_multiplier",damage_multiplier}, {"disable_heart_drops",disable_heart_drops}, {"disable_rupee_drops",disable_rupee_drops}, {"disable_fairy_drops",disable_fairy_drops}, {"disable_magic_drops",disable_magic_drops}, {"disable_ammo_pickups",disable_ammo_pickups}, {"lose_rupees_on_death",lose_rupees_on_death}, {"world_ends_on_death",world_ends_on_death}, {"advance_six_hours_on_death",advance_six_hours_on_death}, {"fast_time",fast_time}, {"double_prices",double_prices}, {"price_multiplier",price_multiplier}, {"inventory_panel_x",inventory_menu.x}, {"inventory_panel_y",inventory_menu.y}, {"inventory_panel_scale",inventory_menu.scale}, {"dialogue_scale",dialogue_scale}, {"dialogue_y",dialogue_y}, {"dialogue_background_width",dialogue_background_width}, {"dialogue_indicator_x",dialogue_indicator_x}, {"dialogue_indicator_y",dialogue_indicator_y}, {"indicator", indicator}, {"show_performance", show_performance}, {"show_startup_settings_hint", show_startup_settings_hint},
        {"scale", scale}, {"opacity", opacity}, {"x", x}, {"y", y},
        {"single_screen_text", single_screen_text}, {"stick_click_controls", stick_click_controls}, {"fix_bloom", fix_bloom}, {"disable_fog", disable_fog},
        {"hd_pictograph_photos", hd_pictograph_photos},
        {"ambient_occlusion", ambient_occlusion.enabled},
        {"ambient_occlusion_strength", ambient_occlusion.strength}, {"ambient_occlusion_radius", ambient_occlusion.radius},
        {"depth_of_field", depth_of_field.enabled}, {"depth_of_field_focus", depth_of_field.focus},
        {"depth_of_field_transition", depth_of_field.transition}, {"depth_of_field_strength", depth_of_field.strength}, {"interpolation_60fps", interpolation_60fps}, {"red_potion_hearts", red_potion_hearts}, {"blue_potion_hearts", blue_potion_hearts}, {"bottled_fairy_hearts", bottled_fairy_hearts}, {"widescreen", widescreen}, {"camera_zoom_out", camera_zoom_out}, {"game_camera_fov", game_camera_fov}, {"cutscene_camera_fov", cutscene_camera_fov}, {"restoration_hud_scale", restoration_hud_scale}, {"minigame_hud_x",minigame_hud.x},{"minigame_hud_y",minigame_hud.y},{"minigame_hud_scale",minigame_hud.scale},{"magic_shadow_enabled",magic_border.shadow_enabled},{"magic_shadow_size",magic_border.shadow_size},{"magic_shadow_opacity",magic_border.shadow_opacity},{"magic_border_enabled",magic_border.enabled},{"magic_border_thickness",magic_border.thickness},{"magic_border_r",magic_border.color[0]},{"magic_border_g",magic_border.color[1]},{"magic_border_b",magic_border.color[2]}, {"magic_bar_y", magic_bar_y}, {"magic_bar_height", magic_bar_height}, {"dpad_y", dpad_y},
        {"minimap", minimap}, {"minimap_scale", minimap_scale}, {"minimap_opacity", minimap_opacity},
        {"minimap_right_margin", minimap_right_margin}, {"minimap_bottom_margin", minimap_bottom_margin}};
    for(const auto& f:SubmenuHeader::Fields) object.insert(f.key,submenu_header.*f.member);
    object.insert("interpolation_cpu_vertices", interpolation_cpu_vertices);
    object.insert("prop_draw_distance",ActorDrawDistance::Clamp(prop_draw_distance));
    object.insert("actor_draw_distance",ActorDrawDistance::Clamp(actor_draw_distance));
    object.insert("weaker_enhanced_defense",weaker_enhanced_defense);
    object.insert("player_hud_layout_version",int(player_hud.layout_version));
    object.insert("player_hud_opacity",player_hud.opacity);
    object.insert("player_hud_banner_darkness",player_hud.banner_darkness);
    object.insert("player_hud_banner_y",player_hud.banner_y);
    object.insert("player_hud_hide_minimap",player_hud.hide_minimap);
    object.insert("mute_music",mute_music);
    object.insert("disable_low_health_beep",disable_low_health_beep);
    for(unsigned i=0;i<6;++i) {
        const auto key=QString("player_hud_button_%1_").arg(HudAdjustments::ButtonKeys[i]);
        object.insert(key+"x",player_hud.button_x[i]);object.insert(key+"y",player_hud.button_y[i]);
    }
    object.insert("player_hud_hide_menu_prompts",player_hud.hide_menu_prompts);
    object.insert("player_hud_heart_spacing",player_hud.heart_spacing);
    object.insert("player_hud_magic_height",player_hud.magic_height_steps);
    object.insert("player_hud_magic_width",player_hud.magic_width);
    object.insert("player_hud_base_scale",player_hud.base_scale);
    object.insert("player_hud_scale",player_hud.overall);
    object.insert("player_hud_top_padding",player_hud.top);
    object.insert("player_hud_bottom_padding",player_hud.bottom);
    for(unsigned i=0;i<HudAdjustments::Count;++i) {
        const auto prefix=QString("player_hud_%1_").arg(HudAdjustments::Keys[i]);
        object.insert(prefix+"scale",player_hud.elements[i]);
        object.insert(prefix+"x",player_hud.x[i]);object.insert(prefix+"y",player_hud.y[i]);
    }
    object.insert("inventory_native",inventory_menu.enabled);
    object.insert("notebook_popup_width",notebook_live.popup_width);
    object.insert("notebook_popup_height",notebook_live.popup_height);
    object.insert("notebook_schedule_prompt",notebook_live.schedule_prompt);
    object.insert("notebook_schedule_prompt_y",notebook_live.schedule_prompt_y);
    object.insert("options_menu_x",options_menu.x);
    object.insert("options_menu_y",options_menu.y);
    object.insert("options_menu_scale",options_menu.scale);
    object.insert("options_menu_spacing",options_menu.spacing);
    object.insert("options_description_x",options_menu.description.x);
    object.insert("options_description_y",options_menu.description.y);
    object.insert("options_description_scale",options_menu.description.scale);
    object.insert("dpad_offset_x",dpad_offset_x);
    object.insert("dpad_offset_y",dpad_offset_y);
    object.insert("carrots_x",carrots.x);
    object.insert("carrots_y",carrots.y);
    object.insert("carrots_scale",carrots.scale);
    object.insert("rupee_keys_x",rupee_keys.x);
    object.insert("rupee_keys_y",rupee_keys.y);
    object.insert("rupee_keys_scale",rupee_keys.scale);
    object.insert("notebook_map_prompt",notebook_live.map_prompt);
    object.insert("notebook_map_prompt_y",notebook_live.map_prompt_y);
    object.insert("notebook_event_prompt",notebook_live.event_prompt);
    object.insert("notebook_buttons_x",notebook_live.prompt_x);
    object.insert("notebook_prompt_y",notebook_live.prompt_y);
    object.insert("notebook_buttons_scale",notebook_live.prompt_scale);
    object.insert("notebook_buttons_icon_scale",notebook_live.prompt_icon_scale);
    object.insert("notebook_native",notebook_live.enabled);
    object.insert("notebook_expanded_rows",notebook_live.expanded_rows);
    object.insert("world_map_native_zoom_x",world_map_native_zoom.x);
    object.insert("world_map_native_zoom_y",world_map_native_zoom.y);
    object.insert("world_map_native_zoom_scale",world_map_native_zoom.scale);
    object.insert("dungeon_map_move_prompt_x",dungeon_map_move_prompt.x);
    object.insert("dungeon_map_move_prompt_y",dungeon_map_move_prompt.y);
    object.insert("dungeon_map_move_prompt_scale",dungeon_map_move_prompt.scale);
    object.insert("dungeon_map_zoom_prompt_x",dungeon_map_zoom_prompt.x);
    object.insert("dungeon_map_zoom_prompt_y",dungeon_map_zoom_prompt.y);
    object.insert("dungeon_map_zoom_prompt_scale",dungeon_map_zoom_prompt.scale);
    object.insert("world_map_zoom_prompt_x",world_map_zoom_prompt.x);
    object.insert("world_map_zoom_prompt_y",world_map_zoom_prompt.y);
    object.insert("world_map_zoom_prompt_scale",world_map_zoom_prompt.scale);
    object.insert("dungeon_map_prompts_x",dungeon_map.prompts.x);
    object.insert("dungeon_map_prompts_y",dungeon_map.prompts.y);
    object.insert("dungeon_map_prompts_scale",dungeon_map.prompts.scale);
    object.insert("dungeon_map_prompts_icon_scale",dungeon_map.prompts.icon_scale);
    object.insert("dungeon_map_prompts_spacing",dungeon_map.prompts.spacing);
    object.insert("dungeon_map_buttons_x",dungeon_map.prompt_group.x);
    object.insert("dungeon_map_buttons_y",dungeon_map.prompt_group.y);
    object.insert("dungeon_map_buttons_scale",dungeon_map.prompt_group.scale);
    object.insert("dungeon_map_buttons_icon_scale",dungeon_map.prompt_group.icon_scale);
    object.insert("dungeon_map_top",dungeon_map.enabled);
    object.insert("dungeon_map_side_panels",dungeon_map.side_panels);
    object.insert("dungeon_map_top_strip",dungeon_map.top_strip);
    object.insert("dungeon_map_top_panel_x",dungeon_map.top_panel.x);
    object.insert("dungeon_map_top_panel_y",dungeon_map.top_panel.y);
    object.insert("dungeon_map_top_panel_scale",dungeon_map.top_panel.scale);
    object.insert("dungeon_map_panel_darkness",dungeon_map.panel_darkness);
    object.insert("dungeon_map_arrows_scale",dungeon_map.arrow_scale);
    for(unsigned i=0;i<4;++i) {
        const auto key=QString("dungeon_map_arrow_%1_").arg(DungeonMap::ArrowNames[i]);
        object.insert(key+"x",dungeon_map.arrow_offsets[i][0]);
        object.insert(key+"y",dungeon_map.arrow_offsets[i][1]);
    }
    for(unsigned i=0;i<2;++i) {
        const auto key=QString("dungeon_map_panel_%1_").arg(i);const auto p=dungeon_map.panels[i];
        object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);
    }
    for(unsigned i=0;i<DungeonMap::Count;++i) {
        const auto key=QString("dungeon_map_%1_").arg(i);const auto p=dungeon_map.poses[i];
        object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);
    }
    object.insert("banners_native",banners_migrated);
    for(unsigned i=0;i<2;++i){const auto key=QString("banner_%1_").arg(i);const auto& p=banners.parts[i];
        object.insert(key+"height",p.height);object.insert(key+"y",p.y);object.insert(key+"text_scale",p.text_scale);
        object.insert(key+"text_x",p.text_x);object.insert(key+"text_y",p.text_y);object.insert(key+"brightness",p.brightness);}
    const auto write_diamond=[&](const QString& key,const HudButtons::Diamond& d){
        object.insert(key+"enabled",d.enabled);object.insert(key+"x",d.x);object.insert(key+"y",d.y);
        object.insert(key+"spacing",d.spacing);object.insert(key+"rotation",d.rotation);};
    write_diamond("hud_diamond_",hud_buttons.diamond);
    for(unsigned i=0;i<3;++i)write_diamond(QString("hud_button_layout_%1_diamond_").arg(i+1),hud_buttons.preset_diamonds[i]);
    const auto write_buttons=[&](const QString& prefix,const HudButtons::Layout& layout){
        for(unsigned i=0;i<HudButtons::Count;++i){const auto key=prefix+QString::number(i)+"_";const auto& p=layout[i];
            object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);}};
    write_buttons("hud_button_",hud_buttons.buttons);
    object.insert("hud_buttons_scale",hud_buttons.scale);
    object.insert("hud_buttons_flip_peak",hud_buttons.flip_peak);
    for(unsigned i=0;i<3;++i){const auto key=QString("hud_button_layout_%1_").arg(i+1);
        object.insert(key+"saved",hud_buttons.saved[i]);write_buttons(key,hud_buttons.presets[i]);object.insert(key+"scale",hud_buttons.preset_scales[i]);object.insert(key+"flip_peak",hud_buttons.preset_flip_peaks[i]);}
    object.insert("song_reset_top",song_reset.enabled);object.insert("song_reset_background_x",song_reset.background_x);
    for(unsigned i=0;i<SongReset::Count;++i){const auto key=QString("song_reset_%1_").arg(i);const auto& p=song_reset.poses[i];
        object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);}
    object.insert("double_time_top",double_time.enabled);object.insert("double_time_current_day",double_time.current_day_only);
    for(unsigned i=0;i<DoubleTime::Count;++i){const auto key=QString("double_time_%1_").arg(i);const auto p=double_time.poses[i];
        object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);}
    object.insert("song_panel_left_x",song_list.panel_left);object.insert("song_panel_right_x",song_list.panel_right);
    object.insert("song_panel_color_r",song_list.panel_color[0]);
    object.insert("song_panel_color_g",song_list.panel_color[1]);
    object.insert("song_panel_color_b",song_list.panel_color[2]);
    object.insert("song_panel_color_opacity",song_list.panel_color_opacity);
    object.insert("song_panel_darkness",song_list.panel_darkness);
    object.insert("song_stone_darkness",song_list.stone_darkness);
    object.insert("song_list_column_spacing",song_list.column_spacing);
    object.insert("song_list_top",song_list.enabled);object.insert("song_list_background_x",song_list.background_x);object.insert("song_list_spacing",song_list.spacing);
    object.insert("song_list_x",song_list.songs.x);object.insert("song_list_y",song_list.songs.y);object.insert("song_list_scale",song_list.songs.scale);
    object.insert("song_banner_x",song_list.banner.x);object.insert("song_banner_y",song_list.banner.y);
    object.insert("song_banner_scale",song_list.banner.scale);
    object.insert("song_banner_width",song_list.banner_width);object.insert("song_banner_height",song_list.banner_height);
    object.insert("song_staff_x",song_list.staff.x);object.insert("song_staff_y",song_list.staff.y);object.insert("song_staff_scale",song_list.staff.scale);
    object.insert("ocarina_prompts",ocarina_prompt.enabled);
    object.insert("ocarina_prompt_x",ocarina_prompt.x);
    object.insert("ocarina_prompt_y",ocarina_prompt.y);
    object.insert("ocarina_prompt_scale",ocarina_prompt.scale);
    object.insert("ocarina_prompt_spacing",ocarina_prompt.spacing);
    object.insert("ocarina_icon_scale",ocarina_prompt.icon_scale);
    object.insert("file_select_prompts",file_select_prompt.enabled);
    object.insert("file_select_prompt_x",file_select_prompt.x);
    object.insert("file_select_prompt_y",file_select_prompt.y);
    object.insert("file_select_prompt_scale",file_select_prompt.scale);
    object.insert("file_select_prompt_spacing",file_select_prompt.spacing);
    object.insert("file_select_prompt_icon_scale",file_select_prompt.icon_scale);
    object.insert("file_select_stone_spacing",file_select.stone_spacing);
    object.insert("file_select_entrance_ease",file_select.entrance_ease);
    object.insert("file_select_native",file_select.enabled);
    object.insert("file_select_header_x",file_select.header.x);
    object.insert("file_select_header_y",file_select.header.y);
    object.insert("file_select_header_scale_x",file_select.header.scale_x);
    object.insert("file_select_header_scale_y",file_select.header.scale_y);
    object.insert("file_select_title_x",file_select.title.x);
    object.insert("file_select_title_y",file_select.title.y);
    object.insert("file_select_title_scale",file_select.title.scale);
    object.insert("classic_saving",classic_saving);
    object.insert("classic_double_time",classic_double_time);
    object.insert("sword_activate_owls",sword_activate_owls);
    object.insert("disable_bomber_approach",disable_bomber_approach);
    object.insert("disable_tatl_reminders",disable_tatl_reminders);
    object.insert("startup_logo_x",startup.logo.x);
    object.insert("startup_logo_y",startup.logo.y);
    object.insert("startup_logo_scale",startup.logo.scale);
    object.insert("startup_copyright_x",startup.copyright.x);
    object.insert("startup_copyright_y",startup.copyright.y);
    object.insert("startup_copyright_scale",startup.copyright.scale);
    object.insert("startup_press_start_x",startup.press_start.x);
    object.insert("startup_press_start_y",startup.press_start.y);
    object.insert("startup_press_start_scale",startup.press_start.scale);
    object.insert("startup_loading_icon_x",startup.loading_icon.x);
    object.insert("startup_loading_icon_y",startup.loading_icon.y);
    object.insert("startup_loading_icon_scale",startup.loading_icon.scale);
    object.insert("item_present_native",item_present.enabled);
    object.insert("item_present_help_x",item_present.help_pose.x);object.insert("item_present_help_y",item_present.help_pose.y);
    object.insert("item_present_help_scale",item_present.help_pose.scale);object.insert("item_present_help_spacing",item_present.help_spacing);
    object.insert("item_present_horizontal",item_present.horizontal);
    for(unsigned i=0;i<NativeItemPresent::Count;++i) {
        const QString key=QString("item_present_%1_").arg(i);const auto& p=item_present.poses[i];
        object.insert(key+"x",p.x);
        if(i!=NativeItemPresent::Background){object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);}
    }
    object.insert("mask_present_native",mask_present.enabled);
    object.insert("mask_present_help_x",mask_present.help_pose.x);object.insert("mask_present_help_y",mask_present.help_pose.y);
    object.insert("mask_present_help_scale",mask_present.help_pose.scale);object.insert("mask_present_help_spacing",mask_present.help_spacing);
    for(unsigned i=0;i<NativeItemPresent::Count;++i) {
        if(i==NativeItemPresent::Background||i==NativeItemPresent::Right)continue;
        const QString key=QString("mask_present_%1_").arg(i);const auto& p=mask_present.poses[i];
        object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);
    }
    for(unsigned i=0;i<NativeFileSelect::Count;++i) {
        const QString key=QString("file_select_%1_").arg(i);const auto& p=file_select.poses[i];
        object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);
    }
    for(unsigned i=0;i<NativeNotebook::Count;++i) {
        const QString key=QString("notebook_live_%1_").arg(i);const auto& p=notebook_live.poses[i];
        object.insert(key+"x",p.x);object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);
    }
    object.insert("gear_enabled",inventory_menu.gear.enabled);
    object.insert("gear_horizontal",inventory_menu.gear.horizontal);
    for(unsigned i=0;i<GearMenu::Count;++i) {
        if(i==GearMenu::Background||i==GearMenu::Model||i==GearMenu::Description)continue; // Shared inventory settings.
        const QString key=QString("gear_%1_").arg(i);
        const auto& p=inventory_menu.gear.poses[i];
        object.insert(key+"x",p.x);
        if(i!=GearMenu::Background){object.insert(key+"y",p.y);object.insert(key+"scale",p.scale);}
    }
    object.insert("inventory_masks_x",inventory_menu.masks_x);
    object.insert("inventory_masks_y",inventory_menu.masks_y);
    object.insert("inventory_masks_scale",inventory_menu.masks_scale);
    object.insert("inventory_high_res_pause",inventory_menu.high_res_pause);
    object.insert("inventory_shared_slide",inventory_menu.shared_slide);
    object.insert("inventory_bar_enabled",inventory_menu.bar_enabled);
    object.insert("inventory_bar_x",inventory_menu.bar_x);
    object.insert("inventory_bar_y",inventory_menu.bar_y);
    object.insert("inventory_bar_scale",inventory_menu.bar_scale);
    object.insert("inventory_bar_spacing",inventory_menu.bar_spacing);
    object.insert("inventory_description_x",inventory_menu.description_x);
    object.insert("inventory_description_y",inventory_menu.description_y);
    object.insert("inventory_description_scale",inventory_menu.description_scale);
    object.insert("inventory_model_x",inventory_menu.model_x);
    object.insert("inventory_model_y",inventory_menu.model_y);
    object.insert("inventory_model_scale",inventory_menu.model_scale);
    object.insert("inventory_button_gear_x",inventory_menu.gear_button.x);
    object.insert("inventory_button_gear_y",inventory_menu.gear_button.y);
    object.insert("inventory_button_gear_scale",inventory_menu.gear_button.scale);
    object.insert("inventory_help_enabled",inventory_menu.help.enabled);
    object.insert("inventory_help_horizontal",inventory_menu.help.horizontal);
    object.insert("inventory_help_spacing",inventory_menu.help.spacing);
    object.insert("inventory_help_button_gap",inventory_menu.help.button_gap);
    object.insert("inventory_help_button_scale",inventory_menu.help.button_scale);
    object.insert("inventory_help_text_scale",inventory_menu.help.text_scale);
    object.insert("inventory_prompt_opacity",inventory_menu.help.opacity);
    object.insert("inventory_help_0_x",inventory_menu.help.pose.x);
    object.insert("inventory_help_0_y",inventory_menu.help.pose.y);
    object.insert("inventory_help_0_scale",inventory_menu.help.pose.scale);
    object.insert("inventory_map_prompt_x",inventory_menu.map_prompt.x);
    object.insert("inventory_map_prompt_y",inventory_menu.map_prompt.y);
    object.insert("inventory_map_prompt_scale",inventory_menu.map_prompt.scale);
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"r","arrow","text"};
        const auto prefix=QString("inventory_map_prompt_%1_").arg(names[i]);
        const auto& p=inventory_menu.map_prompt_elements[i];
        object.insert(prefix+"x",p.x);object.insert(prefix+"y",p.y);object.insert(prefix+"scale",p.scale);
    }
    object.insert("map_items_prompt_x",map_items_prompt.pose.x);
    object.insert("map_items_prompt_y",map_items_prompt.pose.y);
    object.insert("map_items_prompt_scale",map_items_prompt.pose.scale);
    for(unsigned i=0;i<3;++i) {
        const char* names[]={"l","arrow","text"};
        const auto prefix=QString("map_items_prompt_%1_").arg(names[i]);
        const auto& p=map_items_prompt.elements[i];
        object.insert(prefix+"x",p.x);object.insert(prefix+"y",p.y);object.insert(prefix+"scale",p.scale);
    }
    object.insert("inventory_buttons_enabled",inventory_menu.buttons_enabled);
    object.insert("brown_shadow_width",inventory_menu.shadow_width);
    object.insert("brown_shadow_opacity",inventory_menu.shadow_opacity);
    object.insert("inventory_background_enabled",inventory_menu.background_enabled);
    object.insert("inventory_background_x",inventory_menu.background.x);
    object.insert("brown_background_y",inventory_menu.background.y);
    object.insert("brown_background_scale",inventory_menu.background.scale);
    for(unsigned i=0;i<inventory_menu.buttons.size();++i) {
        const char* names[]={"back","masks","items"};
        const auto prefix=QString("inventory_button_%1_").arg(names[i]);
        const auto& b=inventory_menu.buttons[i];
        object.insert(prefix+"x",b.x);object.insert(prefix+"y",b.y);object.insert(prefix+"scale",b.scale);
    }
    QJsonArray saved_draws;
    for(const auto& d:fullscreen_draws) {
        saved_draws.append(QJsonObject{
            {"shader",QString::number(d.shader,16)}, {"swizzle",QString::number(d.swizzle,16)},
            {"vertices",double(d.vertices)}, {"width",double(d.width)}, {"height",double(d.height)},
            {"textures",double(d.textures)}, {"blend",double(d.blend)}, {"depth",double(d.depth)}});
    }
    object.insert("fullscreen_draws",saved_draws);
    const auto bytes = QJsonDocument(object).toJson();
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
}
}
