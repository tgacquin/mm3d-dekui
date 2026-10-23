// Copyright 2022-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once
#include "mm3d/glare_interpolation.h"
#include "mm3d/water_model_interpolation.h"
#include "mm3d/moving_effect_interpolation.h"
#ifdef ENABLE_MM3D_PRESENTATION
#include <map>
#include "mm3d/profiles.h"
#include "mm3d/mirror_projection_interpolation.h"
#include "mm3d/atlas_render.h"
#include "mm3d/camera_midpoint.h"
#include "mm3d/world_map_interpolation.h"
#include "mm3d/link_interpolation.h"
#include "mm3d/ripple_interpolation.h"
#include "mm3d/forest_particle_interpolation.h"
#include "mm3d/snow_interpolation.h"
#include "mm3d/fire_impact_interpolation.h"
#include "mm3d/torch_interpolation.h"
#include "mm3d/water_particle_interpolation.h"
#include "mm3d/burst_particle_interpolation.h"
#include "mm3d/spark_interpolation.h"
#include "mm3d/firework_interpolation.h"
#include "mm3d/chest_particle_interpolation.h"
#include "mm3d/sword_trail_interpolation.h"
#include "mm3d/shockwave_interpolation.h"
#include "mm3d/bubble_interpolation.h"
#include "mm3d/ice_particle_interpolation.h"
#include "mm3d/leaf_particle_interpolation.h"
#include "mm3d/moth_interpolation.h"
#include "mm3d/message_interpolation.h"
#include "mm3d/item_slide_interpolation.h"
#include "mm3d/file_select_interpolation.h"
#include "mm3d/inventory_interpolation.h"
#include "mm3d/banner_interpolation.h"
#include "mm3d/magic_meter_interpolation.h"
#include "mm3d/lens_interpolation.h"
#include "mm3d/letterbox_interpolation.h"
#include "mm3d/scene_effects.h"
#include "mm3d/heart_interpolation.h"
#include "mm3d/splash_interpolation.h"
#include "mm3d/song_effect_interpolation.h"
#include "mm3d/fire_effect_interpolation.h"
#include "mm3d/garo_flame_interpolation.h"
#include "mm3d/pause_capture.h"
#include "mm3d/photo_capture.h"
#include "mm3d/photo_save_queue.h"
#include "video_core/renderer_vulkan/vk_photo_transfer.h"
#include "mm3d/visibility_depth.h"
#include "video_core/renderer_vulkan/vk_visibility_depth.h"
#endif

#include "video_core/rasterizer_accelerated.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

namespace Frontend {
class EmuWindow;
}

namespace MM3D { class PromptLayer; }

namespace VideoCore {
class CustomTexManager;
class RendererBase;
} // namespace VideoCore

namespace Pica {
struct DisplayTransferConfig;
struct MemoryFillConfig;
struct FramebufferConfig;
} // namespace Pica

