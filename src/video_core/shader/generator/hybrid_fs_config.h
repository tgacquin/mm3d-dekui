// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "video_core/shader/generator/pica_fs_config.h"

namespace Pica::Shader::Hybrid {
// An explicit offset leaves the existing FS and optional presentation blocks
// untouched. Only the opt-in Vulkan path allocates/uploads this extra range.
inline constexpr u32 UniformOffset = 4096;
struct alignas(16) Uniforms {
    std::array<std::array<u32, 4>, 6> stages{};
    std::array<u32, 4> control{};
    bool operator==(const Uniforms&) const = default;
};
static_assert(sizeof(Uniforms) == 112);
inline Uniforms MakeUniforms(const FSConfig& config) {
    Uniforms result;
    for (unsigned i = 0; i < result.stages.size(); ++i) {
        const auto& stage = config.texture.tev_stages[i];
        result.stages[i] = {stage.sources_raw, stage.modifiers_raw, stage.ops_raw, stage.scales_raw};
    }
    result.control = {config.texture.combiner_buffer_input.Value(),
                      static_cast<u32>(config.framebuffer.alpha_test_func.Value()), 0, 0};
    return result;
}
// Everything not interpreted at runtime remains part of the family key.
// In particular lighting, fog, texture types, depth, scissor and blending must
// never borrow a fallback compiled for different state.
inline FSConfig Family(FSConfig config) {
    config.texture.tev_stages.fill({});
    config.texture.combiner_buffer_input.Assign(0);
    config.framebuffer.alpha_test_func.Assign(FramebufferRegs::CompareFunc::Always);
    return config;
}
} // namespace Pica::Shader::Hybrid
