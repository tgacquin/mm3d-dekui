// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/microprofile.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/frontend/emu_window.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_present_window.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_swapchain.h"
#include "vk_platform.h"

#include <vk_mem_alloc.h>
#ifdef ENABLE_MM3D_PRESENTATION
#include "mm3d/interpolation_performance.h"
#include "mm3d/overlay.h"
#include "mm3d/pause_control.h"
#endif

MICROPROFILE_DEFINE(Vulkan_WaitPresent, "Vulkan", "Wait For Present", MP_RGB(128, 128, 128));

namespace Vulkan {

namespace {

bool CanBlitToSwapchain(const vk::PhysicalDevice& physical_device, vk::Format format) {
    const vk::FormatProperties props{physical_device.getFormatProperties(format)};
    return static_cast<bool>(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst);
}

[[nodiscard]] vk::ImageSubresourceLayers MakeImageSubresourceLayers() {
    return vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

[[nodiscard]] vk::ImageBlit MakeImageBlit(s32 frame_width, s32 frame_height, s32 swapchain_width,
                                          s32 swapchain_height) {
    return vk::ImageBlit{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffsets =
            std::array{
                vk::Offset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = frame_width,
                    .y = frame_height,
                    .z = 1,
                },
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffsets =
            std::array{
                vk::Offset3D{
                    .x = 0,
                    .y = 0,
                    .z = 0,
                },
                vk::Offset3D{
                    .x = swapchain_width,
                    .y = swapchain_height,
                    .z = 1,
                },
            },
    };
}

[[nodiscard]] vk::ImageCopy MakeImageCopy(u32 frame_width, u32 frame_height, u32 swapchain_width,
                                          u32 swapchain_height) {
    return vk::ImageCopy{
        .srcSubresource = MakeImageSubresourceLayers(),
        .srcOffset =
            vk::Offset3D{
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .dstSubresource = MakeImageSubresourceLayers(),
        .dstOffset =
            vk::Offset3D{
                .x = 0,
                .y = 0,
                .z = 0,
            },
        .extent =
            vk::Extent3D{
                .width = std::min(frame_width, swapchain_width),
                .height = std::min(frame_height, swapchain_height),
                .depth = 1,
            },
    };
}

} // Anonymous namespace

PresentWindow::PresentWindow(Frontend::EmuWindow& emu_window_, const Instance& instance_,
                             Scheduler& scheduler_, bool low_refresh_rate_, [[maybe_unused]] bool primary_window)
    : emu_window{emu_window_}, instance{instance_}, scheduler{scheduler_},
      low_refresh_rate{low_refresh_rate_},
      surface{CreateSurface(instance.GetInstance(), emu_window)}, next_surface{surface},
      swapchain{instance, emu_window.GetFramebufferLayout().width,
                emu_window.GetFramebufferLayout().height, surface, low_refresh_rate_},
      graphics_queue{instance.GetPresentQueue()}, present_renderpass{CreateRenderpass()},
      vsync_enabled{Settings::values.use_vsync.GetValue()},
      blit_supported{
          CanBlitToSwapchain(instance.GetPhysicalDevice(), swapchain.GetSurfaceFormat().format)},
      use_present_thread{Settings::values.async_presentation.GetValue()},
      last_render_surface{emu_window.GetWindowInfo().render_surface} {

    const u32 num_images = swapchain.GetImageCount();
    const vk::Device device = instance.GetDevice();

    const vk::CommandPoolCreateInfo pool_info = {
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                 vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex(),
    };
    command_pool = device.createCommandPool(pool_info);

    const vk::CommandBufferAllocateInfo alloc_info = {
        .commandPool = command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = num_images,
    };
    const std::vector command_buffers = device.allocateCommandBuffers(alloc_info);

    swap_chain.resize(num_images);
    for (u32 i = 0; i < num_images; i++) {
        Frame& frame = swap_chain[i];
        frame.cmdbuf = command_buffers[i];
        frame.render_ready = device.createSemaphore({});
        frame.present_done = device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});
        free_queue.push(&frame);
    }

