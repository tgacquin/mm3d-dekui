// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// ROM-free GPU test: exercise the real overlay on a synthetic Azahar-compatible render target.
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <QCoreApplication>
#include <QTemporaryDir>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "core/frontend/emu_window.h"
#include "mm3d/diagnostics.h"
#include "mm3d/interpolation_performance.h"
#include "mm3d/pass_switch_fixture.h"
#include "mm3d/overlay.h"
#include "mm3d/texture_picker.h"
#include "mm3d/prompt_layer.h"
#include "mm3d/atlas_render.h"
#include "mm3d/pause_capture.h"
#include "mm3d/photo_capture.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_uniforms.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/rasterizer_cache/utils.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"
#include "video_core/renderer_vulkan/vk_photo_transfer.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"

namespace MM3D { void RunSceneEffectsFixture(const Vulkan::Instance&, Vulkan::Scheduler&); }
namespace MM3D { void RunGpuInterpolationFixture(const Vulkan::Instance&, Vulkan::Scheduler&); }
namespace MM3D { void RunVisibilityDepthFixture(const Vulkan::Instance&, Vulkan::Scheduler&); }

class HeadlessWindow final : public Frontend::EmuWindow {
public:
    HeadlessWindow() {
        // ImGui's unmodified Vulkan backend loads WSI helpers even for offscreen rendering.
        // Enable the platform instance extensions without creating any native surface.
#ifdef __APPLE__
        window_info.type = Frontend::WindowSystemType::MacOS;
#elif defined(_WIN32)
        window_info.type = Frontend::WindowSystemType::Windows;
#else
        window_info.type = Frontend::WindowSystemType::X11;
#endif
    }
    ~HeadlessWindow() override = default;
    void PollEvents() override {}
};
static void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// Hold the real GPU queue behind a host-signalled timeline semaphore. Two
// skipped submissions must return while it is blocked; the third must wait for
// the oldest. A watchdog releases the gate even if the implementation regresses
// to Finish(), so the negative case fails rather than deadlocking the test.
static void SkippedFrameFixture(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler) {
    Require(instance.IsTimelineSemaphoreSupported(), "skipped-frame fixture requires timeline semaphores");
    const auto device = instance.GetDevice();
    vk::UniqueDeviceMemory memory;
    auto buffer = device.createBufferUnique({.size = 12, .usage = vk::BufferUsageFlagBits::eTransferDst});
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
    Require(bool(memory), "skipped-frame readback memory available");
    device.bindBufferMemory(*buffer, *memory, 0);
    const vk::SemaphoreTypeCreateInfo type{.semaphoreType = vk::SemaphoreType::eTimeline};
    auto gate = device.createSemaphoreUnique({.pNext = &type});
    std::atomic<bool> released{};
    std::jthread watchdog([&] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        released = true;
        device.signalSemaphoreKHR({.semaphore = *gate, .value = 1});
    });
    scheduler.Flush(nullptr, *gate);
    const auto first_tick = scheduler.CurrentTick();
    bool returned_before_release = true;
    for (u32 i = 0; i < 3; ++i) {
        scheduler.Record([target = *buffer, i](vk::CommandBuffer command) {
            command.fillBuffer(target, i * 4, 4, 0x12340000u + i);
            const vk::MemoryBarrier barrier{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                                            .dstAccessMask = vk::AccessFlagBits::eHostRead};
            command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                                    {}, barrier, {}, {});
        });
        scheduler.FlushSkippedFrame();
        if (i < 2) returned_before_release &= !released.load();
    }
    const bool bounded = released.load() && scheduler.IsFree(first_tick);
    scheduler.Finish();
    scheduler.WaitWorker();
    const auto* values = static_cast<const u32*>(device.mapMemory(*memory, 0, 12));
    const bool correct = values[0] == 0x12340000u && values[1] == 0x12340001u && values[2] == 0x12340002u;
    device.unmapMemory(*memory);
    Require(returned_before_release, "first two skipped frames must not wait for GPU completion");
    Require(bounded, "third skipped frame waits for the oldest outstanding submission");
    Require(correct, "all skipped-frame GPU writes survive until explicit readback");
    std::cout << "PASS skipped frames: asynchronous submission, bounded run-ahead, completed readback\n";
}

