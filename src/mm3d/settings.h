// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/depth_of_field.h"
#include "mm3d/ambient_occlusion.h"
#include "mm3d/ocarina_prompt.h"
#include "mm3d/file_select_prompt.h"
#include "mm3d/magic_border.h"
#include "mm3d/hud_adjustments.h"
#include "mm3d/minigame_hud.h"
#include "mm3d/song_list.h"
#include "mm3d/options_menu.h"
#include "mm3d/double_time.h"
#include "mm3d/song_reset.h"
#include "mm3d/hud_buttons.h"
#include "mm3d/banners.h"
#include "mm3d/dungeon_map.h"
#include "mm3d/world_map_prompt.h"
#include <QString>
#include "mm3d/submenu_header.h"
#include "mm3d/inventory_menu.h"
#include "mm3d/native_notebook.h"
#include "mm3d/native_file_select.h"
#include "mm3d/native_item_present.h"
#include "mm3d/native_startup.h"
#include "mm3d/widescreen_probe.h"
namespace MM3D {
struct Settings {
    SubmenuHeader::Options submenu_header;
    InventoryMenu::Options inventory_menu;
    ItemsMapPrompt::ReturnOptions map_items_prompt;
    NativeNotebook::Options notebook_live;
    OcarinaPrompt::Options ocarina_prompt;
    FileSelectPrompt::Options file_select_prompt;
    SongList::Options song_list;
    OptionsMenu::Options options_menu;
    DoubleTime::Options double_time;
    SongReset::Options song_reset;
    HudButtons::Options hud_buttons;
    HudAdjustments::Options player_hud;
    Banners::Options banners;
    bool banners_migrated=false;
    DungeonMap::Options dungeon_map;
    NativeInventory::ButtonPose world_map_zoom_prompt{0,0,.8f},world_map_native_zoom;
    NativeFileSelect::Options file_select;
    NativeItemPresent::Options item_present;
    NativeItemPresent::Options mask_present;
    NativeStartup::Options startup;
    float actor_draw_distance = 1.f;
    float prop_draw_distance = 1.f;
    int damage_multiplier = 1;
    bool weaker_enhanced_defense = false;
    int red_potion_hearts = 20, blue_potion_hearts = 20, bottled_fairy_hearts = 10;
    bool disable_heart_drops = false;
    bool disable_rupee_drops = false;
    bool disable_fairy_drops = false;
    bool disable_magic_drops = false, disable_ammo_pickups = false;
    bool lose_rupees_on_death = false;
    bool world_ends_on_death = false;
    bool advance_six_hours_on_death = false;
    bool fast_time = false;
    bool double_prices = false;
    int price_multiplier = 2;
    int hle_audio_accuracy = 1; // 1: Balanced, 2: Accurate HLE. All pre-migration preferences become 1.
    bool mute_music = false;
    bool disable_low_health_beep = false;
    bool disable_tatl_reminders = false;
    bool single_screen_text = true;
    bool stick_click_controls = false;
    bool fix_bloom = true;
    bool disable_fog = false;
    bool hd_pictograph_photos = true;
    DepthOfField::Options depth_of_field;
    AmbientOcclusion::Options ambient_occlusion;
    bool interpolation_60fps = true;
    bool interpolation_cpu_vertices = true;
    bool classic_saving = false;
    bool classic_double_time = false;
    bool sword_activate_owls = false;
    bool disable_bomber_approach = false;
    float dialogue_scale = 1.f;
    float dialogue_y = 0.f;
    float dialogue_background_width = 1.f;
    float dialogue_indicator_x = 0.f, dialogue_indicator_y = 0.f;
    bool indicator = true;
    bool show_performance = false;
    bool show_startup_settings_hint = true;
    bool widescreen = false;
    float camera_zoom_out = 1.f;
    float game_camera_fov = 65.f;
    float cutscene_camera_fov = 49.f;
    float restoration_hud_scale = 1.f;
    MagicBorder::Options magic_border;
    MinigameHud::Options minigame_hud;
    NativeInventory::ButtonPose rupee_keys,carrots;
    float magic_bar_y = 0.f;
    float magic_bar_height = 1.f;
    float dpad_y = 0.f;
    float dpad_offset_x = 0.f, dpad_offset_y = 0.f;
    bool minimap = true;
    float minimap_scale = .36f, minimap_opacity = .85f;
    float minimap_right_margin = 8.f, minimap_bottom_margin = 8.f;
    std::vector<WidescreenProbe::Draw> fullscreen_draws;
    float scale = 1.f;
    float opacity = .9f;
    float x = .02f, y = .02f;
    bool Load(const QString& path);
    bool Save(const QString& path) const;
private:
    bool save_blocked = false;
};
}
