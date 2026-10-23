#ifdef ENABLE_MM3D_PRESENTATION
#include "mm3d/interpolation_performance.h"
#endif
// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#ifdef ENABLE_MM3D_PRESENTATION
#include "mm3d/classic_double_time.h"
#include "mm3d/notebook_inspector.h"
#include "mm3d/camera_instruction_probe.h"
#include "mm3d/form_camera.h"
#include "mm3d/fast_time.h"
#include "mm3d/camera_live.h"
#include "mm3d/camera_midpoint.h"
#include "mm3d/link_pose_capture.h"
#include "mm3d/diagnostics.h"
#include "mm3d/minimap.h"
#include "mm3d/native_minimap.h"
#include "mm3d/minimap_shortcut.h"
#include "mm3d/native_inventory.h"
#include "mm3d/dungeon_map.h"
#include "mm3d/difficulty.h"
#include "mm3d/actor_draw_distance.h"
#include "mm3d/prop_draw_distance.h"
#include "mm3d/vegetation_draw_distance.h"
#include "mm3d/death_penalties.h"
#include "mm3d/classic_save_hooks.h"
#include "mm3d/bottle_healing.h"
#include "mm3d/inventory_shadow.h"
#include "mm3d/items_map_prompt.h"
#include "mm3d/menu_help.h"
#include "mm3d/world_map_prompt.h"
#include "mm3d/menu_tabs.h"
#include "mm3d/pause_capture.h"
#include "mm3d/photo_capture.h"
#include "mm3d/visibility_depth.h"
#include "mm3d/native_notebook.h"
#include "mm3d/native_file_select.h"
#include "mm3d/file_select_interpolation.h"
#include "mm3d/file_select_input.h"
#include "mm3d/file_select_prompt.h"
#include "mm3d/ocarina_prompt.h"
#include "mm3d/magic_border.h"
#include "mm3d/minigame_hud.h"
#include "mm3d/player_hud.h"
#include "mm3d/song_list.h"
#include "mm3d/double_time.h"
#include "mm3d/song_reset.h"
#include "mm3d/hud_buttons.h"
#include "mm3d/banners.h"
#include "mm3d/banner_interpolation.h"
#include "mm3d/song_effect.h"
#include "mm3d/native_item_present.h"
#include "mm3d/inventory_interpolation.h"
#include "mm3d/inventory_navigation.h"
#include "mm3d/menu_page_switch.h"
#include "mm3d/menu_memory.h"
#include "mm3d/menu_backdrop.h"
#include "mm3d/screen_shrink.h"
#include "mm3d/single_screen_text.h"
#include "mm3d/stick_controls.h"
#include "mm3d/simple_cheats.h"
#include "mm3d/prices.h"
#include "mm3d/menu_sound.h"
#include "mm3d/music.h"
#include "mm3d/tatl_reminders.h"
#include "mm3d/owl_sword.h"
#include "mm3d/owl_intro.h"
#include "mm3d/bomber_approach.h"
#include "mm3d/menu_close.h"
#include "mm3d/menu_shortcuts.h"
#include "mm3d/options_menu.h"
#include "mm3d/native_startup.h"
#include "mm3d/item_preview.h"
#include "mm3d/dialogue_scale.h"
#include "mm3d/message_layout.h"
#include "mm3d/classic_owl_draw.h"
#include "mm3d/classic_file_marker.h"
#include "mm3d/widescreen.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#endif
#include <csignal>
#include <cstring>
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/optimization_flags.h>
#include "common/assert.h"
#include "common/microprofile.h"
#include "core/arm/dynarmic/arm_dynarmic.h"
#include "core/arm/dynarmic/arm_dynarmic_cp15.h"
#include "core/arm/dynarmic/arm_exclusive_monitor.h"
#include "core/arm/dynarmic/arm_tick_counts.h"
#include "core/arm/exception_handler.h"
#include "core/core.h"
#include "core/core_timing.h"
#ifdef ENABLE_GDBSTUB
#include "core/gdbstub/gdbstub.h"
#endif
#include "core/hle/kernel/svc.h"
#include "core/memory.h"

#ifndef SIGILL
constexpr u32 SIGILL = 4;
#endif

#ifndef SIGTRAP
constexpr u32 SIGTRAP = 5;
#endif

namespace Core {

class DynarmicUserCallbacks final : public Dynarmic::A32::UserCallbacks {
public:
    explicit DynarmicUserCallbacks(ARM_Dynarmic& parent)
        : parent(parent), svc_context(parent.system), memory(parent.memory) {}
    ~DynarmicUserCallbacks() = default;

#ifdef ENABLE_MM3D_PRESENTATION
    MM3D::FormCamera::Hook form_camera;
    MM3D::FormCamera::ArrivalLens arrival_lens;
    MM3D::FormCamera::OcarinaReturnLens ocarina_return_lens;
    MM3D::FormCamera::DekuFreeCamera deku_free_camera;
    MM3D::FormCamera::EntryRecoveryOrbit entry_recovery_orbit;
    unsigned form_trace_count=0,form_projection_count=0;
    float form_last_native=-1.f,form_last_output=-1.f,form_last_radius=-1.f;
    u32 form_last_mode=~0u;
    std::array<u32,6> form_last_pose{};
    bool form_trace_was_active=false;
    MM3D::NativeMinimap::Hook minimap_hook;
    MM3D::DungeonMap::Hook dungeon_map_hook;
    MM3D::InventoryShadow::Hook inventory_shadow_hook;
    MM3D::ItemsMapPrompt::Hook items_map_prompt_hook,map_items_prompt_hook;
    MM3D::MenuHelp::Hook menu_help_hook,select_help_hook;
    MM3D::WorldMapPrompt::Hook world_map_prompt_hook;
    MM3D::MenuTabs::Hook menu_tabs_hook;
    MM3D::NativeInventory::Hook inventory_hook;
    MM3D::OptionsMenu::Hook options_hook;
    MM3D::MenuPageSwitch::Hook menu_page_switch;
    MM3D::MenuMemory::Hook menu_memory;
    MM3D::MenuBackdrop::Hook menu_backdrop;
    // Temporary editor-only observation of pause ownership and capture lifetime.
    // Never stop the emulator or write guest state; log only bounded changes.
#if !defined(ENABLE_MM3D_GAME_FOLDER) && !defined(__ANDROID__)
    std::array<std::array<u32,18>,3> backdrop_trace_previous{};
    std::array<unsigned,3> backdrop_trace_count{};
#endif
    template<class M> void TraceMenuBackdrop(M& m,unsigned phase) {
#if !defined(ENABLE_MM3D_GAME_FOLDER) && !defined(__ANDROID__)
        if(phase>=backdrop_trace_count.size()||backdrop_trace_count[phase]>=128)return;
        using H=MM3D::MenuBackdrop::Hook;
        using MM3D::NativeInventory::Pointer;
        constexpr u32 ctx=MM3D::MenuBackdrop::ScreenContext;
        const u32 g=H::Game(m,ctx),current=m.Read(ctx+8).value_or(0);
        if(!g)return;
        const bool menu=H::Page(m,current)||m.Read(0x656b10)==current||m.Read(0x656b18)==current;
        if(!menu&&!menu_backdrop.owner)return;
        const u32 capture=m.Read(0x66889c).value_or(0),graphics=m.Read(g+0x100).value_or(0);
        const auto picture=H::Source(m,g,false);
        auto state=[&](u32 p){return Pointer(p,0x188)?MM3D::MenuPageSwitch::Byte(m,p+0x186).value_or(~0u):~0u;};
        const std::array<u32,18> now{
            g,current,m.Read(ctx+12).value_or(0),m.Read(g+0xaac).value_or(~0u),
            menu_backdrop.owner,menu_backdrop.page,capture,state(capture),graphics,state(graphics),
            m.Read(g+0x11018).value_or(0),u32(H::Ready(m,g)),u32(picture.has_value()),
            u32(menu_backdrop.Active(m)),
            picture?picture->geometry[0]:0,picture?picture->geometry[1]:0,
            picture?m.Read(picture->geometry[0]+0x10).value_or(0):0,
            picture?m.Read(picture->geometry[1]+0x10).value_or(0):0};
        if(backdrop_trace_count[phase]&&now==backdrop_trace_previous[phase])return;
        backdrop_trace_previous[phase]=now;++backdrop_trace_count[phase];
        LOG_INFO(Core_ARM11,"MM3D pause trace: phase={} frame={} game={:08X} current={:08X} pending={:08X} flags={:08X} owner={:08X} owned_page={:08X} capture={:08X}/{} graphics={:08X}/{} textures={:08X} ready={} source={} active={} geometry={:08X},{:08X} bound={:08X},{:08X}",
            phase,MM3D::guest_frames.load(),now[0],now[1],now[2],now[3],now[4],now[5],
            now[6],now[7],now[8],now[9],now[10],now[11],now[12],now[13],now[14],now[15],now[16],now[17]);
#endif
    }
    u32 menu_sound_context=0;
    MM3D::PauseCapture::Hook pause_capture_hook;
    MM3D::PhotoCapture::Hook photo_capture_hook;
    MM3D::VisibilityDepth::Hook visibility_depth_hook;
    MM3D::NativeNotebook::Hook notebook_hook;
    MM3D::NativeFileSelect::Hook file_select_hook;
    MM3D::FileSelectPrompt::Hook file_select_prompt_hook;
    MM3D::OcarinaPrompt::Hook ocarina_hook;
    MM3D::MagicBorder::Hook magic_hook;
    MM3D::MinigameHud::Hook minigame_hook;
    MM3D::PlayerHud::Hook player_hud_hook;
    MM3D::SongList::Hook song_hook;
    MM3D::DoubleTime::Hook double_time_hook;
    MM3D::ClassicDoubleTime::Hook classic_double_time_hook;
    MM3D::SongReset::Hook song_reset_hook;
    MM3D::HudButtons::Hook hud_buttons_hook;
    MM3D::Banners::Hook banner_hook;
    MM3D::NativeItemPresent::Hook item_present_hook;
    MM3D::NativeStartup::Hook startup_hook;
    MM3D::LinkPoseCapture::Hook link_pose_hook;
    MM3D::NativeStartup::PressHook press_hook;
    MM3D::NativeStartup::LoadingIconHook loading_icon_hook;
    MM3D::ItemPreview::Hook item_preview_hook;
    MM3D::StickControls::Hook stick_controls_hook;
    MM3D::ClassicSaving::Hook classic_save_hook;
    MM3D::DeathPenalties::RespawnSound death_respawn_sound;
    MM3D::ClassicSaving::OwlDraw classic_owl_draw;
    MM3D::ClassicSaving::FileMarker classic_file_marker;
    // Bounded observation for the unresolved sword-activation movement hold.
    u32 owl_trace_actor=0,owl_trace_play=0,owl_trace_ticks=0;
    void TraceOwlState(const char* stage) {
        if(!owl_trace_actor||!owl_trace_play)return;
        const u32 player=memory.Read32OrNullopt(owl_trace_play+0x20dc).value_or(0);
        const auto read=[&](u32 at){return memory.Read32OrNullopt(at).value_or(0xffffffff);};
        LOG_INFO(Core_ARM11,"Owl activation trace: stage={} tick={} owl={:08x} play={:08x} player={:08x} owl_state={:08x} owl_action={:08x} actor_flags={:08x} player_action={:08x} talk_actor={:08x} talk_state={:08x} player_flags={:08x}/{:08x} cs={:08x}/{:08x}/{:08x} message={:08x}",
            stage,owl_trace_ticks,owl_trace_actor,owl_trace_play,player,
            read(owl_trace_actor+0x264),read(owl_trace_actor+0x268),read(owl_trace_actor+4),
            player?read(player+0x92c):0,player?read(player+0x930):0,player?read(player+0x950):0,
            player?read(player+0x11db0):0,player?read(player+0x11db4):0,
            read(0x687c14),read(0x687c18),read(0x687c20),read(owl_trace_play+0x825c));
    }

