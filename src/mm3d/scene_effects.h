// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
#include <vector>
#include "mm3d/depth_of_field.h"
#include "mm3d/ambient_occlusion.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"
namespace Vulkan {class Instance;class Scheduler;class RenderManager;class DescriptorUpdateQueue;}
namespace MM3D {
// Render-thread owned. Uses a separate output image, never attachment feedback
// or CPU depth readback. Two scratch targets avoid resize churn between frames.
class SceneEffects {
public:
    SceneEffects(const Vulkan::Instance&,Vulkan::Scheduler&,Vulkan::RenderManager&,
                       Vulkan::DescriptorUpdateQueue&);
    ~SceneEffects();
    struct Input {
        vk::Image color,depth;
        vk::ImageView color_view,depth_view;
        vk::ImageAspectFlags depth_aspect;
        unsigned width,height;
        std::array<int,4> rect; // x, y, width, height in attachment pixels
        float scale;
        std::array<float,4> viewport{}; // world viewport, which can extend beyond the sampled rect
    };
    bool Process(unsigned slot,const Input&,const DepthOfField::Depth&,DepthOfField::Options,AmbientOcclusion::Options = {});
private:
    struct Target {vk::Image image{};vk::DeviceMemory memory{};vk::ImageView view{};unsigned w{},h{};};
    void Destroy(Target&);
    void Initialize();
    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::RenderManager& manager;
    Vulkan::DescriptorUpdateQueue& updates;
    std::unique_ptr<Vulkan::DescriptorHeap> heap;
    vk::PipelineLayout layout{};
    vk::Pipeline pipeline{};
    vk::Sampler color_sampler{},depth_sampler{};
    std::array<Target,2> targets{};
    struct Retired {Target target;std::uint64_t tick;};
    std::vector<Retired> retired;
};
}