// Native depth must be current while unrelated replay remains unexecuted and
// its resource tick stays reserved. Test the real download path, command-chunk
// rollover, repeated partial submissions, an ensuing mutation, and alias fallback.
static void DepthReadbackOrderingFixture(const Vulkan::Instance& instance,
                                        Vulkan::Scheduler& scheduler,
                                        Vulkan::RenderManager& manager,
                                        Vulkan::TextureRuntime& runtime,
                                        Vulkan::Surface& surface) {
    MM3D::PromptLayer midpoint(instance, scheduler, manager, true);
    const u32 side = 8 * surface.res_scale;
    Require(midpoint.Begin(side, side), "allocate readback replay target");
    midpoint.End();
    scheduler.Finish();
    midpoint.NextFrame();
    scheduler.SetReplayBatchingEnabled(true);
    VideoCore::TextureClear clear{};
    clear.texture_rect = surface.GetScaledRect();
    clear.value.depth = 0.f;
    clear.value.stencil = 13;
    runtime.ClearTexture(surface, clear);
    Vulkan::Framebuffer native_framebuffer(runtime, {}, nullptr, &surface);
    {
        Vulkan::Scheduler::NativeDrawScope native{scheduler};
        manager.BeginRendering(&native_framebuffer, surface.GetScaledRect());
        scheduler.Record([extent = surface.RealExtent()](vk::CommandBuffer cmd) {
            const vk::ClearAttachment attachment{
                .aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                .clearValue = {.depthStencil = {1.f, 91}},
            };
            const vk::ClearRect rect{.rect = {{0, 0}, {extent.width, extent.height}}, .layerCount = 1};
            cmd.clearAttachments(attachment, rect);
        });
        // Keep the native pass open, as it is when a guest depth read interrupts
        // rendering. The priority path must end only this pass before download.
    }
    std::atomic<unsigned> replay_executed{};
    constexpr unsigned draws = 40;
    {
        Vulkan::RenderManager::ReplayScope replay{manager, true, midpoint.Attachments()};
        Require(midpoint.Begin(side, side), "begin deferred replay target");
        for (unsigned i = 0; i < draws; ++i) {
            std::array<u64, 512> snapshot;
            snapshot.fill(i);
            scheduler.Record([snapshot, i, side, &replay_executed](vk::CommandBuffer cmd) {
                Require(std::all_of(snapshot.begin(), snapshot.end(),
                                    [i](u64 value) { return value == i; }),
                        "replay snapshot survives partial submissions");
                ++replay_executed;
                const vk::ClearAttachment attachment{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .colorAttachment = 0,
                    .clearValue = vk::ClearColorValue{
                        std::array<float, 4>{i & 1 ? 0.f : 1.f, i & 1 ? 1.f : 0.f, 0.f, 1.f}},
                };
                const vk::ClearRect rect{.rect = {{0, 0}, {side, side}}, .layerCount = 1};
                cmd.clearAttachments(attachment, rect);
            });
        }
        midpoint.End(false);
    }
    Require(scheduler.HasReplay(), "replay pending at native depth read");
    Require(manager.CanReadbackBeforeReplay(surface.Image()), "independent depth qualifies");
    const auto tick = scheduler.CurrentTick();
    const auto read = [&] {
        const auto staging = runtime.FindStaging(8 * 8 * 4, false);
        surface.Download({.buffer_offset = staging.offset, .buffer_size = staging.size,
                          .texture_rect = {16, 32, 24, 24}, .texture_level = 0}, staging);
        u32 value;
        std::memcpy(&value, staging.mapped.data(), sizeof(value));
        return value;
    };
    for (unsigned i = 0; i < 6; ++i) {
        Require(read() == 0xffffff5bu, "priority download reads current native depth/stencil");
        if (scheduler.ReadbackPriorityEnabled()) {
            Require(replay_executed == 0 && scheduler.HasReplay(),
                    "native readback completes before deferred replay executes");
            Require(scheduler.CurrentTick() == tick && !scheduler.IsFree(tick),
                    "readback fence must not retire replay buffer/descriptor lifetime");
            Require(scheduler.IsStateDirty(Vulkan::StateFlags::Pipeline) &&
                        scheduler.IsStateDirty(Vulkan::StateFlags::DescriptorSets),
                    "partial command-buffer submission invalidates cached bindings");
        }
    }
    // The very next resource mutation must drain all pending replay in order.
    clear.value.depth = 0.f;
    clear.value.stencil = 37;
    runtime.ClearTexture(surface, clear);
    Require(!scheduler.HasReplay(), "resource mutation drains pending replay");
    const auto after_mutation = read();
    if (after_mutation != 37 || replay_executed != draws) {
        throw std::runtime_error(fmt::format(
            "replay before mutation: depth={:#x} expected=0x25 draws={} expected={}",
            after_mutation, replay_executed.load(), draws));
    }

    Vulkan::StreamBuffer result(instance, scheduler, vk::BufferUsageFlagBits::eTransferDst,
                                side * side * 4, Vulkan::BufferType::Download);
    const auto [pixels, offset, invalidated] = result.Map(side * side * 4, 4);
    midpoint.PrepareForSampling();
    scheduler.Record([image = midpoint.Image(), buffer = result.Handle(), offset, side](vk::CommandBuffer cmd) {
        const vk::MemoryBarrier before{.srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
                                       .dstAccessMask = vk::AccessFlagBits::eTransferRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                            vk::PipelineStageFlagBits::eTransfer, {}, before, {}, {});
        const vk::BufferImageCopy copy{.bufferOffset = offset,
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {side, side, 1}};
        cmd.copyImageToBuffer(image, vk::ImageLayout::eGeneral, buffer, copy);
        const vk::MemoryBarrier after{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                                      .dstAccessMask = vk::AccessFlagBits::eHostRead};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eHost, {}, after, {}, {});
    });
    scheduler.Finish();
    result.Commit(side * side * 4);
    for (u32 i = 0; i < side * side; ++i)
        Require(pixels[i * 4] == 0 && pixels[i * 4 + 1] == 255 &&
                    pixels[i * 4 + 2] == 0 && pixels[i * 4 + 3] == 255,
                "all replay pixels retain final draw order after priority readbacks");
    Require(scheduler.IsFree(tick), "normal completion finally retires replay resources");

    // A declared write to the source, or an unknown capture, must use the old
    // ordering and include the pending write in the returned depth value.
    for (const bool known : {true, false}) {
        const std::array writes{surface.Image()};
        {
            Vulkan::RenderManager::ReplayScope replay{manager, true,
                known ? std::span<const vk::Image>{writes} : std::span<const vk::Image>{}};
            clear.value.depth = known ? 1.f : 0.f;
            clear.value.stencil = known ? 53 : 71;
            runtime.ClearTexture(surface, clear);
        }
        Require(!manager.CanReadbackBeforeReplay(surface.Image()), "alias/unknown writes rejected");
        Require(read() == (known ? 0xffffff35u : 71u) && !scheduler.HasReplay(),
                "fallback includes aliasing and unknown deferred depth writes");
    }
    scheduler.Finish();
    scheduler.WaitWorker();
    std::cout << "PASS depth readback ordering: scale=" << surface.res_scale
              << " priority=" << scheduler.ReadbackPriorityEnabled()
              << "; native parity, deferred replay, resource lifetime, chunk rollover, mutation and alias fallback\n";
}