    MM3D::DialogueScale::Hook dialogue_hook;
    MM3D::CameraWriter::Instruction camera_instruction;
    void TraceCameraWrite(VAddr address, std::uint32_t value) {
        if(!parent.camera_tracing || !MM3D::CameraWriter::active.load() ||
           !MM3D::CameraWriter::Matches(value)) return;
        MM3D::CameraWriter::Hit hit;
        hit.instruction = camera_instruction; hit.address = address; hit.value = value;
        hit.core = parent.GetID();
        const auto process = parent.system.Kernel().GetCurrentProcess();
        if(!process) return;
        hit.process = process->process_id;
        hit.instruction_word = memory.Read32OrNullopt(hit.instruction.pc & ~3u).value_or(0);
        for(unsigned i=0; i<hit.stack.size(); ++i) {
            const std::uint64_t at = std::uint64_t(hit.instruction.sp) + i*4;
            if(at > 0xffffffffULL) break;
            const auto word = memory.Read32OrNullopt(static_cast<VAddr>(at));
            if(word) { hit.stack[i] = *word; hit.stack_valid |= 1u << i; }
        }
        MM3D::CameraWriter::Observe(hit);
    }
#endif
    std::optional<std::uint32_t> MemoryReadCode(VAddr vaddr) override {
        const auto instruction=memory.Read32OrNullopt(vaddr);
#ifdef ENABLE_MM3D_PRESENTATION
        if(instruction&&MM3D::ScreenShrink::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::ScreenShrink::CodeMatches(code))
                return MM3D::ScreenShrink::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::ClassicSaving::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::ClassicSaving::CodeMatches(code))
                return MM3D::ClassicSaving::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::StickControls::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::StickControls::CodeMatches(code,vaddr))
                return MM3D::StickControls::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::MinimapShortcut::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::MinimapShortcut::CodeMatches(code))
                return MM3D::MinimapShortcut::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::BottleHealing::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::BottleHealing::CodeMatches(code))
                return MM3D::BottleHealing::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::DeathPenalties::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::DeathPenalties::CodeMatches(code))
                return MM3D::DeathPenalties::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::Difficulty::SpawnInstruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::Difficulty::SpawnCodeMatches(code,vaddr))
                return MM3D::Difficulty::SpawnInstruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::PropDrawDistance::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::PropDrawDistance::CodeMatches(code,vaddr))
                return MM3D::PropDrawDistance::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::VegetationDrawDistance::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::VegetationDrawDistance::CodeMatches(code,vaddr))
                return MM3D::VegetationDrawDistance::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::ActorDrawDistance::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::ActorDrawDistance::CodeMatches(code,vaddr))
                return MM3D::ActorDrawDistance::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::Difficulty::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::Difficulty::CodeMatches(code,vaddr))
                return MM3D::Difficulty::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::FastTime::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::FastTime::CodeMatches(code))
                return MM3D::FastTime::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::NativeStartup::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::NativeStartup::CodeMatches(code))
                return MM3D::NativeStartup::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::ScheduleRows::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::ScheduleRows::CodeMatches(code))
                return MM3D::ScheduleRows::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::NotebookRows::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::NotebookRows::CodeMatches(code))
                return MM3D::NotebookRows::Instruction(vaddr,*instruction);
        }
        if(instruction && MM3D::NativeNotebook::MapInstruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory { Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) {return memory.Read32OrNullopt(at);} } code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::NativeNotebook::MapCodeMatches(code))
                return MM3D::NativeNotebook::MapInstruction(vaddr,*instruction);
        }
        if(instruction && MM3D::NativeNotebook::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory { Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) {return memory.Read32OrNullopt(at);} } code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::NativeNotebook::CodeMatches(code))
                return MM3D::NativeNotebook::Instruction(vaddr,*instruction);
        }
        if(instruction && MM3D::ItemPreview::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
            } code{memory};
            if(process && process->codeset && process->codeset->program_id==0x0004000000125500ULL &&
               MM3D::ItemPreview::CodeMatches(code))
                return MM3D::ItemPreview::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::HudButtons::VisibilityInstruction(vaddr,*instruction)!=*instruction){
            struct ReadMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::HudButtons::VisibilityCodeMatches(code))
                return MM3D::HudButtons::VisibilityInstruction(vaddr,*instruction);
        }
        if(instruction&&vaddr==MM3D::SongList::NavPC&&*instruction==MM3D::SongList::NavWord) {
            struct ReadMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::SongList::CodeMatches(code))return 0xef000000|MM3D::SongList::NavSVC;
        }
        if(instruction&&MM3D::DungeonMap::Instruction(vaddr,*instruction)!=*instruction) {
            struct ReadMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::DungeonMap::CodeMatches(code))
                return MM3D::DungeonMap::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::OptionsMenu::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::OptionsMenu::CodeMatches(code))
                return MM3D::OptionsMenu::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::MenuShortcuts::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::MenuShortcuts::CodeMatches(code))
                return MM3D::MenuShortcuts::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::MenuClose::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::MenuClose::CodeMatches(code))
                return MM3D::MenuClose::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::BomberApproach::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::BomberApproach::CodeMatches(code))
                return MM3D::BomberApproach::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::OwlIntro::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::OwlIntro::CodeMatches(code))
                return MM3D::OwlIntro::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::OwlSword::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::OwlSword::CodeMatches(code))
                return MM3D::OwlSword::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::TatlReminders::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::TatlReminders::CodeMatches(code))
                return MM3D::TatlReminders::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::Music::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::Music::CodeMatches(code))
                return MM3D::Music::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::MenuSound::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::MenuSound::CodeMatches(code))
                return MM3D::MenuSound::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::ClassicDoubleTime::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::ClassicDoubleTime::CodeMatches(code))
                return MM3D::ClassicDoubleTime::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::Prices::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::Prices::CodeMatches(code))
                return MM3D::Prices::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::SingleScreenText::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::SingleScreenText::CodeMatches(code))
                return MM3D::SingleScreenText::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::MenuBackdrop::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::MenuBackdrop::CodeMatches(code))
                return MM3D::MenuBackdrop::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::MenuMemory::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::MenuMemory::CodeMatches(code))
                return MM3D::MenuMemory::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::MenuPageSwitch::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::MenuPageSwitch::CodeMatches(code))
                return MM3D::MenuPageSwitch::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::InventoryNavigation::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::InventoryNavigation::CodeMatches(code))
                return MM3D::InventoryNavigation::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::NativeItemPresent::IntroInstruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::NativeItemPresent::IntroCodeMatches(code))
                return MM3D::NativeItemPresent::IntroInstruction(vaddr,*instruction);
        }
        if (instruction && MM3D::VisibilityDepth::Instruction(vaddr, *instruction) != *instruction) {
            struct CodeMemory { Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
            } code{memory};
            const auto process = parent.system.Kernel().GetCurrentProcess();
            if (process && process->codeset && process->codeset->program_id == MM3D::VisibilityDepth::Title &&
                MM3D::VisibilityDepth::CodeMatches(code))
                return MM3D::VisibilityDepth::Instruction(vaddr, *instruction);
        }
        if(instruction&&MM3D::PhotoCapture::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::PhotoCapture::CodeMatches(code))
                return MM3D::PhotoCapture::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::PauseCapture::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::PauseCapture::CodeMatches(code))
                return MM3D::PauseCapture::Instruction(vaddr,*instruction);
        }
        if(instruction && MM3D::NativeInventory::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
            } code{memory};
            if(process && process->codeset && process->codeset->program_id==0x0004000000125500ULL &&
               MM3D::NativeInventory::CodeMatches(code))
                return MM3D::NativeInventory::Instruction(vaddr,*instruction);
        }
        if(instruction && MM3D::DialogueScale::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
            } code{memory};
            if(process && process->codeset && process->codeset->program_id==0x0004000000125500ULL &&
               MM3D::DialogueScale::CodeMatches(code))
                return MM3D::DialogueScale::Instruction(vaddr,*instruction);
        }
        if (instruction && MM3D::NativeMinimap::Instruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
            } code{memory};
            if (process && process->codeset && process->codeset->program_id==0x0004000000125500ULL &&
                MM3D::NativeMinimap::CodeMatches(code))
                return MM3D::NativeMinimap::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::FormCamera::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::FormCamera::CodeMatches(code))
                return MM3D::FormCamera::Instruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::CameraLive::ChangeModeInstruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::CameraLive::ChangeModeCodeMatches(code))
                return MM3D::CameraLive::ChangeModeInstruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::CameraLive::DialogueCameraInstruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::CameraLive::DialogueCameraCodeMatches(code))
                return MM3D::CameraLive::DialogueCameraInstruction(vaddr,*instruction);
        }
        if(instruction&&MM3D::SongEffect::Instruction(vaddr,*instruction)!=*instruction) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} code{memory};
            const auto process=parent.system.Kernel().GetCurrentProcess();
            if(process&&process->codeset&&process->codeset->program_id==0x0004000000125500ULL&&MM3D::SongEffect::CodeMatches(code))
                return MM3D::SongEffect::Instruction(vaddr,*instruction);
        }
        if(instruction && MM3D::CameraLive::ProjectionInstruction(vaddr,*instruction)!=*instruction) {
            const auto process=parent.system.Kernel().GetCurrentProcess();
            struct CodeMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
            } code{memory};
            if(process && process->codeset && process->codeset->program_id==0x0004000000125500ULL &&
               MM3D::CameraLive::CodeMatches(code,vaddr))
                return MM3D::CameraLive::ProjectionInstruction(vaddr,*instruction);
        }
