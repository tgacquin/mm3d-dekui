// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <vector>
#include <QDir>
#include <QUuid>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QSet>
#include <QFileInfo>

#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#ifdef ANDROID
#include "common/android_utils.h"
#endif
#include "mm3d/diagnostics.h"
#include "mm3d/layout_reset.h"
#include "mm3d/classic_saving.h"
#include "mm3d/classic_double_time.h"
#include "mm3d/audio_capture.h"
#include "audio_core/hle/accuracy.h"
#include "mm3d/input_latency.h"
#include "mm3d/interpolation_performance.h"
#include "mm3d/draw_probe.h"
#include "mm3d/pause_control.h"
#include "mm3d/prompt_layer.h"
#include "mm3d/difficulty.h"
#include "mm3d/actor_draw_distance.h"
#include "mm3d/prop_draw_distance.h"
#include "mm3d/death_penalties.h"
#include "mm3d/fast_time.h"
#include "mm3d/prices.h"
#include "mm3d/single_screen_text.h"
#include "mm3d/stick_controls.h"
#include "mm3d/notebook.h"
#include "mm3d/notebook_inspector.h"
#include "mm3d/profiles.h"
#include "mm3d/texture_picker.h"
#include "mm3d/overlay.h"
#include "mm3d/bloom_sampling.h"
#include "mm3d/fog.h"
#include "mm3d/bottle_healing.h"
#include "mm3d/player_settings.h"
#include "mm3d/settings.h"
#include "mm3d/restoration_hud.h"
#include "mm3d/player_hud.h"
#include "mm3d/minimap.h"
#include "mm3d/native_minimap.h"
#include "mm3d/dialogue_scale.h"
#include "mm3d/inventory_menu.h"
#include "mm3d/widescreen.h"
#include "mm3d/widescreen_probe.h"
#include "mm3d/trace.h"
#include "mm3d/camera_writer.h"
#include "mm3d/camera_live.h"
#include "mm3d/camera_midpoint.h"
#include "mm3d/link_pose_capture.h"
#include "mm3d/effect_capture.h"
#include "mm3d/form_camera.h"
#include "mm3d/music.h"
#include "mm3d/tatl_reminders.h"
#include "mm3d/owl_sword.h"
#include "mm3d/owl_intro.h"
#include "mm3d/simple_cheats.h"
#include "mm3d/photo_capture.h"
#include "mm3d/bomber_approach.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include "mm3d/cpu_interpolation.h"

namespace MM3D {
namespace {
// Only presentation.json settings are snapshotted; installed ROM overrides are separate.
QString SettingsBackupPath(const QString& directory,const QString& kind) {
    const QString folder=directory+"settings-backups/";
    if(!QDir().mkpath(folder))return {};
    return folder+QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd_HH-mm-ss-zzz")+
        "-"+kind+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json";
}
void ApplyFrameInterpolation(bool enabled, bool cpu_vertices) {
    CpuInterpolation::enabled.store(cpu_vertices);
    CameraMidpoint::enabled=enabled;
    CameraMidpoint::present_60=enabled;
    CameraMidpoint::native_preview=false;
    CameraMidpoint::capture_pair_at_us=0;
    ++CameraMidpoint::generation;
    LinkPoseCapture::interpolate=enabled;
    LinkPoseCapture::interpolate_actors=enabled;
    ++LinkPoseCapture::interpolation_generation;
    LinkPoseCapture::replaced_draws=0;LinkPoseCapture::replaced_bones=0;
    if(enabled) {
        PlayerSettings::set_vsync.store(1);
        PlayerSettings::set_hardware_shaders.store(CpuInterpolation::Enabled() ? 0 : 1);
    }
    LOG_INFO(Render_Vulkan, "Interpolation settings: enabled={} vertex_processing={}",
             enabled, cpu_vertices ? "CPU" : "GPU");
}
void ApplyPresentationSettings(const Settings& settings) {
    ApplyFrameInterpolation(settings.interpolation_60fps, settings.interpolation_cpu_vertices);
    HudAdjustments::Apply(settings.player_hud);
    PlayerHud::ApplyRupeeKeys(settings.rupee_keys);
    PlayerHud::ApplyCarrots(settings.carrots);
    PlayerHud::ApplyDpadPosition(settings.dpad_offset_x,settings.dpad_offset_y);
    PlayerHud::ApplyMagicHeight(settings.magic_bar_height);
    AudioCore::HLE::accuracy_mode.store(static_cast<AudioCore::HLE::AccuracyMode>(settings.hle_audio_accuracy));
    Music::muted.store(settings.mute_music);
    Music::disable_low_health_beep.store(settings.disable_low_health_beep);
    TatlReminders::disabled.store(settings.disable_tatl_reminders);
    SingleScreenText::enabled.store(settings.single_screen_text);
    StickControls::Configure(settings.stick_click_controls);
    BloomSampling::enabled.store(settings.fix_bloom);
    Fog::disabled.store(settings.disable_fog);
    DepthOfField::Apply(settings.depth_of_field);
    AmbientOcclusion::Apply(settings.ambient_occlusion);
    ClassicSaving::enabled.store(settings.classic_saving);
    ClassicDoubleTime::enabled.store(settings.classic_double_time);
    OwlSword::enabled.store(settings.sword_activate_owls);
    OwlIntro::disabled.store(settings.sword_activate_owls);
    BomberApproach::disabled.store(settings.disable_bomber_approach);
    BottleHealing::red_hearts.store(settings.red_potion_hearts);
    BottleHealing::blue_hearts.store(settings.blue_potion_hearts);
    BottleHealing::fairy_hearts.store(settings.bottled_fairy_hearts);
    Difficulty::damage_multiplier.store(settings.damage_multiplier);
    PropDrawDistance::multiplier.store(ActorDrawDistance::Clamp(settings.prop_draw_distance));
    ActorDrawDistance::multiplier.store(ActorDrawDistance::Clamp(settings.actor_draw_distance));
    Difficulty::weaker_enhanced_defense.store(settings.weaker_enhanced_defense);
    Prices::Initialize(settings.double_prices,settings.price_multiplier);
    Difficulty::disable_heart_drops.store(settings.disable_heart_drops);
    Difficulty::disable_rupee_drops.store(settings.disable_rupee_drops);
    Difficulty::disable_fairy_drops.store(settings.disable_fairy_drops);
    Difficulty::disable_magic_drops.store(settings.disable_magic_drops);
    Difficulty::disable_ammo_pickups.store(settings.disable_ammo_pickups);
    DeathPenalties::lose_rupees.store(settings.lose_rupees_on_death);
    DeathPenalties::world_ends.store(settings.world_ends_on_death);
    DeathPenalties::advance_six_hours.store(settings.advance_six_hours_on_death);
    FastTime::enabled.store(settings.fast_time);
    InventoryMenu::Apply(settings.inventory_menu);
    NativeNotebook::Apply(settings.notebook_live);
    NativeFileSelect::Apply(settings.file_select);
    OcarinaPrompt::Apply(settings.ocarina_prompt);
    FileSelectPrompt::Apply(settings.file_select_prompt);
    MagicBorder::Apply(settings.magic_border);
    SongList::Apply(settings.song_list);
    OptionsMenu::Apply(settings.options_menu);
    DoubleTime::Apply(settings.double_time);
    SongReset::Apply(settings.song_reset);
    HudButtons::Apply(settings.hud_buttons);
    MinigameHud::Apply(settings.minigame_hud);
    Banners::Apply(settings.banners);
    DungeonMap::Apply(settings.dungeon_map);
    ItemsMapPrompt::ApplyReturn(settings.map_items_prompt);
    WorldMapPrompt::Apply(settings.world_map_zoom_prompt);
    WorldMapPrompt::ApplyNative(settings.world_map_native_zoom);
    NativeItemPresent::Apply(settings.item_present);
    NativeItemPresent::ApplyMask(settings.mask_present);
    NativeStartup::Apply(settings.startup);
    DialogueScale::scale.store(settings.dialogue_scale);
    DialogueScale::y_offset.store(settings.dialogue_y);
    DialogueScale::background_width.store(settings.dialogue_background_width);
    DialogueScale::indicator_x.store(settings.dialogue_indicator_x);
    DialogueScale::indicator_y.store(settings.dialogue_indicator_y);
    CameraLive::SetFov(settings.game_camera_fov);
    CameraLive::SetCutsceneFov(settings.cutscene_camera_fov);
    Minimap::enabled.store(settings.minimap);
    Minimap::scale.store(settings.minimap_scale);
    Minimap::opacity.store(settings.minimap_opacity);
    NativeMinimap::right_margin.store(settings.minimap_right_margin);
    NativeMinimap::bottom_margin.store(settings.minimap_bottom_margin);
    Widescreen::enabled.store(settings.widescreen);
    Widescreen::camera_zoom_out.store(settings.camera_zoom_out);
    {
        std::scoped_lock lock(WidescreenProbe::mutex);
        WidescreenProbe::saved_draws=settings.fullscreen_draws;
    }
}

struct PointerEvent { float x, y; int button; bool down; };
std::mutex input_mutex;
std::vector<PointerEvent> input;
std::vector<std::string> text_input;
std::vector<std::pair<int, bool>> key_input;
float wheel_x{}, wheel_y{};
ImVec2 panel_position{}, panel_size{};
ImVec2 editor_position{}, editor_size{};
std::array<bool, 2> pointer_held{}, panel_owned{};
double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void CheckResult(VkResult result) {
    if (result < 0) throw std::runtime_error("MM3D ImGui Vulkan operation failed");
}
}
bool Scroll(float x, float y, float horizontal, float vertical) {
    if (!Pointer(x, y)) return false;
    std::scoped_lock lock{input_mutex};
    wheel_x += horizontal;
    wheel_y += vertical;
    return true;
}
bool TextInput(const char* text) {
    if (!presentation_available.load() || !overlay_visible.load() || !text || !*text) return false;
    std::scoped_lock lock{input_mutex};
    text_input.emplace_back(text);
    return true;
}
void KeyInput(int key, bool down) {
    std::scoped_lock lock{input_mutex};
    if (key_input.size() < 256) key_input.emplace_back(key, down);
}
bool Pointer(float x, float y, int button, bool down) {
    std::scoped_lock lock{input_mutex};
    const bool visible = presentation_available.load() && overlay_visible.load();
    const auto hit=[&](ImVec2 p,ImVec2 s){return x>=p.x && y>=p.y && x<p.x+s.x && y<p.y+s.y;};
    const bool inside = visible && (hit(panel_position,panel_size) || hit(editor_position,editor_size));
    bool consumed = inside;
    if (button >= 0 && button < int(pointer_held.size())) {
        if (down) {
            pointer_held[button] = true;
            panel_owned[button] = inside;
        }
        consumed = pointer_held[button] && panel_owned[button];
        if (!down) pointer_held[button] = false;
    } else {
        for (unsigned i = 0; i < pointer_held.size(); ++i)
            if (pointer_held[i]) consumed = panel_owned[i];
    }
    if (visible && input.size() < 256 && (button < 0 || consumed))
        input.push_back({x, y, button, down});
    return consumed;
}
static bool EditTransform(Profiles::Profile& p) {
    bool changed = ImGui::SliderFloat("Scale", &p.scale, .1f, 2.f, "%.2fx");
    changed |= ImGui::SliderFloat("Position X", &p.x, -.5f, .5f, "%.3f");
    changed |= ImGui::SliderFloat("Position Y", &p.y, -.5f, .5f, "%.3f");
    ImGui::TextWrapped(p.presentation ? "Crop masks the copied layer, not the atlas thumbnail."
                                    : "Crop masks the transformed draw viewport.");
    if (ImGui::Button("Reset crop")) { p.crop = {0,0,1,1}; changed = true; }
    changed |= ImGui::SliderFloat("Crop left", &p.crop[0], 0.f, p.crop[2]-.01f, "%.3f");
    changed |= ImGui::SliderFloat("Crop top", &p.crop[1], 0.f, p.crop[3]-.01f, "%.3f");
    changed |= ImGui::SliderFloat("Crop right", &p.crop[2], p.crop[0]+.01f, 1.f, "%.3f");
    changed |= ImGui::SliderFloat("Crop bottom", &p.crop[3], p.crop[1]+.01f, 1.f, "%.3f");
    return changed;
}
// Reuses captured GPU images; no extracted images are written to disk.
static void AtlasImage(ImTextureID texture, ImVec2 size, ImVec2 uv0, ImVec2 uv1) {
    const auto pos=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();
    const auto clip_min=draw->GetClipRectMin(), clip_max=draw->GetClipRectMax();
    const float start_x=std::max(0.f,std::floor((clip_min.x-pos.x)/12.f)*12.f);
    const float start_y=std::max(0.f,std::floor((clip_min.y-pos.y)/12.f)*12.f);
    for(float y=start_y;y<std::min(size.y,clip_max.y-pos.y);y+=12)
        for(float x=start_x;x<std::min(size.x,clip_max.x-pos.x);x+=12)
        draw->AddRectFilled({pos.x+x,pos.y+y},{pos.x+std::min(x+12,size.x),pos.y+std::min(y+12,size.y)},
            ((int(x/12)+int(y/12))%2) ? IM_COL32(95,95,95,255) : IM_COL32(55,55,55,255));
    ImGui::Image(texture,size,uv0,uv1);
}
static void EditAtlasGrid(Profiles::Profile::AtlasGrid& grid, ImTextureID texture, float& zoom,
                          const Textures::Thumbnail& detail) {
    static int selection_mode=1;
    static ImVec2 rectangle_start;
    ImGui::Combo("Selection tool",&selection_mode,"Grid cells\0Custom rectangles\0Square\0");
    ImGui::TextUnformatted("Selections snap to source-texture pixels. Edge values below are pixels, not screen coordinates.");
    ImGui::TextWrapped("Custom rectangles: drag over the image to add a region; adjust its edges below. All regions belong to this group.");
    ImGui::BeginDisabled(selection_mode!=0);
    int columns=grid.columns, rows=grid.rows;
    bool changed=ImGui::InputInt("Columns", &columns);
    changed|=ImGui::InputInt("Rows", &rows);
    if(changed) {
        // Equal-sized cells must contain a whole number of source pixels.
        const auto count=[](int requested,int previous,unsigned extent) {
            int n=std::clamp(requested,1,std::min(64,int(extent)));
            const int step=requested>previous ? 1 : -1;
            while(n>1 && n< std::min(64,int(extent)) && extent%unsigned(n)!=0) n+=step;
            while(n>1 && extent%unsigned(n)!=0) --n;
            return n;
        };
        grid.columns=count(columns,grid.columns,grid.width); grid.rows=count(rows,grid.rows,grid.height);
        grid.cells.clear();
    }
    ImGui::TextUnformatted("Equal grid counts snap to divisors of the texture size so cell edges land on pixels.");
    ImGui::TextWrapped("Click cells to toggle selection. Changing the grid clears its selected cells.");
    if(ImGui::Button("Clear cells")) grid.cells.clear();
    ImGui::EndDisabled();
    ImGui::SliderFloat("Zoom",&zoom,.25f,32.f,"%.2fx");
    ImGui::SameLine();
    if(ImGui::Button("Fit width")) zoom=std::clamp((ImGui::GetContentRegionAvail().x+ImGui::GetCursorPosX()-40.f)/grid.width,.25f,32.f);
    ImGui::TextUnformatted("Scroll to explore the zoomed image; use the bottom scrollbar horizontally.");
    const float width=grid.width*zoom;
    const ImVec2 size{width,width*grid.height/grid.width};
    if(ImGui::BeginChild("Atlas canvas",{0,std::max(140.f,ImGui::GetContentRegionAvail().y-170.f)},
        ImGuiChildFlags_Borders,ImGuiWindowFlags_HorizontalScrollbar)) {
    const auto pos=ImGui::GetCursorScreenPos();
    AtlasImage(texture,size,{0,1},{1,0});
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("Atlas selection surface",size);
    const auto mouse=ImGui::GetIO().MousePos;
    const auto snap=[](float value,unsigned extent){return float(int(std::clamp(value,0.f,1.f)*extent+.5f))/extent;};
    const ImVec2 uv{snap((mouse.x-pos.x)/size.x,grid.width),snap((mouse.y-pos.y)/size.y,grid.height)};
    if(selection_mode!=0 && ImGui::IsItemActivated()) rectangle_start=uv;
    auto* draw=ImGui::GetWindowDrawList();
    if(selection_mode!=0 && (ImGui::IsItemActive() || ImGui::IsItemDeactivated())) {
        ImVec2 end=uv;
        if(selection_mode==2) {
            const float dx=(uv.x-rectangle_start.x)*grid.width,dy=(uv.y-rectangle_start.y)*grid.height;
            const float limit_x=(dx<0 ? rectangle_start.x : 1-rectangle_start.x)*grid.width;
            const float limit_y=(dy<0 ? rectangle_start.y : 1-rectangle_start.y)*grid.height;
            const float side=float(int(std::min(std::max(std::abs(dx),std::abs(dy)),std::min(limit_x,limit_y))+.5f));
            end={rectangle_start.x+(dx<0 ? -side : side)/grid.width,
                rectangle_start.y+(dy<0 ? -side : side)/grid.height};
        }
        const std::array<float,4> r{std::min(end.x,rectangle_start.x),std::min(end.y,rectangle_start.y),
            std::max(end.x,rectangle_start.x),std::max(end.y,rectangle_start.y)};
        draw->AddRect({pos.x+r[0]*size.x,pos.y+r[1]*size.y},{pos.x+r[2]*size.x,pos.y+r[3]*size.y},IM_COL32(255,220,40,255));
        if(ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(0) &&
            (r[2]-r[0])*grid.width>=1 && (r[3]-r[1])*grid.height>=1 && grid.rectangles.size()<1024)
            grid.rectangles.push_back(r);
    }
    if(selection_mode==0 && ImGui::IsItemActivated()) {
        const auto mouse=ImGui::GetIO().MousePos;
        const int col=std::clamp(int((mouse.x-pos.x)/size.x*grid.columns),0,grid.columns-1);
        const int row=std::clamp(int((mouse.y-pos.y)/size.y*grid.rows),0,grid.rows-1);
        const int cell=row*grid.columns+col;
        const auto it=std::find(grid.cells.begin(),grid.cells.end(),cell);
        if(it==grid.cells.end()) grid.cells.push_back(cell); else grid.cells.erase(it);
        std::sort(grid.cells.begin(),grid.cells.end());
    }
    for(const auto& r:grid.rectangles) {
        draw->AddRectFilled({pos.x+r[0]*size.x,pos.y+r[1]*size.y},{pos.x+r[2]*size.x,pos.y+r[3]*size.y},IM_COL32(40,230,90,65));
        draw->AddRect({pos.x+r[0]*size.x,pos.y+r[1]*size.y},{pos.x+r[2]*size.x,pos.y+r[3]*size.y},IM_COL32(40,230,90,255));
    }
    for(int cell:grid.cells) {
        const float x=float(cell%grid.columns)/grid.columns, y=float(cell/grid.columns)/grid.rows;
        draw->AddRectFilled({pos.x+x*size.x,pos.y+y*size.y},
            {pos.x+(x+1.f/grid.columns)*size.x,pos.y+(y+1.f/grid.rows)*size.y},IM_COL32(40,230,90,90));
    }
    for(int x=0;selection_mode==0 && x<=grid.columns;++x) draw->AddLine({pos.x+size.x*x/grid.columns,pos.y},
        {pos.x+size.x*x/grid.columns,pos.y+size.y},IM_COL32(220,220,220,160));
    for(int y=0;selection_mode==0 && y<=grid.rows;++y) draw->AddLine({pos.x,pos.y+size.y*y/grid.rows},
        {pos.x+size.x,pos.y+size.y*y/grid.rows},IM_COL32(220,220,220,160));
    }
    ImGui::EndChild();
    ImGui::Text("Selected cells: %u",unsigned(grid.cells.size()));
    if(ImGui::BeginChild("Selected cell thumbnails",{0,140},ImGuiChildFlags_Borders)) {
        for(size_t i=0;i<grid.rectangles.size();) {
            ImGui::PushID(int(i));
            auto& r=grid.rectangles[i];
            ImGui::Text("Region %u",unsigned(i+1));
            ImGui::BeginDisabled(detail.alpha.empty());
            if(ImGui::Button("Trim to visible pixels"))
                Textures::TrimVisible(detail.alpha,detail.width,detail.height,r);
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Includes every nonzero alpha pixel inside this region. Fully transparent selections stay unchanged.");
            const float w=(r[2]-r[0])*grid.width,h=(r[3]-r[1])*grid.height;
            const float s=80.f/std::max(w,h);
            AtlasImage(texture,{w*s,h*s},{r[0],1-r[1]},{r[2],1-r[3]});
            const int width=int(grid.width),height=int(grid.height);
            int left=std::clamp(int(r[0]*width+.5f),0,width-1),top=std::clamp(int(r[1]*height+.5f),0,height-1);
            int right=std::clamp(int(r[2]*width+.5f),left+1,width),bottom=std::clamp(int(r[3]*height+.5f),top+1,height);
            bool edited=ImGui::SliderInt("Left (px)",&left,0,right-1,"%d",ImGuiSliderFlags_AlwaysClamp);
            edited|=ImGui::SliderInt("Top (px)",&top,0,bottom-1,"%d",ImGuiSliderFlags_AlwaysClamp);
            edited|=ImGui::SliderInt("Right (px)",&right,left+1,width,"%d",ImGuiSliderFlags_AlwaysClamp);
            edited|=ImGui::SliderInt("Bottom (px)",&bottom,top+1,height,"%d",ImGuiSliderFlags_AlwaysClamp);
            if(edited) r={float(left)/width,float(top)/height,float(right)/width,float(bottom)/height};
            ImGui::Text("%d x %d pixels | Ctrl-click a slider to type an exact edge",right-left,bottom-top);
            if(ImGui::Button("Remove region")) grid.rectangles.erase(grid.rectangles.begin()+i); else ++i;
            ImGui::PopID();
        }
        unsigned index=0;
        for(int cell:grid.cells) {
            if(index++%4) ImGui::SameLine();
            ImGui::BeginGroup();
            const float u=float(cell%grid.columns)/grid.columns, v=float(cell/grid.columns)/grid.rows;
            const float w=float(grid.width)/grid.columns,h=float(grid.height)/grid.rows;
            const float scale=64.f/std::max(w,h);
            AtlasImage(texture,{w*scale,h*scale},{u,1-v},{u+1.f/grid.columns,1-v-1.f/grid.rows});
            ImGui::Text("R%d C%d",cell/grid.columns+1,cell%grid.columns+1);
            ImGui::EndGroup();
        }
    }
    ImGui::EndChild();
}
struct Overlay::Impl {
    const Vulkan::Instance& instance;
    vk::RenderPass renderpass;
    ImGuiContext* context{};
    Settings settings;
    ImGuiStyle base_style;
    bool resize_settings_window=false;
    int settings_size_step=0;
    bool editing_settings_size=false;
    Notebook::Settings notebook;
    Timing timing;
    QString directory;
    QString status;
    QString active_profile;
    Profiles::Profile draft, last_preview;
    char group_name[64] = "";
    char notebook_source[512] = "";
    char notebook_output[512] = "";
    char fade_source[512] = "";
    int group_folder = 0;
    int atlas_index = -1;
    bool atlas_editor_open = false;
    float atlas_zoom = 2.f;
    double previous = Now();
    double startup_hint_start = -1;
    bool startup_hint_shown = false;
    bool backend_ready{};
    std::mutex* submit_mutex{};
    vk::Sampler thumbnail_sampler{};
    std::vector<VkDescriptorSet> thumbnail_sets;
    std::vector<std::shared_ptr<Textures::Thumbnail>> thumbnails;
    std::shared_ptr<Textures::Thumbnail> atlas_detail;
    VkDescriptorSet atlas_detail_set{};
    QString camera_capture_path;
    QString link_pose_path;
    QString effect_capture_path;
    QString camera_writer_path;
    QString camera_capture_label;
    char camera_label[96] = "standing still";
    bool startup_trace = qEnvironmentVariableIsSet("MM3D_TRACE_INTRO");
    explicit Impl(const Vulkan::Instance& instance_) : instance(instance_) {}
    ~Impl() {
        Notebook::detail_preview.store(false);
        Notebook::transparent_copy.store(false);
        NotebookInspector::enabled.store(false);
        if (context) {
            ImGui::SetCurrentContext(context);
            if (backend_ready) {
                for (auto set : thumbnail_sets) ImGui_ImplVulkan_RemoveTexture(set);
                if(atlas_detail_set) ImGui_ImplVulkan_RemoveTexture(atlas_detail_set);
                ImGui_ImplVulkan_Shutdown();
            }
            ImGui::DestroyContext(context);
        }
        instance.GetDevice().destroySampler(thumbnail_sampler);
        Textures::Clear(); thumbnails.clear();
        instance.GetDevice().destroyRenderPass(renderpass);
    }
};
Overlay::Overlay(const Vulkan::Instance& instance, vk::Format format, unsigned image_count,
                 std::mutex* submit_mutex)
    : impl(std::make_unique<Impl>(instance)) {
    // Exactly one primary window owns this context/backend. Secondary windows never instantiate it.
    auto& state = *impl;
    state.submit_mutex = submit_mutex;
    const vk::AttachmentDescription attachment{{}, format, vk::SampleCountFlagBits::e1,
        vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore,
        vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eTransferSrcOptimal};
    const vk::AttachmentReference reference{0, vk::ImageLayout::eGeneral};
    const vk::SubpassDescription subpass{{}, vk::PipelineBindPoint::eGraphics, 0, nullptr,
        1, &reference};
    const std::array dependencies{
        vk::SubpassDependency{VK_SUBPASS_EXTERNAL, 0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::AccessFlagBits::eColorAttachmentWrite,
            vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite},
        vk::SubpassDependency{0, VK_SUBPASS_EXTERNAL,
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
            vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead}};
    state.renderpass = instance.GetDevice().createRenderPass({.attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = static_cast<unsigned>(dependencies.size()), .pDependencies = dependencies.data()});
    state.context = ImGui::CreateContext();
    ImGui::SetCurrentContext(state.context);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().LogFilename = nullptr;
    ImGui::StyleColorsDark();
    state.base_style=ImGui::GetStyle();
    const auto loader = [](const char* name, void* data) -> PFN_vkVoidFunction {
        return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
            static_cast<const Vulkan::Instance*>(data)->GetInstance(), name);
    };
    if (!ImGui_ImplVulkan_LoadFunctions(Vulkan::TargetVulkanApiVersion, loader,
                                       const_cast<Vulkan::Instance*>(&instance))) {
        throw std::runtime_error("MM3D ImGui Vulkan function loading failed");
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = Vulkan::TargetVulkanApiVersion;
    info.Instance = instance.GetInstance();
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.RenderPass = state.renderpass;
    info.MinImageCount = 2;
    info.ImageCount = std::max(2u, image_count);
    info.DescriptorPoolSize = Textures::Limit + 8;
    info.CheckVkResultFn = CheckResult;
    if (!ImGui_ImplVulkan_Init(&info)) throw std::runtime_error("MM3D ImGui initialization failed");
    state.backend_ready = true;
    state.thumbnail_sampler = instance.GetDevice().createSampler({
        .magFilter = vk::Filter::eNearest, .minFilter = vk::Filter::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge});
    ImGui_ImplVulkan_CreateFontsTexture();
#ifdef ANDROID
    // FileUtil's Android paths are virtual; QFile/QDir need the physical path.
    state.directory = QString::fromStdString(AndroidUtils::TranslateFilePath(
        FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "mm3d-local/"));
#else
    state.directory = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir)) + "mm3d-local/";