    if (instance.HasDebuggingToolAttached()) {
        for (u32 i = 0; i < num_images; ++i) {
            SetObjectName(device, swap_chain[i].cmdbuf, "Swapchain Command Buffer {}", i);
            SetObjectName(device, swap_chain[i].render_ready,
                          "Swapchain Semaphore: render_ready {}", i);
            SetObjectName(device, swap_chain[i].present_done, "Swapchain Fence: present_done {}",
                          i);
        }
    }

#ifdef ENABLE_MM3D_PRESENTATION
    if (primary_window) {
        // A host presenter must remain alive when the emulation thread is paused.
        use_present_thread = true;
        // Font upload uses the same Vulkan queue; serialize with the scheduler.
        std::scoped_lock lock{scheduler.submit_mutex};
        mm3d_overlay = std::make_unique<MM3D::Overlay>(instance, GetSurfaceFormat(), num_images, &scheduler.submit_mutex);
    }
#endif
    if (use_present_thread) {
        present_thread = std::jthread([this](std::stop_token token) { PresentThread(token); });
    }
}

PresentWindow::~PresentWindow() {
    scheduler.Finish();
#ifdef ENABLE_MM3D_PRESENTATION
    if (mm3d_overlay) {
        WaitPresent();
        if (present_thread.joinable()) {
            present_thread.request_stop();
            frame_cv.notify_all();
            present_thread.join();
        }
        std::scoped_lock lock{scheduler.submit_mutex,instance.PresentQueueMutex()};
        instance.GetDevice().waitIdle();
        mm3d_overlay.reset();
        auto& background = mm3d_background;
        instance.GetDevice().destroyFramebuffer(background.framebuffer);
        instance.GetDevice().destroyImageView(background.image_view);
        if (background.image)
            vmaDestroyImage(instance.GetAllocator(), background.image, background.allocation);
    }
#endif
    const vk::Device device = instance.GetDevice();
    // If the window is destroyed before the next_surface is
    // consumed, make sure to destroy it here to prevent a
    // resource leak.
    if (next_surface && next_surface != surface) {
        instance.GetInstance().destroySurfaceKHR(next_surface);
        next_surface = vk::SurfaceKHR{};
    }
    device.destroyCommandPool(command_pool);
    device.destroyRenderPass(present_renderpass);
    for (auto& frame : swap_chain) {
        device.destroyImageView(frame.image_view);
        device.destroyFramebuffer(frame.framebuffer);
        device.destroySemaphore(frame.render_ready);
        device.destroyFence(frame.present_done);
        vmaDestroyImage(instance.GetAllocator(), frame.image, frame.allocation);
    }
}

void PresentWindow::RecreateFrame(Frame* frame, u32 width, u32 height) {
    vk::Device device = instance.GetDevice();
    if (frame->framebuffer) {
        device.destroyFramebuffer(frame->framebuffer);
    }
    if (frame->image_view) {
        device.destroyImageView(frame->image_view);
    }
    if (frame->image) {
        vmaDestroyImage(instance.GetAllocator(), frame->image, frame->allocation);
    }

    const vk::Format format = swapchain.GetSurfaceFormat().format;
    const vk::ImageCreateInfo image_info = {
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
                 vk::ImageUsageFlagBits::eTransferDst,
    };

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkImage unsafe_image{};
    VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);

    VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info, &alloc_info,
                                     &unsafe_image, &frame->allocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating texture with error {}", result);
        UNREACHABLE();
    }
    frame->image = vk::Image{unsafe_image};

    const vk::ImageViewCreateInfo view_info = {
        .image = frame->image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    frame->image_view = device.createImageView(view_info);

    const vk::FramebufferCreateInfo framebuffer_info = {
        .renderPass = present_renderpass,
        .attachmentCount = 1,
        .pAttachments = &frame->image_view,
        .width = width,
        .height = height,
        .layers = 1,
    };
    frame->framebuffer = instance.GetDevice().createFramebuffer(framebuffer_info);

    frame->width = width;
    frame->height = height;
}