// Exercise the production D24S8 conversion and synchronous download, without a ROM,
// presentation, or interpolation draws ahead of it. These wall times are a baseline,
// not an estimate of gameplay FPS or of savings from downloading smaller rectangles.
static void DepthReadbackFixture(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler) {
    Vulkan::RenderManager manager(instance, scheduler);
    Vulkan::DescriptorUpdateQueue updates(instance);
    Vulkan::TextureRuntime runtime(instance, scheduler, manager, updates, 3);
    scheduler.RegisterOnSubmit([&] { manager.EndRendering(); });
    scheduler.RegisterOnDispatch([&] { updates.Flush(); });
    for (const u32 scale : {1u, 2u, 4u}) {
        VideoCore::SurfaceParams params;
        params.addr = 0x18000000;
        params.width = params.stride = 512;
        params.height = 400;
        params.res_scale = scale;
        params.is_tiled = true;
        params.pixel_format = VideoCore::PixelFormat::D24S8;
        params.UpdateParams();
        Vulkan::Surface surface(runtime, params);
        for (const u32 side : {0u, 8u}) {
            // Nonzero offsets exercise the small rectangle's source coordinates.
            const Common::Rectangle<u32> rect = side ?
                Common::Rectangle<u32>{16, 32, 24, 24} : params.GetRect();
            double total_ms = 0;
            double maximum_ms = 0;
            constexpr unsigned warmup = 4;
            constexpr unsigned samples = 32;
            for (unsigned i = 0; i < warmup + samples; ++i) {
                VideoCore::TextureClear clear{};
                clear.texture_rect = surface.GetScaledRect();
                clear.value.depth = (i & 1) ? 1.f : 0.f;
                clear.value.stencil = static_cast<u8>(i + 1);
                runtime.ClearTexture(surface, clear);
                // Complete the clear first: the timed region includes conversion,
                // submission, synchronization and staging commit, not earlier rendering.
                scheduler.Finish();
                const auto staging = runtime.FindStaging(rect.GetWidth() * rect.GetHeight() * 4, false);
                const VideoCore::BufferTextureCopy copy{
                    .buffer_offset = staging.offset,
                    .buffer_size = staging.size,
                    .texture_rect = rect,
                    .texture_level = 0,
                };
                const auto start = MM3D::InterpolationPerformance::Now();
                surface.Download(copy, staging);
                const double elapsed = double(MM3D::InterpolationPerformance::Now() - start) / 1e6;
                if (i >= warmup) {
                    total_ms += elapsed;
                    maximum_ms = std::max(maximum_ms, elapsed);
                }
                const u32 expected = ((i & 1) ? 0xffffff00u : 0u) | clear.value.stencil;
                for (u32 pixel = 0; pixel < rect.GetWidth() * rect.GetHeight(); ++pixel) {
                    u32 actual;
                    std::memcpy(&actual, staging.mapped.data() + pixel * 4, sizeof(actual));
                    Require(actual == expected, "D24S8 readback must contain current depth and stencil");
                }
            }
            std::cout << "PASS depth readback: scale=" << scale << " rect=" << rect.GetWidth()
                      << 'x' << rect.GetHeight() << " samples=" << samples
                      << " mean_ms=" << total_ms / samples << " max_ms=" << maximum_ms << '\n';
        }
        // Compare the exact cache download/encoding path with a full reference.
        // Nonuniform depth/stencil catches tile offset, orientation and scaling
        // errors that a uniform clear cannot reveal.
        const auto download = [&](VideoCore::SurfaceInterval interval) {
            const auto info = surface.FromInterval(interval);
            const auto staging = runtime.FindStaging(info.width * info.height * 4, false);
            surface.Download({.buffer_offset = staging.offset, .buffer_size = staging.size,
                              .texture_rect = surface.GetSubRect(info), .texture_level = 0}, staging);
            std::vector<u8> bytes(interval.upper() - interval.lower());
            VideoCore::EncodeTexture(info, interval.lower(), interval.upper(), staging.mapped,
                                     bytes, runtime.NeedsConversion(surface));
            return bytes;
        };
        for (unsigned version = 0; version < 2; ++version) {
            VideoCore::TextureClear clear{};
            clear.texture_rect = surface.GetScaledRect();
            clear.value.depth = version ? 1.f : 0.f;
            clear.value.stencil = version ? 17 : 29;
            runtime.ClearTexture(surface, clear);
            clear.texture_rect = Common::Rectangle<u32>{16, 32, 24, 24} * scale;
            clear.value.depth = version ? 0.f : 1.f;
            clear.value.stencil = version ? 173 : 93;
            runtime.ClearTexture(surface, clear);
            surface.MarkValid(surface.GetInterval());
            const auto full = download(surface.GetInterval());
            const auto special = surface.GetSubRectInterval({16, 32, 24, 24}).lower();
            Require(!std::equal(full.begin(), full.begin() + 256,
                                full.begin() + (special - surface.addr)),
                    "depth reference has distinct tile contents");
            const PAddr row_end = surface.addr + surface.width * 8 * 4;
            for (const PAddr address : {surface.addr, special + 4, special - 2,
                                        row_end - 2, surface.end - 4}) {
                const auto tile = surface.GetFlushInterval({address, address+4}, surface.GetInterval());
                Require(tile.upper()-tile.lower() <= 512 || tile == surface.GetInterval(),
                        "small depth reads select tiles or the dense-read bulk fallback");
                const auto bytes = download(tile);
                Require(std::equal(bytes.begin(), bytes.end(), full.begin() + (tile.lower()-surface.addr)),
                        "tile readback equals full depth/stencil readback, including row crossings");
                const VideoCore::SurfaceInterval fragment{address, address+4};
                const auto partial = download(surface.GetFlushInterval(fragment, fragment));
                Require(std::equal(partial.begin(), partial.end(), full.begin() + (address-surface.addr)),
                        "partial ownership encodes only the requested bytes");
            }
        }
        std::cout << "PASS depth tile parity: scale=" << scale
                  << "; nonuniform depth/stencil, redraw, boundaries and partial ownership\n";
        DepthReadbackOrderingFixture(instance, scheduler, manager, runtime, surface);
        scheduler.Finish();
    }
    scheduler.Finish();
    scheduler.WaitWorker();
    scheduler.RegisterOnSubmit([] {});
    scheduler.RegisterOnDispatch([] {});
}
template<class Allocate>
static void AtlasFixture(vk::Device device,vk::Queue queue,vk::CommandBuffer command,
                         vk::RenderPass renderpass,Allocate allocate,bool widescreen_only=false,bool photo_only=false) {
    using namespace Pica::Shader::Generator;
    Require(!Vulkan::CompileGLSL(GLSL::GenerateTrivialVertexShader(false,true),
        vk::ShaderStageFlagBits::eVertex).empty(),"vertex shader accepts widescreen uniform layout");
    if(widescreen_only) {
        Require(!Vulkan::CompileGLSL(GLSL::GenerateTrivialVertexShader(true,true),
            vk::ShaderStageFlagBits::eVertex).empty(),"widescreen shader preserves clipping-plane interface");
        std::cout<<"PASS widescreen: generated vertex shaders compile with and without clip planes\n";
        return;
    }
    Pica::RegsInternal regs{};
    // Match the Vulkan renderer: SPIR-V inputs require explicit locations.
    Pica::Shader::Profile profile{}; profile.is_vulkan=true;
    profile.has_separable_shaders=true;
    const auto generated=GLSL::GenerateFragmentShader(Pica::Shader::FSConfig{regs},{},profile);
    Require(!Vulkan::CompileGLSL(MM3D::Atlas::Shader(generated),vk::ShaderStageFlagBits::eFragment).empty(),
        "real PICA shader accepts atlas extension");
    std::string vertex_source=R"(#version 450
layout(location=0) out vec2 texcoord0;
void main() {
    vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    texcoord0=vec2(p.x,1-p.y); gl_Position=vec4(p*2-1,0,1);
})";
    if(photo_only)vertex_source.insert(vertex_source.find(" gl_Position"),
        " texcoord0 = vec2(p.x*400.0/512.0, 1.0-p.y*240.0/256.0);");
    const auto vs=Vulkan::CompileGLSL(vertex_source,vk::ShaderStageFlagBits::eVertex);
    std::string fragment_source=R"(#version 450