#endif
    if(state.settings.Load(state.directory + "presentation.json")) {
#if !defined(ENABLE_MM3D_GAME_FOLDER) && !defined(__ANDROID__)
        const auto backup=SettingsBackupPath(state.directory,"startup");
        if(backup.isEmpty()||!QFile::copy(state.directory+"presentation.json",backup))
            state.status="Automatic settings backup FAILED";
#endif
    }
    HudAdjustments::minimap_visibility_request.store(-1);
    PhotoCapture::enabled.store(state.settings.hd_pictograph_photos);
    PhotoCapture::layout.store(0);
    ApplyPresentationSettings(state.settings);
    const auto inventory_restore=InventoryMenu::RestoreLegacy();
    if(!inventory_restore.isEmpty()) state.status=inventory_restore;
    Notebook::Load(state.notebook, (state.directory + "notebook.json").toStdString());
#ifdef ENABLE_MM3D_GAME_FOLDER
    state.notebook.output_archive.clear();
#endif
    if(!state.settings.banners_migrated){
        const auto restored=Notebook::RemoveTelopOverride();
        if(restored.ok){
            const auto& old=state.notebook;auto& p=state.settings.banners.parts[0];
            p.height=old.telop_scale;p.y=old.telop_y;p.text_scale=old.telop_text_scale;
            p.text_x=old.telop_x*(1+old.telop_text_scale-old.telop_scale);
            p.text_y=old.telop_text_y+66*(old.telop_text_scale-old.telop_scale);
            p.brightness=old.telop_brightness;state.settings.banners.parts[1].brightness=p.brightness;
            state.settings.banners_migrated=true;Banners::Apply(state.settings.banners);
            if(!state.settings.Save(state.directory+"presentation.json"))state.status="Banner migration save FAILED";
        }else state.status=QString::fromStdString(restored.message);
    }
    if(state.notebook.fade_source.empty()) state.notebook.fade_source=Notebook::DefaultFadeArchive();
    std::snprintf(state.fade_source,sizeof(state.fade_source),"%s",state.notebook.fade_source.c_str());
    if (state.notebook.source_archive.empty())
        state.notebook.source_archive = Notebook::DefaultSourceArchive();
    std::snprintf(state.notebook_source, sizeof(state.notebook_source), "%s",
                  state.notebook.source_archive.c_str());
    std::snprintf(state.notebook_output, sizeof(state.notebook_output), "%s",
                  state.notebook.output_archive.c_str());
    Profiles::Load(state.directory.toStdString());
    host_present_attempts.store(0);
    overlay_visible.store(false);
    presentation_available.store(true);
    if (state.startup_trace) Trace::Start();
}
Overlay::~Overlay() {
    Fog::disabled.store(false);
    WidescreenProbe::Stop();
    Widescreen::enabled.store(false);
    Widescreen::supported_layout.store(false);
    draw_probe.selection.store(-1);
    Trace::Stop();
    CameraCapture::Stop();
    CameraWriter::Stop();
    CameraLive::enabled.store(false);
    CameraMidpoint::enabled.store(false);
    CameraMidpoint::present_60.store(false);
    LinkPoseCapture::active=false;
    LinkPoseCapture::interpolate=false;LinkPoseCapture::interpolate_actors=false;++LinkPoseCapture::interpolation_generation;
    CameraLive::Stop();
    presentation_available.store(false);
    overlay_visible.store(false);
    impl.reset();
    std::scoped_lock lock{input_mutex};
    input.clear();
    text_input.clear(); key_input.clear(); text_input_active.store(false);
}
void Overlay::Draw(vk::CommandBuffer command, vk::Framebuffer framebuffer,
                   unsigned width, unsigned height) {
    auto& state = *impl;
    if(const int hidden=HudAdjustments::minimap_visibility_request.exchange(-1);hidden>=0) {
        state.settings.player_hud.hide_minimap=hidden!=0;
        state.status=state.settings.Save(state.directory+"presentation.json")?"Minimap visibility saved":"Minimap visibility save FAILED";
    }
    ImGui::SetCurrentContext(state.context);
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    const double now = Now();
    io.DeltaTime = static_cast<float>(std::clamp(now - state.previous, .00001, .25));
    state.previous = now;
    io.FontGlobalScale = state.settings.scale;
    // Always scale from the original style so repeated adjustments do not drift.
    ImGui::GetStyle()=state.base_style;
    ImGui::GetStyle().ScaleAllSizes(state.settings.scale);
    {
        std::scoped_lock lock{input_mutex};
        for (const auto& event : input) {
            io.AddMousePosEvent(event.x, event.y);
            if (event.button >= 0) io.AddMouseButtonEvent(event.button, event.down);
        }
        input.clear();
        editor_size={}; // Republished only if the editor is drawn this frame.
        io.AddMouseWheelEvent(wheel_x, wheel_y);
        wheel_x = wheel_y = 0;
        for (const auto& [key, down] : key_input) io.AddKeyEvent(ImGuiKey(key), down);
        key_input.clear();
        for (const auto& text : text_input) io.AddInputCharactersUTF8(text.c_str());
        text_input.clear();
    }
    if (!overlay_visible.load()) {
        io.AddMouseButtonEvent(0, false);
        io.ClearInputKeys();
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
#ifndef __ANDROID__
    if(PlayerSettings::startup_hint_requested.exchange(false) && !state.startup_hint_shown) {
        state.startup_hint_shown=true;
        if(state.settings.show_startup_settings_hint)state.startup_hint_start=now;
    }
    if(!state.settings.show_startup_settings_hint)state.startup_hint_start=-1;
    if(state.startup_hint_start>=0) {
        // Hold for two seconds, then fade smoothly over two more seconds.
        const float fade=std::clamp(float((now-state.startup_hint_start-2.0)/2.0),0.f,1.f);
        const float alpha=1.f-fade*fade*(3.f-2.f*fade);
        if(alpha>0) {
            constexpr const char* label="Press F1 to access settings";
            const float scale=std::clamp(float(height)/720.f,.8f,3.f);
            const float font_size=18.f*scale,margin=16.f*scale,padding=8.f*scale;
            const auto size=ImGui::GetFont()->CalcTextSizeA(font_size,FLT_MAX,0,label);
            const ImVec2 pos{margin+padding,float(height)-margin-padding-size.y};
            auto* draw=ImGui::GetForegroundDrawList();
            draw->AddRectFilled({pos.x-padding,pos.y-padding},
                {pos.x+size.x+padding,pos.y+size.y+padding},IM_COL32(0,0,0,int(145*alpha)),5.f*scale);
            draw->AddText(ImGui::GetFont(),font_size,pos,IM_COL32(255,255,255,int(255*alpha)),label);
        } else state.startup_hint_start=-1;
    }
#endif
    if(PlayerOverlay()&&state.settings.show_performance) {
        ImGui::SetNextWindowPos({8,8});
        ImGui::SetNextWindowBgAlpha(.6f);
        ImGui::Begin("Performance",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs);
        ImGui::Text("%.1f FPS",state.timing.guest_fps);ImGui::End();
    }
    #if !defined(ENABLE_MM3D_GAME_FOLDER) && !defined(__ANDROID__)
    if (state.settings.indicator&&!PlayerOverlay()) {
        const auto label = std::string("MM3D DEV | #") +
            std::to_string(state.timing.host_frames + 1) + " | F1";
        const auto text_size = ImGui::CalcTextSize(label.c_str());
        ImGui::SetNextWindowPos({
            std::min(width * state.settings.x, std::max(0.f, width - text_size.x - 20.f)),
            std::min(height * state.settings.y, std::max(0.f, height - text_size.y - 20.f))});
        ImGui::SetNextWindowBgAlpha(state.settings.opacity);
        ImGui::Begin("MM3D developer indicator", nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(label.c_str());
        ImGui::End();
    }
    #endif
    if (overlay_visible.load()) {
        const float ui_scale=state.settings.scale;
        const ImVec2 size{std::min(850.f*ui_scale,float(width)),std::min(680.f*ui_scale,float(height))};
        const auto sizing=state.resize_settings_window?ImGuiCond_Always:ImGuiCond_FirstUseEver;
        ImGui::SetNextWindowPos({std::min(20.f,std::max(0.f,float(width)-size.x)),
            std::min(80.f,std::max(0.f,float(height)-size.y))},sizing);
        ImGui::SetNextWindowSize(size,sizing);
        ImGui::SetNextWindowSizeConstraints({std::min(300.f*ui_scale,float(width)),
            std::min(200.f*ui_scale,float(height))},{float(width),float(height)});
        state.resize_settings_window=false;
#if defined(__ANDROID__)
        ImGui::Begin("Dekui settings");
#elif defined(ENABLE_MM3D_GAME_FOLDER)
        ImGui::Begin("Dekui settings (F1)");
#else
        ImGui::Begin(PlayerOverlay()?"Dekui settings (F2)":"MM3D editor (F1)");
#endif
        {
            std::scoped_lock lock{input_mutex};
            panel_position = ImGui::GetWindowPos();
            panel_size = ImGui::GetWindowSize();
        }
        [[maybe_unused]] const auto settings_size_control=[&]() {
            if(!state.editing_settings_size)
                state.settings_size_step=std::clamp(int(std::lround((state.settings.scale-1.f)*10.f)),-5,10);
            ImGui::SliderInt("Settings size",&state.settings_size_step,-5,10,"%+d",ImGuiSliderFlags_AlwaysClamp);
            // Keep the slider's geometry fixed while dragging. Rescaling it
            // under the pointer feeds a new value back into the next frame.
            state.editing_settings_size=ImGui::IsItemActive();
            bool apply=ImGui::IsItemDeactivatedAfterEdit();
            if(ImGui::IsItemHovered())ImGui::SetTooltip("0 is the default size. Each step changes the size by one tenth. Release to apply.");
            ImGui::SameLine();
            if(ImGui::Button("Reset size")){state.settings_size_step=0;apply=true;}
            if(apply) {
                state.settings.scale=1.f+state.settings_size_step*.1f;
                state.resize_settings_window=true;
                state.status=state.settings.Save(state.directory+"presentation.json")?
                    "Settings size saved":"Settings size save FAILED";
            }
        };
        const auto hover_help=[](const char* text) {
            if(ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize()*32.f);
                ImGui::TextUnformatted(text);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        };
        const auto help_text=[&](const char* text) {
            if(!PlayerOverlay()){ImGui::TextWrapped("%s",text);return;}
            hover_help(text);
        };
        const auto startup_hint_control=[&]() {
#ifndef __ANDROID__
            if(ImGui::Checkbox("Show F1 settings hint",&state.settings.show_startup_settings_hint))
                state.status=state.settings.Save(state.directory+"presentation.json")?"Settings hint saved":"Settings hint save FAILED";
#endif
        };
        const auto audio_accuracy_controls=[&]() {
            int audio_mode=state.settings.hle_audio_accuracy==2?1:0;
            if(ImGui::Combo("Audio mode",&audio_mode,"Balanced\0Accurate HLE\0")) {
                state.settings.hle_audio_accuracy=audio_mode+1;
                AudioCore::HLE::accuracy_mode.store(static_cast<AudioCore::HLE::AccuracyMode>(state.settings.hle_audio_accuracy));
                state.status=state.settings.Save(state.directory+"presentation.json")?"Audio settings saved":"Audio settings save FAILED";
            }
            hover_help("Balanced keeps the usual audio processing, with accurate decoding and resampling for rupee pickup, counter and falling tones. Effects reusing those samples also receive the correction. Accurate HLE applies accurate decoding and resampling to all audio. Changes apply as each sound starts its next buffer.");
        };
        const auto mute_music_control=[&]() {
            if(ImGui::Checkbox("Mute music",&state.settings.mute_music)) {
                Music::muted.store(state.settings.mute_music);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Audio settings saved":"Audio settings save FAILED";
            }
            help_text("Mutes background and battle music. Keeps sound effects, short jingles and playable songs. Applies immediately.");
        };
        const auto interpolation_control=[&]() {
            ImGui::TextUnformatted("Frame rate");
            int method = !state.settings.interpolation_60fps ? 0 :
                state.settings.interpolation_cpu_vertices ? 1 : 2;
            bool changed = ImGui::RadioButton("Original", &method, 0);
            hover_help("The original game's 30fps");
            changed |= ImGui::RadioButton("60FPS CPU interpolation", &method, 1);
            hover_help("Recommended interpolation option to avoid stutter");
            changed |= ImGui::RadioButton("60FPS GPU interpolation", &method, 2);
            hover_help("Better performance but worse stuttering, not recommended.");
            if (changed) {
                state.settings.interpolation_60fps = method != 0;
                if (method != 0) state.settings.interpolation_cpu_vertices = method == 1;
                ApplyFrameInterpolation(state.settings.interpolation_60fps, state.settings.interpolation_cpu_vertices);
                state.status = state.settings.Save(state.directory + "presentation.json")
                    ? "Frame rate saved" : "Frame rate save FAILED";
            }
        };
        const auto fog_control = [&]() {
            if (ImGui::Checkbox("Disable fog", &state.settings.disable_fog)) {
                Fog::disabled.store(state.settings.disable_fog);
                state.status = state.settings.Save(state.directory + "presentation.json") ?
                    "Graphics settings saved" : "Graphics settings save FAILED";
                if (inspection_paused.load()) step_requested.store(true);
            }
            hover_help("Removes distance fog. As fog is used for atmospheric effect, it is not recommended to disable it.");
        };
        const auto photo_control = [&]() {
            if(ImGui::Checkbox("HD Pictograph photos (restart required)",&state.settings.hd_pictograph_photos)) {
                state.status=state.settings.Save(state.directory+"presentation.json")?
                    "Photo setting saved. Restart the game to apply.":"Photo setting save FAILED";
            }
        };
#if !defined(ENABLE_MM3D_GAME_FOLDER) && !defined(__ANDROID__)
        const auto depth_of_field_control = [&]() {
            auto& o=state.settings.depth_of_field;
            bool changed=ImGui::Checkbox("Depth of field",&o.enabled);
            hover_help("Softens distant scenery while keeping nearby objects and the HUD sharp. Requires Vulkan. May reduce performance, especially at high resolutions.");
            if(o.enabled) {
                changed|=ImGui::SliderFloat("Focus distance",&o.focus,50.f,4000.f,"%.0f");
                hover_help("Objects closer than this distance stay sharp. Larger values keep more of the scene in focus.");
                changed|=ImGui::SliderFloat("Focus transition",&o.transition,100.f,6000.f,"%.0f");
                hover_help("Distance over which the background gradually reaches full blur.");
                changed|=ImGui::SliderFloat("Blur strength",&o.strength,0.f,4.f,"%.2f");
                if(ImGui::Button("Reset depth of field")){o=DepthOfField::Options{};o.enabled=true;changed=true;}
            }
            if(changed) {
                DepthOfField::Apply(o);
                state.status=state.settings.Save(state.directory+"presentation.json")?
                    "Graphics settings saved":"Graphics settings save FAILED";
                if(inspection_paused.load())step_requested.store(true);
            }
        };
        const auto ambient_occlusion_control = [&]() {
            auto& o=state.settings.ambient_occlusion;
            bool changed=ImGui::Checkbox("Ambient occlusion",&o.enabled);
            hover_help("Adds soft contact shading where visible surfaces meet. Requires Vulkan. May reduce performance; screen-space shading can change near screen edges.");
            if(o.enabled) {
                changed|=ImGui::SliderFloat("Occlusion strength",&o.strength,0.f,1.5f,"%.2f");
                changed|=ImGui::SliderFloat("Occlusion radius",&o.radius,5.f,150.f,"%.0f");
                hover_help("Size of the area around a surface that contributes contact shading.");
                if(ImGui::Button("Reset ambient occlusion")){o=AmbientOcclusion::Options{};o.enabled=true;changed=true;}
            }
            if(changed) {
                AmbientOcclusion::Apply(o);
                state.status=state.settings.Save(state.directory+"presentation.json")?
                    "Graphics settings saved":"Graphics settings save FAILED";
                if(inspection_paused.load())step_requested.store(true);
            }
        };
#endif
        const auto bloom_control = [&]() {
            if (ImGui::Checkbox("Fix bloom", &state.settings.fix_bloom)) {
                BloomSampling::enabled.store(state.settings.fix_bloom);
                BloomSampling::bindings.store(0);
                state.status = state.settings.Save(state.directory + "presentation.json") ?
                    "Graphics settings saved" : "Graphics settings save FAILED";
                if (inspection_paused.load()) step_requested.store(true);
            }
            help_text("Fixes inaccurate bloom at higher resolutions");
        };
        const auto apply_camera_zoom=[&]() {
            CameraLive::SetFov(state.settings.game_camera_fov);
            // Avoid stacking the old projection zoom with the native camera FOV.
            state.settings.camera_zoom_out=1.f;
            Widescreen::camera_zoom_out.store(1.f);
        };
        const auto camera_zoom_control=[&]() {
            bool changed=ImGui::SliderFloat("Camera zoom (FOV)",&state.settings.game_camera_fov,
                45.f,70.f,"%.1f deg",ImGuiSliderFlags_AlwaysClamp);
            hover_help("45 degrees is the original camera FOV. Higher values show a wider view.");
            ImGui::SameLine();
            if(ImGui::Button("Reset camera zoom")){state.settings.game_camera_fov=65.f;changed=true;}
            if(changed){apply_camera_zoom();
                state.status=state.settings.Save(state.directory+"presentation.json")?"Camera zoom saved":"Camera zoom save FAILED";}
            bool cutscene_changed=ImGui::SliderFloat("Cutscene camera (FOV)",&state.settings.cutscene_camera_fov,
                45.f,70.f,"%.1f deg",ImGuiSliderFlags_AlwaysClamp);
            hover_help("45 degrees is the original game's FOV. Default: 49 degrees.\n\n"
                "NOTE: Higher values than default may reveal missing geometry and NPC's on standby");
            ImGui::SameLine();
            if(ImGui::Button("Reset cutscene camera")){state.settings.cutscene_camera_fov=49.f;cutscene_changed=true;}
            if(cutscene_changed) {
                CameraLive::SetCutsceneFov(state.settings.cutscene_camera_fov);
                state.status=state.settings.Save(state.directory+"presentation.json")?
                    "Cutscene camera saved":"Cutscene camera save FAILED";
            }
        };
        const auto hud_controls=[&]() {
            auto& hud=state.settings.player_hud;
            bool changed=false;
            const auto defaults=HudAdjustments::UiDefaults(hud,PlayerOverlay());
            const auto int_control=[&](const char* label,int& value,int low,int high,int base) {
                int displayed=value-base;
                if(ImGui::SliderInt(label,&displayed,low-base,high-base,"%+d",ImGuiSliderFlags_AlwaysClamp)) {
                    value=displayed+base;changed=true;
                }
            };
            const auto scale_control=[&](const char* label,int& steps,int base=0) {
                ImGui::PushID(label);
                int_control(label,steps,-10,10,base);
                help_text("0 is the default size. Each step changes size by 5%. Overall and individual adjustments combine.");
                ImGui::SameLine();
                if(ImGui::Button("Reset")){steps=base;changed=true;}
                ImGui::PopID();
            };
            const auto percent_control=[&](const char* label,float& value,float fallback,const char* help) {
                ImGui::PushID(label);
                float percent=value*100.f;
                if(ImGui::SliderFloat(label,&percent,0,100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){value=percent/100.f;changed=true;}
                hover_help(help);
                ImGui::SameLine();
                if(ImGui::Button("Reset")){value=fallback;changed=true;}
                ImGui::PopID();
            };
            changed|=ImGui::Checkbox("Mirror item button backgrounds",&hud.mirror_item_buttons);
            hover_help("Mirrors the lower half of the X/Y/I/II backgrounds to hide the original B lettering. A and B stay unchanged.");
            if(PlayerOverlay())scale_control("Overall HUD scale",hud.overall);
            else {
                scale_control("Base HUD scale",hud.base_scale);
                ImGui::TextWrapped("Defines the size at F2 scale 0. F2 changes a separate relative adjustment.");
            }
            percent_control("Overall HUD opacity",hud.opacity,1.f,"Adjusts all gameplay HUD elements, including the minimap. Combines with minimap opacity and the game's own fades.");
            if(!PlayerOverlay()) {
                changed|=ImGui::Checkbox("Hide minimap",&hud.hide_minimap);
                help_text(state.settings.stick_click_controls?"Click the left stick during gameplay to hide or show the minimap.":"Hold ZR and press SELECT during gameplay to hide or show the minimap.");
            }
            if(ImGui::BeginTabBar("HUD options")) {
                if(ImGui::BeginTabItem("Elements")) {
                    if(ImGui::TreeNode("Location banner")) {
                        ImGui::PushID("location_banner");
                        changed|=ImGui::SliderInt("Y position",&hud.banner_y,-240,240,"%+d",ImGuiSliderFlags_AlwaysClamp);
                        hover_help("Moves the location banner and its text vertically. 0 is the default placement.");
                        percent_control("Darkness",hud.banner_darkness,0.f,"Darkens the location banner background. Text and the game's fades stay unchanged.");
                        if(ImGui::Button("Reset element")){hud.banner_y=0;hud.banner_darkness=0.f;changed=true;}
                        ImGui::PopID();
                        ImGui::TreePop();
                    }
                    for(unsigned i=0;i<HudAdjustments::Count;++i) {
                        ImGui::PushID(int(i));
                        if(ImGui::TreeNode(HudAdjustments::Labels[i])) {
                            scale_control("Scale",hud.elements[i],defaults.elements[i]);
                            if(i==HudAdjustments::Hearts) {
                                int_control("Heart spacing",hud.heart_spacing,
                                    -HudAdjustments::HeartSpacingLimit,HudAdjustments::HeartSpacingLimit,defaults.heart_spacing);
                                help_text("Left brings hearts closer; right spreads them out. Each step changes spacing by 0.5%; heart size stays the same.");
                                ImGui::SameLine();
                                if(ImGui::Button("Reset heart spacing")){hud.heart_spacing=defaults.heart_spacing;changed=true;}
                            }
                            if(i==HudAdjustments::Minimap)
                                percent_control("Opacity",state.settings.minimap_opacity,HudAdjustments::DefaultMinimapOpacity(hud),"Controls minimap transparency. Combines with overall HUD opacity.");
                            if(i==HudAdjustments::Magic) {
                                float width=hud.magic_width/defaults.magic_width;
                                if(ImGui::SliderFloat("Width",&width,.5f/defaults.magic_width,3.f/defaults.magic_width,"%.2fx",ImGuiSliderFlags_AlwaysClamp)) {
                                    hud.magic_width=width*defaults.magic_width;changed=true;
                                }
                                hover_help("Extends the fill, black background and border to the right while keeping the left edge fixed. 1.00x is the default width.");
                                ImGui::SameLine();
                                if(ImGui::Button("Reset width")){hud.magic_width=defaults.magic_width;changed=true;}
                                int_control("Height",hud.magic_height_steps,-10,10,defaults.magic_height_steps);
                                help_text("Changes height live while keeping the top edge fixed. Each step is 5% of the default height; width stays unchanged.");
                                ImGui::SameLine();
                                if(ImGui::Button("Reset height")){hud.magic_height_steps=defaults.magic_height_steps;changed=true;}
                            }
                            int_control("X position",hud.x[i],-400,400,defaults.x[i]);
                            int_control("Y position",hud.y[i],-240,240,defaults.y[i]);
                            if(ImGui::Button("Reset position")){hud.x[i]=defaults.x[i];hud.y[i]=defaults.y[i];changed=true;}
                            ImGui::SameLine();
                            if(ImGui::Button("Reset element")){hud.elements[i]=defaults.elements[i];hud.x[i]=defaults.x[i];hud.y[i]=defaults.y[i];
                                if(i==HudAdjustments::Buttons){hud.button_x={};hud.button_y={};}
                                if(i==HudAdjustments::Hearts)hud.heart_spacing=defaults.heart_spacing;
                                if(i==HudAdjustments::Magic){hud.magic_height_steps=defaults.magic_height_steps;hud.magic_width=defaults.magic_width;}
                                if(i==HudAdjustments::Minimap)state.settings.minimap_opacity=HudAdjustments::DefaultMinimapOpacity(hud);changed=true;}
                            if(i==HudAdjustments::Buttons) {
                                for(unsigned b=0;b<6;++b)if(ImGui::TreeNode(HudAdjustments::ButtonLabels[b])) {
                                    const auto slot=StickControls::ItemLayoutIndex(b,2);
                                    changed|=ImGui::SliderInt("X position",&hud.button_x[slot],-400,400,"%+d",ImGuiSliderFlags_AlwaysClamp);
                                    changed|=ImGui::SliderInt("Y position",&hud.button_y[slot],-240,240,"%+d",ImGuiSliderFlags_AlwaysClamp);
                                    if(ImGui::Button("Reset button")){hud.button_x[slot]=hud.button_y[slot]=0;changed=true;}
                                    ImGui::TreePop();
                                }
                                if(ImGui::Button("Reset all button positions")){hud.button_x={};hud.button_y={};changed=true;}
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTabItem();
                }
                if(ImGui::BeginTabItem("Padding")) {
                    changed|=ImGui::SliderInt("Top padding",&hud.top,0,40,"%d",ImGuiSliderFlags_AlwaysClamp);
                    help_text("Moves the upper HUD down.");
                    changed|=ImGui::SliderInt("Bottom padding",&hud.bottom,0,40,"%d",ImGuiSliderFlags_AlwaysClamp);
                    help_text("Moves the lower HUD up.");
                    if(ImGui::Button("Reset padding")){hud.top=0;hud.bottom=0;changed=true;}
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();
            if(ImGui::Button("Reset all HUD settings")){HudAdjustments::ResetPlayer(hud);state.settings.minimap_opacity=HudAdjustments::DefaultMinimapOpacity(hud);changed=true;}
            hover_help("Restores the latest default HUD layout, including size, spacing and positions. Map and Notebook layouts are unchanged.");
            if(changed) {
                HudAdjustments::Apply(hud);
                Minimap::opacity.store(state.settings.minimap_opacity);
                state.status=state.settings.Save(state.directory+"presentation.json")?"HUD settings saved":"HUD settings save FAILED";
            }
        };
        const auto text_controls=[&]() {
            if(ImGui::Checkbox("Use stick-click controls",&state.settings.stick_click_controls)) {
                StickControls::Configure(state.settings.stick_click_controls);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Controls saved":"Controls save FAILED";
            }
            help_text("ZL: Item I. ZR: Item II. Right stick click: first-person / pictograph. Left stick click: hide / show minimap. Bind stick clicks in the controller configuration.");
            if(ImGui::Checkbox("Hide item menu button prompts",&state.settings.player_hud.hide_menu_prompts)) {
                HudAdjustments::hide_menu_prompts.store(state.settings.player_hud.hide_menu_prompts);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Controls saved":"Controls save FAILED";
            }
            hover_help("Hides the bottom button hints on Items, Masks and Gear. Tabs and Items / Map navigation remain visible.");
            if(ImGui::Checkbox("Single-screen game instructions",&state.settings.single_screen_text)) {
                SingleScreenText::enabled.store(state.settings.single_screen_text);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Text setting saved":"Text setting save FAILED";
            }
            help_text("Updates English, French and Spanish game instructions for the selected controls and a single screen. Applies when a message is next opened.");
            ImGui::SeparatorText("Fan translation");
#ifndef ANDROID
            if(ImGui::Button("Import translation folder...")) Translation::import_requested.store(true);
#endif
            static unsigned translation_revision=0;
            static bool translation_enabled=false, translation_installed=false;
            const auto revision=Translation::preferences_revision.load();
            const auto directory=state.directory.toStdString();
            if(translation_revision!=revision) {
                translation_enabled=Translation::Requested(directory);
                translation_installed=Translation::Installed(directory);
                translation_revision=revision;
            }
            ImGui::BeginDisabled(!translation_installed && !translation_enabled);
            if(ImGui::Checkbox("Use imported translation (restart required)",&translation_enabled)) {
                if(Translation::SetEnabled(directory,translation_enabled))
                    state.status="Translation preference saved. Restart the game to apply.";
                else {
                    translation_enabled=Translation::Requested(directory);
                    state.status="Translation could not be enabled. Import the folder again.";
                }
            }
            ImGui::EndDisabled();
            help_text("Imports USA-compatible translation packs in any language. Only messages changed by enabled DekUI features use English fallback: controls, classic save/time dialogue and multiplied shop prices. Other text stays translated. Importing or toggling takes effect when you restart the game.");
            ImGui::TextUnformatted(Translation::active.load() ? "Imported translation active this session" : "Standard resources active this session");
        };
        const auto general_controls=[&]() {
#ifdef ENABLE_MM3D_GAME_FOLDER
            static QString menu_reset_status;
            if(ImGui::Button("Reset menu layouts")) {
                const auto error=ResetMenuLayouts(state.directory+"presentation.json",
                    QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir)));
                if(!error.isEmpty()) menu_reset_status=error;
                else if(state.settings.Load(state.directory+"presentation.json")) {
                    ApplyPresentationSettings(state.settings);
                    menu_reset_status="Menu layouts reset. Previous settings backed up.";
                } else menu_reset_status="Could not reload reset settings. Restart Dekui.";
            }
            hover_help("Restores the Songs, file-select, item-selection, mask-selection and Gear photo layouts to this release's defaults. Keeps gameplay settings, controls and HUD preferences. Backs up your previous settings.");
            if(!menu_reset_status.isEmpty())ImGui::TextWrapped("%s",menu_reset_status.toUtf8().constData());
#endif
            startup_hint_control();
            if(ImGui::Checkbox("Show frame rate",&state.settings.show_performance))
                state.settings.Save(state.directory+"presentation.json");
            settings_size_control();
            constexpr int languages[]={1,2,5}; // Native English, French, Spanish.
            int selected=0;
            for(int i=0;i<3;++i)if(PlayerSettings::game_language.load()==languages[i])selected=i;
            if(ImGui::Combo("Game language",&selected,"English\0Français\0Español\0")) {
                PlayerSettings::game_language.store(languages[selected]);
                PlayerSettings::game_language_status.store(1);
                PlayerSettings::set_game_language.store(languages[selected]);
            }
            hover_help("Changes the game's text language. Restart Dekui to apply. Settings menus remain in English.");
            if(PlayerSettings::game_language_status.load()==3)
                ImGui::TextWrapped("Could not save the game language. Try selecting it again.");
        };
        const auto qol_controls=[&]() {
            if(ImGui::Checkbox("Disable low-heart beeping",&state.settings.disable_low_health_beep)) {
                Music::disable_low_health_beep.store(state.settings.disable_low_health_beep);
                state.status=state.settings.Save(state.directory+"presentation.json")?"QOL setting saved":"QOL save FAILED";
            }
            if(ImGui::Checkbox("Disable Tatl reminders",&state.settings.disable_tatl_reminders)) {
                TatlReminders::disabled.store(state.settings.disable_tatl_reminders);
                state.status=state.settings.Save(state.directory+"presentation.json")?"QOL setting saved":"QOL save FAILED";
            }
            hover_help("Disables regular Tatl reminders on the D-pad. Alarms you set in the notebook still appear.");
        };
        const auto difficulty_controls=[&]() {
            int multiplier=state.settings.damage_multiplier;
            bool changed=ImGui::SliderInt("Damage taken",&multiplier,1,16,"%dx",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderInt("Red potion healing",&state.settings.red_potion_hearts,1,20,"%d hearts",ImGuiSliderFlags_AlwaysClamp);
            help_text("Hearts restored, up to your maximum health.");
            changed|=ImGui::SliderInt("Blue potion healing",&state.settings.blue_potion_hearts,1,20,"%d hearts",ImGuiSliderFlags_AlwaysClamp);
            help_text("Hearts restored, up to your maximum health. Blue potions still refill magic.");
            changed|=ImGui::SliderInt("Bottled fairy healing",&state.settings.bottled_fairy_hearts,1,20,"%d hearts",ImGuiSliderFlags_AlwaysClamp);
            help_text("Hearts restored, up to your maximum health, when a bottled fairy is released or revives you.");
            changed|=ImGui::Checkbox("Weaker enhanced defense",&state.settings.weaker_enhanced_defense);
            hover_help("Once earned, enhanced defense reduces incoming damage by one-third instead of one-half (about 1.5x effective health). Hits round consistently to the nearest health unit. Combines with Damage taken.");
            changed|=ImGui::Checkbox("Disable heart drops",&state.settings.disable_heart_drops);
            changed|=ImGui::Checkbox("Disable rupee drops",&state.settings.disable_rupee_drops);
            changed|=ImGui::Checkbox("Disable fairy drops",&state.settings.disable_fairy_drops);
            changed|=ImGui::Checkbox("Disable magic drops",&state.settings.disable_magic_drops);
            changed|=ImGui::Checkbox("Disable ammo pickups",&state.settings.disable_ammo_pickups);
            help_text("Removes loose arrows, bombs, Deku sticks and Deku nuts.");
            changed|=ImGui::Checkbox("Lose rupees on death",&state.settings.lose_rupees_on_death);
            help_text("Lose all carried rupees when you die. Bank savings are unaffected.");
            changed|=ImGui::Checkbox("Dying moves time forward 6 hours",&state.settings.advance_six_hours_on_death);
            help_text("Advances time by six hours when you continue after dying. Reaching or passing the end of the final day ends the world. Fairy revivals, pit falls and deaths on the Moon are unaffected. World ends on death takes priority if both are enabled.");
            changed|=ImGui::Checkbox("Fast time",&state.settings.fast_time);
            help_text("Uses the first-cycle clock speed (50% faster than normal), including faster time with Inverted Song of Time. Song of Double Time is unchanged.");
            changed|=ImGui::Checkbox("World ends on death",&state.settings.world_ends_on_death);
            changed|=ImGui::Checkbox("Increase purchase prices and fees",&state.settings.double_prices);
            ImGui::BeginDisabled(!state.settings.double_prices);
            changed|=ImGui::SliderInt("Price multiplier",&state.settings.price_multiplier,2,3,"%dx",ImGuiSliderFlags_AlwaysClamp);
            help_text("Multiplies purchase prices and fees. All-Night Mask, Biggest Bomb Bag upgrade and Chateau Romani cost at most 500 rupees. Restart Dekui after changing these settings.");
            ImGui::EndDisabled();
            if(ImGui::Button("Reset difficulty")){multiplier=1;state.settings.weaker_enhanced_defense=false;state.settings.red_potion_hearts=20;state.settings.blue_potion_hearts=20;state.settings.bottled_fairy_hearts=10;state.settings.disable_heart_drops=false;state.settings.disable_rupee_drops=false;state.settings.disable_fairy_drops=false;state.settings.disable_magic_drops=false;state.settings.disable_ammo_pickups=false;state.settings.double_prices=false;state.settings.price_multiplier=2;state.settings.lose_rupees_on_death=false;state.settings.world_ends_on_death=false;state.settings.advance_six_hours_on_death=false;state.settings.fast_time=false;changed=true;}
            if(changed){state.settings.damage_multiplier=multiplier;Difficulty::damage_multiplier.store(multiplier);
                Difficulty::weaker_enhanced_defense.store(state.settings.weaker_enhanced_defense);
                BottleHealing::red_hearts.store(state.settings.red_potion_hearts);
                BottleHealing::blue_hearts.store(state.settings.blue_potion_hearts);
                BottleHealing::fairy_hearts.store(state.settings.bottled_fairy_hearts);
                Difficulty::disable_heart_drops.store(state.settings.disable_heart_drops);
                Difficulty::disable_rupee_drops.store(state.settings.disable_rupee_drops);
                Difficulty::disable_fairy_drops.store(state.settings.disable_fairy_drops);
                Difficulty::disable_magic_drops.store(state.settings.disable_magic_drops);
                Difficulty::disable_ammo_pickups.store(state.settings.disable_ammo_pickups);
                DeathPenalties::lose_rupees.store(state.settings.lose_rupees_on_death);
                DeathPenalties::world_ends.store(state.settings.world_ends_on_death);
                DeathPenalties::advance_six_hours.store(state.settings.advance_six_hours_on_death);
                FastTime::enabled.store(state.settings.fast_time);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Difficulty saved":"Difficulty save FAILED";}
        };
        const auto cheats_controls=[&] {
#ifndef __ANDROID__
            ImGui::TextWrapped("NOTE: Some cheats may not work with DekUI");
            if(ImGui::Button("Open Azahar cheats"))PlayerSettings::open_cheats.store(true);
            ImGui::Separator();
#endif
            using namespace SimpleCheats;
            const bool available=ready.load();
            ImGui::TextWrapped("Buttons apply once. Infinite cheats last for this session. Changes to your inventory can be saved with the game.");
            if(!available)ImGui::TextWrapped("Load a save in the supported USA Restoration build to use cheats.");
            ImGui::BeginDisabled(!available);
            for(unsigned i=0;i<Count;++i) {
                if(i==0)ImGui::SeparatorText("Health, magic and rupees");
                if(i==Masks)ImGui::SeparatorText("Inventory and gear");
                if(i==DungeonKeys)ImGui::SeparatorText("Dungeon inventory");
                const bool current_dungeon=i==RemoveDungeonMap||i==RemoveDungeonCompass;
                ImGui::BeginDisabled(current_dungeon&&!dungeon_ready.load());
                if(ImGui::Button(Labels[i]))Queue(Action(i));
                ImGui::EndDisabled();
                hover_help(Help[i]);
            }
            ImGui::EndDisabled();
            ImGui::SeparatorText("Movement");
            bool moon=moon_jump.load();
            ImGui::BeginDisabled(!available&&!moon);
            if(ImGui::Checkbox("Moon jump (hold L + A)",&moon))moon_jump=moon;
            hover_help("Hold L + A to rise; release to fall normally. Session only. Inactive during pause, locked actions and cutscenes.");
            ImGui::EndDisabled();
            ImGui::SeparatorText("Infinite resources");
            for(unsigned i=0;i<9;++i) {
                const U32 bit=1u<<i;
                bool on=(continuous.load()&bit)!=0;
                // Enabled cheats can always be turned off, even on the title screen.
                ImGui::BeginDisabled(!available&&!on);
                const char* label=i<7?Ammunition[i].label:i==7?"Infinite health":"Infinite magic";
                if(ImGui::Checkbox(label,&on)) {
                    if(on)continuous.fetch_or(bit);else continuous.fetch_and(~bit);
                }
                hover_help(i<7?"Keeps this ammo stocked while you own the item. Turning it off stops refills.":
                    "Refills to your current capacity. Turning it off stops refills.");
                ImGui::EndDisabled();
            }
            if(ImGui::Button("Turn off all infinite cheats"))continuous=0;
            if(pending.load())ImGui::TextUnformatted("Queued — resume the game to apply.");
            else if(result.load()==1)ImGui::TextUnformatted("Cheat applied.");
            else if(result.load()==-1)ImGui::TextUnformatted("Not applied: load a supported game save first.");
            else if(result.load()==-2)ImGui::TextUnformatted("Not applied: enter a temple first.");
        };
        const auto restoration_controls=[&] {
            if(ImGui::Checkbox("Bombers don't approach automatically",&state.settings.disable_bomber_approach)) {
                BomberApproach::disabled.store(state.settings.disable_bomber_approach);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Restoration setting saved":"Restoration save FAILED";
            }
            hover_help("Stops Bombers running up to offer rumors. You can still talk to them yourself.");
            if(ImGui::Checkbox("Activate owl statues with a sword or stick",&state.settings.sword_activate_owls)) {
                OwlSword::enabled.store(state.settings.sword_activate_owls);
                OwlIntro::disabled.store(state.settings.sword_activate_owls);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Restoration setting saved":"Restoration save FAILED";
            }
            hover_help("Stops automatic activation when you approach and skips Tatl's statue hint. Strike an inactive owl statue with a sword or Deku Stick to activate it, then press A to talk. Striking it does not open dialogue.");
            if(ImGui::Checkbox("Classic Song of Double Time",&state.settings.classic_double_time)) {
                ClassicDoubleTime::enabled=state.settings.classic_double_time;
                state.settings.Save(state.directory+"presentation.json");
            }
            hover_help("Replace the time selector with a Yes/No question to advance to the next dawn (6 AM) or night (6 PM). On the final night, time cannot advance further.");
            if(ImGui::Checkbox("Classic saving",&state.settings.classic_saving)) {
                ClassicSaving::enabled=state.settings.classic_saving;
                state.settings.Save(state.directory+"presentation.json");
            }
            hover_help("Automatically saves when Song of Time starts a new cycle. Owl statues create a temporary save; resuming consumes it while your permanent cycle save remains. Saving at an owl statue only returns you to the title screen if a save from a Song of Time reset already exists. You cannot save at feather statues. Disable to use Restoration saving.");
            if(ImGui::Button("Reset Restoration")) {
                state.settings.classic_saving=false;
                state.settings.classic_double_time=false;
                state.settings.sword_activate_owls=false;
                state.settings.disable_bomber_approach=false;
                ClassicSaving::enabled=false;
                ClassicDoubleTime::enabled=false;
                OwlSword::enabled=false;
                OwlIntro::disabled=false;
                BomberApproach::disabled=false;
                state.status=state.settings.Save(state.directory+"presentation.json")?"Restoration reset":"Restoration reset save FAILED";
            }
            hover_help("Turns off every option in this tab.");
            const auto saving_status=ClassicSaving::Status();
            if(!saving_status.empty())ImGui::TextWrapped("%s",saving_status.c_str());
        };
        const auto actor_distance_control=[&] {
            if(ImGui::SliderFloat("Enemy / NPC draw distance",&state.settings.actor_draw_distance,1.f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp)) {
                ActorDrawDistance::multiplier.store(ActorDrawDistance::Clamp(state.settings.actor_draw_distance));
                state.settings.Save(state.directory+"presentation.json");
            }
            hover_help("Extends the native visibility distance for loaded enemies and NPCs. 1x is original; up to 3x. Keeps native fades and room rules. Does not load actors from other rooms or change attack ranges. May reduce performance; some actors have their own distance limits.");
            if(ImGui::SliderFloat("Vegetation / prop draw distance",&state.settings.prop_draw_distance,1.f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp)) {
                PropDrawDistance::multiplier.store(ActorDrawDistance::Clamp(state.settings.prop_draw_distance));
                state.settings.Save(state.directory+"presentation.json");
            }
            hover_help("Extends bushes, grouped vegetation, Deku flowers, wooden signs and liftable rocks, including their native fades. 1x is original; up to 3x. Does not load other rooms or extend every prop. May reduce performance; dense patches still have native model limits.");
        };
        if(PlayerOverlay()) {
        using namespace PlayerSettings;
        if(ImGui::BeginTabBar("Settings")) {
            if(ImGui::BeginTabItem("General")){general_controls();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Graphics")) {
                interpolation_control();
                camera_zoom_control();
                actor_distance_control();
                int factor=resolution.load();
                if(ImGui::SliderInt("Internal resolution",&factor,1,9,"%dx"))set_resolution.store(factor);
                bloom_control();
                fog_control();
                photo_control();
                bool sync=vsync.load()!=0;
                if(ImGui::Checkbox("VSync",&sync))set_vsync.store(sync);
                bool disk=cache.load()!=0;
                if(ImGui::Checkbox("Remember compiled shaders",&disk))set_cache.store(disk);
                help_text("Shader cache changes apply fully on the next launch.");
                bool hardware=hardware_shaders.load()!=0;
                ImGui::BeginDisabled(state.settings.interpolation_60fps);
                if(ImGui::Checkbox("Hardware shaders",&hardware))
                    set_hardware_shaders.store(hardware);
                ImGui::EndDisabled();
                help_text(state.settings.interpolation_60fps
                    ? "Controlled by the selected 60 FPS method. Choose GPU or CPU above."
                    : "Runs vertex shaders on the GPU. Leave off to remove stuttering at cost of higher performance demands.");
                bool async=async_shaders.load()!=0;
                if(ImGui::Checkbox("Async shader compilation (restart required)",&async))
                    set_async_shaders.store(async);
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("Restart Dekui for this change to take effect.\nCompiles shaders in the background.\nMay temporarily cause missing graphics while shaders compile.");
                bool custom=textures.load()!=0;
                if(ImGui::Checkbox("Use custom textures",&custom))set_textures.store(custom);
                if(ImGui::IsItemHovered())
                    ImGui::SetTooltip("Load a compatible texture pack from your texture folder.\nRestart Dekui after adding or replacing texture files.");
#ifndef __ANDROID__
                if(ImGui::Button("Open texture folder"))open_textures.store(true);
                help_text("Restart Dekui after installing or changing a texture pack.");
                if(ImGui::Button("Toggle fullscreen"))fullscreen.store(true);
#endif
                ImGui::Text("Game: %.1f FPS | Presentation: %.1f FPS",state.timing.guest_fps,state.timing.host_fps);
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("HUD")){hud_controls();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Difficulty")){difficulty_controls();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Restoration")){restoration_controls();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("QOL")){qol_controls();ImGui::EndTabItem();}
#ifndef __ANDROID__
            // Opt-in support tools for packaged builds. Never persist this flag in
            // presentation.json or expose the editor's game-modifying controls.
            static const bool diagnostics_enabled =
                qEnvironmentVariable("DEKUI_DIAGNOSTICS") == QStringLiteral("1");
            if(diagnostics_enabled && ImGui::BeginTabItem("Diagnostics")) {
                ImGui::TextWrapped("Build: %s", Common::g_scm_rev);
                const bool capturing = InterpolationPerformance::pending.load();
                ImGui::BeginDisabled(capturing);
                if(ImGui::Button("Capture performance (30 seconds)")) {
                    LOG_INFO(Render_Vulkan,
                        "Interpolation diagnostic configuration build={} mode={} resolution={}x "
                        "hardware_shaders={} async_shaders={} vsync={} bloom_fix={} "
                        "actor_distance={} prop_distance={}",
                        Common::g_scm_rev,
                        !state.settings.interpolation_60fps ? "original" :
                            state.settings.interpolation_cpu_vertices ? "cpu" : "gpu",
                        resolution.load(), hardware_shaders.load(), async_shaders.load(),
                        vsync.load(), state.settings.fix_bloom,
                        state.settings.actor_draw_distance, state.settings.prop_draw_distance);
                    InterpolationPerformance::Start(CameraMidpoint::enabled.load(),
                                                    CameraMidpoint::present_60.load());
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped(capturing ? "Capture pending or active." : "Ready to capture.");
                ImGui::TextWrapped("Starts after 3 seconds. Close F1 and reproduce the slowdown for 30 seconds. Keep graphics settings unchanged during each capture. Repeat in Original and CPU interpolation at the same resolution and scene.");
                ImGui::TextWrapped("Records CPU work, replay batching, waits and supported GPU timestamps. Timings include diagnostic overhead and do not measure display latency. No game files or screenshots are captured; nothing is uploaded.");
                const auto logs = FileUtil::GetUserPath(FileUtil::UserPath::LogDir);
                ImGui::TextWrapped("After capturing, quit the game and copy azahar_log.txt from:\n%s\nCopy it before launching again. Multiple captures in one launch share this log.", logs.c_str());
                ImGui::EndTabItem();
            }
#endif
            if(ImGui::BeginTabItem("Audio")) {
                audio_accuracy_controls();
                int level=volume.load();
                if(ImGui::SliderInt("Volume",&level,0,100,"%d%%"))set_volume.store(level);
                mute_music_control();
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Controls")) {
                text_controls();
                if(ImGui::Button("Configure controller / keyboard"))PlayerSettings::controls.store(true);
#ifndef __ANDROID__
                if(ImGui::Button("Fullscreen shortcut..."))PlayerSettings::fullscreen_shortcut.store(true);
                if(ImGui::Button("Custom textures shortcut..."))PlayerSettings::textures_shortcut.store(true);
#endif
                help_text(state.settings.stick_click_controls?"Click the left stick during gameplay to hide or show the minimap. Game touchscreen input is disabled.":"Hold ZR and press SELECT during gameplay to hide or show the minimap. Game touchscreen input is disabled.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("About")) {
                ImGui::TextWrapped("Dekui uses Azahar and Project Restoration. It is not affiliated with Nintendo.");
#ifndef __ANDROID__
                if(ImGui::Button("Licenses and credits"))about.store(true);
#endif
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if(ImGui::Button("Resume"))ToggleOverlay();
        ImGui::SameLine();
        if(ImGui::Button("Quit game"))ImGui::OpenPopup("Quit Dekui?");
        if(ImGui::BeginPopupModal("Quit Dekui?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save your progress in the game before quitting.");
            if(ImGui::Button("Quit")){quit.store(true);ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        }
#if !defined(ENABLE_MM3D_GAME_FOLDER) && !defined(__ANDROID__)
        else {
        if(ImGui::BeginTabBar("Editor settings")) {
        if(ImGui::BeginTabItem("General")){general_controls();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Graphics")){interpolation_control();actor_distance_control();fog_control();photo_control();depth_of_field_control();ambient_occlusion_control();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Camera")) {
        if(ImGui::CollapsingHeader("Live game camera")) {
            bool inspect=CameraLive::enabled.load();
            if(ImGui::Checkbox("Inspect game camera",&inspect)) {
                CameraLive::enabled.store(inspect);

            }
            const auto camera=CameraLive::GetSnapshot();
            ImGui::TextWrapped("%s",camera.status);
            if(camera.valid) {
                ImGui::Text("Camera: %08X | FOV: %.2f deg | auxiliary: %.2f deg",
                    camera.base,camera.fov,camera.auxiliary_fov);
                ImGui::Text("Projection FOV: %.2f deg | near/far: %.2f / %.2f",
                    camera.matrix_fov,camera.near_plane,camera.far_plane);
                ImGui::Text("Eye: %.2f, %.2f, %.2f",camera.eye[0],camera.eye[1],camera.eye[2]);
                ImGui::Text("Target: %.2f, %.2f, %.2f",camera.target[0],camera.target[1],camera.target[2]);
            }
            camera_zoom_control();
            ImGui::TextWrapped("45 degrees is the original view. Higher values zoom out. Changes apply immediately and are saved across restarts and builds. Scripted and title-screen cameras retain their animation with the same relative zoom out.");
        }
        if(ImGui::CollapsingHeader("Camera midpoint prototype")) {
            ImGui::BeginDisabled(!state.effect_capture_path.isEmpty());
            if(ImGui::Button("Capture song effect (20 seconds)")) {
                state.effect_capture_path=state.directory+"song-effect-"+QString::number(QDateTime::currentMSecsSinceEpoch())+".json";
                EffectCapture::Start();
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("Close F1, then play one song after the 3-second delay. Records shader values, texture references, draw state and geometry references. Repeat once per song.");
            ImGui::Text("Effect draws: %u%s",EffectCapture::count.load(),EffectCapture::active.load()?" (capture pending/active)":"");
            bool feedback=CameraMidpoint::suppress_transform_feedback.load();
            if(ImGui::Checkbox("Disable transformation feedback (diagnostic)",&feedback))
                CameraMidpoint::suppress_transform_feedback=feedback;
            ImGui::TextWrapped("Temporary test: removes the captured scene-feedback effect from both native and intermediate frames while 60 FPS is enabled. Defaults off; not saved.");
            bool enabled=CameraMidpoint::enabled.load();
            if(ImGui::Checkbox("Show camera midpoint preview",&enabled)) {
                CameraMidpoint::enabled=enabled;++CameraMidpoint::generation;
            }
            ImGui::BeginDisabled(LinkPoseCapture::active.load() || !state.link_pose_path.isEmpty());
            if(ImGui::Button("Capture actor skeletons (10 seconds)")) {
                state.link_pose_path=state.directory+"link-pose-"+QString::number(QDateTime::currentMSecsSinceEpoch())+".json";
                LinkPoseCapture::Start();
            }
            ImGui::EndDisabled();
            ImGui::Text("Link poses: %u | sampled draws: %u",LinkPoseCapture::pose_count.load(),LinkPoseCapture::draw_count.load());
            bool link=LinkPoseCapture::interpolate.load();
            if(ImGui::Checkbox("Experimental human Link interpolation",&link)) {
                LinkPoseCapture::interpolate=link;++LinkPoseCapture::interpolation_generation;
                LinkPoseCapture::replaced_draws=0;LinkPoseCapture::replaced_bones=0;
            }
            bool actors=LinkPoseCapture::interpolate_actors.load();
            if(ImGui::Checkbox("Experimental NPC/enemy skeleton interpolation",&actors)) {
                LinkPoseCapture::interpolate_actors=actors;++LinkPoseCapture::interpolation_generation;
                LinkPoseCapture::replaced_draws=0;LinkPoseCapture::replaced_bones=0;
            }
            ImGui::TextWrapped("Shared skeleton prototype: NPCs, enemies and other supported models. Ambiguous draws remain native. Not saved.");
            ImGui::Text("Skeleton interpolated draws: %u | bone slots: %u",LinkPoseCapture::replaced_draws.load(),LinkPoseCapture::replaced_bones.load());
            ImGui::TextWrapped("Requires midpoint preview or 60 Hz presentation. Human form only; matched body/skin transforms on intermediate frames. Unmatched equipment/effects stay native. Not saved.");
            ImGui::TextWrapped("Read-only capture: starts after 3 seconds. Close F1 and keep the actor being tested visible for 10 seconds. Records Link and supported NPC skeletons plus draw palettes; NPC capture requires NPC/enemy interpolation enabled.");
            bool present=CameraMidpoint::present_60.load();
            if(ImGui::Checkbox("Experimental 60 Hz camera presentation",&present)) {
                CameraMidpoint::present_60=present;++CameraMidpoint::generation;
                if(present) { CameraMidpoint::enabled=true;CameraMidpoint::native_preview=false; }
            }
            ImGui::TextWrapped("Requires VSync. Camera interpolation with extra buffering latency; human Link has a separate toggle. Unknown draws, transitions and native 60 Hz menus fall back to normal rendering. Not saved.");
            if(ImGui::Button("Capture rendered frame pair (3 second delay)"))
                CameraMidpoint::capture_pair_at_us=InputLatency::Now()+3000000;
            ImGui::TextWrapped("Close F1 and keep rotating the camera. Saves midpoint/native images to the log folder; may briefly hitch during readback.");
            if(ImGui::Button("Capture moving menu frame pair")) {
                CameraMidpoint::capture_menu_motion_frame=0;
                CameraMidpoint::capture_menu_pair_at_us=InputLatency::Now()+3000000;
            }
            ImGui::TextWrapped("Close F1, wait 3 seconds, then open Gear. Waits up to 20 seconds for horizontal menu motion and saves one midpoint/native image pair in the log folder. Readback may briefly hitch; use these images to inspect geometry, not frame pacing.");
            if(CameraMidpoint::capture_menu_pair_at_us.load())ImGui::TextUnformatted("Waiting for menu motion...");
            ImGui::Text("Presented frame pairs: %u",CameraMidpoint::presented_pairs.load());
            ImGui::TextWrapped("%s",CameraMidpoint::StatusText(CameraMidpoint::presentation_status.load()));
            bool native=CameraMidpoint::native_preview.load();
            if(ImGui::Checkbox("Use current camera for comparison",&native))CameraMidpoint::native_preview=native;
            ImGui::Text("Copied draws: %u | rejected candidates: %u",CameraMidpoint::copied_draws.load(),CameraMidpoint::rejected_draws.load());
            ImGui::Text("Using native camera: %u draws",CameraMidpoint::fallback_draws.load());
            ImGui::TextWrapped("Developer preview in the upper right. Requires Hardware shaders. Replays supported world geometry at the midpoint camera; actors remain at their current animation pose. The inset omits HUD and unsupported draws. The 60 Hz toggle replaces the main scene on intermediate frames and replays supported HUD separately. Gameplay timing stays native. Not saved across restarts.");
        }
        if(ImGui::CollapsingHeader("Camera investigation")) {
            ImGui::TextWrapped("Capture A while standing still, then B after moving only the camera or switching to first-person. Each capture waits 3 seconds so you can close F1, then records 10 guest frames. Keep the scene and Link's position the same. Reset camera zoom to 1.00x first.");
            ImGui::InputText("Camera state note", state.camera_label, sizeof(state.camera_label));
            ImGui::BeginDisabled(CameraCapture::active.load() || !state.camera_capture_path.isEmpty() ||
                CameraWriter::active.load() || !state.camera_writer_path.isEmpty());
            for(const char* slot : {"A", "B"}) {
                if(slot[0] == 'B') ImGui::SameLine();
                const std::string button = std::string("Capture ") + slot;
                if(ImGui::Button(button.c_str())) {
                    state.camera_capture_label = QString::fromUtf8(state.camera_label);
                    state.camera_capture_path = state.directory + "camera-" + slot + "-" +
                        QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";
                    CameraCapture::Start();
                }
            }
            if(ImGui::Button("Capture camera orbit (20 seconds)")) {
                state.camera_capture_label = QString::fromUtf8(state.camera_label);
                state.camera_capture_path = state.directory + "interpolation-" +
                    QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";
                CameraCapture::StartInterpolation();
            }
            ImGui::TextWrapped("Wait 3 seconds after clicking, then rotate the camera for up to 20 seconds. Records timestamped game-camera readings and rendering candidates. Hardware shaders must be on to compare rendering matrices. Does not enable 60 FPS or alter game speed.");
            if(ImGui::Button("Trace projection uploads")) {
                state.camera_capture_label = QString::fromUtf8(state.camera_label);
                state.camera_capture_path = state.directory + "camera-uploads-" +
                    QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";
                CameraCapture::Start(3, true);
            }
            if(ImGui::Button("Trace CPU camera writers")) {
                state.camera_writer_path = state.directory + "camera-writers-" +
                    QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";
                CameraWriter::Start();
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("CPU writer trace: close F1 within 3 seconds. Gameplay will slow for up to 10 seconds, then normal execution resumes. Finds writes matching the captured 45-degree projection, not a verified camera setter.");
            if(CameraWriter::active.load() && ImGui::Button("Stop CPU writer trace")) CameraWriter::Stop();
            ImGui::TextWrapped("%s", CameraWriter::Status().toUtf8().constData());
            ImGui::TextWrapped("Projection upload trace records command-buffer addresses for slots 0–3. It may briefly slow rendering. These addresses are not the CPU writer or camera object.");
            if(CameraCapture::active.load() && ImGui::Button("Stop capture")) CameraCapture::Stop();
            ImGui::TextWrapped("%s", CameraCapture::Status().toUtf8().constData());
            ImGui::TextWrapped("Exports automatically to %s. Captures contain shader constants, not verified camera addresses.", state.directory.toUtf8().constData());
        }
        ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("HUD")){hud_controls();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Difficulty")){difficulty_controls();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Controls")) {
            text_controls();
            if(ImGui::Button("Configure controller / keyboard"))PlayerSettings::controls.store(true);
#ifndef __ANDROID__
                if(ImGui::Button("Fullscreen shortcut..."))PlayerSettings::fullscreen_shortcut.store(true);
                if(ImGui::Button("Custom textures shortcut..."))PlayerSettings::textures_shortcut.store(true);
#endif
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Restoration")){restoration_controls();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("QOL")){qol_controls();ImGui::EndTabItem();}
            if(ImGui::BeginTabItem("Cheats")){cheats_controls();ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("Editor")) {
        ImGui::TextUnformatted("MM3D presentation editor");
        if(ImGui::CollapsingHeader("Settings backups")) {
            if(ImGui::Button("Back Up Settings")) {
                const auto backup=SettingsBackupPath(state.directory,"manual");
                state.status=!backup.isEmpty()&&state.settings.Save(backup)?
                    "Settings backed up to "+backup:"Settings backup FAILED";
            }
            static QString selected_backup;
            const auto folder=state.directory+"settings-backups/";
            const auto files=QDir(folder).entryList({"*.json"},QDir::Files,QDir::Name|QDir::Reversed);
            if(ImGui::BeginCombo("Backup",selected_backup.isEmpty()?"Choose a backup":selected_backup.toUtf8().constData())) {
                for(const auto& name:files)
                    if(ImGui::Selectable(name.toUtf8().constData(),name==selected_backup))selected_backup=name;
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(selected_backup.isEmpty());
            if(ImGui::Button("Restore Backup")) {
                Settings restored;
                const auto current=state.directory+"presentation.json";
                const auto safety=SettingsBackupPath(state.directory,"before-restore");
                if(!restored.Load(folder+selected_backup))state.status="Invalid backup; nothing restored";
                else if(safety.isEmpty()||(QFile::exists(current)&&!QFile::copy(current,safety)))
                    state.status="Could not preserve current settings; nothing restored";
                else if(!restored.Save(current))state.status="Restore FAILED";
                else {
                    state.settings=restored;
                    ApplyPresentationSettings(state.settings);
                    state.status="Settings restored. Reapply archive-based HUD/header changes from their sections if needed.";
                }
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("Dated backups are made after successful startup loads. Restore applies live settings and preserves the previous file first. Covers presentation settings; installed layout archives and separate notebook/profile files are not included.");
        }

        if(ImGui::Checkbox("16:9 camera / preserve HUD proportions (experimental)",&state.settings.widescreen)) {
            Widescreen::enabled.store(state.settings.widescreen);
            const bool saved=QDir().mkpath(state.directory) && state.settings.Save(state.directory+"presentation.json");
            state.status=saved?"Widescreen setting saved":"Widescreen changed; settings save FAILED";
        }
        if(state.settings.widescreen) {
            ImGui::Text("Top-screen draws corrected last frame: %u",Widescreen::corrected_draws.load());
            if(!Widescreen::supported_layout.load()) ImGui::TextWrapped("Requires a landscape top screen with stereoscopic 3D disabled.");
            ImGui::TextWrapped("Keeps HUD and text centered at their original proportions. Check scene edges, menus and transitions; some full-screen effects may need exceptions.");
        }
        bool zoom_changed = ImGui::SliderFloat("Camera zoom out (experimental)",
            &state.settings.camera_zoom_out, 1.f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SameLine();
        if(ImGui::Button("Reset camera zoom")) {
            state.settings.camera_zoom_out = 1.f;
            zoom_changed = true;
        }
        if(zoom_changed) {
            Widescreen::camera_zoom_out.store(state.settings.camera_zoom_out);
            const bool saved = QDir().mkpath(state.directory) &&
                state.settings.Save(state.directory + "presentation.json");
            state.status = saved ? "Camera zoom saved" : "Camera zoom changed; settings save FAILED";
        }
        if(state.settings.camera_zoom_out > 1.f) {
            ImGui::TextWrapped("Widens the world view; 1.00x restores the original FOV. Experimental depth-based filtering may miss effects or affect some UI. Objects may disappear near the edges.");
            if(!Widescreen::supported_layout.load())
                ImGui::TextWrapped("Requires a landscape top screen with stereoscopic 3D disabled.");
        }
        if(ImGui::CollapsingHeader("Minimap")) {
            bool changed=ImGui::Checkbox("Show minimap",&state.settings.minimap);
            changed |= ImGui::SliderFloat("Minimap size",&state.settings.minimap_scale,.2f,.6f,"%.2fx");
            changed |= ImGui::SliderFloat("Minimap opacity",&state.settings.minimap_opacity,0.f,1.f,"%.2f");
            changed |= ImGui::SliderFloat("Right margin",&state.settings.minimap_right_margin,-64.f,120.f,"%.0f",ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Bottom margin",&state.settings.minimap_bottom_margin,-64.f,120.f,"%.0f",ImGuiSliderFlags_AlwaysClamp);
            if(changed) {
                Minimap::enabled.store(state.settings.minimap);
                Minimap::scale.store(state.settings.minimap_scale);
                Minimap::opacity.store(state.settings.minimap_opacity);
                NativeMinimap::right_margin.store(state.settings.minimap_right_margin);
                NativeMinimap::bottom_margin.store(state.settings.minimap_bottom_margin);
                state.status=state.settings.Save(state.directory+"presentation.json") ? "Minimap saved" : "Minimap settings save FAILED";
            }
            ImGui::TextWrapped("Moves the gameplay map to the top screen. Size, opacity and margins update immediately. Smaller margins move the map closer to the edge.");
            ImGui::Text("Native map: %s | quads: %u",
                NativeMinimap::status.load()==2 ? "active" : NativeMinimap::status.load()==3 ? "guarded / no map" : "waiting",
                NativeMinimap::moved_quads.load());
        }
        if(ImGui::CollapsingHeader("Textboxes")) {
            bool changed=ImGui::SliderFloat("Dialogue textbox scale",&state.settings.dialogue_scale,.5f,1.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Textbox Y",&state.settings.dialogue_y,-120.f,120.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Textbox background width (shared)",&state.settings.dialogue_background_width,1.f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Advance icon X",&state.settings.dialogue_indicator_x,-200.f,200.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Advance icon Y",&state.settings.dialogue_indicator_y,-64.f,64.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Reset textbox Y")) { state.settings.dialogue_y=0.f; changed=true; }
            if(ImGui::Button("Reset textbox scale")) { state.settings.dialogue_scale=1.f; changed=true; }
            if(changed) {
                DialogueScale::scale.store(state.settings.dialogue_scale);
                DialogueScale::y_offset.store(state.settings.dialogue_y);
                DialogueScale::background_width.store(state.settings.dialogue_background_width);
                DialogueScale::indicator_x.store(state.settings.dialogue_indicator_x);
                DialogueScale::indicator_y.store(state.settings.dialogue_indicator_y);
                state.status=state.settings.Save(state.directory+"presentation.json") ? "Textbox settings saved" : "Textbox settings save FAILED";
            }
            ImGui::TextWrapped("Background width applies to dialogue and menu descriptions, including Gear, Items, Masks, Maps, and item-giving screens. Scale and Y here affect dialogue; text wrapping is unchanged.");
        }
        if(ImGui::CollapsingHeader("Title screen")) {
            bool changed=false;
            static bool fine_copyright=true,copyright_range_ready=false;
            static ImVec2 copyright_center{};
            const std::array<const char*,4> names{"Logo","Copyright","Press Start","Loading icon"};
            const std::array<GearMenu::Pose*,4> poses{&state.settings.startup.logo,&state.settings.startup.copyright,&state.settings.startup.press_start,&state.settings.startup.loading_icon};
            for(unsigned i=0;i<poses.size();++i) {
                if(ImGui::TreeNode(names[i])) {
                    auto& p=*poses[i];
                    if(i==1) {
                        if(!copyright_range_ready){copyright_center={p.x,p.y};copyright_range_ready=true;}
                        if(ImGui::Checkbox("Fine positioning",&fine_copyright))copyright_center={p.x,p.y};
                        if(fine_copyright) {
                            ImGui::SameLine();
                            if(ImGui::Button("Recenter range"))copyright_center={p.x,p.y};
                        }
                        const float xmin=fine_copyright?std::max(-400.f,copyright_center.x-40.f):-400.f;
                        const float xmax=fine_copyright?std::min(400.f,copyright_center.x+40.f):400.f;
                        const float ymin=fine_copyright?std::max(-240.f,copyright_center.y-24.f):-240.f;
                        const float ymax=fine_copyright?std::min(240.f,copyright_center.y+24.f):240.f;
                        changed|=ImGui::SliderFloat("X",&p.x,xmin,xmax,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::SliderFloat("Y",&p.y,ymin,ymax,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                        ImGui::TextDisabled("Fine positioning uses a 10x smaller range. Ctrl+click to type.");
                    } else {
                        changed|=ImGui::SliderFloat("X",&p.x,-400,400,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::SliderFloat("Y",&p.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    }
                    changed|=ImGui::SliderFloat("Scale",&p.scale,.1f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset")){p={0,0,1};if(i==1)copyright_range_ready=false;changed=true;}
                    ImGui::TreePop();
                }
            }
            if(changed) {
                NativeStartup::Apply(state.settings.startup);
                if(inspection_paused.load())step_requested.store(true);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Title screen settings saved":"Title screen settings save FAILED";
            }
            ImGui::Text("Press Start: %s",NativeStartup::press_status.load()==2?"routed to top":"waiting for title layout");
            constexpr std::array<const char*,3> states{"waiting for title model","title model detected","title model adjusted"};
            ImGui::Text("Native hook: %s",states[std::min(NativeStartup::status.load(),2u)]);
            ImGui::TextWrapped("Positive X moves right; positive Y moves down. Logo and copyright offsets use model units (10 = 1 unit). Press Start offsets use native screen pixels from top-screen center X=200, Y=190. Loading icon offsets use native screen pixels from its original position.");
        }
        if(ImGui::CollapsingHeader("Shared brown backgrounds")) {
            auto& o=state.settings.inventory_menu;
            auto& give=state.settings.item_present;
            ImGui::TextWrapped("Items, Masks and Gear share background X, Y and scale. Y, scale and shadow also apply to Give Item, Songs and Song of Time reset.");
            bool changed=ImGui::SliderFloat("Background Y",&o.background.y,-240.f,240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Background scale",&o.background.scale,.25f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Shadow width",&o.shadow_width,0.f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Shadow opacity",&o.shadow_opacity,0.f,1.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Items / Masks / Gear background X",&o.background.x,-400.f,400.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Give Item background X",&give.poses[NativeItemPresent::Background].x,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Reset shared appearance")){o.background.y=0;o.background.scale=1;o.shadow_width=o.shadow_opacity=1;changed=true;}
            if(changed) {
                InventoryMenu::Apply(o);NativeItemPresent::Apply(give);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Background settings saved":"Background settings save FAILED";
            }
        }
        for(bool masks:{false,true})if(ImGui::CollapsingHeader(masks?"Select Mask":"Give an item")) {
            ImGui::PushID(masks?"select_mask":"give_item");
            auto& o=masks?state.settings.mask_present:state.settings.item_present;
            bool changed=ImGui::Checkbox(masks?"Select Mask on top screen":"Give an item on top screen",&o.enabled);
            if(ImGui::TreeNode("Button prompts")) {
                changed|=ImGui::SliderFloat("Group X",&o.help_pose.x,-512,512,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Group Y",&o.help_pose.y,-512,512,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Group scale",&o.help_pose.scale,.1f,2,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Spacing",&o.help_spacing,0,100,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset prompts")){o.help_pose=NativeItemPresent::Options{}.help_pose;o.help_spacing=6;changed=true;}
                ImGui::TreePop();
            }
            const std::array<const char*,NativeItemPresent::Count> names{"Background",masks?"Mask panel and icons":"Item panel and icons","Right panel",masks?"Select Mask prompt":"Select Item prompt","Return button","Confirm button","Description box"};
            for(unsigned i=0;i<NativeItemPresent::Count;++i) {
                if(i==NativeItemPresent::Background||(masks&&i==NativeItemPresent::Right))continue;
                ImGui::PushID(int(i)+1500);
                if(ImGui::TreeNode(names[i])) {
                    auto& p=o.poses[i];
                    const bool description=i==NativeItemPresent::Description;

                    if(i==NativeItemPresent::Right)changed|=ImGui::Checkbox("Horizontal four-item panel",&o.horizontal);
                    changed|=ImGui::SliderFloat("X",&p.x,description?-320.f:-512.f,description?320.f:512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Y",&p.y,description?-240.f:-512.f,description?240.f:512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Scale",&p.scale,description?.25f:.1f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset")){p=NativeItemPresent::Options{}.poses[i];if(i==NativeItemPresent::Right)o.horizontal=false;changed=true;}
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if(changed){
                if(masks)NativeItemPresent::ApplyMask(o);else NativeItemPresent::Apply(o);
                const bool saved=state.settings.Save(state.directory+"presentation.json");
                state.status=saved?(masks?"Select Mask settings saved":"Give an item settings saved"):
                    (masks?"Select Mask settings save FAILED":"Give an item settings save FAILED");
            }
            ImGui::TextWrapped("Position and scale each part independently. Selection highlights and shadows follow their panel.");
            ImGui::PopID();
        }
        if(ImGui::CollapsingHeader("Location and day/night banners")) {
            constexpr const char* names[]{"New location banner","Day / night banner"};
            for(unsigned i=0;i<2;++i){ImGui::PushID(int(i)+1800);
                if(ImGui::TreeNode(names[i])){auto& p=state.settings.banners.parts[i];bool changed=false;
                    changed|=ImGui::SliderFloat("Banner height",&p.height,.25f,3,"%.2fx");
                    changed|=ImGui::SliderFloat("Banner Y",&p.y,-240,240,"%.1f");
                    changed|=ImGui::SliderFloat("Background brightness",&p.brightness,0,1,"%.2f");
                    changed|=ImGui::SliderFloat("Text scale",&p.text_scale,.25f,3,"%.2fx");
                    changed|=ImGui::SliderFloat("Text X",&p.text_x,-400,400,"%.1f");
                    changed|=ImGui::SliderFloat("Text Y offset",&p.text_y,-240,240,"%.1f");
                    if(ImGui::Button("Reset")){p=Banners::Options{}.parts[i];changed=true;}
                    if(changed){Banners::Apply(state.settings.banners);state.status=state.settings.Save(state.directory+"presentation.json")?"Banner settings saved":"Banner settings save FAILED";}
                    ImGui::TreePop();}ImGui::PopID();}
            ImGui::TextWrapped("Both backgrounds fill the widescreen width. Adjustments apply live; the original fades are preserved.");
        }
        if(ImGui::CollapsingHeader("Top-right item buttons")) {
            auto& o=state.settings.hud_buttons;bool changed=false;
            changed|=ImGui::SliderFloat("All button scale",&o.scale,.1f,3,"%.2fx");
            changed|=ImGui::SliderFloat("A/B flip peak size",&o.flip_peak,1,1.2f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::Checkbox("Arrange X / Y / I / II in a diamond",&o.diamond.enabled);
            if(o.diamond.enabled){
                changed|=ImGui::SliderFloat("Diamond center X",&o.diamond.x,-512,512,"%.1f");
                changed|=ImGui::SliderFloat("Diamond center Y",&o.diamond.y,-240,240,"%.1f");
                changed|=ImGui::SliderFloat("Diamond spacing",&o.diamond.spacing,0,160,"%.1f");
                changed|=ImGui::SliderFloat("Diamond rotation",&o.diamond.rotation,-180,180,"%.1f degrees");
                ImGui::TextWrapped("Spacing 0 stacks all four at the center. Button size is independent. Turn diamond off to restore individual positions.");
            }
            const char* names[]{"A button","X button","Y button","B button",state.settings.stick_click_controls?"Item I (ZL)":"Item I (ZR + X)",state.settings.stick_click_controls?"Item II (ZR)":"Item II (ZR + Y)"};
            for(unsigned i=0;i<HudButtons::Count;++i){if(o.diamond.enabled&&i!=0&&i!=3)continue;ImGui::PushID(int(i)+2300);
                if(ImGui::TreeNode(names[i])){const auto slot=StickControls::ItemLayoutIndex(i,4);auto& p=o.buttons[slot];
                    changed|=ImGui::SliderFloat("X offset",&p.x,-512,512,"%.1f");
                    changed|=ImGui::SliderFloat("Y offset",&p.y,-240,240,"%.1f");
                    if(ImGui::Button("Reset button")){p=HudButtons::Default()[slot];changed=true;}
                    ImGui::TreePop();}ImGui::PopID();}
            for(unsigned i=0;i<3;++i){ImGui::PushID(int(i)+2400);
                ImGui::Text("Layout %u%s",i+1,o.saved[i]?"":" (empty)");ImGui::SameLine();
                if(ImGui::Button("Save")){HudButtons::SaveSlot(o,i);changed=true;}
                ImGui::SameLine();ImGui::BeginDisabled(!o.saved[i]);
                if(ImGui::Button("Load"))changed|=HudButtons::LoadSlot(o,i);
                ImGui::EndDisabled();ImGui::PopID();}
            if(changed){HudButtons::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Item button layout saved":"Item button layout save FAILED";}
            ImGui::TextWrapped("Changes apply live. Layout slots save only these six buttons. Offsets and scale are relative to their normal HUD positions.");
        }
        if(ImGui::CollapsingHeader("Song of Time reset items")) {
            auto& o=state.settings.song_reset;bool changed=ImGui::Checkbox("Reset items on top screen",&o.enabled);
            changed|=ImGui::SliderFloat("Reset background X",&o.background_x,-512,512,"%.1f");
            auto& brown=state.settings.inventory_menu;
            bool brown_changed=ImGui::SliderFloat("Reset background Y (shared)",&brown.background.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            brown_changed|=ImGui::SliderFloat("Reset background scale (shared)",&brown.background.scale,.25f,2,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            brown_changed|=ImGui::SliderFloat("Reset shadow width (shared)",&brown.shadow_width,0,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            brown_changed|=ImGui::SliderFloat("Reset shadow opacity (shared)",&brown.shadow_opacity,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if(brown_changed){InventoryMenu::Apply(brown);changed=true;}
            ImGui::TextWrapped("Two vertically stacked brown panels with a right shadow. X is specific to this screen; Y, scale and shadow are shared with the other brown backgrounds.");
            constexpr const char* names[]{"Main item panel","Right item panel","Bottom item panel","Reset textbox"};
            for(unsigned i=0;i<SongReset::Count;++i){ImGui::PushID(int(i)+1900);
                if(ImGui::TreeNode(names[i])){auto& p=o.poses[i];
                    changed|=ImGui::SliderFloat("X",&p.x,i==SongReset::Description?-320.f:-512.f,i==SongReset::Description?320.f:512.f,"%.1f");
                    changed|=ImGui::SliderFloat("Y",&p.y,-240,240,"%.1f");
                    changed|=ImGui::SliderFloat("Scale",&p.scale,i==SongReset::Description?.25f:.1f,i==SongReset::Description?2.f:3.f,"%.2fx");
                    if(ImGui::Button("Reset")){p=SongReset::Options{}.poses[i];changed=true;}
                    ImGui::TreePop();}ImGui::PopID();}
            if(changed){SongReset::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Reset item settings saved":"Reset item settings save FAILED";}
        }
        if(ImGui::CollapsingHeader("Song of Double Time")) {
            auto& o=state.settings.double_time;bool changed=ImGui::Checkbox("Double Time on top screen",&o.enabled);
            changed|=ImGui::Checkbox("Only show current day",&o.current_day_only);
            constexpr const char* names[]{"Background panel","Skip ahead prompt","Timeline / cursor / Link","Instructions","Skip / Cancel buttons"};
            for(unsigned i=0;i<DoubleTime::Count;++i){ImGui::PushID(int(i)+1700);
                if(ImGui::TreeNode(names[i])){auto& p=o.poses[i];
                    changed|=ImGui::SliderFloat("X",&p.x,-512,512,"%.1f");
                    changed|=ImGui::SliderFloat("Y",&p.y,-240,240,"%.1f");
                    changed|=ImGui::SliderFloat("Scale",&p.scale,.1f,3,"%.2fx");
                    if(ImGui::Button("Reset")){p=DoubleTime::Options{}.poses[i];changed=true;}
                    ImGui::TreePop();}ImGui::PopID();}
            if(changed){DoubleTime::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Double Time settings saved":"Double Time settings save FAILED";}
        }
        if(ImGui::CollapsingHeader("Ocarina song list")) {
            auto& o=state.settings.song_list;bool changed=ImGui::Checkbox("Song list on top screen",&o.enabled);
            changed|=ImGui::SliderFloat("Song panel left X",&o.panel_left,-512,o.panel_right-16,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Song panel right X",&o.panel_right,o.panel_left+16,512,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Song grid X",&o.songs.x,-512,512,"%.1f");
            changed|=ImGui::SliderFloat("Song grid Y",&o.songs.y,-240,240,"%.1f");
            float header_gap=o.songs.y-(o.banner.y+o.banner_height*o.banner.scale);
            if(ImGui::SliderFloat("Header-to-grid gap",&header_gap,0,80,"%.1f")) {
                o.songs.y=o.banner.y+o.banner_height*o.banner.scale+header_gap;changed=true;
            }
            changed|=ImGui::SliderFloat("Song panel darkness",&o.panel_darkness,0,1,"%.2f");
            changed|=ImGui::ColorEdit3("Song panel overlay color",o.panel_color.data());
            float color_opacity=o.panel_color_opacity*100.f;
            if(ImGui::SliderFloat("Song panel color opacity",&color_opacity,0,100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) {
                o.panel_color_opacity=color_opacity*.01f;changed=true;
            }
            hover_help("Adds color over the sidebar background only. 0% hides the color; 100% makes it solid. Song stones, text and the black edges keep their colors.");
            changed|=ImGui::SliderFloat("Song button scale",&o.songs.scale,.1f,2,"%.2f");
            changed|=ImGui::SliderFloat("Song stone darkness",&o.stone_darkness,0,1,"%.2f");
            changed|=ImGui::SliderFloat("Song row spacing",&o.spacing,0,60,"%.1f");
            changed|=ImGui::SliderFloat("Song column spacing",&o.column_spacing,40,180,"%.1f");
            changed|=ImGui::SliderFloat("Songs banner X",&o.banner.x,-512,512,"%.1f");
            changed|=ImGui::SliderFloat("Songs banner Y",&o.banner.y,-240,240,"%.1f");
            changed|=ImGui::SliderFloat("Songs banner scale",&o.banner.scale,.1f,3,"%.2fx");
            if(ImGui::Button("Reset Songs banner")) {
                const SongList::Options defaults;
                o.banner=defaults.banner;o.banner_width=defaults.banner_width;o.banner_height=defaults.banner_height;changed=true;
            }
            changed|=ImGui::SliderFloat("Song name / notes X",&o.staff.x,-512,512,"%.1f");
            changed|=ImGui::SliderFloat("Song name / notes Y",&o.staff.y,-240,240,"%.1f");
            changed|=ImGui::SliderFloat("Song name / notes scale",&o.staff.scale,.1f,2,"%.2f");
            if(ImGui::Button("Reset song list")){o={};changed=true;}
            if(changed){SongList::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Song list saved":"Song list save FAILED";}
        }
        if(ImGui::CollapsingHeader("Ocarina prompts")) {
            auto& o=state.settings.ocarina_prompt;
            bool changed=ImGui::Checkbox("Show Songs / Cancel / Back prompts",&o.enabled);
            changed|=ImGui::SliderFloat("Ocarina prompts X",&o.x,-400,400,"%.1f");
            changed|=ImGui::SliderFloat("Ocarina prompts Y",&o.y,-240,240,"%.1f");
            changed|=ImGui::SliderFloat("Ocarina prompts scale",&o.scale,.5f,2,"%.2f");
            changed|=ImGui::SliderFloat("Vertical spacing",&o.spacing,0,60,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Ocarina button icon scale",&o.icon_scale,.5f,2,"%.2f");
            if(ImGui::Button("Reset ocarina prompts")){o={};changed=true;}
            if(changed){OcarinaPrompt::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Ocarina prompts saved":"Ocarina prompts save FAILED";}
            ImGui::Text("Prompt status: %u",OcarinaPrompt::status.load());
        }
        if(ImGui::CollapsingHeader("File select prompts")) {
            auto& o=state.settings.file_select_prompt;
            bool changed=ImGui::Checkbox("Show Select / Back prompts",&o.enabled);
            changed|=ImGui::SliderFloat("File select prompts X",&o.x,-400,400,"%.1f");
            changed|=ImGui::SliderFloat("File select prompts Y",&o.y,-240,240,"%.1f");
            changed|=ImGui::SliderFloat("File select prompts scale",&o.scale,.5f,2,"%.2f");
            changed|=ImGui::SliderFloat("Vertical spacing",&o.spacing,0,60,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("File select button icon scale",&o.icon_scale,.5f,2,"%.2f");
            if(ImGui::Button("Reset file select prompts")){o={};changed=true;}
            if(changed){FileSelectPrompt::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"File select prompts saved":"File select prompts save FAILED";}
        }
        if(ImGui::CollapsingHeader("File select")) {
            auto& o=state.settings.file_select;
            bool changed=ImGui::Checkbox("File select on top screen",&o.enabled);
            if(ImGui::TreeNode("Green header background")) {
                auto& p=o.header;
                changed|=ImGui::SliderFloat("X",&p.x,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&p.y,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale X",&p.scale_x,.01f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale Y",&p.scale_y,.01f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset header background")){p={};changed=true;}
                ImGui::TextWrapped("Moves the green strip and its shadow on file select. Move Y upward to take the strip offscreen.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Select file title")) {
                auto& t=o.title;
                changed|=ImGui::SliderFloat("X",&t.x,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&t.y,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&t.scale,.01f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset select file title")){t={};changed=true;}
                ImGui::TextWrapped("Moves and scales the Select file title with the header by default. These controls add an independent adjustment.");
                ImGui::TreePop();
            }
            constexpr std::array<const char*,NativeFileSelect::Count> names{"Save summary panel","File stones","Prompt box","Start / Copy / Erase / Options","Confirmation popup"};
            for(unsigned i=0;i<NativeFileSelect::Count;++i) {
                ImGui::PushID(int(i)+1100);
                if(ImGui::TreeNode(names[i])) {
                    auto& p=o.poses[i];
                    if(i==NativeFileSelect::Files)changed|=ImGui::SliderFloat("Extra spacing",&o.stone_spacing,0.f,80.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    if(i==NativeFileSelect::Files) {
                        changed|=ImGui::SliderFloat("Entrance ease-out",&o.entrance_ease,1.f,3.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                        ImGui::TextWrapped("1 is linear. Higher values slow the blocks more as they approach their destination. Applies when entering file select.");
                    }
                    changed|=ImGui::SliderFloat("X",&p.x,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Y",&p.y,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Scale",&p.scale,.1f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset")){p=NativeFileSelect::Options{}.poses[i];if(i==NativeFileSelect::Files){o.stone_spacing=0;o.entrance_ease=2;}changed=true;}
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if(changed){NativeFileSelect::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"File select settings saved":"File select settings save FAILED";}
            ImGui::TextWrapped("Choosing a file slides the stones out and eases the summary and action buttons into their configured positions. Back returns to the stones. Selection highlights follow them. Back remains on the bottom; touch targets retain their original positions.");
        }
        if(ImGui::CollapsingHeader("Bomber's Notebook")) {
            auto& o=state.settings.notebook_live;
            bool changed=ImGui::Checkbox("Native notebook controls",&o.enabled);
            static bool row_capture_pending=false;
            if(ImGui::Button("Capture event rows (15 seconds)")) {
                NotebookRows::StartCapture();row_capture_pending=true;
                state.status="Capturing rows: close F1, close and reopen Event Notes, then close it again. After 15 seconds reopen this section to save.";
            }
            if(row_capture_pending&&NotebookRows::Now()>=NotebookRows::capture_until.load()) {
                QByteArray bytes;
                {std::scoped_lock lock(NotebookRows::capture_mutex);
                    for(const auto& line:NotebookRows::capture_lines){bytes+=QByteArray::fromStdString(line);bytes+='\n';}}
                if(bytes.isEmpty())bytes="No row hook observations during capture.\n";
                QSaveFile file(state.directory+"notebook-row-capture.txt");
                const bool ok=file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
                state.status=ok?"Saved notebook-row-capture.txt":"Row capture save FAILED";
                row_capture_pending=false;
            }
            changed|=ImGui::Checkbox("Fill initial event rows (experimental)",&o.expanded_rows);
            ImGui::TextWrapped("Reopen the notebook after toggling. Uses spare row slots; does not add entries to your notebook.");
            constexpr std::array<const char*,NativeNotebook::Count> names{"Event list","Event details panel","Legacy character icon bar","Schedule grid","Schedule caption + portrait","Event Notes arrows","Legacy down arrow","Event Notes scrollbar","Schedule scrollbar","Schedule arrows","Alarm popup"};
            for(unsigned i=0;i<NativeNotebook::Count;++i) {
                if(i==2||i==4||i==6||i==7)continue; // Shared description and scrollbar controls.
                ImGui::PushID(int(i)+900);
                if(ImGui::TreeNode(i==1?"Notebook description panel":i==8?"Notebook scrollbars":names[i])) {
                    auto& p=o.poses[i];
                    changed|=ImGui::SliderFloat("X",&p.x,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Y",&p.y,-512.f,512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    if(i<5||i==10)changed|=ImGui::SliderFloat("Scale",&p.scale,.1f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(i==10) {
                        changed|=ImGui::SliderFloat("Box width",&o.popup_width,.5f,3.f,"%.2fx");
                        changed|=ImGui::SliderFloat("Box height",&o.popup_height,.5f,3.f,"%.2fx");
                    }
                    if(ImGui::Button("Reset")){p=NativeNotebook::Options{}.poses[i];if(i==10){o.popup_width=1;o.popup_height=1;}changed=true;}
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if(ImGui::TreeNode("Event note buttons")) {
                changed|=ImGui::SliderFloat("X offset",&o.prompt_x,-400,400,"%.1f");
                changed|=ImGui::SliderFloat("Scale",&o.prompt_scale,.5f,2.f,"%.2fx");
                changed|=ImGui::SliderFloat("Button icon scale",&o.prompt_icon_scale,.5f,2.f,"%.2fx");
                ImGui::TextUnformatted("Both prompts share the same left edge and scale.");
                changed|=ImGui::Checkbox("A — Schedule details",&o.event_prompt);
                changed|=ImGui::SliderFloat("Schedule details Y",&o.prompt_y,-240,240,"%.1f");
                changed|=ImGui::Checkbox("X — View Map",&o.map_prompt);
                changed|=ImGui::SliderFloat("View Map Y",&o.map_prompt_y,-240,240,"%.1f");
                changed|=ImGui::Checkbox("R — View schedule",&o.schedule_prompt);
                changed|=ImGui::SliderFloat("View schedule Y",&o.schedule_prompt_y,-240,240,"%.1f");
                if(ImGui::Button("Reset event note buttons")) {
                    o.event_prompt=o.map_prompt=o.schedule_prompt=true;o.prompt_x=0;o.prompt_scale=1;o.prompt_icon_scale=1;
                    o.prompt_y=0;o.map_prompt_y=-20;o.schedule_prompt_y=-40;changed=true;
                }
                ImGui::TreePop();
            }
            if(changed){NativeNotebook::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Notebook settings saved":"Notebook settings save FAILED";}
            const auto schedule_state=NativeNotebook::schedule_status.load();
            constexpr std::array<const char*,6> schedule_states{"waiting for Schedule","schedule grid not submitted",
                "checking schedule","caption layout guard failed","grid layout guard failed","active"};
            ImGui::Text("Schedule: %s",schedule_states[std::min(schedule_state,5u)]);
            ImGui::TextWrapped("Live native event-list, description and character-bar transforms. Touch targets retain their original positions.");
        }
        if(ImGui::CollapsingHeader("Options menu")) {
            auto& o=state.settings.options_menu;
            bool changed=ImGui::SliderFloat("X",&o.x,-400,400,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Y",&o.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Scale",&o.scale,.1f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Row spacing",&o.spacing,0,120,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Reset option rows")){const auto description=o.description;o={};o.description=description;changed=true;}
            if(ImGui::TreeNode("Description textbox")) {
                auto& p=o.description;
                changed|=ImGui::SliderFloat("X",&p.x,-320,320,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&p.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&p.scale,.25f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset textbox")){p={};changed=true;}
                ImGui::TreePop();
            }
            if(ImGui::Button("Reset all Options layout")){o={};changed=true;}
            ImGui::TextWrapped("All seven options in one vertical list. Scale includes row spacing; the selection outline follows the list.");
            if(changed){OptionsMenu::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Options layout saved":"Options layout save FAILED";}
        }
        if(ImGui::CollapsingHeader("Map menu")) {
            auto& o=state.settings.dungeon_map;
            bool changed=ImGui::Checkbox("Dungeon map on top screen",&o.enabled);
            if(ImGui::TreeNode("L - Items prompt")) {
                ImGui::TextUnformatted("Whole prompt");
                ImGui::TextWrapped("World and dungeon maps. Positive X moves right. Uses shared menu prompt opacity.");
                auto& b=state.settings.map_items_prompt.pose;
                changed|=ImGui::SliderFloat("X",&b.x,-400.f,400.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&b.y,-240.f,240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&b.scale,.25f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset global pose")) {b=ItemsMapPrompt::ReturnDefaultPose;changed=true;}
                const char* labels[]={"L button","Red arrow","Items text"};
                for(unsigned i=0;i<3;++i) {
                    if(ImGui::TreeNode(labels[i])) {
                        auto& p=state.settings.map_items_prompt.elements[i];
                        changed|=ImGui::SliderFloat("X",&p.x,-400.f,400.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::SliderFloat("Y",&p.y,-240.f,240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::SliderFloat("Scale",&p.scale,.25f,4.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                        if(ImGui::Button("Reset")) {p=ItemsMapPrompt::ReturnDefaultElements[i];changed=true;}
                        ImGui::TreePop();
                    }
                }
                if(ImGui::Button("Reset entire Items prompt")) {b=ItemsMapPrompt::ReturnDefaultPose;state.settings.map_items_prompt.elements=ItemsMapPrompt::ReturnDefaultElements;changed=true;}
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("World map original Zoom texture")) {
                auto& p=state.settings.world_map_native_zoom;
                changed|=ImGui::SliderFloat("X",&p.x,-800,800,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&p.y,-480,480,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&p.scale,.1f,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Move offscreen")){p.y=400;changed=true;}
                ImGui::SameLine();if(ImGui::Button("Reset")){p={};changed=true;}
                ImGui::TextWrapped("Moves both original Zoom In and Zoom Out artwork. The new X/text prompt is independent.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("World map X Zoom prompt")) {
                auto& p=state.settings.world_map_zoom_prompt;
                changed|=ImGui::SliderFloat("X",&p.x,-400,400,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&p.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&p.scale,.25f,2,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset")){p={0,0,.8f};changed=true;}
                ImGui::TextWrapped("Switches between Zoom in and Zoom out with the map. Uses shared menu prompt opacity.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Dungeon map buttons")) {
                auto& p=o.prompts;
                changed|=ImGui::SliderFloat("X offset",&p.x,-400,400,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y offset",&p.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&p.scale,.5f,2,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Button icon scale",&p.icon_scale,.5f,2,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Spacing",&p.spacing,0,60,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset dungeon map buttons")){p=DungeonMap::Options{}.prompts;o.prompt_group={};changed=true;}
                ImGui::TextWrapped("One layout for Move map, A Zoom, Move cursor and B Return. All rows share the same text size, icon size and alignment. Move map fades without shifting the other rows.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Dungeon map arrows")) {
                changed|=ImGui::SliderFloat("Scale (all four)",&o.arrow_scale,.1f,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                constexpr std::array<const char*,4> labels{"Up arrow","Down arrow","Left arrow","Right arrow"};
                for(unsigned i=0;i<4;++i)if(ImGui::TreeNode(labels[i])) {
                    changed|=ImGui::SliderFloat("X",&o.arrow_offsets[i][0],-800,800,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Y",&o.arrow_offsets[i][1],-480,480,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset position")){o.arrow_offsets[i]={};changed=true;}
                    ImGui::TreePop();
                }
                if(ImGui::Button("Reset all arrows")){o.arrow_scale=1;o.arrow_offsets={};changed=true;}
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Dungeon side panels")) {
                changed|=ImGui::Checkbox("Show brown side panels",&o.side_panels);
                float darkness=o.panel_darkness*100;
                if(ImGui::SliderFloat("Darkness",&darkness,0,100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){o.panel_darkness=darkness/100;changed=true;}
                for(unsigned i=0;i<2;++i)if(ImGui::TreeNode(i?"Right panel":"Left panel")) {
                    auto& p=o.panels[i];
                    changed|=ImGui::SliderFloat("X",&p.x,-512,512,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Y",&p.y,-480,480,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Scale",&p.scale,.1f,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset")){p=DungeonMap::Options{}.panels[i];changed=true;}
                    ImGui::TreePop();
                }
                if(ImGui::Button("Reset side panels")){o.side_panels=true;o.panel_darkness=0;o.panels=DungeonMap::Options{}.panels;changed=true;}
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Dungeon top brown strip")) {
                changed|=ImGui::Checkbox("Show top strip",&o.top_strip);
                ImGui::TextWrapped("Four full panels joined horizontally. Uses the side panels' Darkness setting.");
                auto& p=o.top_panel;
                changed|=ImGui::SliderFloat("X",&p.x,-1024,1024,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&p.y,-480,480,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&p.scale,.1f,3,"%.3fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset strip")){o.top_strip=true;p=DungeonMap::Options{}.top_panel;changed=true;}
                ImGui::TreePop();
            }
            constexpr std::array<const char*,DungeonMap::Count> labels{
                "Map, markers and magnifying cursor", "Map background", "Dungeon items and counts",
                "Floor selector", "Location name", "B Return button", "A Zoom button", "Item preview model", "Item description box", "Magnifying glass"};
            for(unsigned i=0;i<DungeonMap::Count;++i) {
                ImGui::PushID(int(i)+1800);
                if(i!=DungeonMap::Zoom&&ImGui::TreeNode(labels[i])) {
                    auto& p=o.poses[i];const bool model=i==DungeonMap::Model,description=i==DungeonMap::Description;
                    changed|=ImGui::SliderFloat("X",&p.x,model?-20.f:description?-320.f:-512.f,model?20.f:description?320.f:512.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Y",&p.y,model?-20.f:-240.f,model?20.f:240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Scale",&p.scale,model||description?.25f:.1f,model||description?2.f:3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset")){p=DungeonMap::Options{}.poses[i];changed=true;}
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TextWrapped("Outside dungeons, shows only the world map and disables A map zoom. X world-map zoom stays native. Soaring is unchanged. Touch targets retain their original positions.");
            ImGui::Text("Map mode: %s",DungeonMap::status.load()==2?"dungeon":DungeonMap::status.load()==1?"world":"waiting / guarded");
            if(changed){DungeonMap::Apply(o);ItemsMapPrompt::ApplyReturn(state.settings.map_items_prompt);WorldMapPrompt::Apply(state.settings.world_map_zoom_prompt);WorldMapPrompt::ApplyNative(state.settings.world_map_native_zoom);state.status=state.settings.Save(state.directory+"presentation.json")?"Map settings saved":"Map settings save FAILED";}
        }
        if(ImGui::CollapsingHeader("Gear menu")) {
            auto& options=state.settings.inventory_menu.gear;
            bool changed=ImGui::Checkbox("Gear panels on top screen",&options.enabled);
            constexpr std::array<const char*,GearMenu::Count> labels{
                "Main panel and gear", "Right-hand strip", "Brown background", "Buttons", "Rotating model", "Description", "Photo and border"};
            for(unsigned i=0;i<GearMenu::Count;++i) {
                if(i==GearMenu::Background||i==GearMenu::Model||i==GearMenu::Description)continue;
                ImGui::PushID(int(i)+700);
                if(ImGui::TreeNode(labels[i])) {
                    auto& p=options.poses[i];const bool model=i==GearMenu::Model;

                    if(i==GearMenu::Right) {
                        changed|=ImGui::Checkbox("Horizontal strip",&options.horizontal);
                        if(ImGui::Button("Place above main panel")) {
                            options.horizontal=true;p.x=options.poses[GearMenu::Panel].x-260.f;
                            p.y=options.poses[GearMenu::Panel].y-56.f*p.scale-4.f;changed=true;
                        }
                    }
                    changed|=ImGui::SliderFloat("X",&p.x,model?-20.f:-320.f,model?20.f:320.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Y",&p.y,model?-20.f:-240.f,model?20.f:240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    changed|=ImGui::SliderFloat("Scale",&p.scale,.25f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset")) {p=GearMenu::Options{}.poses[i];changed=true;}
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TextWrapped("Model and description X/Y/scale are shared with Items and Masks under Inventory item menu. Panel contents and selection cursor move together. Touch targets retain their original positions.");
            if(changed) {
                GearMenu::Apply(options);
                state.status=state.settings.Save(state.directory+"presentation.json") ? "Gear settings saved" : "Gear settings save FAILED";
            }
        }
        if(ImGui::CollapsingHeader("Inventory item menu")) {
            auto& options=state.settings.inventory_menu;
            bool changed=ImGui::Checkbox("Item grid on top screen",&options.enabled);
            changed|=ImGui::SliderFloat("Panel X", &options.x, -320.f, 320.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Panel Y", &options.y, -240.f, 240.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Panel scale", &options.scale, .5f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Reset inventory panel")) { options.x=0.f; options.y=32.f; options.scale=.8f; changed=true; }
            ImGui::SameLine();
            if(ImGui::Button("Native grid size")) { options.scale=1.f; changed=true; }
            if(ImGui::TreeNode("Masks grid adjustments")) {
                ImGui::TextWrapped("Offsets from the shared panel position; scale multiplies the shared panel scale.");
                changed|=ImGui::SliderFloat("Masks X offset", &options.masks_x, -320.f, 320.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Masks Y offset", &options.masks_y, -240.f, 240.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Masks scale multiplier", &options.masks_scale, .5f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset Masks adjustments")) {
                    options.masks_x=options.masks_y=0.f;options.masks_scale=1.f;changed=true;
                }
                ImGui::TreePop();
            }
            ImGui::Separator();
            changed|=ImGui::Checkbox("High-resolution pause background",&options.high_res_pause);
            ImGui::TextWrapped("Reopen the menu after enabling to capture a fresh background.");
            ImGui::Text("Pause capture: %ux | %u textures",PauseCapture::captured_scale.load(),PauseCapture::bound_textures.load());
            changed|=ImGui::Checkbox("Animate Items / Masks panel and equipped bar",&options.shared_slide);
            ImGui::TextWrapped("With the horizontal bar enabled, the vertically stacked brown backgrounds and grid slide left; the equipped strip and top bar slide upward together. Opening and closing move at constant speed.");
            changed|=ImGui::Checkbox("Horizontal equipped-item bar",&options.bar_enabled);
            changed|=ImGui::SliderFloat("Bar X", &options.bar_x, -320.f, 320.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Bar Y", &options.bar_y, -240.f, 240.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Bar scale", &options.bar_scale, .35f, 1.5f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Slot spacing", &options.bar_spacing, 40.f, 120.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Place bar above grid")) {
                options.bar_x=options.x; options.bar_y=options.y-64.f*options.bar_scale-4.f; changed=true;
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Description — Items / Masks / Gear");
            changed|=ImGui::SliderFloat("Description X", &options.description_x, -320.f, 320.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Description Y", &options.description_y, -240.f, 240.f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Description scale", &options.description_scale, .25f, 2.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Reset item description")) {
                options.description_x=options.description_y=0.f; options.description_scale=1.f; changed=true;
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Rotating model — Items / Masks / Gear");
            changed|=ImGui::SliderFloat("Model X", &options.model_x, -20.f, 20.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Model Y", &options.model_y, -20.f, 20.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            changed|=ImGui::SliderFloat("Model scale", &options.model_scale, .25f, 2.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Reset item model")) {
                options.model_x=options.model_y=0.f; options.model_scale=1.f; changed=true;
            }
            ImGui::TextWrapped("Live offsets from the original positions. Model offsets use 3D units; small adjustments are best. Positive X moves right; positive Y moves down.");
            ImGui::Separator();
            changed|=ImGui::Checkbox("Menu buttons on top screen",&options.buttons_enabled);
            const char* button_names[]={"Back button","Gear button","Masks button","Items button"};
            for(unsigned i=0;i<4;++i) {
                ImGui::PushID(button_names[i]);
                if(ImGui::TreeNode(button_names[i])) {
                    auto& b=i==1?options.gear_button:options.buttons[i?i-1:0];
                    if(i>=2) {
                        changed|=ImGui::DragFloat("X",&b.x,.1f,-400.f,400.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::DragFloat("Y",&b.y,.1f,-240.f,240.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
                        ImGui::TextDisabled("Drag for fine positioning; Ctrl+click to enter a value.");
                    } else {
                        changed|=ImGui::SliderFloat("X",&b.x,-400.f,400.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::SliderFloat("Y",&b.y,-240.f,240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                    }
                    changed|=ImGui::SliderFloat("Scale",&b.scale,.25f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                    if(ImGui::Button("Reset")) {b={};changed=true;}
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if(ImGui::TreeNode("Menu button prompts")) {
                auto& h=options.help;
                changed|=ImGui::Checkbox("Show prompts",&h.enabled);
                float opacity=h.opacity*100;
                if(ImGui::SliderFloat("Shared opacity",&opacity,0,100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){h.opacity=opacity/100;changed=true;}
                ImGui::TextWrapped("Also controls the Items / Map arrows, buttons and text.");
                int arrangement=h.horizontal?1:0;
                if(ImGui::Combo("Arrangement",&arrangement,"Vertical\0Horizontal\0")){h.horizontal=arrangement==1;changed=true;}
                changed|=ImGui::SliderFloat("Prompt spacing",&h.spacing,0,100,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Button / text spacing",&h.button_gap,0,40,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Button size",&h.button_scale,.25f,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Text size",&h.text_scale,.25f,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Group X",&h.pose.x,-512,512,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Group Y",&h.pose.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Group scale",&h.pose.scale,.1f,2,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset prompts")){h={};changed=true;}
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("R - Map prompt")) {
                ImGui::TextUnformatted("Whole prompt");
                ImGui::TextWrapped("Edit L / Items separately under Map menu.");
                auto& b=options.map_prompt;
                changed|=ImGui::SliderFloat("X",&b.x,-400.f,400.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&b.y,-240.f,240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&b.scale,.25f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset global pose")) {b=ItemsMapPrompt::DefaultPose;changed=true;}
                const char* labels[]={"R button","Red arrow","Map text"};
                for(unsigned i=0;i<3;++i) {
                    if(ImGui::TreeNode(labels[i])) {
                        auto& p=options.map_prompt_elements[i];
                        changed|=ImGui::SliderFloat("X",&p.x,-400.f,400.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::SliderFloat("Y",&p.y,-240.f,240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                        changed|=ImGui::SliderFloat("Scale",&p.scale,.25f,4.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                        if(ImGui::Button("Reset")) {p=ItemsMapPrompt::DefaultElements[i];changed=true;}
                        ImGui::TreePop();
                    }
                }
                if(ImGui::Button("Reset entire Map prompt")) {b=ItemsMapPrompt::DefaultPose;options.map_prompt_elements=ItemsMapPrompt::DefaultElements;changed=true;}
                ImGui::TreePop();
            }
            ImGui::TextWrapped("Button offsets are relative to their original positions. Scaling keeps each button anchored. Requires Item grid on top screen; touch targets retain their original positions.");
            ImGui::Separator();
            changed|=ImGui::Checkbox("Brown background on top screen",&options.background_enabled);
            ImGui::TextWrapped("Background placement and shadow: see Shared brown backgrounds.");
            if(changed) {
                InventoryMenu::Apply(options);
                state.status=state.settings.Save(state.directory+"presentation.json") ?
                    "Inventory settings saved" : "Inventory settings save FAILED";
            }
            ImGui::TextWrapped("Live position and scale for the Items grid, icons and quantities. Negative Y moves upward. The horizontal bar keeps slot frames, labels and icons upright. Navigation and touch input retain their original arrangement.");
            ImGui::Text("Native inventory: %s | separate icons: %u",NativeInventory::status.load()==2?"active":
                NativeInventory::status.load()>=3?"guarded / unsupported layout":"waiting for Items menu",NativeInventory::routed_icons.load());
        }
        if(ImGui::CollapsingHeader("Submenu headers")) {
            auto& options=state.settings.submenu_header;
            ImGui::TextWrapped("Shared green title bar. Negative Y moves upward. Changes require a game restart.");
            for(const auto& field:SubmenuHeader::Fields)
                ImGui::SliderFloat(field.label,&(options.*field.member),field.lo,field.hi,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if(ImGui::Button("Widescreen header preset")) options=SubmenuHeader::Widescreen();
            ImGui::SameLine();
            if(ImGui::Button("Background-only test")) options=SubmenuHeader::Options{};
            bool apply=ImGui::Button("Apply header (restart game)");
            ImGui::SameLine();
            if(ImGui::Button("Restore original header")) { options=SubmenuHeader::Original(); apply=true; }
            if(apply) {
                const auto error=SubmenuHeader::Install(options);
                if(!error.isEmpty()) state.status=error;
                else state.status=state.settings.Save(state.directory+"presentation.json") ?
                    "Header saved — restart the game" : "Header installed; settings save FAILED";
            }
            ImGui::TextWrapped("Presets update the sliders; Apply installs them. Other archive layouts are preserved.");
        }
        if(ImGui::CollapsingHeader("Audio")) { audio_accuracy_controls(); mute_music_control(); }
        if(ImGui::CollapsingHeader("Audio diagnostics")) {
            static bool pending=false;
            if(!pending&&ImGui::Button("Capture rupee audio (10 seconds)")) {
                AudioCapture::Start();pending=true;
            }
            if(pending&&AudioCapture::Running())
                ImGui::TextUnformatted("Capturing: close F1, collect one rupee, then return here.");
            if(pending&&!AudioCapture::Running()) {
                std::scoped_lock lock(AudioCapture::mutex);
                const QString path=state.directory+"audio-capture-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".bin";
                QSaveFile file(path);
                const auto& b=AudioCapture::bytes;
                const bool ok=file.open(QIODevice::WriteOnly)&&file.write(reinterpret_cast<const char*>(b.data()),qint64(b.size()))==qint64(b.size())&&file.commit();
                state.status=ok?("Audio capture saved: "+path+(AudioCapture::truncated?" (size limit reached)":"")):"Audio capture save FAILED";
                AudioCapture::bytes.clear();pending=false;
            }
            ImGui::TextWrapped("HLE only. Captures individual voices and playback settings for 10 seconds. Reopen this section afterward to save the capture.");
        }
        if(ImGui::CollapsingHeader("Restoration HUD")) {
            if(ImGui::TreeNode("D-pad position")) {
                auto& x=state.settings.dpad_offset_x;auto& y=state.settings.dpad_offset_y;
                bool changed=ImGui::SliderFloat("X",&x,-400,400,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset")){x=y=0;changed=true;}
                if(changed){PlayerHud::ApplyDpadPosition(x,y);state.status=state.settings.Save(state.directory+"presentation.json")?"D-pad position saved":"D-pad position save FAILED";}
                ImGui::TextWrapped("Moves the D-pad and its Tatl prompt live. User-facing HUD offsets apply on top. No restart required.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Rupees / Keys")) {
                auto& p=state.settings.rupee_keys;
                bool changed=ImGui::SliderFloat("X",&p.x,-400,400,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&p.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&p.scale,.25f,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset")){p={};changed=true;}
                if(changed){PlayerHud::ApplyRupeeKeys(p);state.status=state.settings.Save(state.directory+"presentation.json")?"Rupees / keys saved":"Rupees / keys save FAILED";}
                ImGui::TextWrapped("Live default position and size for both counters, including icons and numbers. User-facing HUD adjustments apply on top.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Epona carrots")) {
                auto& p=state.settings.carrots;
                bool changed=ImGui::SliderFloat("X",&p.x,-400,400,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&p.y,-240,240,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&p.scale,.25f,3,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset")){p={};changed=true;}
                if(changed){PlayerHud::ApplyCarrots(p);state.status=state.settings.Save(state.directory+"presentation.json")?"Epona carrots saved":"Epona carrots save FAILED";}
                ImGui::TextWrapped("Moves all six carrots together and scales around their center. User HUD adjustments apply on top.");
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Minigame score counter")) {
                auto& o=state.settings.minigame_hud;
                ImGui::PushID("minigame_hud");
                bool changed=ImGui::SliderFloat("X",&o.x,-400.f,400.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Y",&o.y,-240.f,240.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
                changed|=ImGui::SliderFloat("Scale",&o.scale,.25f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
                if(ImGui::Button("Reset")){o={};changed=true;}
                if(changed){MinigameHud::Apply(o);state.status=state.settings.Save(state.directory+"presentation.json")?"Minigame counter saved":"Minigame counter save FAILED";}
                ImGui::TextWrapped("Moves and scales the minigame icon and score together, including logs and Zora hoops. Changes apply live. Negative Y moves upward.");
                ImGui::PopID();
                ImGui::TreePop();
            }
            auto& border=state.settings.magic_border;
            bool border_changed=ImGui::Checkbox("Custom magic border",&border.enabled);
            border_changed|=ImGui::SliderFloat("Magic border thickness",&border.thickness,1.f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            border_changed|=ImGui::ColorEdit3("Magic border color",border.color.data());
            if(ImGui::Button("White magic border")){border.color={1,1,1};border.enabled=true;border_changed=true;}
            border_changed|=ImGui::Checkbox("Magic bar drop shadow",&border.shadow_enabled);
            border_changed|=ImGui::SliderFloat("Magic shadow size",&border.shadow_size,0.f,8.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            border_changed|=ImGui::SliderFloat("Magic shadow opacity",&border.shadow_opacity,0.f,1.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if(border_changed){MagicBorder::Apply(border);state.status=state.settings.Save(state.directory+"presentation.json")?"Magic border saved":"Magic border save FAILED";}
            ImGui::TextWrapped("Border changes apply live. Thickness follows the meter height and keeps its interior covered.");

            ImGui::SliderFloat("HUD scale", &state.settings.restoration_hud_scale,
                .5f,1.5f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            bool apply=ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SliderFloat("Magic bar Y", &state.settings.magic_bar_y,
                -64.f,64.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            apply |= ImGui::IsItemDeactivatedAfterEdit();
            if(ImGui::SliderFloat("Magic bar height", &state.settings.magic_bar_height,
                .5f,2.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp))
                PlayerHud::ApplyMagicHeight(state.settings.magic_bar_height);
            if(ImGui::IsItemDeactivatedAfterEdit())
                state.status=state.settings.Save(state.directory+"presentation.json")?
                    "Magic bar height saved (applied live)":"Magic bar height save FAILED";
            bool width_changed=ImGui::SliderFloat("Magic bar width",&state.settings.player_hud.magic_width,
                .5f,3.f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
            hover_help("Extends the fill, black background, border and shadow together to the right. Shares the Width control in HUD → Magic meter.");
            ImGui::SameLine();
            if(ImGui::Button("Reset magic bar width")){state.settings.player_hud.magic_width=1.f;width_changed=true;}
            if(width_changed){HudAdjustments::Apply(state.settings.player_hud);
                state.status=state.settings.Save(state.directory+"presentation.json")?"Magic bar width saved":"Magic bar width save FAILED";}
            ImGui::SliderFloat("D-pad Y", &state.settings.dpad_y,
                -120.f,120.f,"%.1f",ImGuiSliderFlags_AlwaysClamp);
            apply |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if(ImGui::Button("Reset HUD scale")) { state.settings.restoration_hud_scale=1.f; apply=true; }
            if(ImGui::Button("Reset magic bar Y")) { state.settings.magic_bar_y=0.f; apply=true; }
            if(apply) {
                const auto error=RestorationHud::Install(state.settings.restoration_hud_scale,state.settings.magic_bar_y,state.settings.magic_bar_height,state.settings.dpad_y);
                if(!error.isEmpty()) state.status=error;
                else {
                    const bool saved=state.settings.Save(state.directory+"presentation.json");
                    state.status=saved ? "HUD scale saved — restart the game to apply" :
                        "HUD layout updated; settings save FAILED. Restart the game to apply.";
                }
            }
            ImGui::TextWrapped("Magic bar height applies live. HUD scale, Magic bar Y and D-pad Y require restarting the game. Magic bar Y moves only the meter; left/right HUD groups stay anchored and the clock stays centered.");
        }
        bool picker_tab = false, advanced_tab = false;
        if (ImGui::BeginTabBar("Presentation tabs")) {
            if (ImGui::BeginTabItem("Groups")) {
                if (ImGui::CollapsingHeader("Create panel copy")) {
                    static char panel_name[64]="panel", panel_folder[64]="menus";
                    ImGui::InputText("New group name",panel_name,sizeof(panel_name));
                    ImGui::InputText("Folder",panel_folder,sizeof(panel_folder));
                    const auto create_panel = [&](bool import_notebook) {
                        Profiles::Profile p;
                        p.name=panel_name; p.id=std::string(panel_folder)+"/"+panel_name+".json";
                        p.panel_copy=true; p.presentation=true; p.enabled=false;
                        p.scale=.5f; p.x=.05f; p.y=.3f;
                        if (import_notebook) {
                            p.scale=Notebook::detail_scale.load(); p.x=Notebook::detail_x.load(); p.y=Notebook::detail_y.load();
                            p.crop={0.f,Notebook::detail_top.load()/240.f,1.f,Notebook::detail_bottom.load()/240.f};
                            p.required_context="notebook/notebook_open_signal.json";
                            std::lock_guard lock(Notebook::exclusions_mutex); p.exclusions=Notebook::exclusions;
                        }
                        for (const auto& existing:Profiles::List()) if(existing.id==p.id) {
                            state.status="Name already exists; choose a new name"; return;
                        }
                        const auto result=Profiles::Save(p); state.status=QString::fromStdString(result);
                        if(result.starts_with("Saved")) {
                            state.active_profile=QString::fromStdString(p.id);
                            if(import_notebook) Notebook::detail_preview.store(false);
                        }
                    };
                    if (ImGui::Button("Create saved panel group")) create_panel(false);
                    ImGui::TextWrapped("Created disabled. Select source, crop and visibility below, then enable and save. No texture capture needed except for background exclusions.");
                }
                const auto profiles = Profiles::List();
                ImGui::TextWrapped("%s", Profiles::Status().c_str());
                if (Profiles::guest_conflicts.load())
                    ImGui::TextWrapped("Overlapping guest profiles detected: first filename wins. Disable unwanted duplicate groups.");
                if (ImGui::BeginTable("Profile browser", 2, ImGuiTableFlags_Resizable)) {
                    ImGui::TableNextColumn();
                    QSet<QString> folders{"title-screen", "file-select", "hud", "menus"};
                    for (const auto& p : profiles) folders.insert(QString::fromStdString(p.id).section('/',0,0));
                    auto sorted_folders = folders.values(); std::sort(sorted_folders.begin(),sorted_folders.end());
                    for (const auto& folder_name : sorted_folders) {
                        const auto folder = folder_name.toStdString();
                        if (ImGui::TreeNode(folder.c_str())) {
                            for (const auto& p : profiles) {
                                if (!p.id.starts_with(std::string(folder) + "/")) continue;
                                const auto label = p.name + (p.panel_copy ? " [Panel copy]" : p.presentation ? " [Copy to top]" : " [Adjust original]") +
                                    (p.enabled ? "" : " [Disabled]");
                                if (ImGui::Selectable(label.c_str(), state.active_profile.toStdString() == p.id))
                                    state.active_profile = QString::fromStdString(p.id);
                            }
                            ImGui::TreePop();
                        }
                    }
                    if (ImGui::Button("Deselect profile")) state.active_profile.clear();
                    ImGui::TableNextColumn();
                    for (auto p : profiles) {
                        if (state.active_profile.toStdString() != p.id) continue;
                        ImGui::TextUnformatted(p.name.c_str());
                        ImGui::TextUnformatted(p.presentation ? "Copy to top screen" : "Adjust in original screen");
                        ImGui::Text("Identity rules: %u", unsigned(p.rules.size()));
                        if(p.atlas_selection) {
                            unsigned cells=0,rectangles=0; for(const auto& g:p.atlas_grids) { cells+=g.cells.size(); rectangles+=g.rectangles.size(); }
                            ImGui::Text("Atlas: %u cells, %u rectangles",cells,rectangles);
                        }
                        if (p.rules.empty() && !p.panel_copy) ImGui::TextWrapped("Needs recapture: select the textures, then Add selected to active profile.");
                        bool changed = ImGui::Checkbox("Enabled", &p.enabled);
                        if(!p.panel_copy) changed |= ImGui::Checkbox("Observe only (no transform)",&p.signal_only);
                        int render_mode=p.panel_copy ? 2 : p.presentation ? 1 : 0;
                        if(ImGui::Combo("Placement",&render_mode,"Adjust original\0Copy to top screen\0Panel copy\0")) {
                            p.presentation=render_mode!=0; p.panel_copy=render_mode==2;
                            if(p.panel_copy) { p.atlas_selection=false; p.atlas_grids.clear(); }
                            changed=true;
                        }
                        if (p.presentation) {
                            int source_screen = p.source_top ? 0 : 1;
                            if (ImGui::Combo("Presentation source", &source_screen,
                                             "Top screen\0Bottom screen\0")) {
                                p.source_top = source_screen == 0;
                                changed = true;
                            }
                        }
                        std::vector<const char*> context_items{"None"};
                        std::vector<std::string> context_ids{""};
                        for (const auto& candidate : profiles) {
                            if (candidate.id == p.id || candidate.rules.empty() || candidate.panel_copy) continue;
                            context_ids.push_back(candidate.id);
                        }
                        context_items.reserve(context_ids.size());
                        if(!p.required_context.empty() && std::find(context_ids.begin(),context_ids.end(),p.required_context)==context_ids.end())
                            context_ids.push_back(p.required_context);
                        for (size_t i = 1; i < context_ids.size(); ++i)
                            context_items.push_back(context_ids[i].c_str());
                        int context_index = 0;
                        for (int i = 1; i < int(context_ids.size()); ++i)
                            if (context_ids[i] == p.required_context) context_index = i;
                        if (ImGui::Combo("Visible when", &context_index, context_items.data(),
                                         int(context_items.size()))) {
                            p.required_context = context_ids[context_index];
                            changed = true;
                        }
                        ImGui::TextWrapped("Presentation appears only while this context profile matches.");
                        if(ImGui::TreeNode("Fade source and diagnostics")) {
                            std::vector<std::string> fade_ids{""};
                            for(const auto& candidate:profiles)
                                if(candidate.id!=p.id && !candidate.panel_copy && !candidate.rules.empty())
                                    fade_ids.push_back(candidate.id);
                            if(!p.fade_source.empty() && std::find(fade_ids.begin(),fade_ids.end(),p.fade_source)==fade_ids.end())
                                fade_ids.push_back(p.fade_source);
                            std::vector<const char*> labels{"None"};
                            int selected=0;
                            for(size_t i=1;i<fade_ids.size();++i) {
                                labels.push_back(fade_ids[i].c_str());
                                if(fade_ids[i]==p.fade_source) selected=int(i);
                            }
                            if(ImGui::Combo("Fade source group",&selected,labels.data(),int(labels.size()))) {
                                p.fade_source=fade_ids[selected]; p.follow_fade=false; changed=true;
                            }
                            const auto probe=Profiles::LastFadeProbe(p.fade_source.empty()?p.id:p.fade_source);
                            ImGui::Text("Source matched draws last frame: %u",probe.draws);
                            for(int i=0;i<7;++i)
                                ImGui::Text("%s %d alpha: %.3f .. %.3f",i==6?"Blend":"TEV",i==6?0:i,
                                    probe.minimum[i],probe.maximum[i]);
                            changed |= ImGui::Combo("Alpha channel",&p.fade_channel,
                                "TEV 0\0TEV 1\0TEV 2\0TEV 3\0TEV 4\0TEV 5\0Blend constant\0");
                            ImGui::BeginDisabled(!p.presentation || p.fade_source.empty() || p.signal_only);
                            changed |= ImGui::Checkbox("Follow verified alpha channel",&p.follow_fade);
                            ImGui::EndDisabled();
                            ImGui::TextWrapped("Raw alpha constants, not measured final opacity. Open/close the menu and verify a channel follows its fade before enabling. Absent or conflicting source draws hide the copy. Applies to copies only; does not stop source scrolling or replace its own fade.");
                            ImGui::TreePop();
                        }
                        if (p.panel_copy) {
                            changed |= ImGui::SliderFloat("Panel width",&p.scale,.1f,1.f);
                            changed |= ImGui::SliderFloat("Panel X",&p.x,0.f,1.f);
                            changed |= ImGui::SliderFloat("Panel Y",&p.y,0.f,1.f);
                            const int w=p.source_top?400:320;
                            int l=int(std::lround(p.crop[0]*w)),t=int(std::lround(p.crop[1]*240));
                            int r=int(std::lround(p.crop[2]*w)),b=int(std::lround(p.crop[3]*240));
                            bool crop=ImGui::SliderInt("Source left (pixels)",&l,0,r-1);
                            crop |= ImGui::SliderInt("Source top (pixels)",&t,0,b-1);
                            crop |= ImGui::SliderInt("Source right (pixels)",&r,l+1,w);
                            crop |= ImGui::SliderInt("Source bottom (pixels)",&b,t+1,240);
                            if(crop) { p.crop={float(l)/w,float(t)/240,float(r)/w,float(b)/240}; changed=true; }
                            ImGui::Text("Excluded draw rules: %u",unsigned(p.exclusions.size()));
                            if(ImGui::Button("Clear background exclusions")) {p.exclusions.clear();changed=true;}
                            ImGui::TextWrapped("To exclude a background: keep this group selected, capture/select its texture in Select, then Exclude selected from panel copy. Return here to Save group. Original screen remains intact. Unsupported draws may be missing.");
                        } else changed |= EditTransform(p);
                        ImGui::TextUnformatted("Transition: instant. Fade / freeze / move are not implemented.");
                        if (changed) {
                            Profiles::Update(p);
                            if (inspection_paused.load()) step_requested.store(true);
                        }
                        if (ImGui::Button("Save group")) state.status = QString::fromStdString(Profiles::Save(p));
                        if (!p.panel_copy && ImGui::Button("Edit selection in Select tab")) {
                            const auto keys = Profiles::CapturedKeys(p);
                            if (keys.empty()) state.status = "Capture textures while this group is visible first";
                            else {
                                Textures::SetSelection(keys);
                                state.draft = p;
                                state.atlas_index=-1;
                                const auto folder=p.id.substr(0,p.id.find('/'));
                                const char* names[]={"title-screen","file-select","hud","menus","notebook"};
                                for(int n=0;n<5;++n) if(folder==names[n]) state.group_folder=n;
                                std::snprintf(state.group_name, sizeof(state.group_name), "%s", p.name.c_str());
                                state.status = "Group selected; open Select";
                            }
                        }
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Select")) {
                picker_tab = true;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced")) {
                advanced_tab = true;
        ImGui::TextUnformatted("Azahar-derived application | developer slice");
        ImGui::TextUnformatted("MM3D semantic mode: INACTIVE (exact USA revision unverified)");
        ImGui::Text("Host FPS (present submissions): %.2f", state.timing.host_fps);
        ImGui::Text("Guest FPS (GSP submissions): %.2f", state.timing.guest_fps);
        ImGui::Text("Host frame: %llu | Guest frame: %llu",
            static_cast<unsigned long long>(state.timing.host_frames),
            static_cast<unsigned long long>(guest_frames.load()));
        ImGui::Text("CPU present interval: %.3f ms | max: %.3f ms",
            state.timing.interval_ms, state.timing.max_interval_ms);
        ImGui::TextWrapped("GSP submissions are not verified simulation ticks. These timings do not measure GPU completion or input-to-photon latency.");
        if(ImGui::Button("Capture interpolation performance (30 seconds)"))
            InterpolationPerformance::Start(CameraMidpoint::enabled.load(),CameraMidpoint::present_60.load());
        ImGui::TextWrapped("Starts after 3 seconds. Close settings and stay in the same scene. Capture once with interpolation off, then again with it on, using the same resolution and draw distances. CPU timings and completed GPU submission timings go to the emulator log. GPU timestamps require device support; these are not display latency measurements. Avoid other diagnostic captures during this comparison.");
        if(InterpolationPerformance::pending.load())ImGui::TextUnformatted("Performance capture pending or active.");
        if(ImGui::Button("Trace input timing (30 seconds)")) InputLatency::Start();
        ImGui::TextWrapped("Close F1, then press and release a keyboard key mapped to A several times, about one second apart. Then repeat with the controller. Timing is written to the emulator log; it does not identify the first visibly responding frame.");
        if(InputLatency::Active())ImGui::TextUnformatted("Input timing capture active.");
        ImGui::Separator();
        ImGui::TextUnformatted("Scene/room, camera matrices, draw identity: unavailable");
        ImGui::TextUnformatted("Interpolation resets / unsupported draws: not instrumented");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Overrides")) {
                if(ImGui::CollapsingHeader("Live widescreen fade test")) {
                    ImGui::TextWrapped("Capture, trigger a transition, then choose a candidate. Test removes widescreen compression for matching draws for 30 seconds. Other UI sharing that draw signature can also change.");
                    if(ImGui::Button("Capture transition draws (30 s)")) WidescreenProbe::Capture();
                    ImGui::SameLine();
                    if(ImGui::Button("Stop / reset live test")) WidescreenProbe::Stop();
                    std::scoped_lock probe_lock(WidescreenProbe::mutex);
                    ImGui::Text("Capture: %s | candidates: %u",WidescreenProbe::Now()<WidescreenProbe::capture_until?"running":"stopped",unsigned(WidescreenProbe::candidates.size()));
                    if(ImGui::Button("Export widescreen draw report")) {
                        QJsonArray draws;
                        for(const auto& c:WidescreenProbe::candidates) {
                            const auto& d=c.draw;QJsonArray scissor;
                            for(int v:c.scissor)scissor.append(v);
                            draws.append(QJsonObject{{"shader",QString::number(d.shader,16)},
                                {"swizzle",QString::number(d.swizzle,16)},{"vertices",int(d.vertices)},
                                {"width",int(d.width)},{"height",int(d.height)},{"textures",int(d.textures)},
                                {"blend",int(d.blend)},{"depth",int(d.depth)},
                                {"observations",int(c.observations)},{"scissor",scissor}});
                        }
                        QSaveFile file(state.directory+"widescreen-draws.json");
                        const auto data=QJsonDocument(QJsonObject{{"draws",draws}}).toJson();
                        const bool saved=file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();
                        state.status=saved?"Widescreen draw report saved":"Widescreen draw report save FAILED";
                    }
                    if(ImGui::BeginListBox("Candidate draw",ImVec2(-1,140))) {
                        for(int i=0;i<int(WidescreenProbe::candidates.size());++i) {
                            const auto& c=WidescreenProbe::candidates[i];
                            char label[160];
                            std::snprintf(label,sizeof(label),"#%d %s | %u vertices | shader %016llx | %u sightings",i,
                                c.draw.textures?"Textured":"Untextured",c.draw.vertices,
                                static_cast<unsigned long long>(c.draw.shader),c.observations);
                            if(ImGui::Selectable(label,WidescreenProbe::selected==i)) {
                                WidescreenProbe::selected=i; WidescreenProbe::test_until=0;
                            }
                        }
                        ImGui::EndListBox();
                    }
                    ImGui::BeginDisabled(WidescreenProbe::selected<0);
                    if(ImGui::Button("Test selected: fill widescreen (30 s)")) {
                        WidescreenProbe::bypassed=0; WidescreenProbe::test_until=WidescreenProbe::Now()+30000;
                    }
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(WidescreenProbe::selected<0);
                    if(ImGui::Button("Add selected to saved fullscreen effects")) {
                        auto next=state.settings;
                        const auto draw=WidescreenProbe::candidates[WidescreenProbe::selected].draw;
                        if(draw!=WidescreenProbe::TransitionOverlay && std::find(next.fullscreen_draws.begin(),next.fullscreen_draws.end(),draw)==next.fullscreen_draws.end())
                            next.fullscreen_draws.push_back(draw);
                        if(QDir().mkpath(state.directory) && next.Save(state.directory+"presentation.json")) {
                            state.settings=next;
                            WidescreenProbe::saved_draws=next.fullscreen_draws;
                            WidescreenProbe::test_until=0;
                        } else state.status="Fullscreen draw save FAILED";
                    }
                    ImGui::EndDisabled();
                    ImGui::TextUnformatted("Scene-transition fix: built in and always retained.");
                    ImGui::Text("Saved fullscreen effects: %u",unsigned(state.settings.fullscreen_draws.size()));
                    for(unsigned i=0;i<state.settings.fullscreen_draws.size();++i){
                        const auto& d=state.settings.fullscreen_draws[i];ImGui::PushID(int(i)+2100);
                        ImGui::Text("%u: %u vertices | textures %u | blend %u | depth %u",i+1,d.vertices,d.textures,d.blend,d.depth);
                        ImGui::SameLine();
                        bool removed=false;
                        if(ImGui::Button("Remove this effect")){
                            auto next=state.settings;next.fullscreen_draws.erase(next.fullscreen_draws.begin()+i);
                            if(next.Save(state.directory+"presentation.json")){
                                state.settings=next;WidescreenProbe::saved_draws=next.fullscreen_draws;
                                WidescreenProbe::test_until=0;removed=true;
                            }else state.status="Fullscreen effect removal FAILED";
                        }
                        ImGui::PopID();if(removed)break;
                    }
                    ImGui::Text("Test: %s | draws bypassed: %u",WidescreenProbe::Now()<WidescreenProbe::test_until?"active":"off",WidescreenProbe::bypassed);
                    if(WidescreenProbe::selected>=0) {
                        const auto& s=WidescreenProbe::candidates[WidescreenProbe::selected].scissor;
                        ImGui::Text("Last draw scissor L/T/R/B: %d / %d / %d / %d",s[0],s[1],s[2],s[3]);
                    }
                    ImGui::TextWrapped("Candidates are small top-screen draws without depth writes. Start with untextured candidates; no match does not prove there is no fade. This live test changes no archive and expires automatically.");
                }
                ImGui::SeparatorText("Scene transition FadeMask (restart required)");
                ImGui::InputText("FadeMask source GAR",state.fade_source,sizeof(state.fade_source));
                state.notebook.fade_source=state.fade_source;
                ImGui::InputFloat("FadeMask width",&state.notebook.fade_width,1.f,10.f,"%.3f");
                if(ImGui::Button("800 test width")) state.notebook.fade_width=800.f;
                ImGui::SameLine();
                if(ImGui::Button("16:9 candidate width")) state.notebook.fade_width=400.f*16.f/15.f;
                ImGui::SameLine();
                if(ImGui::Button("400 original width")) state.notebook.fade_width=400.f;
                ImGui::TextWrapped("Changes only the 400x240 FadeMask pane width. 800 is a coverage test; 426.667 is the candidate for our 16:9 correction. Original dump remains untouched.");
                if(ImGui::Button("Install FadeMask override")) {
                    const auto result=Notebook::InstallFadeOverride(state.notebook.fade_source,state.notebook.fade_width);
                    state.status=QString::fromStdString(result.message);
                    if(result.ok && !(QDir().mkpath(state.directory) && Notebook::Save(state.notebook,(state.directory+"notebook.json").toStdString())))
                        state.status+="; settings save FAILED";
                }
                ImGui::SameLine();
                if(ImGui::Button("Restore original FadeMask"))
                    state.status=QString::fromStdString(Notebook::RemoveFadeOverride().message);
                ImGui::TextUnformatted(QFileInfo(QString::fromStdString(Notebook::FadeOverridePath())).isFile()
                    ?"FadeMask override present (restart after changes)":"FadeMask: original archive active");
                ImGui::TextWrapped("%s", state.status.toUtf8().constData());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (advanced_tab) ImGui::Text("Layer draws: matched %u | copied %u | unsupported %u",
            prompt_matches.load(), prompt_replays.load(), prompt_unsupported.load());
        if (picker_tab) {
        ImGui::TextUnformatted("Select textures or atlas regions, adjust, then save a group.");
        if (ImGui::Button("Exclude selected from panel copy")) {
            const auto selected = Profiles::Selection(Textures::SelectedKeys());
            bool saved_panel=false;
            for(auto p:Profiles::List()) if(p.id==state.active_profile.toStdString() && p.panel_copy) {
                saved_panel=true;
                for(const auto& r:selected.rules)
                    if(std::find(p.exclusions.begin(),p.exclusions.end(),r)==p.exclusions.end()) p.exclusions.push_back(r);
                Profiles::Update(p);
                state.status=selected.rules.empty()?"Capture/select the background first":
                    "Exclusions updated; return to Groups and Save group";
            }
            if(!saved_panel) {
            std::lock_guard lock(Notebook::exclusions_mutex);
            Notebook::exclusions = selected.rules;
            Notebook::excluded_draws.store(0);
            state.status = selected.rules.empty() ? "Capture/select the background first" :
                "Background excluded from transparent panel copy (session only)";
            }
        }
        if (ImGui::Button("Clear panel exclusions")) {
            bool saved_panel=false;
            for(auto p:Profiles::List()) if(p.id==state.active_profile.toStdString() && p.panel_copy) {
                p.exclusions.clear(); Profiles::Update(p); saved_panel=true;
                state.status="Exclusions cleared; return to Groups and Save group";
            }
            if(!saved_panel) {
            std::lock_guard lock(Notebook::exclusions_mutex);
            Notebook::exclusions.clear();
            }
        }
        if (ImGui::Button("Capture textures")) Textures::Start();
        ImGui::SameLine();
        if (ImGui::Button("Restore draws")) {
            Textures::Restore();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear captures")) {
            Textures::Clear();
            // Previous submitted frames may still sample these descriptors.
            std::unique_lock<std::mutex> lock;
            if (state.submit_mutex) lock = std::unique_lock<std::mutex>(*state.submit_mutex);
            state.instance.GetDevice().waitIdle();
            for (auto set : state.thumbnail_sets) ImGui_ImplVulkan_RemoveTexture(set);
            state.thumbnail_sets.clear();
            state.thumbnails.clear();
            state.atlas_index=-1;
        }
        const auto captured = Textures::Snapshot();
        while (state.thumbnails.size() < captured.size()) {
            const auto& thumbnail = captured[state.thumbnails.size()];
            state.thumbnail_sets.push_back(ImGui_ImplVulkan_AddTexture(state.thumbnail_sampler,
                thumbnail->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
            state.thumbnails.push_back(thumbnail);
        }
        ImGui::Text("Textures: %u / %u | hidden draws: %u", unsigned(state.thumbnails.size()),
            Textures::Limit, Textures::hidden.load());
        ImGui::SameLine();
        ImGui::TextUnformatted(DrawProbe::Now() < Textures::capture_until.load() ? "Capturing (C)" : "C: capture");
        ImGui::TextWrapped("Click thumbnails to select/deselect whole textures. Use Edit grid to select atlas cells. Save group stores metadata only.");
        if(ImGui::Checkbox("Atlas region selection",&state.draft.atlas_selection)) {
            if(!state.draft.atlas_selection) state.draft.atlas_grids.clear();
        }

        if (ImGui::BeginChild("Texture thumbnails", {0, 235}, ImGuiChildFlags_Borders)) {
            static ImGuiID preview_id = 0;
            static double preview_started = 0.0;
            for (unsigned i = 0; i < state.thumbnails.size(); ++i) {
                const auto& thumbnail = state.thumbnails[i];
                ImGui::PushID(int(i));
                if (i % 3) ImGui::SameLine();
                ImGui::BeginGroup();
                const float scale = 112.f / std::max(thumbnail->width, thumbnail->height);
                const bool selected = Textures::IsSelected(thumbnail->key);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{.15f, .55f, .3f, 1.f});
                if (ImGui::ImageButton("texture", (ImTextureID)state.thumbnail_sets[i],
                    {thumbnail->width * scale, thumbnail->height * scale}, {0,1}, {1,0},
                    {0.25f,0.25f,0.25f,1})) {
                    Textures::ToggleSelection(thumbnail->key);
                }
                if (selected) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    const ImGuiID id = ImGui::GetItemID();
                    if (preview_id != id) {
                        preview_id = id;
                        preview_started = ImGui::GetTime();
                    }
                    if (ImGui::GetTime() - preview_started >= 2.0) {
                        ImGui::BeginTooltip();
                        const float preview_scale = 320.f /
                            std::max(thumbnail->width, thumbnail->height);
                        ImGui::Image((ImTextureID)state.thumbnail_sets[i],
                            {thumbnail->width * preview_scale, thumbnail->height * preview_scale},
                            {0, 1}, {1, 0});
                        ImGui::Text("Texture #%u | %ux%u", i, thumbnail->width, thumbnail->height);
                        ImGui::EndTooltip();
                    }
                } else if (preview_id == ImGui::GetItemID()) {
                    preview_id = 0;
                }
                ImGui::Text("#%u %ux%u%s", i, thumbnail->width, thumbnail->height,
                    Textures::IsSelected(thumbnail->key) ? " *" : "");
                if(ImGui::Button("Edit grid")) {
                    state.atlas_index=int(i);
                    state.atlas_editor_open=true;
                    state.draft.atlas_selection=true;
                    if(!Textures::IsSelected(thumbnail->key)) Textures::ToggleSelection(thumbnail->key);
                }
                ImGui::EndGroup(); ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if(state.draft.atlas_selection) {
            ImGui::TextWrapped("Atlas regions: scale on the original screen, or copy lower-screen draws to the top with Presentation layer. Up to 64 regions per draw; unsupported paths stay original.");
            if(state.atlas_index>=0 && size_t(state.atlas_index)<state.thumbnails.size()) {
                if(ImGui::Button("Open atlas editor")) state.atlas_editor_open=true;
                ImGui::SetNextWindowSize({850,750},ImGuiCond_FirstUseEver);
                if(state.atlas_editor_open) {
                if(ImGui::Begin("Atlas grid editor",&state.atlas_editor_open)) {
                { std::scoped_lock lock{input_mutex}; editor_position=ImGui::GetWindowPos(); editor_size=ImGui::GetWindowSize(); }
                const auto& t=state.thumbnails[state.atlas_index];
                const auto identity=Profiles::Selection({t->key});
                if(identity.rules.empty()) ImGui::TextWrapped("Capture this texture again to obtain its content identity.");
                else {
                    auto& grids=state.draft.atlas_grids;
                    auto it=std::find_if(grids.begin(),grids.end(),[&](const auto& g){return g.hash==identity.rules[0].hash && g.width==t->width && g.height==t->height;});
                    if(it==grids.end()) {
                        Profiles::Profile::AtlasGrid g;
                        g.hash=identity.rules[0].hash; g.width=t->width; g.height=t->height;
                        grids.push_back(g); it=std::prev(grids.end());
                    }
                    ImGui::Text("Editing atlas #%d",state.atlas_index);
                    Textures::RequestDetail(t->key,identity.rules[0].hash);
                    const auto detail=Textures::Detail();
                    if(detail!=state.atlas_detail) {
                        if(state.atlas_detail_set) {
                            // Presentations can overlap. Finish readers before
                            // freeing the descriptor and its captured image.
                            std::unique_lock<std::mutex> lock;
                            if (state.submit_mutex) lock = std::unique_lock<std::mutex>(*state.submit_mutex);
                            state.instance.GetDevice().waitIdle();
                            ImGui_ImplVulkan_RemoveTexture(state.atlas_detail_set);
                        }
                        state.atlas_detail_set={}; state.atlas_detail=detail;
                        if(detail) state.atlas_detail_set=ImGui_ImplVulkan_AddTexture(state.thumbnail_sampler,
                            detail->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    }
                    if(detail) {
                        ImGui::Text("Full-resolution capture: %u x %u",detail->width,detail->height);
                        EditAtlasGrid(*it,(ImTextureID)state.atlas_detail_set,state.atlas_zoom,*detail);
                    } else {
                        ImGui::TextWrapped("Waiting for full-resolution capture. The texture must be drawn again; resume or advance while it is visible.");
                        if(inspection_paused.load() && ImGui::Button("Advance 2 frames for detail")) step_requested.store(true);
                    }
                }
                }
                ImGui::End();
                }
            } else ImGui::TextUnformatted("Choose Edit grid beneath a texture.");
        }
        ImGui::Text("Selected: %u", Textures::SelectionCount());
        bloom_control();
        ImGui::Text("Bloom texture bindings corrected: %u", BloomSampling::bindings.load());
        static bool hide_dynamic_buffers = false;
        ImGui::Checkbox("Hide by address (animated effects)", &hide_dynamic_buffers);
        if (hide_dynamic_buffers)
            ImGui::TextWrapped("Temporary diagnostic: hides draws using the selected address, size and format, even when pixels change. Restore draws cancels it. Recapture after changing scenes.");
        if (ImGui::CollapsingHeader("Advanced capture diagnostics")) {
        static int recording_phase = 0;
        ImGui::Combo("Recording phase", &recording_phase, "Intro\0Return\0");
        if (ImGui::Button("Record selected draws")) {
            QDir().mkpath(state.directory);
            const std::string phase = recording_phase == 0 ? "intro" : "return";
            Textures::StartRecording((state.directory + QString::fromStdString("logo-" + phase + ".log")).toStdString(), phase);
        }
        ImGui::TextWrapped("%s", Textures::RecordingStatus().c_str());
        ImGui::TextWrapped("Records 5 running seconds. Resume to record; GPU synchronization may briefly stutter.");
        }
        ImGui::BeginDisabled(Textures::SelectionCount() == 0 || state.draft.atlas_selection);
        if (ImGui::Button(inspection_paused.load() ? "Hide selected + advance 2 frames" :
                          "Hide selected textures for 5 seconds")) {
            if (hide_dynamic_buffers) {
                QDir().mkpath(state.directory);
                Textures::StartRecording((state.directory + "effect-hide.log").toStdString(), "effect-hide");
            }
            Textures::HideSelected(hide_dynamic_buffers);
            // Human-readable record of requested suppression, not visual confirmation.
            QByteArray entry = (QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) +
                (hide_dynamic_buffers ? " Address hide requested (selection below; disappearance not automatically verified)\n" :
                 " Content hide requested (selection below; disappearance not automatically verified)\n")).toUtf8();
            for (unsigned i = 0; i < state.thumbnails.size(); ++i) {
                const auto& texture = state.thumbnails[i];
                if (!Textures::IsSelected(texture->key)) continue;
                entry += QString("Hidden: texture #%1 | %2x%3 | key=0x%4\n")
                    .arg(i).arg(texture->width).arg(texture->height)
                    .arg(qulonglong(texture->key), 0, 16).toUtf8();
            }
            entry += '\n';
            QFile log(state.directory + "hidden-textures.log");
            const bool saved = QDir().mkpath(state.directory) &&
                log.open(QIODevice::WriteOnly | QIODevice::Append) &&
                log.write(entry) == entry.size() && log.flush();
            state.status = saved ? "Saved hidden-textures.log" : "Could not write hidden-textures.log";
        }
        ImGui::EndDisabled();
        if(!state.draft.atlas_selection) ImGui::TextUnformatted("Selection and transform changes preview automatically. Save group to keep them.");
        auto& draft = state.draft;
        int mode = draft.presentation ? 1 : 0;
        if (ImGui::Combo("Placement", &mode, "Adjust original\0Copy to top screen\0")) draft.presentation = mode == 1;
        EditTransform(draft);
        auto keys = Textures::SelectedKeys();
        std::sort(keys.begin(), keys.end());
        const auto identity = Profiles::Selection(keys);
        draft.keys = keys;
        draft.rules = identity.rules;
        if(draft.atlas_selection) {
            // Only selected atlases with chosen cells belong to this draft.
            std::erase_if(draft.atlas_grids,[&](const auto& g){return std::none_of(draft.rules.begin(),draft.rules.end(),[&](const auto& r){return r.hash==g.hash && r.width==g.width && r.height==g.height;});});
        }
        draft.enabled = true;
        draft.id = "@preview";
        if (!(draft == state.last_preview)) {
            const bool had_preview = !state.last_preview.rules.empty();
            Profiles::Preview(draft);
            state.last_preview = draft;
            if (inspection_paused.load() && (had_preview || !draft.rules.empty())) step_requested.store(true);
        }
        ImGui::Text("Captured identity rules: %u", unsigned(draft.rules.size()));
        if (!keys.empty() && draft.rules.empty())
            ImGui::TextWrapped("Identity unavailable. Capture again while these textures are visible; no address fallback is applied.");
        const char* folders[] = {"title-screen", "file-select", "hud", "menus", "notebook"};
        ImGui::InputText("Group name", state.group_name, sizeof(state.group_name));
        ImGui::Combo("Folder", &state.group_folder, folders, IM_ARRAYSIZE(folders));
        if (ImGui::Button("Save group")) {
            auto p = draft;
            p.name = state.group_name;
            p.id = std::string(folders[state.group_folder]) + "/" + p.name + ".json";
            for (const auto& existing : Profiles::List())
                if (existing.id == p.id) p.enabled = existing.enabled;
            state.status = QString::fromStdString(Profiles::Save(p));
        }
        bool compatible_profile=false;
        for(const auto& p:Profiles::List()) if(p.id==state.active_profile.toStdString()) compatible_profile=p.atlas_selection==draft.atlas_selection;
        ImGui::BeginDisabled(state.active_profile.isEmpty() || draft.rules.empty() || !compatible_profile);
        if (ImGui::Button("Add selected to active profile")) {
            for (auto p : Profiles::List()) {
                if (p.id != state.active_profile.toStdString()) continue;
                for (const auto& r : draft.rules)
                    if (std::find(p.rules.begin(),p.rules.end(),r) == p.rules.end()) p.rules.push_back(r);
                for (auto k : draft.keys)
                    if (std::find(p.keys.begin(),p.keys.end(),k) == p.keys.end()) p.keys.push_back(k);
                for(const auto& grid:draft.atlas_grids) {
                    auto it=std::find_if(p.atlas_grids.begin(),p.atlas_grids.end(),[&](const auto& g){return g.hash==grid.hash && g.width==grid.width && g.height==grid.height && g.columns==grid.columns && g.rows==grid.rows;});
                    if(it==p.atlas_grids.end()) p.atlas_grids.push_back(grid);
                    else {
                        for(int cell:grid.cells) if(std::find(it->cells.begin(),it->cells.end(),cell)==it->cells.end()) it->cells.push_back(cell);
                        for(const auto& r:grid.rectangles) if(std::find(it->rectangles.begin(),it->rectangles.end(),r)==it->rectangles.end()) it->rectangles.push_back(r);
                    }
                }
                state.status = QString::fromStdString(Profiles::Save(p));
            }
        }
        ImGui::EndDisabled();
        if (inspection_paused.load()) {
            ImGui::TextUnformatted("Paused: GUI stays live. Hide timer starts when you resume.");
            if (ImGui::Button("Advance 2 frames")) step_requested.store(true);
        }
        }
        ImGui::Separator();
        if (advanced_tab) {
        ImGui::Checkbox("Developer indicator", &state.settings.indicator);
        ImGui::SliderFloat("Opacity", &state.settings.opacity, .2f, 1.f);
        ImGui::SliderFloat("Indicator X (normalized)", &state.settings.x, 0.f, .75f);
        ImGui::SliderFloat("Indicator Y (normalized)", &state.settings.y, 0.f, .9f);
        ImGui::TextUnformatted("All available settings apply live; no restart needed.");
        ImGui::Separator();
        ImGui::TextUnformatted("Form camera capture (Link/Goron/Zora/Deku)");
        ImGui::TextWrapped("Start before reproducing a camera issue, then stop afterward. For Deku, include burrowing, flight, C-stick movement, petal release and landing.");
        if (!MM3D::FormCamera::diagnostics.load()) {
            if (ImGui::Button("Start form camera capture")) MM3D::FormCamera::diagnostics.store(true);
        } else {
            if (ImGui::Button("Stop form camera capture")) MM3D::FormCamera::diagnostics.store(false);
            ImGui::SameLine(); ImGui::TextUnformatted("Recording until stopped");
        }
        if (ImGui::Button("Save local settings")) {
            const bool ok = QDir().mkpath(state.directory) && state.settings.Save(state.directory + "presentation.json");
            state.status = ok ? "Settings saved" : "Settings save FAILED";
        }
        if (ImGui::Button("Export non-ROM diagnostics")) {
            const auto json = QJsonDocument(QJsonObject{{"schema", 1},
                {"mm3d_semantic_mode", false}, {"host_fps", state.timing.host_fps},
                {"guest_gsp_fps", state.timing.guest_fps},
                {"host_frames", double(state.timing.host_frames)},
                {"guest_gsp_frames", double(guest_frames.load())},
                {"present_interval_ms", state.timing.interval_ms},
                {"max_present_interval_ms", state.timing.max_interval_ms},
                {"width", int(width)}, {"height", int(height)}}).toJson();
            QDir().mkpath(state.directory);
            QSaveFile file(state.directory + "diagnostics.json");
            const bool ok = file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
            state.status = ok ? "Exported to " + state.directory + "diagnostics.json" : "Export FAILED";
        }
        }
        ImGui::TextWrapped("%s", state.status.toUtf8().constData());
        ImGui::EndTabItem();}
        ImGui::EndTabBar();}
        }
#endif
        ImGui::End();
    }
    if(!state.camera_writer_path.isEmpty()) {
        CameraWriter::Status();
        if(!CameraWriter::active.load()) {
            const bool ok = QDir().mkpath(state.directory) && CameraWriter::Export(state.camera_writer_path);
            state.status = ok ? "CPU writer trace exported to " + state.camera_writer_path :
                "CPU writer export FAILED: " + state.camera_writer_path;
            state.camera_writer_path.clear();
        }
    }
    if(!state.effect_capture_path.isEmpty()) {
        EffectCapture::Poll();
        if(!EffectCapture::active.load()) {
            const bool ok=QDir().mkpath(state.directory)&&EffectCapture::Export(state.effect_capture_path.toStdString());
            state.status=ok?"Effect capture exported to "+state.effect_capture_path:"Effect capture FAILED: "+state.effect_capture_path;
            state.effect_capture_path.clear();
        }
    }
    if(!state.link_pose_path.isEmpty()) {
        LinkPoseCapture::Poll();
        if(!LinkPoseCapture::active.load()) {
            const bool ok=QDir().mkpath(state.directory)&&LinkPoseCapture::Export(state.link_pose_path.toStdString());
            state.status=ok?"Link capture exported to "+state.link_pose_path:"Link capture FAILED: "+state.link_pose_path;
            state.link_pose_path.clear();
        }
    }
    if(!state.camera_capture_path.isEmpty()) {
        CameraCapture::Status(); // Also enforce timeout while F1 is closed or the guest is paused.
        if(!CameraCapture::active.load()) {
            const bool ok = QDir().mkpath(state.directory) &&
                CameraCapture::Export(state.camera_capture_path, state.camera_capture_label);
            state.status = ok ? "Camera capture exported to " + state.camera_capture_path :
                "Camera export FAILED: " + state.camera_capture_path;
            state.camera_capture_path.clear();
        }
    }
    if (state.startup_trace) {
        Trace::Status();
        if (!Trace::active.load()) {
            QDir().mkpath(state.directory);
            Trace::Export(state.directory + "intro-trace.json");
            state.startup_trace = false;
        }
    }
    // Classic owl confirmation remains part of the game image while the
    // entire image fades. Acknowledge black before the guest exits gameplay.
    {
        using ClassicSaving::ExitFade;
        static ExitFade previous=ExitFade::Idle;
        static float alpha=0,elapsed=0;
        const auto phase=ClassicSaving::exit_fade.load();
        if(phase!=previous){elapsed=0;previous=phase;}
        const float delta=std::clamp(io.DeltaTime,0.f,0.1f);
        if(phase!=ExitFade::In)elapsed+=delta;
        if(phase==ExitFade::Idle)alpha=0;
        if(phase==ExitFade::Out)alpha=std::min(1.f,elapsed/0.8f);
        if(phase==ExitFade::Black)alpha=1;
        if(phase==ExitFade::In) {
            alpha=ClassicSaving::TitleHandoffAlpha(elapsed,delta,ClassicSaving::exit_title_ready.load());
            if(alpha==0)ClassicSaving::exit_fade=ExitFade::Idle;
        }
        // Gameplay fade is a native black pane BELOW the retained textbox.
        // After exiting gameplay, use the host overlay for the title handoff.
        ClassicSaving::exit_world_alpha=(phase==ExitFade::Out||phase==ExitFade::Black)?alpha:0.f;
        Music::transition_gain=1.f-alpha;
        if(alpha>0 && phase==ExitFade::In)ImGui::GetForegroundDrawList()->AddRectFilled(
            ImVec2(0,0),io.DisplaySize,IM_COL32(0,0,0,int(alpha*255)));
        ClassicSaving::exit_black_rendered=(phase==ExitFade::Out && alpha==1 &&
            ClassicSaving::exit_world_black_drawn.exchange(false));
    }
    ImGui::Render();
    text_input_active.store(overlay_visible.load() && io.WantTextInput);
    const vk::RenderPassBeginInfo begin{.renderPass = state.renderpass, .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {width, height}}};
    command.beginRenderPass(begin, vk::SubpassContents::eInline);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
    command.endRenderPass();
}
void Overlay::Presented() {
    if(ClassicSaving::exit_black_rendered.exchange(false)) {
        auto expected=ClassicSaving::ExitFade::Out;
        ClassicSaving::exit_fade.compare_exchange_strong(expected,ClassicSaving::ExitFade::Black);
    }
    impl->timing.Present(Now(), guest_frames.load());
    host_present_attempts.fetch_add(1);
}
} // namespace MM3D