#endif
        return instruction;
    }

    std::uint8_t MemoryRead8(VAddr vaddr) override {
        return memory.Read8(vaddr);
    }
    std::uint16_t MemoryRead16(VAddr vaddr) override {
        return memory.Read16(vaddr);
    }
    std::uint32_t MemoryRead32(VAddr vaddr) override {
#ifdef ENABLE_MM3D_PRESENTATION
        if(vaddr>=Memory::VRAM_VADDR && vaddr<Memory::VRAM_VADDR_END &&
           MM3D::InterpolationPerformance::pending.load(std::memory_order_relaxed)) {
            const auto& regs=parent.jit->Regs();
            MM3D::InterpolationPerformance::GuestReadScope origin{vaddr,regs[15],regs[14]};
            return memory.Read32(vaddr);
        }
#endif
        return memory.Read32(vaddr);
    }
    std::uint64_t MemoryRead64(VAddr vaddr) override { return memory.Read64(vaddr); }

    void MemoryWrite8(VAddr vaddr, std::uint8_t value) override {
        memory.Write8(vaddr, value);
    }
    void MemoryWrite16(VAddr vaddr, std::uint16_t value) override {
        memory.Write16(vaddr, value);
    }
    void MemoryWrite32(VAddr vaddr, std::uint32_t value) override {
        memory.Write32(vaddr, value);
#ifdef ENABLE_MM3D_PRESENTATION
        TraceCameraWrite(vaddr, value);
#endif
    }
    void MemoryWrite64(VAddr vaddr, std::uint64_t value) override {
        memory.Write64(vaddr, value);
#ifdef ENABLE_MM3D_PRESENTATION
        TraceCameraWrite(vaddr, std::uint32_t(value));
        TraceCameraWrite(vaddr + 4, std::uint32_t(value >> 32));
#endif
    }

    bool MemoryWriteExclusive8(u32 vaddr, u8 value, u8 expected) override {
        return memory.WriteExclusive8(vaddr, value, expected);
    }
    bool MemoryWriteExclusive16(u32 vaddr, u16 value, u16 expected) override {
        return memory.WriteExclusive16(vaddr, value, expected);
    }
    bool MemoryWriteExclusive32(u32 vaddr, u32 value, u32 expected) override {
        const bool written = memory.WriteExclusive32(vaddr, value, expected);
#ifdef ENABLE_MM3D_PRESENTATION
        if(written) {
            TraceCameraWrite(vaddr, std::uint32_t(value));
        }
#endif
        return written;
    }
    bool MemoryWriteExclusive64(u32 vaddr, u64 value, u64 expected) override {
        const bool written = memory.WriteExclusive64(vaddr, value, expected);
#ifdef ENABLE_MM3D_PRESENTATION
        if(written) {
            TraceCameraWrite(vaddr, std::uint32_t(value));
            TraceCameraWrite(vaddr + 4, std::uint32_t(value >> 32));
        }
#endif
        return written;
    }

    void InterpreterFallback(VAddr pc, std::size_t num_instructions) override {
        // Should never happen.
        UNREACHABLE_MSG("InterpeterFallback reached with pc = 0x{:08x}, code = 0x{:08x}, num = {}",
                        pc, MemoryReadCode(pc).value(), num_instructions);
    }

    void CallSVC(std::uint32_t swi) override {
#ifdef ENABLE_MM3D_PRESENTATION
        if (swi == MM3D::VisibilityDepth::FirstSVC || swi == MM3D::VisibilityDepth::FirstSVC + 1) {
            auto& regs = parent.jit->Regs();
            const bool enabled = MM3D::CameraMidpoint::enabled.load();
            const auto generation = MM3D::CameraMidpoint::generation.load();
            if (swi == MM3D::VisibilityDepth::FirstSVC) {
                struct CodeMemory { Memory::MemorySystem& memory;
                    std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
                } code{memory};
                visibility_depth_hook.Begin(code, enabled, MM3D::guest_frames.load(), generation);
                regs[0] = 0; // displaced MOV r0,#0; flags unchanged
            } else {
                const auto cached = visibility_depth_hook.Read(regs[2], regs[0], enabled, generation);
                if (cached) regs[0] = *cached;
                else {
                    const u32 address = regs[2] + (regs[0] << 2);
                    MM3D::InterpolationPerformance::GuestReadScope origin{
                        address, MM3D::VisibilityDepth::LoadPC, regs[14]};
                    regs[0] = memory.Read32(address); // exact displaced LDR on every fallback
                }
            }
            return;
        }
        if(swi==MM3D::ScreenShrink::GuardSVC) {
            auto& regs=parent.jit->Regs();
            regs[15]=MM3D::ScreenShrink::Guard(regs[5],regs[0],parent.jit->Cpsr(),regs[3]);
            return;
        }
        if(swi==MM3D::ScreenShrink::SVC) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}
            } guest{memory};
            MM3D::ScreenShrink::Store(guest,parent.jit->Regs()[0],parent.jit->ExtRegs()[0],
                MM3D::Widescreen::supported_layout.load());
            return;
        }

        if(swi>=MM3D::ClassicSaving::FirstSVC&&swi<MM3D::ClassicSaving::FirstSVC+MM3D::ClassicSaving::Sites.size()) {
            struct GuestMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            struct Service {
                bool Begin(unsigned slot,MM3D::ClassicSaving::Kind kind){return MM3D::ClassicSaving::BeginWrite(slot,kind);}
                bool Finish(unsigned slot,MM3D::ClassicSaving::Kind kind){return MM3D::ClassicSaving::FinishWrite(slot,kind);}
                void Load(unsigned slot,std::span<const std::uint8_t> bytes){MM3D::ClassicSaving::Resume(slot,bytes);}
            } service;
            const bool trace_owl=swi==MM3D::ClassicSaving::FirstSVC+15&&
                MM3D::OwlSword::QuietCompletion(guest,parent.jit->Regs()[4]);
            if(trace_owl) {
                owl_trace_actor=parent.jit->Regs()[4];owl_trace_play=parent.jit->Regs()[6];owl_trace_ticks=0;
                TraceOwlState("completion-before-release");
            }
            const bool trace_return=swi==MM3D::ClassicSaving::FirstSVC+17&&MM3D::ClassicSaving::ResumeNoticeArmed();
            const bool trace_return_finish=swi==MM3D::ClassicSaving::FirstSVC+16&&classic_save_hook.returning.player&&parent.jit->Regs()[0]==2;
            const bool trace_finish=swi==MM3D::ClassicSaving::FirstSVC+16&&classic_save_hook.quiet_player!=0;
            const u32 native_message_state=trace_finish?parent.jit->Regs()[0]:0;
            if(swi==MM3D::ClassicSaving::FirstSVC+19&&
               (MM3D::ClassicSaving::ResumeNoticeArmed()||classic_save_hook.returning.player)) {
                const auto& r=parent.jit->Regs();
                const u32 height=(guest.Read(r[0]).value_or(0)>>8)&255;
                const bool hidden=classic_save_hook.returning.HideFocusBars(guest);
                const u32 state=height|(hidden?0x100:0)|(MM3D::ClassicSaving::ResumeNoticeArmed()?0x200:0);
                static u32 last_state=~0u;
                if(state!=last_state) {
                    last_state=state;
                    LOG_INFO(Core_ARM11,"Classic owl return bars: height={} hidden={} pending={}",
                             height,hidden,MM3D::ClassicSaving::ResumeNoticeArmed());
                }
            }
            classic_save_hook.Handle(guest,parent.jit->Regs(),swi-MM3D::ClassicSaving::FirstSVC,service);
            if(trace_return&&!MM3D::ClassicSaving::ResumeNoticeArmed())
                LOG_INFO(Core_ARM11,"Classic owl return: {}",classic_save_hook.returning.player?"native conversation started before idle input":"unsupported message resource; notice skipped");
            if(trace_return_finish)LOG_INFO(Core_ARM11,"Classic owl return: native conversation finished; movement released");
            if(trace_finish)LOG_INFO(Core_ARM11,"Owl activation trace: conversation state native={} effective={}",native_message_state,parent.jit->Regs()[0]);
            if(trace_owl) {
                const auto& r=parent.jit->Regs();
                LOG_INFO(Core_ARM11,"Owl activation trace: quiet completion r0={:08x} r1={:08x} r2={} lr={:08x} pc={:08x}",r[0],r[1],r[2],r[14],r[15]);
            }
            if(MM3D::ClassicSaving::enabled && swi<=MM3D::ClassicSaving::FirstSVC+1) {
                const auto& regs=parent.jit->Regs();
                const u32 ctx=regs[swi==MM3D::ClassicSaving::FirstSVC?0:4];
                const u32 status=guest.Read(ctx+0x180).value_or(~0u);
                static u32 last_ctx=0,last_status=~0u;
                if(ctx!=last_ctx||status!=last_status) {
                    last_ctx=ctx;last_status=status;
                    LOG_INFO(Core_ARM11,"Classic cycle trace: ctx={:08x} state={:08x} status={} window={:08x} captured={:08x}",
                        ctx,guest.Read(ctx+0x13c).value_or(0),status,
                        guest.Read(ctx+0x174).value_or(0),classic_save_hook.cycle);
                }
            }
            return;
        }
        if(swi==MM3D::StickControls::PadSVC||swi==MM3D::StickControls::MapSVC) {
            struct GuestMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            if(swi==MM3D::StickControls::PadSVC) {
                stick_controls_hook.Pad(guest,r);
                MM3D::SimpleCheats::MoonJump(guest);
            }
            else stick_controls_hook.Map(guest,r);
            return;
        }
        if(swi==MM3D::MinimapShortcut::SVC) {
            struct GuestMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            MM3D::MinimapShortcut::Handle(guest,parent.jit->Regs());
            return;
        }
        if(swi>=MM3D::BottleHealing::FirstSVC&&swi<MM3D::BottleHealing::FirstSVC+3) {
            struct GuestMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            MM3D::BottleHealing::Apply(guest,parent.jit->Regs(),swi-MM3D::BottleHealing::FirstSVC,
                MM3D::BottleHealing::red_hearts.load(),MM3D::BottleHealing::blue_hearts.load(),
                MM3D::BottleHealing::fairy_hearts.load());
            return;
        }
        if(swi==MM3D::DeathPenalties::FirstSVC||swi==MM3D::DeathPenalties::FirstSVC+1||
           swi==MM3D::DeathPenalties::SceneSoundSVC) {
            struct GuestMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            if(swi==MM3D::DeathPenalties::FirstSVC)
                MM3D::DeathPenalties::Death(guest,parent.jit->Regs(),MM3D::DeathPenalties::lose_rupees.load());
            else if(swi==MM3D::DeathPenalties::FirstSVC+1)
                MM3D::DeathPenalties::Continue(guest,parent.jit->Regs(),MM3D::DeathPenalties::world_ends.load(),
                    MM3D::DeathPenalties::advance_six_hours.load(),&death_respawn_sound);
            else
                death_respawn_sound.SceneInit(guest,parent.jit->Regs());
            return;
        }
        if(swi>=MM3D::FastTime::FirstSVC&&swi<MM3D::FastTime::FirstSVC+MM3D::FastTime::Hooks.size()) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            MM3D::FastTime::Update(guest,parent.jit->Regs(),swi,MM3D::FastTime::enabled.load());
            return;
        }
        if(swi==MM3D::Difficulty::SpawnSVC) {
            struct GuestMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            MM3D::Difficulty::Spawn(guest,parent.jit->Regs(),MM3D::Difficulty::disable_heart_drops.load(),
                                    MM3D::Difficulty::disable_rupee_drops.load(),
                                    MM3D::Difficulty::disable_fairy_drops.load(),
                                    MM3D::Difficulty::disable_magic_drops.load(),
                                    MM3D::Difficulty::disable_ammo_pickups.load());
            return;
        }
        if(swi>=MM3D::VegetationDrawDistance::FirstSVC&&swi<MM3D::VegetationDrawDistance::FirstSVC+MM3D::VegetationDrawDistance::Hooks.size()) {
            struct GuestMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            MM3D::VegetationDrawDistance::Load(guest,swi-MM3D::VegetationDrawDistance::FirstSVC,
                parent.jit->Regs(),parent.jit->ExtRegs(),MM3D::PropDrawDistance::multiplier.load());
            return;
        }
        if(swi>=MM3D::PropDrawDistance::FirstSVC&&swi<MM3D::PropDrawDistance::FirstSVC+MM3D::PropDrawDistance::Hooks.size()) {
            MM3D::PropDrawDistance::Load(swi-MM3D::PropDrawDistance::FirstSVC,parent.jit->Regs(),
                parent.jit->ExtRegs(),MM3D::PropDrawDistance::multiplier.load());
            return;
        }
        if(swi==MM3D::ActorDrawDistance::SVC) {
            struct GuestMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            auto& ext=parent.jit->ExtRegs();
            ext[0]=MM3D::ActorDrawDistance::Apply(guest,parent.jit->Regs()[4],ext[16],
                                               MM3D::ActorDrawDistance::multiplier.load());
            ext[0]=MM3D::PropDrawDistance::Apply(guest,parent.jit->Regs()[4],ext[0],
                                              MM3D::PropDrawDistance::multiplier.load());
            return;
        }
        if(swi==MM3D::Difficulty::DefenseSVC) {
            auto& regs=parent.jit->Regs();
            regs[4]=MM3D::Difficulty::DefenseDelta(regs[4],regs[1],parent.jit->Cpsr(),
                MM3D::Difficulty::weaker_enhanced_defense.load());
            return; // MOV without S: preserve CPSR and all other registers.
        }
        if(swi==MM3D::Difficulty::SVC) {
            auto& regs=parent.jit->Regs();
            regs[2]=MM3D::Difficulty::ApplyDamage(regs[2],regs[4],MM3D::Difficulty::damage_multiplier.load());
            return; // displaced ADD does not alter CPSR or the other registers
        }
        if(swi==MM3D::NativeStartup::FirstSVC||swi==MM3D::NativeStartup::FirstSVC+1) {
            struct TitleMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& regs=parent.jit->Regs();
            if(swi==MM3D::NativeStartup::FirstSVC) {
                startup_hook.Enter(guest,regs[0]);
                link_pose_hook.Enter(guest,regs[0]);regs[5]=regs[0];
            } else {
                startup_hook.Matrix(guest,regs[5],regs[4]);
                link_pose_hook.MatrixBuilt(guest,regs[5],regs[4],MM3D::guest_frames.load());
                regs[0]=memory.Read32(regs[5]+0xc);
            }
            return;
        }
        if(swi>=MM3D::ItemPreview::FirstSVC && swi<MM3D::ItemPreview::FirstSVC+MM3D::ItemPreview::PCs.size()) {
            struct PreviewMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
                void Write(u32 at,u32 value) { memory.Write32(at,value); }
            } guest{memory};
            auto& regs=parent.jit->Regs();
            switch(swi-MM3D::ItemPreview::FirstSVC) {
            case 0:
                item_preview_hook.Enter(guest,regs[0],regs[1]);
                // Shared Items/Masks entry runs after the complete icon array is built.
                inventory_hook.Capture(guest,regs[0]);
                regs[4]=regs[0]; break;
            case 1: item_preview_hook.Leave(regs[0]); regs[4]=regs[0]; break;
            case 2: item_preview_hook.ModelFrame(regs[0]); dungeon_map_hook.ModelFrame(guest,regs[0]); regs[4]=regs[0]; break;
            case 3: item_preview_hook.ModelBegin(guest,regs[4],regs[6]); dungeon_map_hook.preview.ModelBegin(guest,regs[4],regs[6]); regs[1]=regs[6]; break;
            case 4: dungeon_map_hook.preview.ModelEnd(guest); item_preview_hook.ModelEnd(guest); break; // displaced NOP
            case 5:
                item_preview_hook.EnterGear(guest,regs[0],regs[1]);
                inventory_hook.CaptureGear(guest,regs[0]);
                regs[4]=regs[0]; break;
            case 6:
                // This shared Gear Close instruction is dispatched through
                // ItemPreview first; release the backdrop's owned pause bit too.
                menu_backdrop.Release(guest);
                item_preview_hook.Leave(regs[0]);
                if(inventory_hook.gear_page==regs[0]) inventory_hook.Capture(guest,0);
                regs[5]=regs[0]; break;
            }
            return;
        }
        if(swi==MM3D::ScheduleRows::FirstSVC||swi==MM3D::ScheduleRows::FirstSVC+1) {
            struct RowMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& regs=parent.jit->Regs();
            if(swi==MM3D::ScheduleRows::FirstSVC) {
                notebook_hook.schedule_rows.Enter(guest,regs[0]);regs[4]=regs[0];
            } else {
                regs[1]=0; // Native stop/reset path for Schedule vertical scrolling.
                regs[2]=0; // Displaced MOV r2,#0.
            }
            return;
        }
        if(swi==MM3D::NotebookRows::SyncSVC) {
            struct RowMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            auto& regs=parent.jit->Regs();
            if(notebook_hook.row_window.ImmediateScroll(guest,regs[0],regs[2],regs[3])) {
                regs[2]|=8; // Populate the changed window now.
                regs[3]=0; // Native reset path, without the deferred scroll animation.
            }
            regs[4]=regs[0];return;
        }
        if(swi==MM3D::NotebookRows::FirstSVC||swi==MM3D::NotebookRows::FirstSVC+1) {
            struct RowMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& regs=parent.jit->Regs();
            if(swi==MM3D::NotebookRows::FirstSVC){notebook_hook.row_window.Enter(guest,regs[0]);regs[4]=regs[0];}
            else {notebook_hook.row_window.Leave(guest);regs[0]=regs[4];}
            return;
        }
        if(swi==MM3D::NativeNotebook::MapInputSVC||swi==0xffffd3) {
            struct NotebookMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);} } guest{memory};
            const auto& regs=parent.jit->Regs();
            parent.jit->SetCpsr(swi==0xffffd3 ?
                MM3D::NativeNotebook::ScheduleInputFlags(guest,regs[4],regs[3],parent.jit->Cpsr()) :
                MM3D::NativeNotebook::MapInputFlags(guest,regs[4],regs[3],parent.jit->Cpsr()));
            return;
        }
        if(swi==MM3D::NativeNotebook::SVC) {
            struct NotebookMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);} } guest{memory};
            auto& regs=parent.jit->Regs();notebook_hook.Capture(guest,regs[0]);regs[4]=regs[0];return;
        }
        if(swi==MM3D::SongList::NavSVC){
            auto& r=parent.jit->Regs();
            if(MM3D::SongList::enabled&&song_hook.panel&&memory.Read32OrNullopt(r[4]+0x14)==std::optional<u32>(song_hook.panel)){
                auto context=memory.Read32OrNullopt(r[7]+4);
                auto input=context?memory.Read32OrNullopt(*context+0x16c):std::optional<u32>{};
                auto a=input?memory.Read32OrNullopt(*input+0x34):std::optional<u32>{};
                auto b=input?memory.Read32OrNullopt(*input+0x8c):std::optional<u32>{};
                auto flags=memory.Read32OrNullopt(r[5]+0x1f8),mask=memory.Read32OrNullopt(r[6]+0x1c);
                if(a&&b&&flags&&mask)r[2]=MM3D::SongList::Selection(r[1],*a|*b,(*flags&*mask)!=0);
            }
            const u32 a=r[1],b=r[2],d=a-b;
            const u32 flags=(d&0x80000000u)|(d==0?0x40000000u:0)|(a>=b?0x20000000u:0)|(((a^b)&(a^d)&0x80000000u)>>3);
            parent.jit->SetCpsr((parent.jit->Cpsr()&0x0fffffffu)|flags);return;
        }
        if(swi==MM3D::BomberApproach::FirstSVC||swi==MM3D::BomberApproach::FirstSVC+1) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            auto& r=parent.jit->Regs();
            const bool suppress=MM3D::BomberApproach::Suppress(guest,r[4]);
            if(swi==MM3D::BomberApproach::FirstSVC) {
                const auto original=static_cast<u32>(static_cast<s32>(static_cast<s16>(memory.Read16(r[5]+0x8c))));
                r[0]=MM3D::BomberApproach::State(original,suppress);
            } else if(suppress)r[15]=MM3D::BomberApproach::ResetPC;
            else r[1]=0xf;
            return;
        }
        if(swi==MM3D::OwlIntro::FirstSVC||swi==MM3D::OwlIntro::FirstSVC+1) {
            auto& r=parent.jit->Regs();
            if(swi==MM3D::OwlIntro::FirstSVC)MM3D::OwlIntro::Enter(r);
            else r[0]=MM3D::OwlIntro::TatlFlags(memory.Read8(r[0]+0x2d3));
            return;
        }
        if(swi==MM3D::OwlSword::FirstSVC||swi==MM3D::OwlSword::FirstSVC+1||
           swi==MM3D::OwlSword::TriggerSVC) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            if(swi==MM3D::OwlSword::TriggerSVC) {
                MM3D::OwlSword::StrikeTrigger(guest,r);
            }
            else if(swi==MM3D::OwlSword::FirstSVC) {
                r[4]=r[0];
                const auto before=guest.Read(r[4]+0x268);
                MM3D::OwlSword::BlockFeatherAcceptance(guest,r[4],MM3D::ClassicSaving::enabled.load());
                MM3D::OwlSword::Update(guest,r[4]);
                if(before!=guest.Read(r[4]+0x268)&&guest.Read(r[4]+0x268)==MM3D::OwlSword::Activate) {
                    owl_trace_actor=r[4];owl_trace_play=r[1];owl_trace_ticks=0;
                    TraceOwlState("sword-hit");
                }
                if(owl_trace_actor==r[4]&&owl_trace_play==r[1]&&owl_trace_ticks<180) {
                    ++owl_trace_ticks;
                    if(owl_trace_ticks<=3||owl_trace_ticks%30==0)TraceOwlState("actor-update");
                }
            }
            else if(MM3D::OwlSword::Locked(guest,r[0]) ||
                    MM3D::OwlSword::FeatherBlocked(guest,r[0],MM3D::ClassicSaving::enabled.load()))r[0]=0;
            else {r[14]=MM3D::OwlSword::OfferPC+4;r[15]=0x3c322c;}
            return;
        }
        if(swi==MM3D::TatlReminders::SVC) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            r[4]=MM3D::TatlReminders::Request(guest,r[0],r[1]);return;
        }
        if(swi==MM3D::Music::SVC) {
            struct SoundMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 word){memory.Write32(at,word);}} guest{memory};
            const auto& regs=parent.jit->Regs();
            MM3D::Music::Store(guest,regs[4],regs[5],parent.jit->ExtRegs()[16]);return;
        }
        if(swi==MM3D::MenuSound::SVC) {
            struct SoundMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            MM3D::MenuSound::Store(guest,menu_sound_context,r[4],r[0]);return;
        }
        if(swi>=MM3D::ClassicDoubleTime::FirstSVC&&swi<MM3D::ClassicDoubleTime::FirstSVC+MM3D::ClassicDoubleTime::Sites.size()) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            const auto site=swi-MM3D::ClassicDoubleTime::FirstSVC;
            const bool owned=classic_double_time_hook.target.has_value();
            auto& r=parent.jit->Regs();
            classic_double_time_hook.Handle(guest,r,site);
            if(site==0&&MM3D::ClassicDoubleTime::enabled.load())
                LOG_INFO(Core_ARM11,"Classic Double Time: dispatch PC={:08X}, question={}, message_data={:08X}",
                    r[15],classic_double_time_hook.target?int(classic_double_time_hook.target->question):-1,
                    classic_double_time_hook.message_data);
            else if(owned&&(site==2||site==3))
                LOG_INFO(Core_ARM11,"Classic Double Time: choice site={}, continuation={:08X}",site,r[15]);
            return;
        }
        if(swi>=MM3D::Prices::FirstSVC&&swi<MM3D::Prices::FirstSVC+MM3D::Prices::Sites.size()) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            MM3D::Prices::Execute(guest,parent.jit->Regs(),swi-MM3D::Prices::FirstSVC,MM3D::Prices::multiplier.load());
            return;
        }
        if(swi==MM3D::SingleScreenText::SVC) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            classic_double_time_hook.Message(guest,r[0],r[1]);
            MM3D::ClassicSaving::ObserveMessage(r[1]);
            MM3D::SingleScreenText::Apply(guest,r[0],r[1]);
            if(MM3D::Prices::CodeMatches(guest))MM3D::Prices::Message(guest,r[0],r[1],MM3D::Prices::multiplier.load());
            r[3]=memory.Read32(r[0]+4); // original LDR r3,[r0,#4]
            return;
        }
        if(swi>=MM3D::MenuBackdrop::FirstSVC&&swi<MM3D::MenuBackdrop::FirstSVC+MM3D::MenuBackdrop::PCs.size()) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            switch(swi-MM3D::MenuBackdrop::FirstSVC) {
            case 0:
                menu_backdrop.Pause(guest,r[7],r[4],r[1],r[6]);
                LOG_INFO(Core_ARM11,"MM3D Gear backdrop: full_pause={} captured_scene={}",
                    (guest.Read(r[7]+0xaac).value_or(0)&0xffff)==3,menu_backdrop.Active(guest));
                break;
            case 1: case 2: menu_backdrop.Pause(guest,r[5],r[4],r[1],r[0]);break;
            case 3: menu_backdrop.Release(guest);r[5]=r[0];break;
            case 4: menu_backdrop.CloseMap(guest,r,menu_page_switch.KeepCapture(guest,MM3D::MenuBackdrop::Hook::Game(guest,r[1])+0x11010));break;
            case 5: menu_backdrop.GearImageImport(guest,r);break;
            case 6: menu_backdrop.FinishGearPhoto(guest,r);break;
            }
            return;
        }
        if(swi>=MM3D::MenuMemory::FirstSVC&&swi<MM3D::MenuMemory::FirstSVC+MM3D::MenuMemory::PCs.size()) {
            struct Guest {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            if(swi==MM3D::MenuMemory::FirstSVC) {
                if(!menu_memory.Prepare(guest,r)&&
                   !(MM3D::MenuBackdrop::CodeMatches(guest)&&menu_backdrop.Prepare(guest,r))) {
                    r[15]=r[2]&~1u;
                    parent.jit->SetCpsr((parent.jit->Cpsr()&~0x20u)|((r[2]&1u)<<5));
                }
            } else if(swi==MM3D::MenuMemory::FirstSVC+1)menu_memory.Ready(guest,r[4],menu_backdrop.PreparedReady(guest,r[4],r[0]));
            else if(!(MM3D::MenuBackdrop::CodeMatches(guest)&&menu_backdrop.KeepGearCapture(guest,r[0],r[4],r[7])))
                menu_memory.GearOpenCapture(guest,r[0],r[6],r[4],r[7]);
            return;
        }
        if(swi>=MM3D::MenuPageSwitch::FirstSVC&&swi<MM3D::MenuPageSwitch::FirstSVC+MM3D::MenuPageSwitch::PCs.size()) {
            struct PageMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& r=parent.jit->Regs();
            switch(swi-MM3D::MenuPageSwitch::FirstSVC) {
            case 0:
                menu_sound_context=r[0];
                TraceMenuBackdrop(guest,0);
                menu_backdrop.Tick(guest);
                TraceMenuBackdrop(guest,1);
                if(MM3D::MenuMemory::CodeMatches(guest))menu_memory.Tick(guest,r[0]);
                menu_page_switch.Tick(guest,r[0],menu_backdrop.HasRetainedCapture()&&menu_backdrop.Active(guest));r[4]=r[0];break;
            case 1:
                r[14]=MM3D::MenuPageSwitch::PCs[1]+4;
                if(menu_page_switch.PairPending(guest,r[1]))r[0]=1;
                else {r[15]=r[2]&~1u;parent.jit->SetCpsr((parent.jit->Cpsr()&~0x20u)|((r[2]&1u)<<5));}
                break;
            case 2:
                if(menu_page_switch.FinishLayout(guest,r[0]))parent.jit->ExtRegs()[0]=std::bit_cast<u32>(10.f);
                r[4]=r[0];break;
            case 6: parent.jit->ExtRegs()[1]=MM3D::MenuBackdrop::CodeMatches(guest)&&menu_backdrop.Active(guest)?0:MM3D::MenuPageSwitch::GearBackdropAlpha();break;
            case 7: menu_page_switch.MapCloseSound(guest,r);break;
            default:
                if(swi==MM3D::MenuPageSwitch::FirstSVC+5&&
                   menu_backdrop.CloseGearPhoto(guest,r,menu_page_switch.KeepCapture(guest,r[0])))break;
                menu_page_switch.CaptureCall(guest,r,swi-MM3D::MenuPageSwitch::FirstSVC);break;
            }
            return;
        }
        if(swi>=MM3D::InventoryNavigation::FirstSVC&&swi<=MM3D::InventoryNavigation::FirstSVC+2) {
            struct NavMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& regs=parent.jit->Regs();const bool vertical=swi==MM3D::InventoryNavigation::FirstSVC;
            if(swi==MM3D::InventoryNavigation::FirstSVC+2) {
                regs[2]|=regs[12];
                if(auto next=MM3D::InventoryNavigation::GearMove(guest,regs[0],regs[2])) {
                    const auto current=memory.Read8(regs[0]+0x195);
                    regs[2]=*next;regs[15]=*next==current?0x5a56e0:0x5a574c;
                }
                return;
            }
            if(MM3D::InventoryNavigation::PresentMove(guest,regs[4],regs[13],vertical,regs[5])||
               MM3D::InventoryNavigation::Move(guest,regs[4],regs[13],vertical,regs[5]))
                regs[15]=vertical?0x5a0eb0:0x5a14c0;
            else regs[1]=memory.Read8(regs[4]+(vertical?0x19:0x18));
            return;
        }
        if(swi==MM3D::NativeItemPresent::IntroSVC) {
            struct ReadMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            auto& regs=parent.jit->Regs();
            regs[14]=MM3D::NativeItemPresent::IntroPC+4;
            if(MM3D::NativeItemPresent::SkipIntro(guest,regs[5])) {
                regs[0]=1;regs[15]=regs[14]; // Native caller closes the message and advances focus.
            } else regs[15]=0x1adb90; // Replay the original BL for unsupported/disabled cases.
            return;
        }
        if(swi>=MM3D::PhotoCapture::FirstSVC&&swi<MM3D::PhotoCapture::FirstSVC+4) {
            struct ReadMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            auto& r=parent.jit->Regs();
            switch(swi-MM3D::PhotoCapture::FirstSVC) {
            case 0: photo_capture_hook.Begin(r[6]);r[3]=memory.Read32(r[13]+4);break;
            case 1: photo_capture_hook.Keep(guest);r[0]=memory.Read32(r[13]+12);break;
            case 2: photo_capture_hook.Import(guest,r[4],r[5],r[6]);r[2]=r[4]+12;break;
            case 3: photo_capture_hook.Bind(guest,r[0],r[1],r[2]);r[5]=r[1];break;
            }
            return;
        }
        if(swi>=MM3D::PauseCapture::FirstSVC&&swi<MM3D::PauseCapture::FirstSVC+3) {
            struct ReadMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            auto& regs=parent.jit->Regs();
            switch(swi-MM3D::PauseCapture::FirstSVC) {
            case 0: photo_capture_hook.Convert(guest,regs[7],regs[8],regs[4],regs[5]);if(!menu_backdrop.HasRetainedCapture())pause_capture_hook.Convert(guest,regs[7],regs[8],regs[4],regs[5]);regs[0]=regs[7]+(regs[8]<<2);break;
            case 1: if(!menu_backdrop.HasRetainedCapture())pause_capture_hook.Bind(guest,regs[3],regs[2]);regs[12]=regs[0]+0x1c;break;
            case 2: photo_capture_hook.Destroy(regs[0]);if(!menu_backdrop.HasRetainedCapture())pause_capture_hook.Reset();regs[4]=regs[0];break;
            }
            return;
        }
        if(swi>=MM3D::NativeInventory::FirstSVC && swi<MM3D::NativeInventory::FirstSVC+4) {
            struct InventoryMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
                void Write(u32 at,u32 value) { memory.Write32(at,value); }
            } guest{memory};
            auto& regs=parent.jit->Regs();
            switch(swi-MM3D::NativeInventory::FirstSVC) {
            case 0: inventory_hook.Capture(guest,regs[0]); regs[4]=regs[0]; break;
            case 1: MM3D::ScreenShrink::Update(guest); if(MM3D::MenuBackdrop::CodeMatches(guest))menu_backdrop.Pass(guest,regs[0],regs[1]);
                if(regs[1]==0)TraceMenuBackdrop(guest,2);
                options_hook.Pass(guest,regs[0],regs[1]); song_reset_hook.Pass(guest,regs[0],regs[1]); double_time_hook.Pass(guest,regs[0],regs[1]); dungeon_map_hook.Pass(guest,regs[0],regs[1]); song_hook.Pass(guest,regs[0],regs[1]); ocarina_hook.Pass(guest,regs[0],regs[1]); item_present_hook.Pass(guest,regs[0],regs[1]);
                if(regs[1]==4) MM3D::ItemSlideInterpolation::Publish({MM3D::guest_frames.load(),
                    MM3D::LinkPoseCapture::interpolation_generation.load(),item_present_hook.panel?item_present_hook.owner:0,
                    item_present_hook.transition_x,item_present_hook.transition_time,item_present_hook.transition_travel,
                    item_present_hook.transitioning,item_present_hook.transition_opening});
                press_hook.Pass(guest,regs[0],regs[1]); inventory_hook.Pass(guest,regs[0],regs[1]); notebook_hook.Pass(guest,regs[0],regs[1]); file_select_hook.Pass(guest,regs[0],regs[1]);
                if(regs[1]==5) {
                    // Inspect the submitted queues after routing. A stale page
                    // pointer alone cannot keep interpolation active after exit.
                    const u32 menu_owner=inventory_hook.SubmittedInterpolationOwner(guest,regs[0]);
                    MM3D::InventoryInterpolation::Publish(MM3D::guest_frames.load(),
                        MM3D::LinkPoseCapture::interpolation_generation.load(),menu_owner,
                        inventory_hook.SubmittedInterpolationOwner(guest,regs[0],true));
                }
                if(regs[1]==5) MM3D::FileSelectInput::Observe(file_select_hook.active &&
                    file_select_hook.entrance_pending);
                if(regs[1]==5) MM3D::FileSelectInterpolation::Publish({MM3D::guest_frames.load(),
                    MM3D::LinkPoseCapture::interpolation_generation.load(),
                    file_select_hook.active?file_select_hook.panel:options_hook.panel,
                    {file_select_hook.files_slide,file_select_hook.summary_slide,file_select_hook.actions_slide},
                    file_select_hook.presentation,file_select_hook.stone_slide});
                if(regs[1]==5&&file_select_hook.route_status!=file_select_hook.last_route_status) {
                    LOG_INFO(Core_ARM11,"MM3D FileSelect: active={} stage={} panel={:08x} summary={:08x} popup={:08x}",
                        file_select_hook.active,file_select_hook.route_status,file_select_hook.panel,file_select_hook.summary,file_select_hook.popup);
                    file_select_hook.last_route_status=file_select_hook.route_status;
                }
                banner_hook.Pass(guest,regs[0],regs[1]);
                if(regs[1]==0) MM3D::BannerInterpolation::Publish(MM3D::guest_frames.load(),
                    MM3D::LinkPoseCapture::interpolation_generation.load(),banner_hook.owner);
                regs[4]=regs[0]; break;
            case 2: classic_save_hook.BeginCycleDraw(guest,regs[4]); minigame_hook.Begin(guest,regs[4]); hud_buttons_hook.Begin(guest,regs[4]); song_reset_hook.Begin(guest,regs[4]); banner_hook.Begin(guest,regs[4],MM3D::Widescreen::Active()); double_time_hook.Begin(guest,regs[4]); dungeon_map_hook.Begin(guest,regs[4]); song_hook.Begin(guest,regs[4]); item_present_hook.Begin(guest,regs[4]); loading_icon_hook.Begin(guest,regs[4]); press_hook.Begin(guest,regs[4]); inventory_hook.Begin(guest,regs[4],MM3D::Widescreen::Active()); notebook_hook.Begin(guest,regs[4],MM3D::Widescreen::Active()); file_select_hook.Begin(guest,regs[4],MM3D::Widescreen::Active());
                if(MM3D::ClassicSaving::enabled && classic_save_hook.cycle) {
                    static std::vector<u32> traced_layouts;
                    static u32 traced_cycle=0;
                    if(traced_cycle!=classic_save_hook.cycle) {
                        traced_cycle=classic_save_hook.cycle;traced_layouts.clear();
                    }
                    if(traced_layouts.size()<32 && std::find(traced_layouts.begin(),traced_layouts.end(),regs[4])==traced_layouts.end()) {
                        traced_layouts.push_back(regs[4]);
                        const auto window=guest.Read(traced_cycle+0x174).value_or(0);
                        LOG_INFO(Core_ARM11,"Classic cycle draw: layout={:08x} message={} owned={:08x} inner={:08x} flags={:08x} hidden={}",
                            regs[4],MM3D::NativeInventory::LayoutNamed(guest,regs[4],"MessageWindow"),window,
                            window?guest.Read(window+0x358).value_or(0):0,
                            guest.Read(regs[4]+8+0xd4).value_or(0),
                            !classic_save_hook.cycle_draws.empty() && classic_save_hook.cycle_draws.back().address!=0);
                    }
                }
                options_hook.Begin(guest,regs[4]);
                player_hud_hook.Begin(guest,regs[4]);
                regs[2]=notebook_hook.PromptRoot(guest,regs[4],regs[13]);
                {const auto root=ocarina_hook.Root(guest,regs[4],regs[13],MM3D::Widescreen::Active());
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=magic_hook.Root(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=song_hook.ShadowRoot(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto content=regs[2];
                 regs[2]=song_hook.HeaderRoot(guest,regs[4],content,content!=regs[4]+8?content:regs[13]);}
                {const auto content=regs[2];
                 regs[2]=song_hook.PanelRoot(guest,regs[4],content,content!=regs[4]+8?content:regs[13]);}
                {const auto root=inventory_shadow_hook.Root(guest,inventory_hook,regs[4],regs[13],&item_present_hook);
                 if(root!=regs[4]+8)regs[2]=root;}
                items_map_prompt_hook.ResolveDescription(guest,item_preview_hook.owner,inventory_hook.panel);
                {const auto existing_root=regs[2];
                 const auto prompt_sp=existing_root!=regs[4]+8?existing_root:regs[13];
                 const auto root=items_map_prompt_hook.Root(guest,inventory_hook,regs[4],
                    prompt_sp,existing_root,MM3D::Widescreen::Active(),
                    parent.system.CoreTiming().GetGlobalTimeUs().count());
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=dungeon_map_hook.PromptRoot(guest,regs[4],regs[13],MM3D::Widescreen::Active());
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=dungeon_map_hook.PanelRoot(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                {const auto root=dungeon_map_hook.ShadowRoot(guest,regs[4],regs[13]);
                 if(root!=regs[4]+8)regs[2]=root;}
                regs[2]=map_items_prompt_hook.MapRoot(guest,dungeon_map_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],MM3D::Widescreen::Active(),
                    parent.system.CoreTiming().GetGlobalTimeUs().count());
                regs[2]=world_map_prompt_hook.Root(guest,dungeon_map_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],MM3D::Widescreen::Active());
                regs[2]=menu_tabs_hook.Root(guest,inventory_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2]);
                menu_help_hook.source.ResolveDescription(guest,item_preview_hook.owner,inventory_hook.panel);
                regs[2]=menu_help_hook.Root(guest,inventory_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2]);
                if(!item_present_hook.IsDescription(guest,select_help_hook.source.description))select_help_hook.source.description=0;
                regs[2]=select_help_hook.Root(guest,inventory_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],&item_present_hook);
                regs[2]=file_select_prompt_hook.Root(guest,file_select_hook,options_hook,regs[4],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],regs[2],MM3D::Widescreen::Active());
                regs[2]=classic_file_marker.Root(guest,file_select_hook,regs[4],regs[2],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],
                    file_select_hook.active?MM3D::ClassicSaving::PendingMask():0,
                    parent.system.CoreTiming().GetGlobalTimeUs().count());
                regs[2]=classic_owl_draw.Root(guest,regs[4],regs[2],
                    regs[2]!=regs[4]+8?regs[2]:regs[13],
                    classic_save_hook.presentation.window,classic_save_hook.presentation.frames);
                {
                    static unsigned last_owl_draw_status=0;
                    const auto status=MM3D::ClassicSaving::owl_draw_status.load();
                    if(status!=last_owl_draw_status) {
                        LOG_INFO(Core_ARM11,"Classic owl draw: status={} (layout=1 labels=2 cursor=4 fade=8)",status);
                        last_owl_draw_status=status;
                    }
                }
                regs[2]=double_time_hook.ShadeRoot(guest,regs[4],regs[2],
                    regs[2]!=regs[4]+8?regs[2]:regs[13]);
                if(regs[2]!=regs[4]+8)regs[13]=regs[2];
                if(MM3D::CameraMidpoint::present_60.load())
                    MM3D::MessageInterpolation::Publish(MM3D::guest_frames.load(),
                        MM3D::LinkPoseCapture::interpolation_generation.load(),
                        MM3D::MessageLayout::Read(guest,regs[4]));
                break;
            case 3:
                if(const auto sp=double_time_hook.shade_scratch.End(guest))regs[13]=sp;
                classic_save_hook.EndCycleDraw(guest);
                if(const auto sp=classic_owl_draw.End(guest))regs[13]=sp;
                if(const auto sp=classic_file_marker.scratch.End(guest))regs[13]=sp;
                if(const auto sp=file_select_prompt_hook.End(guest))regs[13]=sp;
                if(const auto sp=select_help_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=menu_help_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=menu_tabs_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=world_map_prompt_hook.End(guest))regs[13]=sp;
                if(const auto sp=map_items_prompt_hook.prompt.End(guest))regs[13]=sp;
                if(const auto sp=dungeon_map_hook.shadow_scratch.End(guest))regs[13]=sp;
                if(const auto sp=dungeon_map_hook.panel_scratch.End(guest))regs[13]=sp;
                if(const auto sp=dungeon_map_hook.map_prompts.scratch.End(guest))regs[13]=sp;
                if(const auto sp=items_map_prompt_hook.prompt.End(guest))regs[13]=sp;
                if(const auto sp=inventory_shadow_hook.scratch.End(guest))regs[13]=sp;
                if(const auto sp=song_hook.backdrop.scratch.End(guest))regs[13]=sp;
                if(const auto sp=song_hook.header.scratch.End(guest))regs[13]=sp;
                if(const auto sp=song_hook.shadow_scratch.End(guest))regs[13]=sp;
                if(const auto sp=magic_hook.End(guest))regs[13]=sp;
                if(const auto sp=ocarina_hook.End(guest))regs[13]=sp;
                if(const auto sp=notebook_hook.prompt_hook.End(guest))regs[13]=sp;
                player_hud_hook.End(guest);
                options_hook.End(guest);
                song_hook.End(guest); item_present_hook.End(guest); file_select_hook.End(guest); notebook_hook.End(guest); inventory_hook.End(guest); press_hook.End(guest); loading_icon_hook.End(guest); dungeon_map_hook.End(guest); double_time_hook.End(guest); banner_hook.End(guest); song_reset_hook.End(guest); hud_buttons_hook.End(guest); minigame_hook.End(guest); guest.Write(regs[6],regs[0]); break;
            }
            return;
        }
        if(swi==MM3D::DialogueScale::BeforeSVC || swi==MM3D::DialogueScale::AfterSVC) {
            struct PoseMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
                void Write(u32 at,u32 value) { memory.Write32(at,value); }
            } pose{memory};
            const auto layout=parent.jit->Regs()[4];
            if(swi==MM3D::DialogueScale::BeforeSVC) {
                const bool description=item_preview_hook.Description(pose,layout);
                const bool present_description=item_present_hook.IsDescription(pose,layout);
                if(present_description){select_help_hook.source.description=layout;
                    select_help_hook.source.description_panel=item_present_hook.panel;}
                const auto& setting=(item_present_hook.mask_menu?MM3D::NativeItemPresent::mask_poses:
                    MM3D::NativeItemPresent::poses)[MM3D::NativeItemPresent::Description];
                const MM3D::GearMenu::Pose description_pose{setting.x.load(),setting.y.load(),setting.scale.load()};
                const bool map_description=dungeon_map_hook.IsDescription(pose,layout);
                const auto& map_setting=MM3D::DungeonMap::poses[MM3D::DungeonMap::Description];
                const MM3D::GearMenu::Pose map_pose{map_setting.x.load(),map_setting.y.load(),map_setting.scale.load()};
                const bool reset_description=song_reset_hook.IsDescription(pose,layout);
                const auto& reset_setting=MM3D::SongReset::poses[MM3D::SongReset::Description];
                const MM3D::GearMenu::Pose reset_pose{reset_setting.x.load(),reset_setting.y.load(),reset_setting.scale.load()};
                const bool options_description=options_hook.IsDescription(pose,layout);
                dialogue_hook.Begin(pose,layout,description,description&&item_preview_hook.gear,
                    options_description?&options_hook.frame.description:reset_description?&reset_pose:map_description?&map_pose:present_description?&description_pose:nullptr);
                item_present_hook.BeginPromptPose(pose,layout);
                parent.jit->Regs()[0]=layout+8; // displaced ADD r0,r4,#8
            } else {
                item_present_hook.EndPromptPose(pose);
                dialogue_hook.End(pose,layout);
                parent.jit->Regs()[13]+=8; // displaced ADD sp,sp,#8
            }
            return;
        }
        if(swi>=MM3D::DungeonMap::FirstSVC&&swi<MM3D::DungeonMap::FirstSVC+MM3D::DungeonMap::PCs.size()) {
            struct MapMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& regs=parent.jit->Regs();
            switch(swi-MM3D::DungeonMap::FirstSVC) {
            case 0: dungeon_map_hook.Capture(guest,regs[0],regs[1]);regs[4]=regs[0];break;
            case 1:
                dungeon_map_hook.BeforeMap(guest);
                regs[14]=MM3D::DungeonMap::PCs[1]+4;regs[15]=0x2a326c;break;
            case 2: dungeon_map_hook.AfterMap(guest);regs[0]=memory.Read8(regs[4]+0x74c);break;
            case 3:
                if(dungeon_map_hook.valid) MM3D::DungeonMap::LogInput(guest,regs[4],memory.Read8(regs[6]+0xc));
                regs[0]=memory.Read8(regs[6]+0xc);
                regs[0]=MM3D::DungeonMap::FilterA(guest,regs[4],regs[0]);
                break;
            case 4:
                regs[0]=memory.Read32(regs[4]+0x1c);
                if(dungeon_map_hook.SkipWorld(guest,regs[4]))regs[15]=MM3D::DungeonMap::WorldEnd;
                break;
            }
            return;
        }
        if(swi==MM3D::OptionsMenu::DrawSVC||swi==MM3D::OptionsMenu::SelectSVC) {
            struct CodeMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            auto& regs=parent.jit->Regs();
            if(swi==MM3D::OptionsMenu::DrawSVC)options_hook.Capture(guest,regs[0]);
            else regs[2]=MM3D::OptionsMenu::Selection(guest,regs[0],regs[2]);
            regs[4]=regs[0];return;
        }
        if (swi==MM3D::NativeMinimap::BeforeSVC || swi==MM3D::NativeMinimap::AfterSVC) {
            struct MapMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }
                void Write(u32 at,u32 value) { memory.Write32(at,value); }
            } map_memory{memory};
            const auto screen=parent.jit->Regs()[4];
            if (swi==MM3D::NativeMinimap::BeforeSVC) {
                minimap_hook.Begin(map_memory,screen,MM3D::Minimap::enabled.load(),parent.jit->Regs()[0]);
            } else {
                minimap_hook.End(map_memory,screen,MM3D::Minimap::enabled.load(),
                    MM3D::Minimap::scale.load(),MM3D::Minimap::opacity.load(),
                    MM3D::Widescreen::Active() ? 400.f+(240.f*16.f/9.f-400.f)/2.f : 400.f);
                // Emulate the displaced LDR r0,[r4,#0x38], including paths which skip map drawing.
                parent.jit->Regs()[0]=memory.Read32(screen+0x38);
            }
            return;
        }
        if(swi>=MM3D::SongEffect::FirstSVC&&swi<MM3D::SongEffect::FirstSVC+MM3D::SongEffect::Loads.size()) {
            struct EffectMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 value){memory.Write32(at,value);}} guest{memory};
            auto& regs=parent.jit->Regs();
            MM3D::SongEffect::Expand(guest,regs[13],MM3D::CameraLive::desired_fov.load(),
                MM3D::Widescreen::Active(),MM3D::Widescreen::camera_zoom_out.load());
            regs[1]=regs[13]; // displaced MOV, including guarded fallback
            return;
        }
        if(swi==MM3D::FormCamera::EntrySVC||swi==MM3D::FormCamera::RadiusSVC||swi==MM3D::FormCamera::LockSVC) {
            struct CameraMemory {Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}
                void Write(u32 at,u32 word){memory.Write32(at,word);}} guest{memory};
            auto& regs=parent.jit->Regs();
            if(swi==MM3D::FormCamera::EntrySVC) {
                const bool native_entry_orbit=entry_recovery_orbit.Native(guest,regs[0]);
                form_camera.Entry(guest,regs,native_entry_orbit);
                const auto deku_lens_before=guest.Read(regs[0]+0x110).value_or(0);
                deku_free_camera.Step(guest,regs[0]);
                if(MM3D::FormCamera::diagnostics.load()) {
                    const auto actor=guest.Read(regs[0]+0xa4).value_or(0);
                    LOG_INFO(Core_ARM11,"Deku free camera: armed={} recovering={} flags3={:08x} ground={:08x} lens_before={} lens_after={}",
                        deku_free_camera.armed,deku_free_camera.recovering,
                        actor?guest.Read(actor+0x11db8).value_or(0):0,
                        actor?guest.Read(actor+0x94).value_or(0):0,
                        std::bit_cast<float>(deku_lens_before),
                        std::bit_cast<float>(guest.Read(regs[0]+0x110).value_or(0)));
                }
                const bool ocarina_before=ocarina_return_lens.pending||ocarina_return_lens.active;
                const auto ocarina_bits=ocarina_before?guest.Read(regs[0]+0x110).value_or(0):0;
                ocarina_return_lens.Step(guest,regs[0]);
                if(ocarina_before&&MM3D::FormCamera::diagnostics.load())
                    LOG_INFO(Core_ARM11,"Ocarina return lens: cam={:08x} active={} before={} after={}",
                        regs[0],ocarina_return_lens.active,std::bit_cast<float>(ocarina_bits),
                        std::bit_cast<float>(guest.Read(regs[0]+0x110).value_or(0)));
                const bool arrival_before=arrival_lens.active;
                const auto arrival_bits=guest.Read(regs[0]+0x110).value_or(0);
                const auto expected_bits=arrival_lens.last_bits;
                arrival_lens.Step(guest,regs[0]);
                if((arrival_before||arrival_lens.active)&&MM3D::FormCamera::diagnostics.load())
                    LOG_INFO(Core_ARM11,"Arrival lens step: active={} setting={} normal={} before={} expected={} after={} rate={} orbit={} orbit_target={}",
                        arrival_lens.active,guest.Read(regs[0]+0x154).value_or(0)>>16,
                        arrival_lens.NormalFov(guest,guest.Read(regs[0]+0x154).value_or(0)>>16).value_or(-1.f),
                        std::bit_cast<float>(arrival_bits),std::bit_cast<float>(expected_bits),
                        std::bit_cast<float>(guest.Read(regs[0]+0x110).value_or(0)),
                        std::bit_cast<float>(guest.Read(regs[0]+0xe8).value_or(0)),
                        arrival_lens.radius,arrival_lens.radius_target);
                if(MM3D::FormCamera::diagnostics.load())
                    LOG_INFO(Core_ARM11,"Form camera entry: cam={:08x} phase={:08x} target={:08x} radius={} saved={} native_entry_orbit={}",
                        regs[4],guest.Read(regs[4]+0x170).value_or(0),guest.Read(regs[4]+0xbc).value_or(0),
                        form_camera.radius,form_camera.saved,native_entry_orbit);
            }
            else if(swi==MM3D::FormCamera::RadiusSVC) {
                const auto before=guest.Read(regs[13]);
                form_camera.Radius(guest,regs);
                arrival_lens.Radius(guest,regs,form_camera);
                if(MM3D::FormCamera::diagnostics.load()) {
                    const auto after=guest.Read(regs[13]);
                    LOG_INFO(Core_ARM11,"Form camera orbit: cam={:08x} native={} applied={} saved={}",
                        regs[4],before?std::bit_cast<float>(*before):0.f,
                        after?std::bit_cast<float>(*after):0.f,form_camera.saved);
                }
            } else {
                const auto requested=regs[1];
                const auto cam=regs[0];
                form_camera.LockMode(guest,regs);
                if(MM3D::FormCamera::diagnostics.load()) {
                    const auto target=form_camera.Target(guest,cam,true);
                    const u32 player=target?(*target)[0]:0;
                    LOG_INFO(Core_ARM11,"Form camera lock request: cam={:08x} requested={} mode={} native_lock_allowed={} camera_flags={:08x} camera_target={:08x} player_flags={:08x} player_target={:08x}",
                        cam,requested,guest.Read(cam+0x158).value_or(0)&65535,
                        regs[15]==regs[14]&&regs[0]==0,
                        guest.Read(cam+0x170).value_or(0),guest.Read(cam+0xbc).value_or(0),
                        player?guest.Read(player+0x11db0).value_or(0):0,
                        player?guest.Read(player+0xdfc).value_or(0):0);
                }
            }
            return;
        }
        if(swi==MM3D::FormCamera::SVC) {
            struct CameraMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            const auto cam=parent.jit->Regs()[4];
            const float before_s0=std::bit_cast<float>(parent.jit->ExtRegs()[0]);
            const float before_s1=std::bit_cast<float>(parent.jit->ExtRegs()[1]);
            const auto mode=guest.Read(cam+0x158),native=guest.Read(cam+0xf0);
            form_camera.Distance(guest,cam,parent.jit->ExtRegs());
            MM3D::FormCamera::DekuFreeCamera::Distance(guest,cam,parent.jit->ExtRegs());
            const bool capture=MM3D::FormCamera::diagnostics.load();
            if(capture&&!form_trace_was_active) {form_trace_count=form_projection_count=0;form_last_native=form_last_output=form_last_radius=-1.f;form_last_mode=~0u;}
            form_trace_was_active=capture;
            if(capture) {
                ++form_trace_count;
                LOG_INFO(Core_ARM11,"Form camera distance: cam={:08x} mode={} native_radius={} s0_before={} s1_before={} s0_after={} s1_after={} saved_radius={} saved={}",
                    cam,mode?(*mode&65535):999u,native?std::bit_cast<float>(*native):0.f,
                    before_s0,before_s1,std::bit_cast<float>(parent.jit->ExtRegs()[0]),
                    std::bit_cast<float>(parent.jit->ExtRegs()[1]),form_camera.radius,form_camera.saved);
            }
            return;
        }
        if(swi==MM3D::CameraLive::ChangeModeSVC) {
            struct CameraMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            const auto cam=parent.jit->Regs()[0];
            const auto requested=parent.jit->Regs()[1];
            const auto mode_before=guest.Read(cam+0x158);
            const auto effective=form_camera.RequestedMode(guest,cam,requested);
            parent.jit->Regs()[1]=effective;
            const bool arrival_before=arrival_lens.active;
            arrival_lens.Observe(guest,cam,effective);
            ocarina_return_lens.Observe(guest,cam,effective);
            form_camera.ChangeMode(guest,cam,effective);
            entry_recovery_orbit.Observe(guest,cam,effective);
            deku_free_camera.Observe(cam,effective);
            if(MM3D::FormCamera::diagnostics.load()) {
                const auto player=guest.Read(cam+0xa4).value_or(0);
                LOG_INFO(Core_ARM11,"Arrival lens mode: before={} active={} setting={} normal={} native={} ground={:08x} requested={}",
                    arrival_before,arrival_lens.active,guest.Read(cam+0x154).value_or(0)>>16,
                    arrival_lens.NormalFov(guest,guest.Read(cam+0x154).value_or(0)>>16).value_or(-1.f),
                    std::bit_cast<float>(guest.Read(cam+0x110).value_or(0)),
                    player?guest.Read(player+0x94).value_or(0):0,effective);
                ++form_trace_count;
                LOG_INFO(Core_ARM11,"Form camera mode: cam={:08x} requested={} mode_before={} effective={} radius={} saved={}",
                    cam,requested,mode_before?(*mode_before&65535):999u,effective,form_camera.radius,form_camera.saved);
            }
            parent.jit->Regs()[4]=cam+0x100; // displaced ADD; native mode selection retained
            return;
        }
        if(swi==MM3D::CameraLive::ProjectionSVC) {
            // SVC is a synchronization boundary: R4 and S0 are live here.
            // Emulate the replaced VLDR, changing only its destination register.
            const u32 native=memory.Read32(parent.jit->Regs()[4]+0x18);
            struct CameraMemory {Memory::MemorySystem& memory;std::optional<u32> Read(u32 at){return memory.Read32OrNullopt(at);}} guest{memory};
            const auto projection=parent.jit->Regs()[4];
            const float configured=MM3D::CameraLive::ConfiguredFov(guest,projection);
            const auto output=MM3D::CameraLive::ProjectionValue(native,configured);
            parent.jit->ExtRegs()[0]=output;
            // Manual form capture remains available for all supported forms, including Deku.
            if(MM3D::FormCamera::diagnostics.load()) {
                const auto ctx=memory.Read32(0x72f5c0);
                std::optional<u32> game,player,form;
                if(ctx) game=memory.Read32(ctx+0x16c);
                if(game) player=memory.Read32(*game+0x20dc);
                if(player) form=memory.Read32(*player+0x1fc);
                const bool form_match=form&&(MM3D::FormCamera::SupportedForm(*form>>24)||(*form>>24)==3);
                if(form_match&&memory.Read32(MM3D::CameraLive::CameraPointer)==projection) {
                    const auto indices=game?memory.Read32(*game+0xa78):std::optional<u32>{};
                    const unsigned index=indices?(*indices>>16):99;
                    const auto camera=(game&&index<4)?(*game+0x408+index*0x198):0;
                    const auto mode=camera?memory.Read32(camera+0x158):std::optional<u32>{};
                    const auto radius=camera?memory.Read32(camera+0xf0):std::optional<u32>{};
                    const u32 mode_value=mode?(u32(*mode)&65535):999u;
                    const float native_value=std::bit_cast<float>(native),output_value=std::bit_cast<float>(output),
                        radius_value=radius?std::bit_cast<float>(*radius):0.f;
                    std::array<u32,6> pose{};
                    for(unsigned axis=0;axis<6;++axis)pose[axis]=memory.Read32(projection+0x28+axis*4);
                    const bool changed=pose!=form_last_pose||mode_value!=form_last_mode||std::abs(native_value-form_last_native)>.05f||
                        std::abs(output_value-form_last_output)>.05f||std::abs(radius_value-form_last_radius)>.05f;
                    if(changed) {
                        ++form_projection_count;form_last_pose=pose;form_last_mode=mode_value;form_last_native=native_value;
                        form_last_output=output_value;form_last_radius=radius_value;
                        LOG_INFO(Core_ARM11,"Form camera projection: form={} mode={} native_fov={} configured={} output_fov={} radius={} projection={:08x} eye=({},{},{}) at=({},{},{}) target={:08x} flags={:08x} camera={:08x} index={} setting={}",
                            *form>>24,mode_value,native_value,configured,output_value,radius_value,projection,
                            std::bit_cast<float>(pose[0]),std::bit_cast<float>(pose[1]),std::bit_cast<float>(pose[2]),
                            std::bit_cast<float>(pose[3]),std::bit_cast<float>(pose[4]),std::bit_cast<float>(pose[5]),
                            camera?guest.Read(camera+0xbc).value_or(0):0,camera?guest.Read(camera+0x170).value_or(0):0,
                            camera,index,camera?guest.Read(camera+0x154).value_or(0)>>16:0);
                    }
                }
            }
            return;
        }
