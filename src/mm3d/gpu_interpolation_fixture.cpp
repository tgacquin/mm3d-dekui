// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstring>
#include <iostream>
#include <stdexcept>
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_interpolation_inputs.h"

namespace MM3D {
void RunGpuInterpolationFixture(const Vulkan::Instance& instance,Vulkan::Scheduler& scheduler) {
    const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto device=instance.GetDevice();
    const auto allocate=[&](vk::MemoryRequirements req,vk::MemoryPropertyFlags flags) {
        const auto props=instance.GetPhysicalDevice().getMemoryProperties();
        for(unsigned i=0;i<props.memoryTypeCount;++i)
            if((req.memoryTypeBits&(1u<<i)) && (props.memoryTypes[i].propertyFlags&flags)==flags)
                return device.allocateMemoryUnique({.allocationSize=req.size,.memoryTypeIndex=i});
        throw std::runtime_error("GPU fixture memory type unavailable");
    };
    const auto host=vk::MemoryPropertyFlagBits::eHostVisible|vk::MemoryPropertyFlagBits::eHostCoherent;
    vk::UniqueDeviceMemory image_memory,vertex_memory,read_memory;
    auto image=device.createImageUnique({.imageType=vk::ImageType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
        .extent={16,16,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
        .tiling=vk::ImageTiling::eOptimal,.usage=vk::ImageUsageFlagBits::eColorAttachment|vk::ImageUsageFlagBits::eTransferSrc});
    image_memory=allocate(device.getImageMemoryRequirements(*image),vk::MemoryPropertyFlagBits::eDeviceLocal);
    device.bindImageMemory(*image,*image_memory,0);
    auto view=device.createImageViewUnique({.image=*image,.viewType=vk::ImageViewType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
        .subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}});
    const vk::AttachmentDescription attachment{.format=vk::Format::eR8G8B8A8Unorm,.samples=vk::SampleCountFlagBits::e1,
        .loadOp=vk::AttachmentLoadOp::eClear,.storeOp=vk::AttachmentStoreOp::eStore,
        .initialLayout=vk::ImageLayout::eUndefined,.finalLayout=vk::ImageLayout::eTransferSrcOptimal};
    const vk::AttachmentReference ref{0,vk::ImageLayout::eColorAttachmentOptimal};
    const vk::SubpassDescription subpass{.pipelineBindPoint=vk::PipelineBindPoint::eGraphics,.colorAttachmentCount=1,.pColorAttachments=&ref};
    const vk::SubpassDependency dependency{.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,
        .srcStageMask=vk::PipelineStageFlagBits::eColorAttachmentOutput,.dstStageMask=vk::PipelineStageFlagBits::eTransfer,
        .srcAccessMask=vk::AccessFlagBits::eColorAttachmentWrite,.dstAccessMask=vk::AccessFlagBits::eTransferRead};
    auto renderpass=device.createRenderPassUnique({.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,
        .dependencyCount=1,.pDependencies=&dependency});
    const auto view_handle=*view;
    auto framebuffer=device.createFramebufferUnique({.renderPass=*renderpass,.attachmentCount=1,.pAttachments=&view_handle,.width=16,.height=16,.layers=1});
    auto read=device.createBufferUnique({.size=1024,.usage=vk::BufferUsageFlagBits::eTransferDst});
    read_memory=allocate(device.getBufferMemoryRequirements(*read),host);device.bindBufferMemory(*read,*read_memory,0);
    constexpr unsigned vertex_bytes=sizeof(GpuInterpolation::Snapshot::PackedVertex);
    const unsigned stride=(vertex_bytes+instance.GetMinVertexStrideAlignment()-1)/instance.GetMinVertexStrideAlignment()*instance.GetMinVertexStrideAlignment();
    auto vertices=device.createBufferUnique({.size=12*stride,.usage=vk::BufferUsageFlagBits::eVertexBuffer});
    vertex_memory=allocate(device.getBufferMemoryRequirements(*vertices),host);device.bindBufferMemory(*vertices,*vertex_memory,0);
    auto pipeline_layout=device.createPipelineLayoutUnique({});
    const auto vs=Vulkan::CompileGLSL(R"(#version 450
layout(location=0) in vec4 position;
layout(location=3) in vec4 color;
layout(location=8) in vec4 fixed_value;
layout(location=15) in vec4 unused_value;
layout(location=0) out vec4 tint;
void main(){gl_Position=position;tint=color*fixed_value.w*unused_value.w;}
)",vk::ShaderStageFlagBits::eVertex);
    const auto fs=Vulkan::CompileGLSL(R"(#version 450
layout(location=0) in vec4 tint;
layout(location=0) out vec4 color;
void main(){color=tint;}
)",vk::ShaderStageFlagBits::eFragment);
    require(!vs.empty() && !fs.empty(),"GPU input fixture shaders compile");
    auto vert=device.createShaderModuleUnique({.codeSize=vs.size()*4,.pCode=vs.data()});
    auto frag=device.createShaderModuleUnique({.codeSize=fs.size()*4,.pCode=fs.data()});
    const std::array stages{
        vk::PipelineShaderStageCreateInfo{.stage=vk::ShaderStageFlagBits::eVertex,.module=*vert,.pName="main"},
        vk::PipelineShaderStageCreateInfo{.stage=vk::ShaderStageFlagBits::eFragment,.module=*frag,.pName="main"}};
    const auto layout=Vulkan::InterpolatedInputLayout();
    const vk::VertexInputBindingDescription binding{0,stride,vk::VertexInputRate::eVertex};
    std::array<vk::VertexInputAttributeDescription,16> attrs;
    for(unsigned i=0;i<16;++i)attrs[i]={layout.attributes[i].location,0,vk::Format::eR32G32B32A32Sfloat,layout.attributes[i].offset};
    const vk::PipelineVertexInputStateCreateInfo input{.vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,
        .vertexAttributeDescriptionCount=16,.pVertexAttributeDescriptions=attrs.data()};
    const vk::Viewport viewport{0,0,16,16,0,1};const vk::Rect2D scissor{{0,0},{16,16}};
    const vk::PipelineViewportStateCreateInfo viewport_state{.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    const vk::PipelineRasterizationStateCreateInfo raster{.polygonMode=vk::PolygonMode::eFill,.cullMode=vk::CullModeFlagBits::eNone,.lineWidth=1};
    const vk::PipelineMultisampleStateCreateInfo samples{.rasterizationSamples=vk::SampleCountFlagBits::e1};
    const vk::PipelineColorBlendAttachmentState color{.blendEnable=true,.srcColorBlendFactor=vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor=vk::BlendFactor::eOneMinusSrcAlpha,.colorBlendOp=vk::BlendOp::eAdd,
        .srcAlphaBlendFactor=vk::BlendFactor::eOne,.dstAlphaBlendFactor=vk::BlendFactor::eZero,.alphaBlendOp=vk::BlendOp::eAdd,
        .colorWriteMask=vk::ColorComponentFlags{15}};
    const vk::PipelineColorBlendStateCreateInfo blend{.attachmentCount=1,.pAttachments=&color};
    for(const auto topology:{vk::PrimitiveTopology::eTriangleList,vk::PrimitiveTopology::eTriangleStrip,vk::PrimitiveTopology::eTriangleFan}) {
        if(topology==vk::PrimitiveTopology::eTriangleFan && !instance.IsTriangleFanSupported())continue;
        const vk::PipelineInputAssemblyStateCreateInfo assembly{.topology=topology};
        auto pipeline=device.createGraphicsPipelineUnique({},vk::GraphicsPipelineCreateInfo{.stageCount=2,.pStages=stages.data(),
            .pVertexInputState=&input,.pInputAssemblyState=&assembly,.pViewportState=&viewport_state,.pRasterizationState=&raster,
            .pMultisampleState=&samples,.pColorBlendState=&blend,.layout=*pipeline_layout,.renderPass=*renderpass}).value;
        Pica::RegsInternal regs{};regs.vs.max_input_attribute_index.Assign(2);regs.vs.input_attribute_to_register_map_low=0x830;
        std::vector<Pica::AttributeBuffer> native(4);
        const std::array<std::array<float,2>,4> points{{{-1,-1},{1,-1},{-1,1},{1,1}}};
        for(unsigned i=0;i<4;++i) {
            native[i][0]={Pica::f24::FromFloat32(points[i][0]),Pica::f24::FromFloat32(points[i][1]),Pica::f24::Zero(),Pica::f24::One()};
            native[i][1]={Pica::f24::One(),Pica::f24::Zero(),Pica::f24::Zero(),Pica::f24::FromFloat32(.5)};
            native[i][2]={Pica::f24::Zero(),Pica::f24::Zero(),Pica::f24::Zero(),Pica::f24::One()};
        }
        const std::vector<u32> order=topology==vk::PrimitiveTopology::eTriangleList?std::vector<u32>{0,1,2,2,1,3}:
            topology==vk::PrimitiveTopology::eTriangleStrip?std::vector<u32>{0,1,2,3}:std::vector<u32>{0,1,3,2};
        const auto first=GpuInterpolation::Snapshot::Pack(regs,native,order);
        for(auto& v:native){v[1].x=Pica::f24::Zero();v[1].y=Pica::f24::One();}
        const auto second=GpuInterpolation::Snapshot::Pack(regs,native,order);
        require(first && second,"GPU fixture inputs pack");
        auto* memory=static_cast<u8*>(device.mapMemory(*vertex_memory,0,VK_WHOLE_SIZE));
        for(unsigned i=0;i<order.size();++i){std::memcpy(memory+i*stride,(*first)[i].data(),vertex_bytes);std::memcpy(memory+(i+order.size())*stride,(*second)[i].data(),vertex_bytes);}
        device.unmapMemory(*vertex_memory);
        scheduler.Record([&](vk::CommandBuffer cmd) {
            const vk::ClearValue clear{vk::ClearColorValue{std::array<float,4>{0,0,0,0}}};
            cmd.beginRenderPass({.renderPass=*renderpass,.framebuffer=*framebuffer,.renderArea=scissor,.clearValueCount=1,.pClearValues=&clear},vk::SubpassContents::eInline);
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,*pipeline);
            cmd.bindVertexBuffers(0,*vertices,vk::DeviceSize{0});cmd.draw(u32(order.size()),1,0,0);
            cmd.bindVertexBuffers(0,*vertices,vk::DeviceSize{order.size()*stride});cmd.draw(u32(order.size()),1,0,0);
            cmd.endRenderPass();
            const vk::BufferImageCopy copy{.imageSubresource={vk::ImageAspectFlagBits::eColor,0,0,1},.imageExtent={16,16,1}};
            cmd.copyImageToBuffer(*image,vk::ImageLayout::eTransferSrcOptimal,*read,copy);
        });
        scheduler.Finish();
        const auto* pixels=static_cast<const u8*>(device.mapMemory(*read_memory,0,VK_WHOLE_SIZE));
        for(unsigned i=0;i<256;++i)require(std::abs(int(pixels[i*4])-64)<=1 && std::abs(int(pixels[i*4+1])-128)<=1 && pixels[i*4+2]==0,
            "GPU readback matches two half-opacity draws with packed attributes and preserved topology");
        device.unmapMemory(*read_memory);
    }
    std::cout<<"PASS actual GPU interpolation input upload: list/strip/fan, register mapping, fixed/default attributes and dual-draw blend\n";
}
}