layout(location=0) in vec2 texcoord0;
layout(location=0) out vec4 color;
layout(binding=2,std140) uniform fs_data { vec4 padding[82]; vec4 blend_color; };
void main() { color=vec4(1,0,0,1); }
)";
    if(photo_only) {
        const auto at=fragment_source.find("void main()");
        fragment_source.replace(at,std::string::npos,R"(
// Sample a real rotated display surface with a known upright image.
layout(binding=0) uniform sampler2D tex0;
vec4 sampleTexUnit0() { return vec4(1,0,1,1); }
void main() { color=sampleTexUnit0(); }
)");
    }
    fragment_source=MM3D::Atlas::Shader(fragment_source);
    const auto fs=Vulkan::CompileGLSL(fragment_source,vk::ShaderStageFlagBits::eFragment);
    Require(!vs.empty() && !fs.empty(),"compile atlas fixture");
    auto vert=device.createShaderModuleUnique({.codeSize=vs.size()*4,.pCode=vs.data()});
    auto frag=device.createShaderModuleUnique({.codeSize=fs.size()*4,.pCode=fs.data()});
    const vk::DescriptorSetLayoutBinding binding{.binding=2,.descriptorType=vk::DescriptorType::eUniformBuffer,
        .descriptorCount=1,.stageFlags=vk::ShaderStageFlagBits::eFragment};
    const std::array bindings{binding,vk::DescriptorSetLayoutBinding{.binding=0,
        .descriptorType=vk::DescriptorType::eCombinedImageSampler,.descriptorCount=1,
        .stageFlags=vk::ShaderStageFlagBits::eFragment}};
    auto layout=device.createDescriptorSetLayoutUnique({.bindingCount=photo_only?2u:1u,.pBindings=bindings.data()});
    const auto layout_handle=*layout;
    auto pipeline_layout=device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&layout_handle});
    const std::array pool_sizes{vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer,1},
        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler,1}};
    auto pool=device.createDescriptorPoolUnique({.maxSets=1,.poolSizeCount=photo_only?2u:1u,.pPoolSizes=pool_sizes.data()});
    const auto set=device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&layout_handle})[0];
    vk::UniqueDeviceMemory uniform_mem,image_mem,read_mem;
    auto uniform=device.createBufferUnique({.size=sizeof(FSUniformData)+sizeof(MM3D::Atlas::Uniforms),.usage=vk::BufferUsageFlagBits::eUniformBuffer});
    const auto host=vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent;
    uniform_mem=allocate(device.getBufferMemoryRequirements(*uniform),host);
    device.bindBufferMemory(*uniform,*uniform_mem,0);
    const vk::DescriptorBufferInfo info{*uniform,0,sizeof(FSUniformData)+sizeof(MM3D::Atlas::Uniforms)};
    device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet=set,.dstBinding=2,.descriptorCount=1,
        .descriptorType=vk::DescriptorType::eUniformBuffer,.pBufferInfo=&info},{});
    auto image=device.createImageUnique({.imageType=vk::ImageType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
        .extent={16,16,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
        .tiling=vk::ImageTiling::eOptimal,.usage=vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferSrc});
    image_mem=allocate(device.getImageMemoryRequirements(*image),vk::MemoryPropertyFlagBits::eDeviceLocal);
    device.bindImageMemory(*image,*image_mem,0);
    auto view=device.createImageViewUnique({.image=*image,.viewType=vk::ImageViewType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
        .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}});
    const auto view_handle=*view;
    auto framebuffer=device.createFramebufferUnique({.renderPass=renderpass,.attachmentCount=1,.pAttachments=&view_handle,.width=16,.height=16,.layers=1});
    auto read=device.createBufferUnique({.size=1024,.usage=vk::BufferUsageFlagBits::eTransferDst});
    read_mem=allocate(device.getBufferMemoryRequirements(*read),host);device.bindBufferMemory(*read,*read_mem,0);
    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage=vk::ShaderStageFlagBits::eVertex,.module=*vert,.pName="main"},
        vk::PipelineShaderStageCreateInfo{.stage=vk::ShaderStageFlagBits::eFragment,.module=*frag,.pName="main"}};
    const vk::PipelineVertexInputStateCreateInfo input{};
    const vk::PipelineInputAssemblyStateCreateInfo assembly{.topology=vk::PrimitiveTopology::eTriangleList};
    const vk::PipelineViewportStateCreateInfo viewport{.viewportCount=1,.scissorCount=1};
    const vk::PipelineRasterizationStateCreateInfo raster{.polygonMode=vk::PolygonMode::eFill,.cullMode=vk::CullModeFlagBits::eNone,.lineWidth=1};
    const vk::PipelineMultisampleStateCreateInfo samples{.rasterizationSamples=vk::SampleCountFlagBits::e1};
    const vk::PipelineColorBlendAttachmentState color{.colorWriteMask=vk::ColorComponentFlags{15}};
    const vk::PipelineColorBlendStateCreateInfo blend{.attachmentCount=1,.pAttachments=&color};
    const std::array dynamics{vk::DynamicState::eViewport,vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic{.dynamicStateCount=2,.pDynamicStates=dynamics.data()};
    auto pipeline=device.createGraphicsPipelineUnique({},vk::GraphicsPipelineCreateInfo{.stageCount=2,.pStages=stages.data(),
        .pVertexInputState=&input,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,
        .pMultisampleState=&samples,.pColorBlendState=&blend,.pDynamicState=&dynamic,.layout=*pipeline_layout,.renderPass=renderpass}).value;
    // Match CapturePhoto's rotated display-buffer storage and LoadPhoto's
    // inverse PNG rotation. A coordinate-only fragment stub cannot detect an
    // upside-down replacement, so this fixture samples actual uploaded pixels.
    vk::UniqueDeviceMemory source_mem,upload_mem;
    vk::UniqueImage source_image;vk::UniqueImageView source_view;
    vk::UniqueBuffer upload;vk::UniqueSampler sampler;
    if(photo_only) {
        source_image=device.createImageUnique({.imageType=vk::ImageType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
            .extent={16,16,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
            .tiling=vk::ImageTiling::eOptimal,.usage=vk::ImageUsageFlagBits::eSampled|vk::ImageUsageFlagBits::eTransferDst});
        source_mem=allocate(device.getImageMemoryRequirements(*source_image),vk::MemoryPropertyFlagBits::eDeviceLocal);
        device.bindImageMemory(*source_image,*source_mem,0);
        source_view=device.createImageViewUnique({.image=*source_image,.viewType=vk::ImageViewType::e2D,
            .format=vk::Format::eR8G8B8A8Unorm,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}});
        sampler=device.createSamplerUnique({.magFilter=vk::Filter::eNearest,.minFilter=vk::Filter::eNearest,
            .addressModeU=vk::SamplerAddressMode::eClampToEdge,.addressModeV=vk::SamplerAddressMode::eClampToEdge,
            .addressModeW=vk::SamplerAddressMode::eClampToEdge});
        upload=device.createBufferUnique({.size=1024,.usage=vk::BufferUsageFlagBits::eTransferSrc});
        upload_mem=allocate(device.getBufferMemoryRequirements(*upload),host);device.bindBufferMemory(*upload,*upload_mem,0);
        auto* data=static_cast<u8*>(device.mapMemory(*upload_mem,0,1024));
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
            const unsigned at=(x*16+15-y)*4;
            data[at]=y*17;data[at+1]=x*17;data[at+2]=255;data[at+3]=255;
        }
        device.unmapMemory(*upload_mem);
        command.reset();command.begin({.flags=vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        const vk::ImageMemoryBarrier writable{.dstAccessMask=vk::AccessFlagBits::eTransferWrite,
            .oldLayout=vk::ImageLayout::eUndefined,.newLayout=vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=*source_image,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}};
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,vk::PipelineStageFlagBits::eTransfer,{}, {},{},writable);
        command.copyBufferToImage(*upload,*source_image,vk::ImageLayout::eTransferDstOptimal,
            vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={16,16,1}});
        auto readable=writable;readable.srcAccessMask=vk::AccessFlagBits::eTransferWrite;
        readable.dstAccessMask=vk::AccessFlagBits::eShaderRead;readable.oldLayout=vk::ImageLayout::eTransferDstOptimal;
        readable.newLayout=vk::ImageLayout::eShaderReadOnlyOptimal;
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eFragmentShader,{}, {},{},readable);
        command.end();queue.submit(vk::SubmitInfo{.commandBufferCount=1,.pCommandBuffers=&command});queue.waitIdle();
        const vk::DescriptorImageInfo texture{*sampler,*source_view,vk::ImageLayout::eShaderReadOnlyOptimal};
        device.updateDescriptorSets(vk::WriteDescriptorSet{.dstSet=set,.dstBinding=0,.descriptorCount=1,
            .descriptorType=vk::DescriptorType::eCombinedImageSampler,.pImageInfo=&texture},{});
    }
    for(int test=0;test<(photo_only?2:4);++test) {
        MM3D::Atlas::Uniforms mask;
        if(photo_only)mask.pause[0]=MM3D::PhotoCapture::TextureBinding;
        else {
            if(test!=3) MM3D::Atlas::Add(mask,{.125f,.125f,.375f,.375f});
            mask.info[1]=test==1;
        }
        auto* bytes=static_cast<unsigned char*>(device.mapMemory(*uniform_mem,0,info.range));
        std::memset(bytes,0,sizeof(FSUniformData));std::memcpy(bytes+sizeof(FSUniformData),&mask,sizeof(mask));
        device.unmapMemory(*uniform_mem);
        command.reset();command.begin({.flags=vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        const vk::ClearValue clear{.color={std::array<float,4>{0,0,0,0}}};
        command.beginRenderPass({.renderPass=renderpass,.framebuffer=*framebuffer,.renderArea={{0,0},{16,16}},
            .clearValueCount=1,.pClearValues=&clear},vk::SubpassContents::eInline);
        command.bindPipeline(vk::PipelineBindPoint::eGraphics,*pipeline);
        command.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,*pipeline_layout,0,set,{});
        const float extent=test==2 ? 32.f : 16.f;
        command.setViewport(0,vk::Viewport{0,0,extent,extent,0,1});command.setScissor(0,vk::Rect2D{{0,0},{16,16}});
        command.draw(3,1,0,0);command.endRenderPass();
        const vk::MemoryBarrier transfer{.srcAccessMask=vk::AccessFlagBits::eColorAttachmentWrite,.dstAccessMask=vk::AccessFlagBits::eTransferRead};
        command.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,vk::PipelineStageFlagBits::eTransfer,{},transfer,{},{});
        command.copyImageToBuffer(*image,vk::ImageLayout::eTransferSrcOptimal,*read,
            vk::BufferImageCopy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={16,16,1}});
        const vk::MemoryBarrier readable{.srcAccessMask=vk::AccessFlagBits::eTransferWrite,.dstAccessMask=vk::AccessFlagBits::eHostRead};
        command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,vk::PipelineStageFlagBits::eHost,{},readable,{},{});
        command.end();queue.submit(vk::SubmitInfo{.commandBufferCount=1,.pCommandBuffers=&command});queue.waitIdle();
        const auto* pixels=static_cast<unsigned char*>(device.mapMemory(*read_mem,0,1024));
        const int lo=test==2 ? 4 : 2,hi=test==2 ? 12 : 6;
        for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
            bool visible=x>=lo && x<hi && y>=lo && y<hi;
            if(test==1) visible=!visible; if(test==3) visible=true;
            if(photo_only) {
                const unsigned at=(y*16+x)*4;
                const int red=y*17;
                const int green=x*17;
                Require(std::abs(int(pixels[at])-red)<=1 && std::abs(int(pixels[at+1])-green)<=1 &&
                        pixels[at+2]==255 && pixels[at+3]==255,
                        "photo is upright and fills its entire UV rectangle with no padding bars");
            } else Require(pixels[(y*16+x)*4+3]==(visible?255:0),"atlas mask orientation/isolation/scale/reset");
        }
        device.unmapMemory(*read_mem);
    }
    if(photo_only)std::cout<<"PASS photo shader: real rotated image, upright orientation and complete framing\n";
    else std::cout<<"PASS atlas GPU: PICA shader compilation, selected/inverse masks, orientation, 2x scale, reset\n";
}
// Exercise the production photo readback with the resolution that crashed and
// the largest captured/imported photo. Row patterns expose gaps, flipped rows,
// and overwritten chunks when the shared staging ring wraps.
static void PhotoReadbackFixture(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler) {
    Vulkan::RenderManager manager(instance, scheduler);
    Vulkan::DescriptorUpdateQueue updates(instance);
    Vulkan::TextureRuntime runtime(instance, scheduler, manager, updates, 3);
    scheduler.RegisterOnSubmit([&] { manager.EndRendering(); });
    scheduler.RegisterOnDispatch([&] { updates.Flush(); });
    const auto pixel=[](u32 x,u32 y,unsigned c) -> u8 {
        return c==0 ? u8(x) : c==1 ? u8(y) : c==2 ? u8((x>>8)^(y>>8)) : 255;
    };
    for(const auto [width,height]:std::array<std::pair<u32,u32>,4>{
            {{1440,2400},{1680,2800},{3840,6400},{3840,6827}}}) {
        VideoCore::SurfaceParams params;
        params.width=params.stride=width;params.height=height;params.res_scale=1;
        params.pixel_format=VideoCore::PixelFormat::RGBA8;params.is_tiled=false;params.UpdateParams();
        Vulkan::Surface image(runtime,params);
        const auto staging=runtime.FindStaging(width*height*4,true);
        for(u32 y=0;y<height;++y)for(u32 x=0;x<width;++x)for(unsigned c=0;c<4;++c)
            staging.mapped[(std::size_t(y)*width+x)*4+c]=pixel(x,y,c);
        image.Upload({.buffer_offset=staging.offset,.buffer_size=staging.size,
            .texture_rect={0,height,width,0},.texture_level=0},staging);
        const auto check=[&](Common::Rectangle<u32> rect) {
            const auto result=Vulkan::PhotoTransfer::Read(runtime,image,rect);
            Require(result.size()==std::size_t(rect.GetWidth())*rect.GetHeight()*4,"photo readback has full extent");
            for(u32 y=0;y<rect.GetHeight();++y)for(u32 x=0;x<rect.GetWidth();++x)for(unsigned c=0;c<4;++c)
                Require(result[(std::size_t(y)*rect.GetWidth()+x)*4+c]==pixel(x+rect.left,y+rect.bottom,c),
                        "photo readback preserves all pixels across staging wrap and partial rows");
        };
        check({0,height,width,0});
        check({13,height-11,width-17,19});
        for(const auto point:MM3D::PauseCapture::ProbePoints) {
            check({point[1]*width/240,(point[0]+1)*height/400,
                   (point[1]+1)*width/240,point[0]*height/400});
        }
        Require(Vulkan::PhotoTransfer::Read(runtime,image,{0,0,0,0}).empty(),"empty photo patch rejected");
        Require(Vulkan::PhotoTransfer::Read(runtime,image,{0,height+1,width,0}).empty(),"photo bounds checked");
        runtime.Finish();
        std::cout<<"PASS photo readback: "<<width<<"x"<<height<<", complete image and orientation patches\n";
    }
    scheduler.Finish();scheduler.WaitWorker();
    scheduler.RegisterOnSubmit([] {});
    scheduler.RegisterOnDispatch([] {});
}
namespace MM3D { void RunHybridShaderFixture(const Vulkan::Instance&, Vulkan::Scheduler&); }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    FileUtil::SetUserPath(directory.path().toStdString());
    Common::Log::Initialize();
    Common::Log::Start();
    try {
#ifdef __APPLE__
        if (argc > 1 && std::string_view(argv[1]) == "--hybrid-shaders") {
            // Match the experimental renderer's required Metal arithmetic mode.
            setenv("MVK_CONFIG_FAST_MATH_ENABLED", "0", 1);
            std::cout << "Hybrid fixture: Metal fast math disabled for exact TEV comparison\n";
        }
#endif
        HeadlessWindow window;
        Vulkan::Instance instance(window, 0);
        const bool profile=argc>2 && std::string_view(argv[2])=="--performance";
        if(profile) {
            MM3D::InterpolationPerformance::Start(true,true);
            std::scoped_lock lock{MM3D::InterpolationPerformance::mutex};
            MM3D::InterpolationPerformance::begin=MM3D::InterpolationPerformance::Now();
        }
        Vulkan::Scheduler scheduler(instance);
        scheduler.RegisterOnSubmit([] {});
        scheduler.RegisterOnDispatch([] {});
        if (argc > 1 && std::string_view(argv[1]) == "--hybrid-shaders") {
            MM3D::RunHybridShaderFixture(instance, scheduler);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--gpu-interpolation") {
            MM3D::RunGpuInterpolationFixture(instance, scheduler);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--photo-readback") {
            PhotoReadbackFixture(instance, scheduler);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--skipped-frames") {
            SkippedFrameFixture(instance, scheduler);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--scene-effects") {
            MM3D::RunSceneEffectsFixture(instance, scheduler);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--pass-switching") {
            MM3D::RunPassSwitchFixture(instance, scheduler);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--visibility-depth") {
            MM3D::RunVisibilityDepthFixture(instance, scheduler);
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--depth-readback") {
            DepthReadbackFixture(instance, scheduler);
            return 0;
        }
        const auto device = instance.GetDevice();
        const auto queue = instance.GetGraphicsQueue();
        const auto memory_properties = instance.GetPhysicalDevice().getMemoryProperties();
        const auto allocate = [&](vk::MemoryRequirements requirements, vk::MemoryPropertyFlags flags) {
            for (unsigned i = 0; i < memory_properties.memoryTypeCount; ++i) {
                if ((requirements.memoryTypeBits & (1u << i)) &&
                    (memory_properties.memoryTypes[i].propertyFlags & flags) == flags)
                    return device.allocateMemoryUnique({.allocationSize = requirements.size, .memoryTypeIndex = i});
            }
            throw std::runtime_error("No compatible fixture memory");
        };
        if (argc > 1 && (std::string_view(argv[1]) == "--prompt-only" || std::string_view(argv[1]) == "--midpoint-target")) {
            Vulkan::RenderManager manager(instance, scheduler);
            const bool depth=std::string_view(argv[1])=="--midpoint-target";
            MM3D::PromptLayer layer(instance, scheduler, manager, depth);
            vk::UniquePipelineLayout depth_layout;
            vk::UniquePipeline depth_pipeline;
            if(depth) {
                const auto vs=Vulkan::CompileGLSL(R"(#version 450
layout(push_constant) uniform Params { vec4 params; } pc;
void main() { vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    gl_Position=vec4(p*2-1,pc.params.x,1); }
)",vk::ShaderStageFlagBits::eVertex);
                const auto fs=Vulkan::CompileGLSL(R"(#version 450
layout(push_constant) uniform Params { vec4 params; } pc;
layout(location=0) out vec4 color;
void main() { color=vec4(pc.params.y,0,pc.params.z,1); }
)",vk::ShaderStageFlagBits::eFragment);
                Require(!vs.empty()&&!fs.empty(),"depth preview shaders compile");
                auto vert=device.createShaderModuleUnique({.codeSize=vs.size()*4,.pCode=vs.data()});
                auto frag=device.createShaderModuleUnique({.codeSize=fs.size()*4,.pCode=fs.data()});
                const vk::PushConstantRange push{vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment,0,16};
                depth_layout=device.createPipelineLayoutUnique({.pushConstantRangeCount=1,.pPushConstantRanges=&push});
                const std::array stages{
                    vk::PipelineShaderStageCreateInfo{.stage=vk::ShaderStageFlagBits::eVertex,.module=*vert,.pName="main"},
                    vk::PipelineShaderStageCreateInfo{.stage=vk::ShaderStageFlagBits::eFragment,.module=*frag,.pName="main"}};
                const vk::PipelineVertexInputStateCreateInfo input{};
                const vk::PipelineInputAssemblyStateCreateInfo assembly{.topology=vk::PrimitiveTopology::eTriangleList};
                const vk::PipelineViewportStateCreateInfo viewport{.viewportCount=1,.scissorCount=1};
                const vk::PipelineRasterizationStateCreateInfo raster{.polygonMode=vk::PolygonMode::eFill,.cullMode=vk::CullModeFlagBits::eNone,.lineWidth=1};
                const vk::PipelineMultisampleStateCreateInfo multisample{.rasterizationSamples=vk::SampleCountFlagBits::e1};
                const vk::PipelineDepthStencilStateCreateInfo ds{.depthTestEnable=true,.depthWriteEnable=true,.depthCompareOp=vk::CompareOp::eLess};
                const vk::PipelineColorBlendAttachmentState color{.colorWriteMask=vk::ColorComponentFlagBits::eR|vk::ColorComponentFlagBits::eG|vk::ColorComponentFlagBits::eB|vk::ColorComponentFlagBits::eA};
                const vk::PipelineColorBlendStateCreateInfo blend{.attachmentCount=1,.pAttachments=&color};
                const std::array dynamic{vk::DynamicState::eViewport,vk::DynamicState::eScissor};
                const vk::PipelineDynamicStateCreateInfo dyn{.dynamicStateCount=2,.pDynamicStates=dynamic.data()};
                depth_pipeline=device.createGraphicsPipelineUnique({},vk::GraphicsPipelineCreateInfo{
                    .stageCount=2,.pStages=stages.data(),.pVertexInputState=&input,.pInputAssemblyState=&assembly,
                    .pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&multisample,
                    .pDepthStencilState=&ds,.pColorBlendState=&blend,.pDynamicState=&dyn,
                    .layout=*depth_layout,.renderPass=manager.GetRenderpass(VideoCore::PixelFormat::RGBA8,VideoCore::PixelFormat::D24S8,false)}).value;
            }
            const auto draw_depth=[&](bool near) {
                scheduler.Record([pipeline=*depth_pipeline,layout=*depth_layout,near](vk::CommandBuffer cmd) {
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,pipeline);
                    cmd.setViewport(0,vk::Viewport{0,0,8,8,0,1});cmd.setScissor(0,vk::Rect2D{{0,0},{8,8}});
                    const std::array<float,4> values{near?.25f:.75f,near?1.f:0.f,near?0.f:1.f,0};
                    cmd.pushConstants(layout,vk::ShaderStageFlagBits::eVertex|vk::ShaderStageFlagBits::eFragment,0,16,values.data());
                    cmd.draw(3,1,0,0);
                });
            };
            Require(!layer.Visible(), "new layer invisible");
            for (unsigned extent : {32u, 64u}) {
                layer.NextFrame();
                Require(!layer.Visible(), "absent draws do not persist");
                Require(layer.Begin(extent, extent), "create replay target");
                if(depth) { draw_depth(true); }
                else scheduler.Record([](vk::CommandBuffer cmd) {
                    const vk::ClearAttachment fill{.aspectMask=vk::ImageAspectFlagBits::eColor,
                        .colorAttachment=0, .clearValue={.color={std::array<float,4>{.5f,0,0,.5f}}}};
                    const vk::ClearRect rect{.rect={{0,0},{8,8}}, .baseArrayLayer=0, .layerCount=1};
                    cmd.clearAttachments(fill, rect);
                });
                layer.End(false);
                Require(layer.Begin(extent, extent), "resume unsampled replay target");
                if(depth)draw_depth(false);
                layer.End(false);
                layer.PrepareForSampling();
                layer.PrepareForSampling(); // repeated presentation needs no new barrier
                Require(layer.Visible(), "draw makes layer visible");
                const vk::DeviceSize size = extent * extent * 4;
                auto buffer = device.createBufferUnique({.size=size, .usage=vk::BufferUsageFlagBits::eTransferDst});
                auto mem = allocate(device.getBufferMemoryRequirements(*buffer),
                    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
                device.bindBufferMemory(*buffer, *mem, 0);
                auto read = [&] {
                    scheduler.Record([image=layer.Image(), buffer=*buffer, extent](vk::CommandBuffer cmd) {
                        const vk::MemoryBarrier barrier{
                            .srcAccessMask=vk::AccessFlagBits::eMemoryWrite,
                            .dstAccessMask=vk::AccessFlagBits::eTransferRead};
                        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                            vk::PipelineStageFlagBits::eTransfer, {}, barrier, {}, {});
                        const vk::BufferImageCopy copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},
                            .imageExtent={extent,extent,1}};
                        cmd.copyImageToBuffer(image, vk::ImageLayout::eGeneral, buffer, copy);
                    });
                    scheduler.Finish();
                };
                read();
                auto* pixels = static_cast<const unsigned char*>(device.mapMemory(*mem,0,size));
                Require(pixels[0] == (depth?255:128) && pixels[2]==0 && pixels[3] == (depth?255:128), "replay color, coverage and depth occlusion retained");
                Require(pixels[size-1] == (depth?255:0), "background clear has expected opacity");
                device.unmapMemory(*mem);
                Require(layer.Begin(extent, extent), "append same-frame draw");
                if(depth)draw_depth(false);
                layer.End(); read();
                pixels = static_cast<const unsigned char*>(device.mapMemory(*mem,0,size));
                Require(pixels[0] == (depth?255:128) && pixels[2]==0 && pixels[3] == (depth?255:128), "same-frame color and depth accumulate");
                device.unmapMemory(*mem);
                layer.NextFrame();
                Require(layer.Begin(extent, extent), "next-frame target");
                layer.End(); read();
                pixels = static_cast<const unsigned char*>(device.mapMemory(*mem,0,size));
                Require(pixels[0] == 0 && pixels[3] == (depth?255:0), "new frame clears old target");
                device.unmapMemory(*mem);
                buffer.reset();
            }
            layer.NextFrame();
            Require(!layer.Visible(), "toggle hides layer");
            if(profile) {
                // Exercise query recycling beyond the 64-slot ring. Blocking is fixture-only.
                for(unsigned i=0;i<80;++i)scheduler.Finish();
                scheduler.WaitWorker();
                const auto bits=instance.GetPhysicalDevice().getQueueFamilyProperties()[
                    instance.GetGraphicsQueueFamilyIndex()].timestampValidBits;
                std::scoped_lock lock{MM3D::InterpolationPerformance::mutex};
                const auto sample=MM3D::InterpolationPerformance::gpu_sample;
                Require(!bits || (sample.calls>=64 && sample.ns>0),"GPU timestamp readback and slot recycling");
                unsigned completed_traces = 0;
                unsigned correlated_gpu = 0;
                for (unsigned i = 0; i < MM3D::InterpolationPerformance::submission_trace_count; ++i) {
                    const auto& trace = *MM3D::InterpolationPerformance::submission_traces[i];
                    Require(MM3D::InterpolationTimeline::Measure(trace).valid,
                            "Finished submissions have an ordered CPU timeline");
                    ++completed_traces;
                    if (trace.gpu_available.load()) ++correlated_gpu;
                }
                Require(completed_traces >= 80, "Track synchronous Finish submissions across query ring reuse");
                Require(!bits || correlated_gpu >= 64, "GPU query results correlate with their CPU submission");
                std::cout << "Submission timeline completed=" << completed_traces
                          << " correlated_gpu=" << correlated_gpu << '\n';
                std::cout<<"GPU timestamp samples="<<sample.calls<<" total_ns="<<sample.ns<<"\n";
            }
            std::cout << "PASS layer GPU: color/depth target, coverage, accumulation, frame reset, resize, toggle\n";
            return 0;
        }
        auto pool = device.createCommandPoolUnique({.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex()});
        auto commands = device.allocateCommandBuffersUnique({.commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
        const auto command = *commands.front();
        const auto format = vk::Format::eR8G8B8A8Unorm;
        const vk::AttachmentDescription attachment{{}, format, vk::SampleCountFlagBits::e1,
            vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
            vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare,
            vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal};
        const vk::AttachmentReference reference{0, vk::ImageLayout::eGeneral};
        const vk::SubpassDescription subpass{{}, vk::PipelineBindPoint::eGraphics, 0, nullptr, 1, &reference};
        auto renderpass = device.createRenderPassUnique({.attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass});
        if(argc>1 && std::string_view(argv[1])=="--photo-sampling") {
            AtlasFixture(device,queue,command,*renderpass,allocate,false,true);return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--atlas-only") {
            AtlasFixture(device,queue,command,*renderpass,allocate);return 0;
        }
        if(argc>1 && std::string_view(argv[1])=="--widescreen-only") {
            AtlasFixture(device,queue,command,*renderpass,allocate,true);return 0;
        }
        MM3D::ResetGuestFrames();
        MM3D::Overlay overlay(instance, format, 3);
        for (const auto extent : {vk::Extent2D{1280, 720}, vk::Extent2D{2560, 1080}, vk::Extent2D{640, 480}}) {
            vk::UniqueDeviceMemory texture_memory;
            auto texture = device.createImageUnique({.imageType = vk::ImageType::e2D, .format = format,
                .extent = {extent.width, extent.height, 1}, .mipLevels = 1, .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
                .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc});
            texture_memory = allocate(device.getImageMemoryRequirements(*texture), vk::MemoryPropertyFlagBits::eDeviceLocal);
            device.bindImageMemory(*texture, *texture_memory, 0);
            auto view = device.createImageViewUnique({.image = *texture, .viewType = vk::ImageViewType::e2D, .format = format,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
            const auto view_handle = *view;
            auto framebuffer = device.createFramebufferUnique({.renderPass = *renderpass, .attachmentCount = 1, .pAttachments = &view_handle,
                .width = extent.width, .height = extent.height, .layers = 1});
            const vk::DeviceSize size = vk::DeviceSize(extent.width) * extent.height * 4;
            vk::UniqueDeviceMemory buffer_memory;
            auto buffer = device.createBufferUnique({.size = size, .usage = vk::BufferUsageFlagBits::eTransferDst});
            buffer_memory = allocate(device.getBufferMemoryRequirements(*buffer),
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            device.bindBufferMemory(*buffer, *buffer_memory, 0);
            std::size_t indicator_pixels{};
            for (int frame = 0; frame < 11; ++frame) {
                if (frame == 2) Require(MM3D::ToggleOverlay(), "open overlay");
                // Exercise GPU capture lifetime without assuming a tab's screen coordinates.
                // Pointer capture/release routing is checked independently below.
                if (extent.width == 1280 && frame == 9)
                    MM3D::Textures::Clear();
                command.reset();
                command.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
                const vk::ClearValue clear{vk::ClearColorValue{std::array<float, 4>{0, 0, 0, 1}}};
                command.beginRenderPass({.renderPass = *renderpass, .framebuffer = *framebuffer,
                    .renderArea = {{0, 0}, extent}, .clearValueCount = 1, .pClearValues = &clear}, vk::SubpassContents::eInline);
                command.endRenderPass();
                overlay.Draw(command, *framebuffer, extent.width, extent.height);
                const vk::BufferImageCopy copy{0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                    {0, 0, 0}, {extent.width, extent.height, 1}};
                command.copyImageToBuffer(*texture, vk::ImageLayout::eTransferSrcOptimal, *buffer, copy);
                const vk::MemoryBarrier barrier{.srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eHostRead};
                command.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                    {}, barrier, {}, {});
                command.end();
                queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command});
                queue.waitIdle();
                overlay.Presented();
                const auto* pixels = static_cast<const unsigned char*>(device.mapMemory(*buffer_memory, 0, size));
                std::size_t colored{};
                for (std::size_t i = 0; i < size; i += 4)
                    if (pixels[i] || pixels[i + 1] || pixels[i + 2]) ++colored;
                if (frame == 1) indicator_pixels = colored;
                // ImGui's first auto-sized window has a layout-only frame.
                if (frame > 0) Require(colored > 100, "overlay produced visible GPU pixels");
                if (frame == 7) {
                    Require(colored > indicator_pixels + 1000, "F1 diagnostics add visible GPU pixels");
                    if (argc > 1 && extent.width == 1280) {
                        std::ofstream out(argv[1], std::ios::binary);
                        out << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
                        for (std::size_t i = 0; i < size; i += 4) out.write(reinterpret_cast<const char*>(pixels + i), 3);
                    }
                }
                device.unmapMemory(*buffer_memory);
                if (frame == 1 && extent.width == 1280) {
                    const auto key = MM3D::Textures::Key(4096, extent.width, extent.height, 0);
                    Require(!MM3D::Textures::Matches(key), "texture suppression defaults off");
                    MM3D::Textures::Start();
                    MM3D::Textures::Capture(instance, scheduler, *texture, format,
                        extent.width, extent.height, key, vk::ImageLayout::eTransferSrcOptimal);
                    Require(MM3D::Textures::Snapshot().size() == 1, "GPU thumbnail captured");
                    Require(!MM3D::Textures::Wants(key), "GPU thumbnail deduplicated");
                }
                if (extent.width == 1280 && frame == 10) {
                    Require(MM3D::Textures::Snapshot().empty(), "Clearing captures empties GPU snapshots");
                    Require(MM3D::Textures::SelectionCount() == 0, "Clear cancels selection");
                    MM3D::Textures::Start();
                    const auto key = MM3D::Textures::Key(4096, extent.width, extent.height, 0);
                    Require(MM3D::Textures::Wants(key), "cleared texture can be captured again");
                    MM3D::Textures::Capture(instance, scheduler, *texture, format,
                        extent.width, extent.height, key, vk::ImageLayout::eTransferSrcOptimal);
                    Require(MM3D::Textures::Snapshot().size() == 1, "recapture succeeds");
                }
                if (frame == 10) {
                    Require(!MM3D::Pointer(-1.f, -1.f, 0, true),
                        "outside panel click reaches game");
                    Require(!MM3D::Pointer(40, 100, 0, false), "game release stays with game");
                    Require(MM3D::Pointer(40, 100, 0, true), "panel click captured");
                    Require(MM3D::Pointer(-1.f, -1.f, 0, false),
                        "panel release stays with panel");
                }
                Require(MM3D::guest_frames == 0, "host rendering must not advance guest state");
            }
            MM3D::ToggleOverlay();
            std::cout << "PASS GPU " << extent.width << 'x' << extent.height << ": indicator, F1, guest unchanged\n";
            // Bound resources must be destroyed before their memory allocations.
            framebuffer.reset(); view.reset(); texture.reset(); buffer.reset();
        }
        device.waitIdle();
    } catch (const std::exception& error) {
        std::cerr << "FAIL GPU: " << error.what() << '\n';
        return 1;
    }
}
