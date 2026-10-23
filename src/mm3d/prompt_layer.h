// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <string_view>
#include <fmt/format.h>
#include "mm3d/profiles.h"
#include "video_core/renderer_vulkan/vk_platform.h"
namespace Vulkan { class Instance; class Scheduler; class RenderManager; }
namespace MM3D {
inline std::atomic<unsigned> prompt_matches{0}, prompt_replays{0}, prompt_unsupported{0};
// Emulation/render thread only. Composited before frame submission, never shared with ImGui.
class PromptLayer {
public:
    PromptLayer(const Vulkan::Instance&, Vulkan::Scheduler&, Vulkan::RenderManager&, bool with_depth = false);
    ~PromptLayer();
    bool Begin(unsigned width, unsigned height, float depth_clear = 1.f,
               bool reset_depth = false);
    void End(bool prepare_sampling = true);
    void PrepareForSampling();
    void NextFrame(std::array<float,4> background = {0,0,0,1}) {
        visible = false; clear_color = background;
    }
    bool Visible() const { return visible; }
    Profiles::Profile profile;
    vk::ImageView View() const { return view; }
    vk::Image Image() const { return image; }
    vk::ImageView DepthSampleView();
    std::array<vk::Image, 2> Attachments() const { return {image, depth_image}; }
    unsigned Width() const { return width; }
    unsigned Height() const { return height; }
    bool CanBatch(unsigned w, unsigned h) const {
        return image && w == width && h == height;
    }
    // Source guest viewport, in target pixels, used to crop/rotate the layer at presentation.
    int x{}, y{}, viewport_width{}, viewport_height{};
private:
    void Destroy();
    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::RenderManager& manager;
    vk::Image image{};
    vk::DeviceMemory memory{};
    vk::ImageView view{};
    vk::Framebuffer framebuffer{};
    vk::Image depth_image{};
    vk::DeviceMemory depth_memory{};
    vk::ImageView depth_view{};
    vk::ImageView depth_sample_view{};
    bool with_depth{};
    unsigned width{}, height{};
    bool visible{};
    std::array<float,4> clear_color{0,0,0,1};
    bool pending_sample_barrier{};
};
}