#endif
        svc_context.CallSVC(swi);
    }

    void ExceptionRaised(VAddr pc, Dynarmic::A32::Exception exception) override {
        switch (exception) {
        case Dynarmic::A32::Exception::UndefinedInstruction:
        case Dynarmic::A32::Exception::UnpredictableInstruction:
        case Dynarmic::A32::Exception::DecodeError:
        case Dynarmic::A32::Exception::NoExecuteFault:
            break;
        case Dynarmic::A32::Exception::Breakpoint:
#ifdef ENABLE_GDBSTUB
            if (GDBStub::IsConnected()) {
                parent.SetPC(pc);
                parent.ServeBreak(SIGTRAP);
                return;
            }
#endif
            break;
        case Dynarmic::A32::Exception::SendEvent:
        case Dynarmic::A32::Exception::SendEventLocal:
        case Dynarmic::A32::Exception::WaitForInterrupt:
        case Dynarmic::A32::Exception::WaitForEvent:
        case Dynarmic::A32::Exception::Yield:
        case Dynarmic::A32::Exception::PreloadData:
        case Dynarmic::A32::Exception::PreloadDataWithIntentToWrite:
        case Dynarmic::A32::Exception::PreloadInstruction:
            return;
        }

        static constexpr auto ExceptionToString = [](Dynarmic::A32::Exception e) -> std::string {
            switch (e) {
            case Dynarmic::A32::Exception::UndefinedInstruction:
                return "UndefinedInstruction";
            case Dynarmic::A32::Exception::UnpredictableInstruction:
                return "UnpredictableInstruction";
            case Dynarmic::A32::Exception::DecodeError:
                return "DecodeError";
            case Dynarmic::A32::Exception::NoExecuteFault:
                return "NoExecuteFault";
            case Dynarmic::A32::Exception::Breakpoint:
                return "Breakpoint";
            default:
                return fmt::format("Unknown({})", e);
            }
        };

        parent.SetPC(pc);
#ifdef ENABLE_GDBSTUB
        if (GDBStub::IsConnected()) {
            parent.ServeBreak(SIGILL);
        } else
#endif
        {
            Core::ExceptionType exc_type;
            switch (exception) {
            case Dynarmic::A32::Exception::UndefinedInstruction:
            case Dynarmic::A32::Exception::UnpredictableInstruction:
            case Dynarmic::A32::Exception::DecodeError:
            case Dynarmic::A32::Exception::NoExecuteFault:
                exc_type = Core::ExceptionType::UndefinedInstruction;
                break;
            case Dynarmic::A32::Exception::Breakpoint:
                exc_type = Core::ExceptionType::Break;
                break;
            default:
                exc_type = Core::ExceptionType::UndefinedInstruction;
                break;
            }
            Core::LogException(parent.system, exc_type);
        }
    }

    void AddTicks(std::uint64_t ticks) override {
        parent.GetTimer().AddTicks(ticks);
    }
    std::uint64_t GetTicksRemaining() override {
        s64 ticks = parent.GetTimer().GetDowncount();
        return static_cast<u64>(ticks <= 0 ? 0 : ticks);
    }
    std::uint64_t GetTicksForCode(bool is_thumb, VAddr, std::uint32_t instruction) override {
        return Core::TicksForInstruction(is_thumb, instruction);
    }

    ARM_Dynarmic& parent;
    Kernel::SVCContext svc_context;
    Memory::MemorySystem& memory;
};