namespace Vulkan {

struct ScreenInfo;

class Instance;
class Scheduler;
class RenderManager;

class RasterizerVulkan : public VideoCore::RasterizerAccelerated
#ifdef ENABLE_MM3D_PRESENTATION
    , public MM3D::PauseCapture::Backend, public MM3D::PhotoCapture::Backend, public MM3D::VisibilityDepth::Backend
#endif
{
public:
    explicit RasterizerVulkan(Memory::MemorySystem& memory, Pica::PicaCore& pica,
                              VideoCore::CustomTexManager& custom_tex_manager,
                              VideoCore::RendererBase& renderer, Frontend::EmuWindow& emu_window,
                              const Instance& instance, Scheduler& scheduler,
                              RenderManager& renderpass_cache, DescriptorUpdateQueue& update_queue,
                              u32 image_count);
    ~RasterizerVulkan() override;

    void TickFrame();
#ifdef ENABLE_MM3D_PRESENTATION
    void EndGuestFrame() override;
    const auto& PromptOverlays() const { return prompt_layers; }
    MM3D::CameraMidpoint::PresentationStatus MidpointStatus(u64 completed) const {
        using namespace MM3D::CameraMidpoint;
        if(!midpoint_target_frame)return PresentationStatus::WaitingScene;
        if(!IsCompletedCapture(midpoint_target_frame,completed))return PresentationStatus::FrameMismatch;
        if(!midpoint_frame_interpolated)return PresentationStatus::NoMidpoint;
        if(midpoint_hud_frame!=midpoint_target_frame)return PresentationStatus::NoHud;
        if(midpoint_hud_failed || midpoint_scene_clear.Pending(*midpoint_target_frame))
            return PresentationStatus::UnsupportedHud;
        return PresentationStatus::Active;
    }
    std::optional<u32> ReadVisibilityDepth(u32 address, const MM3D::VisibilityDepth::Context& context) override;
    void ResetVisibilityDepth() override;
    void CapturePause(u32 source,unsigned eye) override;
    void BindPause(u32 texture,unsigned eye,const MM3D::PauseCapture::Probes& probes) override;
    void ResetPause() override;
    void ResetPhoto() override;
    void CapturePhoto(u32 source,unsigned eye) override;
    bool BindPhoto(u32 texture,unsigned eye,const MM3D::PhotoCapture::Probes& probes) override;
    void KeepPhoto(std::span<const u8> native) override;
    void LoadPhoto(std::span<const u8> native) override;
#endif
    void LoadDefaultDiskResources(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) override;

    void DrawTriangles() override;
    void FlushAll() override;
    void FlushRegion(PAddr addr, u32 size) override;
    void InvalidateRegion(PAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(PAddr addr, u32 size) override;
    void ClearAll(bool flush) override;
    bool AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateFill(const Pica::MemoryFillConfig& config) override;
    bool AccelerateDisplay(const Pica::FramebufferConfig& config, PAddr framebuffer_addr,
                           u32 pixel_stride, ScreenInfo& screen_info);
    bool AccelerateDrawBatch(bool is_indexed) override;

    /// Switches the disk resources to the specified title
    void SwitchDiskResources(u64 title_id) override;

private:
#ifdef ENABLE_MM3D_PRESENTATION
    std::map<std::string,std::unique_ptr<MM3D::PromptLayer>> prompt_layers;
    std::vector<MM3D::Profiles::Profile> mm3d_draw_profiles;
    bool panel_excluded = false;
    std::vector<MM3D::Profiles::Rule> panel_draw_identities;
    MM3D::Atlas::Uniforms atlas_uniforms{};
    MM3D::CameraMidpoint::History midpoint_history;
    MM3D::WorldMapInterpolation::History world_map_history;
    std::unique_ptr<MM3D::SceneEffects> depth_of_field;
    struct DofScene {
        u64 frame{};
        PAddr color{},depth{};
        std::optional<MM3D::DepthOfField::Depth> transform;
        bool applied{};
        std::array<float,4> viewport{};
    } dof_scene;

    MM3D::CameraMidpoint::ColorClears midpoint_color_clears;
    // CPU replay uploads into the stream buffer before deferred commands are
    // recorded; those commands never retain pointers into this scratch storage.
    std::vector<HardwareVertex> midpoint_cpu_vertices;
    MM3D::LinkInterpolation::History link_history;
    MM3D::GaroFlameInterpolation::History garo_flame_history;
    MM3D::FireEffectInterpolation::History fire_effect_history;
    std::optional<MM3D::FireEffectInterpolation::Values> fire_effect_overrides;
    MM3D::SongEffectInterpolation::History song_history;
    std::optional<MM3D::SongEffectInterpolation::Values> song_overrides;
    MM3D::LinkInterpolation::Overrides link_overrides{};
    std::vector<MM3D::LinkPoseCapture::LivePose> link_poses;
    std::optional<u64> link_frame;
    unsigned link_generation{};
    std::optional<MM3D::CameraMidpoint::Matrix> midpoint_projection;
    std::optional<std::array<std::array<float,4>,96>> gpu_midpoint_uniforms;
    unsigned midpoint_generation{};
    unsigned midpoint_width{}, midpoint_height{};
    unsigned midpoint_copies{}, midpoint_rejected{}, midpoint_fallback{};
    std::optional<u64> midpoint_target_frame, midpoint_fade_frame, midpoint_hud_frame, midpoint_title_depth_frame;
    std::optional<u64> midpoint_world_frame;
    std::optional<u64> midpoint_opaque_world_frame;
    std::optional<u64> midpoint_stencil_frame;
    bool midpoint_hud_failed{}, midpoint_frame_interpolated{};
    std::array<u64,6> midpoint_rejection_logged{};
    std::array<u64,2> midpoint_transfer_logged{};
    u64 glare_trace_session{}, glare_trace_frame{};
    unsigned glare_trace_samples{};
    bool visibility_depth_enabled{};
    bool visibility_depth_refresh_enabled{};
    std::unique_ptr<VisibilityDepthCache> visibility_depth;
    MM3D::WaterModelInterpolation::History water_model_history;
    MM3D::MirrorProjectionInterpolation::History mirror_projection_history;
    std::optional<MM3D::MirrorProjectionInterpolation::Sample> mirror_projection_override;
    MM3D::MovingEffectInterpolation::History moving_effect_history;
    std::optional<MM3D::MovingEffectInterpolation::Colors> moving_effect_colors;
    std::optional<std::pair<unsigned,float>> water_model_alpha_override;
    MM3D::GlareInterpolation glare_history;
    MM3D::GlareInterpolation::ScreenHistory glare_cpu_history;
    std::optional<float> glare_alpha_override;
    MM3D::RippleInterpolation::History ripple_history;
    MM3D::ForestParticleInterpolation::History forest_particle_history;
    MM3D::SnowInterpolation::History snow_history;
    MM3D::FireImpactInterpolation::History fire_impact_history;
    MM3D::LeafParticleInterpolation::History leaf_particle_history;
    MM3D::MothInterpolation::History moth_history;
    MM3D::MessageInterpolation::History message_history;
    MM3D::ItemSlideInterpolation::History item_slide_history;
    std::optional<MM3D::ItemSlideInterpolation::Pose> item_slide_override;
    MM3D::InventoryInterpolation::History inventory_history;
    MM3D::BannerInterpolation::History banner_history;
    MM3D::MagicMeterInterpolation::History magic_meter_history;
    MM3D::HeartInterpolation::History heart_history;
    MM3D::LensInterpolation::History lens_history;
    MM3D::LetterboxInterpolation::History letterbox_history;
    std::optional<MM3D::InventoryInterpolation::Pose> inventory_override;
    MM3D::FileSelectInterpolation::History file_select_history;
    std::optional<MM3D::FileSelectInterpolation::Pose> file_select_override;
    std::optional<MM3D::MessageInterpolation::Pose> message_override;
    MM3D::TorchInterpolation::History torch_history;
    MM3D::WaterParticleInterpolation::History water_particle_history;
    MM3D::BurstParticleInterpolation::History burst_particle_history;
    MM3D::SparkInterpolation::History spark_history;
    MM3D::FireworkInterpolation::History firework_history;
    MM3D::ChestParticleInterpolation::History chest_particle_history;
    MM3D::SwordTrailInterpolation::History sword_trail_history;
    MM3D::ShockwaveInterpolation::History shockwave_history;
    MM3D::BubbleInterpolation::History bubble_history;
    MM3D::IceParticleInterpolation::History ice_particle_history;
    MM3D::SplashInterpolation::History splash_history, splash_cpu_history;
    std::optional<std::array<u32,3>> splash_offsets;
    bool splash_blend{};
    std::optional<std::array<u32,2>> ripple_offsets;
    std::optional<u32> glare_vertex_offset;
    MM3D::CameraMidpoint::SceneCopies midpoint_scene_copies;
    MM3D::CameraMidpoint::SceneClearPair midpoint_scene_clear;
    s64 midpoint_last_draw_ms{};
#endif
    /// Syncs pipeline state from PICA registers
    void SyncDrawState();

    /// Syncs and uploads the lighting, fog and proctex LUTs
    void SyncAndUploadLUTs();
    void SyncAndUploadLUTsLF();

    /// Syncs all enabled PICA texture units
    void SyncTextureUnits(const Framebuffer* framebuffer);

    /// Syncs all utility textures in the fragment shader.
    void SyncUtilityTextures(const Framebuffer* framebuffer);

    /// Binds the PICA shadow cube required for shadow mapping
    void BindShadowCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                        vk::DescriptorSet texture_set);

    /// Binds a texture cube to texture unit 0
    void BindTextureCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                         vk::DescriptorSet texture_set);