Frame* PresentWindow::GetRenderFrame() {
    MICROPROFILE_SCOPE(Vulkan_WaitPresent);

    // Wait for free presentation frames
    std::unique_lock lock{free_mutex};
    free_cv.wait(lock, [this] { return !free_queue.empty(); });

    // Take the frame from the queue
    Frame* frame = free_queue.front();
    free_queue.pop();

    vk::Device device = instance.GetDevice();
    vk::Result result{};

    const auto wait = [&]() {
        result = device.waitForFences(frame->present_done, false, std::numeric_limits<u64>::max());
        return result;
    };

    // Wait for the presentation to be finished so all frame resources are free
    while (wait() != vk::Result::eSuccess) {
        // Retry if the waiting times out
        if (result == vk::Result::eTimeout) {
            continue;
        }

        // eErrorInitializationFailed occurs on Mali GPU drivers due to them
        // using the ppoll() syscall which isn't correctly restarted after a signal,
        // we need to manually retry waiting in that case
        if (result == vk::Result::eErrorInitializationFailed) {
            continue;
        }
    }

    device.resetFences(frame->present_done);
    return frame;
}

void PresentWindow::Present(Frame* frame) {
#ifdef ENABLE_MM3D_PRESENTATION
    if(frame->input_timing.id)frame->input_timing.queued=MM3D::InputLatency::Now();
#endif
    if (!use_present_thread) {
        scheduler.WaitWorker();
        CopyToSwapchain(frame);
        free_queue.push(frame);
        return;
    }

    scheduler.Record([this, frame](vk::CommandBuffer) {
        std::unique_lock lock{queue_mutex};
        present_queue.push(frame);
        frame_cv.notify_one();
    });
    // The next operation may acquire another frame without recording any GPU
    // work (the native half of an interpolated pair). With two frames and one
    // retained for paused redraws, that acquisition waits for this handoff to
    // release the retained frame. Dispatch now rather than leaving the callback
    // buffered on the waiting render thread. Worker ordering still puts it after
    // the render-ready submission; no GPU completion wait is needed.
    scheduler.DispatchWork();
}

void PresentWindow::WaitPresent() {
    if (!use_present_thread) {
        return;
    }

    // Wait for the present queue to be empty
    {
        std::unique_lock queue_lock{queue_mutex};
        frame_cv.wait(queue_lock, [this] { return present_queue.empty(); });
    }

    // The above condition will be satisfied when the last frame is taken from the queue.
    // To ensure that frame has been presented as well take hold of the swapchain
    // mutex.
    std::scoped_lock swapchain_lock{swapchain_mutex};
}

