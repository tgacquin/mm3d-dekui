// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Native-window, ROM-free exercise of Azahar's real PresentWindow path.
#include <chrono>
#include <iostream>
#include <thread>
#include <fmt/format.h>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QWindow>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/settings.h"
#include "core/frontend/emu_window.h"
#include "core/perf_stats.h"
#include "mm3d/diagnostics.h"
#include "mm3d/pause_control.h"
#include "mm3d/texture_picker.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_present_window.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#ifdef __APPLE__
#include "citra_qt/util/metal_util.h"
#endif
class FixtureWindow final : public QWindow, public Frontend::EmuWindow {
public:
    FixtureWindow() {
#ifdef __APPLE__
        setSurfaceType(QWindow::MetalSurface);
#else
        setSurfaceType(QWindow::VulkanSurface);
#endif
        setTitle("MM3D synthetic presentation fixture - no ROM");
        resize(960, 540);
        create();
#ifdef __APPLE__
        window_info.type = Frontend::WindowSystemType::MacOS;
        window_info.render_surface = MetalUtil::CreateMetalLayer(winId());
#elif defined(_WIN32)
        window_info.type = Frontend::WindowSystemType::Windows;
        window_info.render_surface = reinterpret_cast<void*>(winId());
#endif
        show();
        QCoreApplication::processEvents();
        UpdateCurrentFramebufferLayout(unsigned(960 * devicePixelRatio()), unsigned(540 * devicePixelRatio()));
    }
    void PollEvents() override { QCoreApplication::processEvents(); }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_F1 && !event->isAutoRepeat()) MM3D::ToggleOverlay();
    }
};
// Caller stops paused redraws and drains the presentation thread before readback.
static void CheckCleanBackground(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler,
                                 const Vulkan::Frame* frame) {
    const auto device = instance.GetDevice();
    // The copy can now run on a separate queue: a graphics Finish alone does
    // not guarantee presentation work has completed before this readback.
    (void)device.waitForFences(frame->present_done,true,UINT64_MAX);
    vk::UniqueDeviceMemory memory;
    auto buffer = device.createBufferUnique({.size = 4, .usage = vk::BufferUsageFlagBits::eTransferDst});
    const auto requirements = device.getBufferMemoryRequirements(*buffer);
    const auto properties = instance.GetPhysicalDevice().getMemoryProperties();
    for (unsigned i = 0; i < properties.memoryTypeCount; ++i) {
        const auto flags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & flags) == flags) {
            memory = device.allocateMemoryUnique({.allocationSize = requirements.size, .memoryTypeIndex = i});
            break;
        }
    }
    if (!memory) throw std::runtime_error("no readback memory");
    device.bindBufferMemory(*buffer, *memory, 0);
    const auto target = *buffer;
    scheduler.Record([frame, target](vk::CommandBuffer command) {
        const vk::BufferImageCopy copy{0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                      {45, 89, 0}, {1, 1, 1}};
        command.copyImageToBuffer(frame->image, vk::ImageLayout::eTransferSrcOptimal, target, copy);
        const vk::MemoryBarrier barrier{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                                        .dstAccessMask = vk::AccessFlagBits::eHostRead};
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                                {}, barrier, {}, {});
    });
    scheduler.Finish();
    const auto* bytes = static_cast<const unsigned char*>(device.mapMemory(*memory, 0, 4));
    // Accept either RGBA or BGRA channel order for the synthetic clear color.
    const bool clean = bytes[1] >= 19 && bytes[1] <= 21 &&
        ((bytes[0] >= 9 && bytes[0] <= 11 && bytes[2] >= 30 && bytes[2] <= 32) ||
         (bytes[2] >= 9 && bytes[2] <= 11 && bytes[0] >= 30 && bytes[0] <= 32));
    device.unmapMemory(*memory);
    if (!clean) throw std::runtime_error("moving paused GUI left pixels on the game image");
}
static void CheckFrameStep() {
    Core::FrameLimiter limiter{};
    limiter.SetFrameAdvancing(true);
    std::atomic<unsigned> frames{0};
    std::atomic<bool> done{false};
    std::jthread worker([&] {
        while (!done.load()) {
            limiter.DoFrameLimiting(std::chrono::microseconds(0));
            if (!done.load()) frames.fetch_add(1);
        }
    });
    MM3D::Textures::ToggleSelection(123);
    MM3D::Textures::ToggleSelection(456);
    MM3D::Textures::HideSelected();
    const bool requested = MM3D::step_requested.exchange(false);
    if (requested) limiter.AdvanceFrame(); // Same existing limiter API used by the Qt dispatcher.
    for (unsigned i = 0; i < 100 && frames.load() == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const bool stepped_once = frames.load() == 1 && limiter.IsFrameAdvancing();
    const bool hidden = MM3D::Textures::Matches(123) && MM3D::Textures::Matches(456) &&
                        !MM3D::Textures::Matches(789);
    MM3D::Textures::ToggleSelection(123);
    const bool deselected = !MM3D::Textures::IsSelected(123) && MM3D::Textures::IsSelected(456);
    MM3D::Textures::HideSelected();
    const bool group_updated = !MM3D::Textures::Matches(123) && MM3D::Textures::Matches(456);
    done.store(true);
    limiter.SetFrameAdvancing(false);
    worker.join();
    const auto remaining = MM3D::Textures::hide_until.load() - MM3D::InspectionMilliseconds();
    MM3D::Textures::Clear();
    if (!requested || !stepped_once || !hidden || !deselected || !group_updated || remaining < 4900 || remaining > 5000)
        throw std::runtime_error("hide must step once, stay paused, then retain five seconds after resume");
    std::cout << "PASS real frame limiter: hide advances once; stays paused; five seconds remain after resume\n";
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    FileUtil::SetUserPath(directory.path().toStdString());
    Common::Log::Initialize();
    Common::Log::Start();
    try {
        CheckFrameStep();
        FixtureWindow window;
        Vulkan::Instance instance(window, 0);
        Vulkan::Scheduler scheduler(instance);
        // The full renderer normally installs these callbacks.
        scheduler.RegisterOnSubmit([] {});
        scheduler.RegisterOnDispatch([] {});
        for (const bool asynchronous : {false, true}) {
            Settings::values.async_presentation = asynchronous;
            MM3D::ResetGuestFrames();
            window.resize(960, 540);
            window.PollEvents();
            window.UpdateCurrentFramebufferLayout(unsigned(960 * window.devicePixelRatio()), unsigned(540 * window.devicePixelRatio()));
            {
                Vulkan::PresentWindow presentation(window, instance, scheduler, false, true);
                // Reproduce two-image drivers even if this host's swapchain has
                // more images. Keep excess render frames checked out until
                // teardown, leaving one retained frame and one working frame.
                // GetRenderFrame resets their fences, so never wait on these
                // unused frames: no GPU work was submitted for them.
                for (u32 spare = 2; spare < presentation.ImageCount(); ++spare)
                    (void)presentation.GetRenderFrame();
                std::cout << "Testing two usable presentation frames" << std::endl;
                Vulkan::Frame* last_frame{};
                for (unsigned i = 0; i < 180; ++i) {
                    window.PollEvents();
                    if (i == 60) {
                        scheduler.Finish();
                        presentation.WaitPresent();
                        MM3D::SetInspectionPaused(true);
                        const auto before = MM3D::host_present_attempts.load();
                        for (unsigned idle = 0; idle < 90; ++idle) {
                            window.PollEvents();
                            if (idle == 10) MM3D::Pointer(45, 89, 0, true);
                            if (idle == 20) MM3D::Pointer(695, 189);
                            if (idle == 30) MM3D::Pointer(695, 189, 0, false);
                            if (idle == 40) {
                                if (!MM3D::Scroll(710, 230, 0, -2))
                                    throw std::runtime_error("moved paused GUI did not receive scroll");
                                if (MM3D::Pointer(45, 89))
                                    throw std::runtime_error("paused GUI stayed at old position");
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(16));
                        }
                        if (MM3D::host_present_attempts.load() <= before + 10 || MM3D::guest_frames != 0)
                            throw std::runtime_error("paused redraw stalled or changed guest state");
                        if (!MM3D::Textures::IsSelected(123)) MM3D::Textures::ToggleSelection(123);
                        MM3D::Textures::HideSelected();
                        if (!MM3D::step_requested.exchange(false) || !MM3D::Textures::Matches(123))
                            throw std::runtime_error("paused hide did not request a step");
                        MM3D::Textures::Restore();
                        if (!MM3D::step_requested.exchange(false) || MM3D::Textures::Matches(123))
                            throw std::runtime_error("paused restore did not request a step");
                        MM3D::SetInspectionPaused(false);
                        presentation.WaitPresent();
                        {
                            std::scoped_lock lock{scheduler.submit_mutex};
                            instance.GetDevice().waitIdle();
                        }
                        CheckCleanBackground(instance, scheduler, last_frame);
                    }
                    if (i == 30 || i == 150) {
                        QKeyEvent key(QEvent::KeyPress, Qt::Key_F1, Qt::NoModifier);
                        QCoreApplication::sendEvent(&window, &key);
                    }
                    const unsigned width = unsigned((i < 90 ? 960 : 1280) * window.devicePixelRatio());
                    const unsigned height = unsigned(540 * window.devicePixelRatio());
                    if (i == 90) {
                        window.resize(1280, 540);
                        window.UpdateCurrentFramebufferLayout(width, height);
                        window.PollEvents();
                    }
                    auto* frame = presentation.GetRenderFrame();
                    last_frame = frame;
                    frame->interpolated_pair = i < 30; // 15 visual pairs; no guest updates.
                    if (frame->width != width || frame->height != height) {
                        presentation.WaitPresent();
                        scheduler.Finish();
                        presentation.RecreateFrame(frame, width, height);
                    }
                    const auto renderpass = presentation.Renderpass();
                    scheduler.Record([frame, renderpass, width, height](vk::CommandBuffer command) {
                        const vk::ClearValue clear{vk::ClearColorValue{std::array<float, 4>{.04f, .08f, .12f, 1}}};
                        command.beginRenderPass({.renderPass = renderpass, .framebuffer = frame->framebuffer,
                            .renderArea = {{0, 0}, {width, height}}, .clearValueCount = 1, .pClearValues = &clear},
                            vk::SubpassContents::eInline);
                        command.endRenderPass();
                    });
                    scheduler.Flush(frame->render_ready);
                    presentation.Present(frame);
                    // A caller may wait without a subsequent draw or Finish.
                    // Deferred diagnostic handoffs must reach the presenter too.
                    if (i == 15) presentation.WaitPresent();
                    // Burst after the paced pairs to exercise in-flight frame
                    // reuse, then pause/read back the retained clean background.
                    // Never modifies the guest clock or frame limiter.
                    if (i < 30 || i >= 60)
                        std::this_thread::sleep_for(std::chrono::milliseconds(8));
                }
                scheduler.Finish();
                presentation.WaitPresent();
                instance.GetDevice().waitIdle();
                if (MM3D::host_present_attempts <= 190 || MM3D::guest_frames != 0)
                    throw std::runtime_error("Present hook count mismatch or guest advanced");
                std::cout << "PASS native swapchain: async=" << asynchronous
                          << " host_attempts=" << MM3D::host_present_attempts.load()
                          << " guest_frames=0; paused redraw, dragging, scroll, hide/restore requests, F1 and resize\n";
            }
            if (MM3D::presentation_available) throw std::runtime_error("Overlay survived teardown");
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL native swapchain: " << error.what() << '\n';
        return 1;
    }
}
