// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// ROM-free equivalence test: execute generated specialized and runtime TEV code
// on the GPU with identical inputs. Texture samples are deterministic fixtures;
// full fragment variants are also compiled with their real sampler interfaces.
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include "mm3d/atlas_render.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_shader_disk_cache.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/hybrid_fs_config.h"

namespace MM3D {
namespace {
using namespace Pica::Shader;
using Stage = Pica::TexturingRegs::TevStageConfig;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Body(const std::string& source, bool hybrid = false) {
    const auto start = source.find("vec4 combiner_buffer =");
    if (start == std::string::npos) {
        Require(source.find("discard; }") != std::string::npos, "missing specialized TEV body");
        return "return vec4(-1.0);\n";
    }
    const auto end = source.find("gl_FragDepth = depth;", start);
    Require(end != std::string::npos, "missing TEV body end");
    auto body = source.substr(start, end - start);
    for (std::size_t p = 0; (p = body.find("discard;", p)) != std::string::npos;)
        body.replace(p, 8, "return vec4(-1.0);");
    const std::string marker = "combiner_buffer = next_combiner_buffer;";
    for (std::size_t p = 0, stage = 0; (p = body.find(marker, p)) != std::string::npos; ++stage) {
        const auto write = hybrid ? "outputs[test_id*14u + uint(stage)*2u] = combiner_output;\n" :
            "outputs[test_id*14u + " + std::to_string(stage*2+1) + "u] = combiner_output;\n";
        body.insert(p, write); p += write.size() + marker.size();
    }
    return body + "return byteround(combiner_output);\n";
}
}

void RunHybridShaderFixture(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler) {
    using namespace Pica::Shader::Generator;
    Profile profile{};
    profile.is_vulkan = profile.has_separable_shaders = true;
    profile.has_logic_op = true;
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    FSConfig base{regs};
    base.framebuffer.alpha_test_func.Assign(Pica::FramebufferRegs::CompareFunc::Always);
    base.framebuffer.scissor_test_mode.Assign(Pica::RasterizerRegs::ScissorMode::Disabled);
    base.texture.fog_mode.Assign(Pica::TexturingRegs::FogMode::None);
    base.framebuffer.shadow_rendering.Assign(0);

    // Full shaders retain texture/lighting/fog/atlas interfaces. These states
    // must stay distinct, even though their TEV and alpha settings can share.
    for (unsigned variant = 0; variant < 12; ++variant) {
        auto config = base;
        config.texture.texture0_type.Assign(variant % 2 ? Pica::TexturingRegs::TextureConfig::Texture2D :
                                                        Pica::TexturingRegs::TextureConfig::TextureCube);
        if (variant & 2) {
            config.lighting.enable.Assign(1);
            config.lighting.src_num.Assign(1);
        }
        if (variant & 4) config.texture.fog_mode.Assign(Pica::TexturingRegs::FogMode::Fog);
        if (variant & 8) config.proctex.enable.Assign(1);
        const auto source = GLSL::FragmentModule{Hybrid::Family(config), {}, profile, true}.Generate();
        Require(!Vulkan::CompileGLSL(source, vk::ShaderStageFlagBits::eFragment).empty(),
                "hybrid full fragment shader compiles");
        Require(!Vulkan::CompileGLSL(Atlas::Shader(source), vk::ShaderStageFlagBits::eFragment).empty(),
                "hybrid shader preserves presentation/atlas interface");
        if (config != base)
            Require(Hybrid::Family(config) != Hybrid::Family(base), "family retains non-TEV state");
    }

    constexpr unsigned Configs = 64, Samples = 16, Count = Configs * Samples;
    std::mt19937 random{0x48594252};
    constexpr std::array sources{0u, 1u, 2u, 3u, 4u, 5u, 6u, 13u, 14u, 15u};
    constexpr std::array modifiers{0u, 1u, 2u, 3u, 4u, 5u, 8u, 9u, 12u, 13u};
    constexpr std::array alpha_ops{0u, 1u, 2u, 3u, 4u, 5u, 8u, 9u};
    std::vector<FSConfig> configs;
    for (unsigned i = 0; i < Configs; ++i) {
        auto config = base;
        config.framebuffer.alpha_test_func.Assign(static_cast<Pica::FramebufferRegs::CompareFunc>(i % 8));
        config.texture.combiner_buffer_input.Assign(random() & 255);
        for (unsigned j = 0; j < 6; ++j) {
            Stage stage{};
            for (unsigned k = 0; k < 3; ++k) {
                stage.sources_raw |= sources[random() % sources.size()] << (k * 4);
                stage.sources_raw |= sources[random() % sources.size()] << (16 + k * 4);
                stage.modifiers_raw |= modifiers[random() % modifiers.size()] << (k * 4);
                stage.modifiers_raw |= (random() % 8) << (12 + k * 4);
            }
            stage.color_op.Assign(static_cast<Stage::Operation>((i + j) % 10));
            stage.alpha_op.Assign(static_cast<Stage::Operation>(alpha_ops[(i + j) % alpha_ops.size()]));
            stage.color_scale.Assign((i + j) % 4);
            stage.alpha_scale.Assign((i / 4 + j) % 4);
            // Pass-through stages must still update the delayed combiner buffer.
            if ((i + j) % 7 == 0) {
                stage.sources_raw = 0x0fff0fff;
                stage.modifiers_raw = stage.ops_raw = stage.scales_raw = 0;
            }
            config.texture.tev_stages[j] = {stage.sources_raw, stage.modifiers_raw, stage.ops_raw, stage.scales_raw};
        }
        Require(Hybrid::Family(config) == Hybrid::Family(base), "TEV/alpha states share family");
        configs.push_back(config);
    }
    const auto hybrid = GLSL::FragmentModule{Hybrid::Family(base), {}, profile, true}.Generate();
    const auto helper_begin = hybrid.find("vec4 hybrid_source(");
    const auto helper_end = hybrid.find("void main()", helper_begin);
    Require(helper_begin != std::string::npos && helper_end != std::string::npos, "hybrid helpers present");
    // Uniforms deliberately come from storage here so every invocation can
    // compare a different state using the exact generated TEV implementations.
    std::string code = R"(#version 450
layout(local_size_x=64) in;
struct Input { uvec4 stages[6]; uvec4 control; vec4 colors[6]; vec4 primary; };
layout(set=0,binding=0,std430) readonly buffer Inputs { Input inputs[]; };
layout(set=0,binding=1,std430) writeonly buffer Outputs { vec4 outputs[]; };
uint test_id;
#define hybrid_stages inputs[test_id].stages
#define hybrid_control inputs[test_id].control
#define const_color inputs[test_id].colors
#define alphatest_ref int(test_id & 255u)
const vec4 tev_combiner_buffer_color = vec4(0.17,0.29,0.53,0.71);
float byteround(float x) { return round(x * 255.0) * (1.0 / 255.0); }
vec3 byteround(vec3 x) { return round(x * 255.0) * (1.0 / 255.0); }
vec4 byteround(vec4 x) { return round(x * 255.0) * (1.0 / 255.0); }
vec4 sampleTexUnit0() { return const_color[0].wzyx; }
vec4 sampleTexUnit1() { return const_color[1].yzxw; }
vec4 sampleTexUnit2() { return const_color[2].zwyx; }
vec4 sampleTexUnit3() { return const_color[3].wxzy; }
)";
    code += hybrid.substr(helper_begin, helper_end - helper_begin);
    const std::string preamble = R"(
vec4 rounded_primary_color = byteround(inputs[test_id].primary);
vec4 primary_fragment_color = const_color[4];
vec4 secondary_fragment_color = const_color[5];
)";
    code += "vec4 fallback() {\n" + preamble + Body(hybrid, true) + "}\n";
    code += "vec4 specialized(uint variant) {\n" + preamble + "switch (variant) {\n";
    for (unsigned i = 0; i < configs.size(); ++i) {
        code += "case " + std::to_string(i) + "u: {\n";
        code += Body(GLSL::GenerateFragmentShader(configs[i], {}, profile));
        code += "}\n";
    }
    code += "} return vec4(-2.0); }\nvoid main() { test_id=gl_GlobalInvocationID.x; outputs[test_id*14u+12u]=fallback(); outputs[test_id*14u+13u]=specialized(test_id%64u); }\n";
    const auto spirv = Vulkan::CompileGLSL(code, vk::ShaderStageFlagBits::eCompute);
    Require(!spirv.empty(), "equivalence compute shader compiles");

    struct alignas(16) Input {
        Hybrid::Uniforms state;
        std::array<std::array<float,4>,6> colors;
        std::array<float,4> primary;
    };
    static_assert(sizeof(Input) == 224);
    std::vector<Input> input(Count);
    for (unsigned i = 0; i < Count; ++i) {
        input[i].state = Hybrid::MakeUniforms(configs[i % Configs]);
        for (auto& color : input[i].colors) for (auto& value : color) value = float(random() & 255) / 255;
        for (auto& value : input[i].primary) value = float(random() & 65535) / 65535;
    }
    const auto device = instance.GetDevice();
    const auto allocate = [&](vk::MemoryRequirements requirements) {
        const auto properties = instance.GetPhysicalDevice().getMemoryProperties();
        const auto flags = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
        for (unsigned i = 0; i < properties.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
                return device.allocateMemoryUnique({.allocationSize=requirements.size, .memoryTypeIndex=i});
        throw std::runtime_error("hybrid fixture host memory unavailable");
    };
    vk::UniqueDeviceMemory input_memory, output_memory;
    auto input_buffer = device.createBufferUnique({.size=sizeof(Input)*Count, .usage=vk::BufferUsageFlagBits::eStorageBuffer});
    auto output_buffer = device.createBufferUnique({.size=sizeof(float)*56*Count, .usage=vk::BufferUsageFlagBits::eStorageBuffer});
    input_memory = allocate(device.getBufferMemoryRequirements(*input_buffer));
    output_memory = allocate(device.getBufferMemoryRequirements(*output_buffer));
    device.bindBufferMemory(*input_buffer, *input_memory, 0);
    device.bindBufferMemory(*output_buffer, *output_memory, 0);
    auto* mapped = device.mapMemory(*input_memory, 0, VK_WHOLE_SIZE);
    std::memcpy(mapped, input.data(), sizeof(Input)*Count);
    device.unmapMemory(*input_memory);
    const std::array bindings{
        vk::DescriptorSetLayoutBinding{0,vk::DescriptorType::eStorageBuffer,1,vk::ShaderStageFlagBits::eCompute},
        vk::DescriptorSetLayoutBinding{1,vk::DescriptorType::eStorageBuffer,1,vk::ShaderStageFlagBits::eCompute}};
    auto set_layout = device.createDescriptorSetLayoutUnique({.bindingCount=2,.pBindings=bindings.data()});
    auto pipeline_layout = device.createPipelineLayoutUnique({.setLayoutCount=1,.pSetLayouts=&*set_layout});
    const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer,2};
    auto pool = device.createDescriptorPoolUnique({.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size});
    const auto set = device.allocateDescriptorSets({.descriptorPool=*pool,.descriptorSetCount=1,.pSetLayouts=&*set_layout}).front();
    const std::array infos{vk::DescriptorBufferInfo{*input_buffer,0,VK_WHOLE_SIZE},vk::DescriptorBufferInfo{*output_buffer,0,VK_WHOLE_SIZE}};
    std::array<vk::WriteDescriptorSet,2> writes{};
    for (unsigned i=0;i<2;++i)
        writes[i]={.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=vk::DescriptorType::eStorageBuffer,.pBufferInfo=&infos[i]};
    device.updateDescriptorSets(writes,{});
    auto shader = device.createShaderModuleUnique({.codeSize=spirv.size()*sizeof(u32),.pCode=spirv.data()});
    auto pipeline = device.createComputePipelineUnique({}, {
        .stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=*shader,.pName="main"},.layout=*pipeline_layout}).value;
    scheduler.Record([&](vk::CommandBuffer command) {
        command.bindPipeline(vk::PipelineBindPoint::eCompute,*pipeline);
        command.bindDescriptorSets(vk::PipelineBindPoint::eCompute,*pipeline_layout,0,set,{});
        command.dispatch(Count/64,1,1);
        const vk::MemoryBarrier barrier{.srcAccessMask=vk::AccessFlagBits::eShaderWrite,.dstAccessMask=vk::AccessFlagBits::eHostRead};
        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,vk::PipelineStageFlagBits::eHost,{},barrier,{},{});
    });
    scheduler.Finish();
    const auto* output = static_cast<const float*>(device.mapMemory(*output_memory,0,VK_WHOLE_SIZE));
    unsigned failures=0;
    for (unsigned i=0;i<Count;++i) for (unsigned step=0;step<7;++step) for (unsigned channel=0;channel<4;++channel) {
        // The specialized Never shader exits before evaluating any stages.
        if (i%8==0 && step<6) continue;
        const float actual=output[i*56+step*8+channel], expected=output[i*56+step*8+4+channel];
        if (!std::isfinite(actual) || std::abs(actual-expected)>1e-6f) {
            if (failures++<8) {
                std::cerr<<"Hybrid mismatch sample="<<i<<" step="<<step<<" channel="<<channel<<" actual="<<actual<<" expected="<<expected<<'\n';
                for(unsigned stage=0;stage<6;++stage) {
                    std::cerr<<"  stage "<<stage;
                    for(unsigned c=0;c<4;++c)std::cerr<<" "<<output[i*56+stage*8+c]<<"/"<<output[i*56+stage*8+4+c];
                    std::cerr<<'\n';
                }
            }
        }
    }
    device.unmapMemory(*output_memory);
    Require(failures==0,"GPU hybrid/specialized combiner output differs");
    // Exercise the real fallback cache and driver pipeline construction too.
    // No draw is submitted here; arithmetic output is checked above.
    {
        Vulkan::RenderManager manager(instance, scheduler);
        Vulkan::DescriptorUpdateQueue updates(instance);
        Vulkan::PipelineCache pipelines(instance, scheduler, manager, updates);
        pipelines.SetProgramID(0x0004000000125500ULL);
        std::atomic_bool stop{false};
        pipelines.LoadCache(stop, {});
        Vulkan::ShaderDiskCache cache(pipelines, 0x0004000000125500ULL);
        Vulkan::Shader vertex(instance, vk::ShaderStageFlagBits::eVertex,
                              GLSL::GenerateTrivialVertexShader(instance.IsShaderClipDistanceSupported(), true));
        const auto first = cache.GetHybridShader(configs[1], {}, false);
        const auto equivalent = cache.GetHybridShader(configs[2], {}, false);
        Require(first == equivalent, "real cache shares TEV/alpha fallback families");
        Vulkan::PipelineInfo info{};
        info.state.attachments.color = VideoCore::PixelFormat::RGBA8;
        info.state.attachments.depth = VideoCore::PixelFormat::Invalid;
        info.state.blending.color_write_mask = 15;
        info.state.vertex_layout.binding_count = 1;
        info.state.vertex_layout.attribute_count = 8;
        info.state.vertex_layout.bindings[0].byte_count.Assign(128);
        for (unsigned i = 0; i < 8; ++i) {
            auto& attribute = info.state.vertex_layout.attributes[i];
            attribute.location.Assign(i);
            attribute.type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
            attribute.size.Assign(4);
            attribute.offset.Assign(i * 16);
        }
        info.state.rasterization.topology.Assign(Pica::PipelineRegs::TriangleTopology::List);
        const std::array<Vulkan::Shader*, 3> stages{&vertex, first.second, nullptr};
        auto* first_pipeline = cache.GetHybridPipeline(info, stages, first);
        first_pipeline->TryBuild(true);
        first_pipeline->WaitDone();
        Require(bool(first_pipeline->Handle()), "real fallback graphics pipeline builds");
        info.state.shader_ids[1] = 123; // Different specialized FS, same fallback family.
        Require(cache.GetHybridPipeline(info, stages, equivalent) == first_pipeline,
                "native fragment identity does not split equivalent fallback pipelines");
        info.state.blending.color_write_mask = 7;
        auto* different = cache.GetHybridPipeline(info, stages, equivalent);
        Require(different != first_pipeline, "fallback pipeline preserves fixed-function state");
        different->TryBuild(true);
        different->WaitDone();
        Require(bool(different->Handle()), "second fallback graphics pipeline builds");
        // PipelineCache installs a callback capturing itself; remove it before
        // these local fixture objects leave scope.
        scheduler.RegisterOnDispatch([] {});
    }
    std::cout<<"PASS hybrid shaders: real fallback cache/pipelines; 24 full shader compilations; "<<Count
             <<" GPU equivalence samples, all TEV operations/modifiers, alpha tests, scales, buffer updates and pass-through\n";
}
} // namespace MM3D
