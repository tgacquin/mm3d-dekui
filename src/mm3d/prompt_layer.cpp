// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/prompt_layer.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include <stdexcept>
namespace MM3D {
PromptLayer::PromptLayer(const Vulkan::Instance& i, Vulkan::Scheduler& s, Vulkan::RenderManager& m, bool depth)
    : instance(i), scheduler(s), manager(m), with_depth(depth) {}
PromptLayer::~PromptLayer() {
    manager.EndRendering(); scheduler.Finish(); Destroy();
}
void PromptLayer::Destroy() {
    pending_sample_barrier=false;
    auto device = instance.GetDevice();
    device.destroyFramebuffer(framebuffer); device.destroyImageView(view);
    device.destroyImage(image); device.freeMemory(memory);
    device.destroyImageView(depth_sample_view);depth_sample_view=nullptr;
    device.destroyImageView(depth_view); device.destroyImage(depth_image); device.freeMemory(depth_memory);
    depth_view=nullptr; depth_image=nullptr; depth_memory=nullptr;
    framebuffer = nullptr; view = nullptr; image = nullptr; memory = nullptr;
}
vk::ImageView PromptLayer::DepthSampleView() {
    if(depth_image&&!depth_sample_view)depth_sample_view=instance.GetDevice().createImageView({
        .image=depth_image,.viewType=vk::ImageViewType::e2D,
        .format=instance.GetTraits(VideoCore::PixelFormat::D24S8).native,
        .subresourceRange={vk::ImageAspectFlagBits::eDepth,0,1,0,1}});
    return depth_sample_view;
}
bool PromptLayer::Begin(unsigned w, unsigned h, float depth_clear, bool reset_depth) {
    if (!w || !h || w > 8192 || h > 8192) return false;
    // Do not silently discard earlier matches when multiple viewports occur in one frame.
    if (visible && (w != width || h != height)) return false;
    if ((scheduler.CapturingReplay() || scheduler.NativeDrawActive()) && visible && !reset_depth &&
        manager.MatchesFramebuffer(framebuffer, w, h)) {
        return true;
    }
    manager.EndRendering();
    const auto pass = manager.GetRenderpass(VideoCore::PixelFormat::RGBA8,
                                            with_depth ? VideoCore::PixelFormat::D24S8 : VideoCore::PixelFormat::Invalid, false);
    bool fresh = false;
    if (!image || w != width || h != height) {
        scheduler.Finish(); Destroy();
        width = w; height = h;
        const auto device = instance.GetDevice();
        const vk::ImageCreateInfo info{
            .imageType = vk::ImageType::e2D, .format = vk::Format::eR8G8B8A8Unorm,
            .extent = {w,h,1}, .mipLevels = 1, .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc};
        image = device.createImage(info);
        const auto req = device.getImageMemoryRequirements(image);
        const auto props = instance.GetPhysicalDevice().getMemoryProperties();
        unsigned type = 0;
        for (; type < props.memoryTypeCount; ++type)
            if ((req.memoryTypeBits & (1u << type)) &&
                (props.memoryTypes[type].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) break;
        if (type == props.memoryTypeCount) throw std::runtime_error("No prompt layer memory type");
        memory = device.allocateMemory({.allocationSize=req.size, .memoryTypeIndex=type});
        device.bindImageMemory(image, memory, 0);
        view = device.createImageView({.image=image, .viewType=vk::ImageViewType::e2D,
            .format=vk::Format::eR8G8B8A8Unorm,
            .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}});
        if(with_depth) {
            const auto format=instance.GetTraits(VideoCore::PixelFormat::D24S8).native;
            auto depth_info=info;depth_info.format=format;
            depth_info.usage=vk::ImageUsageFlagBits::eDepthStencilAttachment|vk::ImageUsageFlagBits::eTransferDst|vk::ImageUsageFlagBits::eSampled;
            depth_image=device.createImage(depth_info);
            const auto depth_req=device.getImageMemoryRequirements(depth_image);
            unsigned depth_type=0;
            for(;depth_type<props.memoryTypeCount;++depth_type)
                if((depth_req.memoryTypeBits&(1u<<depth_type)) &&
                   (props.memoryTypes[depth_type].propertyFlags&vk::MemoryPropertyFlagBits::eDeviceLocal))break;
            if(depth_type==props.memoryTypeCount)throw std::runtime_error("No preview depth memory type");
            depth_memory=device.allocateMemory({.allocationSize=depth_req.size,.memoryTypeIndex=depth_type});
            device.bindImageMemory(depth_image,depth_memory,0);
            depth_view=device.createImageView({.image=depth_image,.viewType=vk::ImageViewType::e2D,
                .format=format,.subresourceRange={vk::ImageAspectFlagBits::eDepth|vk::ImageAspectFlagBits::eStencil,0,1,0,1}});
        }
        const std::array<vk::ImageView,2> attachments{view,depth_view};
        framebuffer = device.createFramebuffer({.renderPass=pass, .attachmentCount=with_depth?2u:1u,
            .pAttachments=attachments.data(), .width=w, .height=h, .layers=1});
        fresh = true;
    }
    const bool clear = !visible;
    // Consecutive private replay draws only write attachments. No shader has
    // sampled these images yet, so avoid fragment/transfer-wide dependencies.
    const bool continuing=pending_sample_barrier && !fresh && !clear && !reset_depth;
    const auto background=with_depth && profile.id!="@camera_midpoint_hud" ?
        clear_color : std::array<float,4>{0,0,0,0};
    scheduler.Record([image=image, fresh, clear, continuing, background](vk::CommandBuffer cmd) {
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor,0,1,0,1};
        vk::ImageMemoryBarrier barrier{
            .srcAccessMask=fresh ? vk::AccessFlags{} : vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
            .dstAccessMask=vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eColorAttachmentRead |
                           vk::AccessFlagBits::eColorAttachmentWrite,
            .oldLayout=fresh ? vk::ImageLayout::eUndefined : vk::ImageLayout::eGeneral,
            .newLayout=vk::ImageLayout::eGeneral, .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED, .image=image, .subresourceRange=range};
        if(continuing) {
            barrier.srcAccessMask=vk::AccessFlagBits::eColorAttachmentWrite;
            barrier.dstAccessMask=vk::AccessFlagBits::eColorAttachmentRead|vk::AccessFlagBits::eColorAttachmentWrite;
        }
        cmd.pipelineBarrier(continuing?vk::PipelineStageFlagBits::eColorAttachmentOutput:vk::PipelineStageFlagBits::eAllCommands,
            continuing?vk::PipelineStageFlags(vk::PipelineStageFlagBits::eColorAttachmentOutput):
                vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eColorAttachmentOutput,
            {}, {}, {}, barrier);
        if (clear) {
            cmd.clearColorImage(image, vk::ImageLayout::eGeneral,
                vk::ClearColorValue{background}, range);
            barrier.srcAccessMask=vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask=vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
            barrier.oldLayout=vk::ImageLayout::eGeneral;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {}, barrier);
        }
    });
    if(with_depth) scheduler.Record([depth=depth_image,fresh,clear,reset_depth,depth_clear,continuing](vk::CommandBuffer cmd) {
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eDepth|vk::ImageAspectFlagBits::eStencil,0,1,0,1};
        vk::ImageMemoryBarrier barrier{
            .srcAccessMask=fresh?vk::AccessFlags{}:vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask=vk::AccessFlagBits::eTransferWrite|vk::AccessFlagBits::eDepthStencilAttachmentRead|vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .oldLayout=fresh?vk::ImageLayout::eUndefined:vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=depth,.subresourceRange=range};
        const auto tests=vk::PipelineStageFlagBits::eEarlyFragmentTests|vk::PipelineStageFlagBits::eLateFragmentTests;
        if(continuing)barrier.dstAccessMask=vk::AccessFlagBits::eDepthStencilAttachmentRead|vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        cmd.pipelineBarrier(continuing?tests:vk::PipelineStageFlags(vk::PipelineStageFlagBits::eAllCommands),
            continuing?tests:vk::PipelineStageFlagBits::eTransfer|tests,{}, {}, {},barrier);
        if(clear || reset_depth) {
            auto clear_range=range;
            // A screen-space stencil mask may precede the first world draw.
            // Choosing that world's depth convention must preserve the mask.
            if(!clear)clear_range.aspectMask=vk::ImageAspectFlagBits::eDepth;
            cmd.clearDepthStencilImage(depth,vk::ImageLayout::eGeneral,vk::ClearDepthStencilValue{depth_clear,0},clear_range);
            barrier.srcAccessMask=vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask=vk::AccessFlagBits::eDepthStencilAttachmentRead|vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eEarlyFragmentTests|vk::PipelineStageFlagBits::eLateFragmentTests,{}, {}, {},barrier);
        }
    });
    manager.BeginRendering(Vulkan::RenderPass{.framebuffer=framebuffer, .render_pass=pass,
        .render_area={{0,0},{w,h}}, .clear={}, .do_clear=0});
    visible = true;
    return true;
}
void PromptLayer::End(bool prepare_sampling) {
    if ((!scheduler.CapturingReplay() && !scheduler.NativeDrawActive()) || prepare_sampling)
        manager.EndRendering();
    pending_sample_barrier=true;
    if(prepare_sampling)PrepareForSampling();
}
void PromptLayer::PrepareForSampling() {
    if(!pending_sample_barrier)return;
    manager.EndRendering();
    pending_sample_barrier=false;
    scheduler.Record([image=image](vk::CommandBuffer cmd) {
        const vk::ImageMemoryBarrier barrier{
            .srcAccessMask=vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask=vk::AccessFlagBits::eShaderRead,
            .oldLayout=vk::ImageLayout::eGeneral, .newLayout=vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=image, .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);
    });
}
}