void PresentWindow::PresentThread(std::stop_token token) {
    Common::SetCurrentThreadName("VulkanPresent");
    while (!token.stop_requested()) {
        std::unique_lock lock{queue_mutex};

        // Wait for presentation frames
#ifdef ENABLE_MM3D_PRESENTATION
        if (mm3d_overlay) {
            frame_cv.wait_for(lock, std::chrono::milliseconds(16),
                              [&] { return token.stop_requested() || !present_queue.empty(); });
        } else
#endif
        Common::CondvarWait(frame_cv, lock, token, [this] { return !present_queue.empty(); });
        if (token.stop_requested()) {
            return;
        }

        // Idle redraws reuse an exclusively retained frame. Its binary render-ready
        // semaphore was consumed on the first presentation and must not be waited twice.
#ifdef ENABLE_MM3D_PRESENTATION
        if (present_queue.empty()) {
            std::exchange(lock, std::unique_lock{swapchain_mutex});
            if (mm3d_retained && MM3D::inspection_paused.load()) {
                const auto device = instance.GetDevice();
                (void)device.waitForFences(mm3d_retained->present_done, true, UINT64_MAX);
                device.resetFences(mm3d_retained->present_done);
                CopyToSwapchain(mm3d_retained, true);
            }
            continue;
        }
#endif
        // Take the frame and notify anyone waiting
        Frame* frame = present_queue.front();
        present_queue.pop();
        frame_cv.notify_one();

        // By exchanging the lock ownership we take the swapchain lock
        // before the queue lock goes out of scope. This way the swapchain
        // lock in WaitPresent is guaranteed to occur after here.
        std::exchange(lock, std::unique_lock{swapchain_mutex});

#ifdef ENABLE_MM3D_PRESENTATION
        if (mm3d_overlay && mm3d_retained) {
            // Same-size snapshot copies are ordered by GPU barriers on the present
            // queue. Only destroying/recreating that shared image needs a host wait.
            // Keep ownership until this exceptional wait completes: once returned,
            // the renderer may reset the frame's fence for its next submission.
            const bool resize_snapshot = mm3d_background.width != frame->width ||
                                         mm3d_background.height != frame->height;
            if (resize_snapshot) {
                (void)instance.GetDevice().waitForFences(mm3d_retained->present_done, true, UINT64_MAX);
            }
            std::scoped_lock fl{free_mutex};
            // GetRenderFrame still waits for present_done before any reuse. Let
            // that wait overlap preparation of the next presentation instead of
            // serializing the presenter here. Pool size and pacing are unchanged.
            free_queue.push(std::exchange(mm3d_retained, nullptr));
            free_cv.notify_one();
        }
#endif
        CopyToSwapchain(frame);
#ifdef ENABLE_MM3D_PRESENTATION
        if (mm3d_overlay) {
            mm3d_retained = frame;
            continue;
        }
#endif

        // Free the frame for reuse
        std::scoped_lock fl{free_mutex};
        free_queue.push(frame);
        free_cv.notify_one();
    }
}

void PresentWindow::NotifySurfaceChanged() {
#ifdef ANDROID
    std::scoped_lock lock{recreate_surface_mutex};

    // surfaceChanged() may notify us that a surface has changed
    // for the same surface multiple times. If that is the case
    // skip creating the surface again as that would cause a
    // vulkan ErrorNativeWindowInUseKHR.
    void* const render_surface = emu_window.GetWindowInfo().render_surface;
    if (render_surface == last_render_surface) {
        return;
    }
    last_render_surface = render_surface;

    // If an earlier notification produced a surface that CopyToSwapchain() has not consumed yet,
    // release it rather than just overwritting its handle and causing a leak.
    if (next_surface && next_surface != surface) {
        instance.GetInstance().destroySurfaceKHR(next_surface);
        next_surface = vk::SurfaceKHR{};
    }

    next_surface = CreateSurface(instance.GetInstance(), emu_window);
    recreate_surface_cv.notify_one();
#endif
}

void PresentWindow::CopyToSwapchain(Frame* frame, bool repeat) {
#ifdef ENABLE_MM3D_PRESENTATION
    MM3D::InterpolationPerformance::PresentTrace present_trace;
    present_trace.session = MM3D::InterpolationPerformance::ActiveSession();
    if (present_trace.session) {
        present_trace.owner = reinterpret_cast<std::uintptr_t>(&scheduler);
        present_trace.started = MM3D::InterpolationPerformance::Now();
        present_trace.shared_queue = instance.GetPresentQueue() == instance.GetGraphicsQueue();
    }
    const auto pacing_now=std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto pacing_deadline=midpoint_pacer.Deadline(pacing_now,frame->interpolated_pair && !repeat);
    if(pacing_deadline>pacing_now) {
        const auto target=std::chrono::steady_clock::time_point{
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::nanoseconds{pacing_deadline})};
        // OS timer coalescing can overshoot a simple sleep by several ms. Sleep
        // for the bulk, then yield for at most the final 3 ms (no unbounded spin).
        const auto coarse=target-std::chrono::milliseconds(3);
        if(std::chrono::steady_clock::now()<coarse)std::this_thread::sleep_until(coarse);
        while(std::chrono::steady_clock::now()<target)std::this_thread::yield();
    }
    if(frame->input_timing.id) {
        frame->input_timing.pacing_arrival=pacing_now/1000;
        frame->input_timing.pacing_deadline=pacing_deadline/1000;
        frame->input_timing.paired=frame->interpolated_pair && !repeat;
    }
    const auto timing_copy=frame->input_timing.id?MM3D::InputLatency::Now():0;
    if (present_trace.session) present_trace.paced = MM3D::InterpolationPerformance::Now();