    /// Upload the uniform blocks to the uniform buffer object
    void UploadUniforms(bool accelerate_draw);

    /// Generic draw function for DrawTriangles and AccelerateDrawBatch
    bool Draw(bool accelerate, bool is_indexed);

    /// Internal implementation for AccelerateDrawBatch
    bool AccelerateDrawBatchInternal(bool is_indexed);

    /// Setup index array for AccelerateDrawBatch
    void SetupIndexArray();

    /// Setup vertex array for AccelerateDrawBatch
    void SetupVertexArray();

    /// Setup the fixed attribute emulation in vulkan
    void SetupFixedAttribs();

    /// Setup vertex shader for AccelerateDrawBatch
    bool SetupVertexShader();

    /// Setup geometry shader for AccelerateDrawBatch
    bool SetupGeometryShader();

    /// Creates the vertex layout struct used for software shader pipelines
    void MakeSoftwareVertexLayout();

private:
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;
    PipelineCache pipeline_cache;
    TextureRuntime runtime;
    RasterizerCache res_cache;
#ifdef ENABLE_MM3D_PRESENTATION
    std::array<std::unique_ptr<Surface>,2> pause_images;
    std::array<u32,2> pause_textures{};
    std::array<unsigned,2> pause_orientation{};
    std::array<int,4> pause_draw{};
    std::array<std::unique_ptr<Surface>,2> photo_images;
    std::array<u32,2> photo_textures{};
    std::array<unsigned,2> photo_orientation{};
    std::string photo_saved_key;
    std::array<std::shared_ptr<const PhotoTransfer::Pixels>,2> photo_saved_pixels;
    MM3D::PhotoCapture::SaveQueue photo_saves;
#endif

    VertexLayout software_layout;
    std::array<u32, 16> binding_offsets{};
    std::array<bool, 16> enable_attributes{};
    std::array<vk::Buffer, 16> vertex_buffers;
    u32 replay_index_offset{};
    vk::IndexType replay_index_type{};
    VertexArrayInfo vertex_info;
    PipelineInfo pipeline_info{};

    StreamBuffer stream_buffer;     ///< Vertex+Index buffer
    StreamBuffer uniform_buffer;    ///< Uniform buffer
    StreamBuffer texture_buffer;    ///< Texture buffer
    StreamBuffer texture_lf_buffer; ///< Texture Light-Fog buffer
    vk::UniqueBufferView texture_lf_view;
    vk::UniqueBufferView texture_rg_view;
    vk::UniqueBufferView texture_rgba_view;
    vk::DeviceSize uniform_buffer_alignment;
    u32 uniform_size_aligned_vs_pica;
    u32 uniform_size_aligned_vs;
    u32 uniform_size_aligned_fs;
    bool async_shaders{false};
};

} // namespace Vulkan
