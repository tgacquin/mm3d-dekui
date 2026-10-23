// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/gpu_interpolation_inputs.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
namespace Vulkan {
inline VertexLayout InterpolatedInputLayout() {
    VertexLayout layout{};
    layout.binding_count=1;layout.attribute_count=16;
    layout.bindings[0].byte_count.Assign(sizeof(MM3D::GpuInterpolation::Snapshot::PackedVertex));
    for(unsigned a=0;a<16;++a) {
        layout.attributes[a].location.Assign(a);
        layout.attributes[a].offset.Assign(a*16);
        layout.attributes[a].type.Assign(Pica::PipelineRegs::VertexAttributeFormat::FLOAT);
        layout.attributes[a].size.Assign(4);
    }
    return layout;
}
}