#endif
    const auto recreate_swapchain = [&] {
#ifdef ANDROID
        {
            std::unique_lock lock{recreate_surface_mutex};
            recreate_surface_cv.wait(lock, [this]() { return surface != next_surface; });
            surface = next_surface;
        }
#endif
        auto& queue_mutex=instance.GetPresentQueue()==instance.GetGraphicsQueue() ?
            scheduler.submit_mutex : instance.PresentQueueMutex();
        std::scoped_lock submit_lock{queue_mutex};
        graphics_queue.waitIdle();
        swapchain.Create(frame->width, frame->height, surface, low_refresh_rate);
    };

#ifndef ANDROID
    const bool use_vsync = Settings::values.use_vsync.GetValue();
    const bool size_changed =
        swapchain.GetWidth() != frame->width || swapchain.GetHeight() != frame->height;
    const bool vsync_changed = vsync_enabled != use_vsync;
    if (vsync_changed || size_changed) [[unlikely]] {
        vsync_enabled = use_vsync;
        recreate_swapchain();
    }
#endif

    while (!swapchain.AcquireNextImage()) {
        recreate_swapchain();
    }

    const vk::Image swapchain_image = swapchain.Image();
#ifdef ENABLE_MM3D_PRESENTATION
    const auto timing_acquired=frame->input_timing.id?MM3D::InputLatency::Now():0;
    if (present_trace.session) present_trace.acquired = MM3D::InterpolationPerformance::Now();
#endif

    const vk::CommandBufferBeginInfo begin_info = {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    const vk::CommandBuffer cmdbuf = frame->cmdbuf;
    cmdbuf.begin(begin_info);
#ifdef ENABLE_MM3D_PRESENTATION
    if (mm3d_overlay) {
        auto& background = mm3d_background;
        if (background.width != frame->width || background.height != frame->height)
            RecreateFrame(&background, frame->width, frame->height);

        // Save the clean composition on guest frames; restore it before every idle
        // redraw so moving/closing translucent GUI windows leaves no trails.
        const auto source = repeat ? background.image : frame->image;
        const auto destination = repeat ? frame->image : background.image;
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        const std::array barriers{
            vk::ImageMemoryBarrier{.srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = source, .subresourceRange = range},
            vk::ImageMemoryBarrier{.srcAccessMask = vk::AccessFlagBits::eMemoryRead |
                                                    vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = destination, .subresourceRange = range}};
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
            vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barriers);
        cmdbuf.copyImage(source, vk::ImageLayout::eTransferSrcOptimal, destination,
            vk::ImageLayout::eTransferDstOptimal,
            MakeImageCopy(frame->width, frame->height, frame->width, frame->height));
        const vk::ImageMemoryBarrier ready{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = destination, .subresourceRange = range};
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, ready);
        // Host-only work on the final frame, after composition and before swapchain transfer.
        mm3d_overlay->Draw(cmdbuf, frame->framebuffer, frame->width, frame->height);
    }