ARM_Dynarmic::ARM_Dynarmic(Core::System& system_, Memory::MemorySystem& memory_, u32 core_id_,
                           std::shared_ptr<Core::Timing::Timer> timer_,
                           Core::ExclusiveMonitor& exclusive_monitor_)
    : ARM_Interface(core_id_, timer_), system(system_), memory(memory_),
      cb(std::make_unique<DynarmicUserCallbacks>(*this)),
      exclusive_monitor{dynamic_cast<Core::DynarmicExclusiveMonitor&>(exclusive_monitor_)} {
    SetPageTable(memory.GetCurrentPageTable());
}

ARM_Dynarmic::~ARM_Dynarmic() = default;

MICROPROFILE_DEFINE(ARM_Jit, "ARM JIT", "ARM JIT", MP_RGB(255, 64, 64));

void ARM_Dynarmic::Run() {
    ASSERT(memory.GetCurrentPageTable() == current_page_table);
    MICROPROFILE_SCOPE(ARM_Jit);
    if (break_flag) [[unlikely]] {
        return;
    }

#ifdef ENABLE_MM3D_PRESENTATION
    if(MM3D::CameraMidpoint::enabled.load() || MM3D::CameraLive::enabled.load() || MM3D::CameraLive::capture_requested.load() || MM3D::CameraLive::desired_fov.load()>45.f || MM3D::CameraLive::needs_service.load()) {
        const auto process=system.Kernel().GetCurrentProcess();
        if(process && process->codeset && process->codeset->program_id==0x0004000000125500ULL) {
            struct CameraMemory {
                Memory::MemorySystem& memory;
                std::optional<u32> Read(u32 at) { return memory.Read32OrNullopt(at); }


            } camera_memory{memory};
            MM3D::CameraLive::Service(camera_memory,current_page_table.get(),std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }
    }
    UpdateCameraTraceMode();
    if(camera_tracing && MM3D::CameraWriter::ShouldTrace()) {
        // Bound host time independently of guest progress/rendering. Each Step
        // returns after one instruction, including conditional and idle-loop code.
        for(unsigned i=0; i<256 && GetTimer().GetDowncount()>0; ++i) {
            if(!MM3D::CameraWriter::ShouldTrace()) break;
            const auto halt = MM3D::CameraWriter::StepWithSnapshot(*jit, cb->camera_instruction);
            if((halt & Dynarmic::HaltReason::UserDefined1) == Dynarmic::HaltReason::UserDefined1) break;
        }
    } else
#endif
    {
        jit->Run();
    }
#ifdef ENABLE_MM3D_PRESENTATION
    if (MM3D::NotebookInspector::enabled.load() && current_page_table) {
        struct Pages {
            Memory::MemorySystem& memory;
            Memory::PageTable& table;
            size_t size() const { return Memory::PAGE_TABLE_NUM_ENTRIES; }
            u8* operator[](size_t i) {
                if (table.attributes[i] == Memory::PageType::RasterizerCachedMemory)
                    return memory.GetPointer(static_cast<u32>(i << 12));
                return table.GetPointerArray()[i];
            }
        } pages{memory, *current_page_table};
        MM3D::NotebookInspector::Poll(pages, current_page_table.get());
    }
#endif
}

void ARM_Dynarmic::Step() {
    if (break_flag) [[unlikely]] {
        return;
    }

#ifdef ENABLE_MM3D_PRESENTATION
    UpdateCameraTraceMode();
    if(camera_tracing && MM3D::CameraWriter::ShouldTrace()) MM3D::CameraWriter::StepWithSnapshot(*jit, cb->camera_instruction);
    else
#endif
        jit->Step();
}

#ifdef ENABLE_MM3D_PRESENTATION
void ARM_Dynarmic::UpdateCameraTraceMode() {
    const bool wanted = MM3D::CameraWriter::ShouldTrace();
    if(wanted == camera_tracing) return;
    ThreadContext saved{};
    SaveContext(saved);
    // Switching backends may spuriously fail an outstanding exclusive store;
    // ARM permits this. Do not carry a stale local reservation into the new JIT.
    jit->ClearExclusiveState();
    jit = nullptr;
    jits.swap(camera_other_jits);
    camera_tracing = wanted;
    SetPageTable(current_page_table);
    jit->ClearExclusiveState();
    LoadContext(saved);
}
#endif
void ARM_Dynarmic::SetPC(u32 pc) {
    jit->Regs()[15] = pc;
}

u32 ARM_Dynarmic::GetPC() const {
    return jit->Regs()[15];
}

u32 ARM_Dynarmic::GetReg(int index) const {
    return jit->Regs()[index];
}

void ARM_Dynarmic::SetReg(int index, u32 value) {
    jit->Regs()[index] = value;
}

u32 ARM_Dynarmic::GetVFPReg(int index) const {
    return jit->ExtRegs()[index];
}

void ARM_Dynarmic::SetVFPReg(int index, u32 value) {
    jit->ExtRegs()[index] = value;
}

u32 ARM_Dynarmic::GetVFPSystemReg(VFPSystemRegister reg) const {
    switch (reg) {
    case VFP_FPSCR:
        return jit->Fpscr();
    case VFP_FPEXC:
        return fpexc;
    default:
        UNREACHABLE_MSG("Unknown VFP system register: {}", reg);
    }

    return UINT_MAX;
}

void ARM_Dynarmic::SetVFPSystemReg(VFPSystemRegister reg, u32 value) {
    switch (reg) {
    case VFP_FPSCR:
        jit->SetFpscr(value);
        return;
    case VFP_FPEXC:
        fpexc = value;
        return;
    default:
        UNREACHABLE_MSG("Unknown VFP system register: {}", reg);
    }
}

u32 ARM_Dynarmic::GetCPSR() const {
    return jit->Cpsr();
}

void ARM_Dynarmic::SetCPSR(u32 cpsr) {
    jit->SetCpsr(cpsr);
}

u32 ARM_Dynarmic::GetCP15Register(CP15Register reg) const {
    switch (reg) {
    case CP15_THREAD_UPRW:
        return cp15_state.cp15_thread_uprw;
    case CP15_THREAD_URO:
        return cp15_state.cp15_thread_uro;
    default:
        UNREACHABLE_MSG("Unknown CP15 register: {}", reg);
    }

    return 0;
}

void ARM_Dynarmic::SetCP15Register(CP15Register reg, u32 value) {
    switch (reg) {
    case CP15_THREAD_UPRW:
        cp15_state.cp15_thread_uprw = value;
        return;
    case CP15_THREAD_URO:
        cp15_state.cp15_thread_uro = value;
        return;
    default:
        UNREACHABLE_MSG("Unknown CP15 register: {}", reg);
    }
}

void ARM_Dynarmic::SaveContext(ThreadContext& ctx) {
    ctx.cpu_registers = jit->Regs();
    ctx.cpsr = jit->Cpsr();
    ctx.fpu_registers = jit->ExtRegs();
    ctx.fpscr = jit->Fpscr();
    ctx.fpexc = fpexc;
}

void ARM_Dynarmic::LoadContext(const ThreadContext& ctx) {
    jit->Regs() = ctx.cpu_registers;
    jit->SetCpsr(ctx.cpsr);
    jit->ExtRegs() = ctx.fpu_registers;
    jit->SetFpscr(ctx.fpscr);
    fpexc = ctx.fpexc;
}

void ARM_Dynarmic::PrepareReschedule() {
    if (jit->IsExecuting()) {
        jit->HaltExecution();
    }
}

void ARM_Dynarmic::ClearInstructionCache() {
#ifdef ENABLE_MM3D_PRESENTATION
    cb->menu_page_switch={};cb->menu_memory={};cb->menu_backdrop={};cb->menu_sound_context=0;
    for(const auto& j : camera_other_jits) j.second->ClearCache();
#endif
    for (const auto& j : jits) {
        j.second->ClearCache();
    }
}

void ARM_Dynarmic::InvalidateCacheRange(u32 start_address, std::size_t length) {
    jit->InvalidateCacheRange(start_address, length);
#ifdef ENABLE_MM3D_PRESENTATION
    // Also invalidate the dormant mode's cache for the active process.
    auto other = camera_other_jits.find(current_page_table);
    if(other != camera_other_jits.end()) other->second->InvalidateCacheRange(start_address, length);
#endif
}

void ARM_Dynarmic::ClearExclusiveState() {
    jit->ClearExclusiveState();
}

std::shared_ptr<Memory::PageTable> ARM_Dynarmic::GetPageTable() const {
    return current_page_table;
}

void ARM_Dynarmic::SetPageTable(const std::shared_ptr<Memory::PageTable>& page_table) {
    current_page_table = page_table;
    ThreadContext ctx{};
    if (jit) {
        SaveContext(ctx);
    }

    auto iter = jits.find(current_page_table);
    if (iter != jits.end()) {
        jit = iter->second.get();
        LoadContext(ctx);
        return;
    }

    auto new_jit = MakeJit();
    jit = new_jit.get();
    LoadContext(ctx);
    jits.emplace(current_page_table, std::move(new_jit));
}

void ARM_Dynarmic::ServeBreak([[maybe_unused]] int signal) {
#ifdef ENABLE_GDBSTUB
    GDBStub::Break(signal);
#endif
}

std::unique_ptr<Dynarmic::A32::Jit> ARM_Dynarmic::MakeJit() {
    Dynarmic::A32::UserConfig config;
    config.callbacks = cb.get();
    if (current_page_table
#ifdef ENABLE_MM3D_PRESENTATION
        && !camera_tracing
#endif
    ) {
        config.page_table = &current_page_table->GetPointerArray();
    }
#ifdef ENABLE_MM3D_PRESENTATION
    if(camera_tracing) config.code_cache_size = 16 * 1024 * 1024;
#endif
    config.coprocessors[15] = std::make_shared<DynarmicCP15>(cp15_state);
    config.define_unpredictable_behaviour = true;

    // Multi-process state
    config.processor_id = GetID();
    config.global_monitor = &exclusive_monitor.monitor;

    return std::make_unique<Dynarmic::A32::Jit>(config);
}

} // namespace Core