#endif

    const vk::Extent2D extent = swapchain.GetExtent();
    const std::array pre_barriers{
        vk::ImageMemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eNone,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchain_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
        vk::ImageMemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = frame->image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        },
    };
    const vk::ImageMemoryBarrier post_barrier{
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::ePresentSrcKHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain_image,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        },
    };

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                           vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                           {}, {}, pre_barriers);

    if (blit_supported) {
        cmdbuf.blitImage(frame->image, vk::ImageLayout::eTransferSrcOptimal, swapchain_image,
                         vk::ImageLayout::eTransferDstOptimal,
                         MakeImageBlit(frame->width, frame->height, extent.width, extent.height),
                         Settings::values.filter_mode.GetValue() ? vk::Filter::eLinear
                                                                 : vk::Filter::eNearest);
    } else {
        cmdbuf.copyImage(frame->image, vk::ImageLayout::eTransferSrcOptimal, swapchain_image,
                         vk::ImageLayout::eTransferDstOptimal,
                         MakeImageCopy(frame->width, frame->height, extent.width, extent.height));
    }

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                           vk::PipelineStageFlagBits::eAllCommands,
                           vk::DependencyFlagBits::eByRegion, {}, {}, post_barrier);

    cmdbuf.end();

    static constexpr std::array<vk::PipelineStageFlags, 2> wait_stage_masks = {
        vk::PipelineStageFlagBits::eAllCommands,
        vk::PipelineStageFlagBits::eAllCommands,
    };

    const vk::Semaphore present_ready = swapchain.GetPresentReadySemaphore();
    const vk::Semaphore image_acquired = swapchain.GetImageAcquiredSemaphore();
    const std::array wait_semaphores = {image_acquired, frame->render_ready};

    vk::SubmitInfo submit_info = {
        .waitSemaphoreCount = repeat ? 1u : static_cast<u32>(wait_semaphores.size()),
        .pWaitSemaphores = wait_semaphores.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1u,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &present_ready,
    };

    // The render-ready semaphore links the graphics queue to this queue;
    // present_done protects frame reuse. Keep the original lock when shared.
    auto& queue_mutex=instance.GetPresentQueue()==instance.GetGraphicsQueue() ?
        scheduler.submit_mutex : instance.PresentQueueMutex();
#ifdef ENABLE_MM3D_PRESENTATION
    if (present_trace.session) present_trace.lock_requested = MM3D::InterpolationPerformance::Now();
#endif
    {
        std::scoped_lock submit_lock{queue_mutex, recreate_surface_mutex};
#ifdef ENABLE_MM3D_PRESENTATION
        if (present_trace.session) present_trace.locked = MM3D::InterpolationPerformance::Now();
#endif

        try {
            graphics_queue.submit(submit_info, frame->present_done);
        } catch (vk::DeviceLostError& err) {
            LOG_CRITICAL(Render_Vulkan, "Device lost during present submit: {}", err.what());
            UNREACHABLE();
        }

#ifdef ENABLE_MM3D_PRESENTATION
        if (present_trace.session) present_trace.submitted = MM3D::InterpolationPerformance::Now();
#endif
        swapchain.Present();
#ifdef ENABLE_MM3D_PRESENTATION
        if (present_trace.session) present_trace.returned = MM3D::InterpolationPerformance::Now();
        if(frame->input_timing.id)
            MM3D::InputLatency::Presented(frame->input_timing,timing_copy,timing_acquired,
                                        MM3D::InputLatency::Now(),repeat);
        if (mm3d_overlay) {
            mm3d_overlay->Presented(); // submission attempt, not physical scanout completion
        }
#endif
    }
#ifdef ENABLE_MM3D_PRESENTATION
    if (present_trace.session) {
        present_trace.unlocked = MM3D::InterpolationPerformance::Now();
        MM3D::InterpolationPerformance::RecordPresent(present_trace);
    }
#endif
}

vk::RenderPass PresentWindow::CreateRenderpass() {
    const vk::AttachmentReference color_ref = {
        .attachment = 0,
        .layout = vk::ImageLayout::eGeneral,
    };

    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &color_ref,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = nullptr,
    };

    const vk::AttachmentDescription color_attachment = {
        .format = swapchain.GetSurfaceFormat().format,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eTransferSrcOptimal,
    };

    const vk::RenderPassCreateInfo renderpass_info = {
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 0,
        .pDependencies = nullptr,
    };

    return instance.GetDevice().createRenderPass(renderpass_info);
}

} // namespace Vulkan
