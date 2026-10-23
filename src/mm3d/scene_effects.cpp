// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/scene_effects.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include <stdexcept>
namespace MM3D {
namespace {
// Fixed 16-tap disk; in-focus pixels take the one-sample early exit. A depth
// rejection prevents blurred backgrounds from pulling foreground silhouettes.
constexpr auto Shader=R"glsl(#version 450
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D color_image;
layout(binding=1) uniform sampler2D depth_image;
layout(binding=2,rgba8) writeonly uniform image2D output_image;
layout(push_constant) uniform Params {
    ivec4 rect;
    vec4 depth; // A, B, W-buffer, clear
    vec4 lens; // focus distance, transition, radius in target pixels, scale
    vec4 projection; // rotated PICA perspective: p01, p10, p02, p12
    vec4 ao; // strength, world radius, unused, unused
    vec4 viewport;
} p;
float distance_at(ivec2 at) {
    float z=texelFetch(depth_image,at,0).r;
    if(abs(z-p.depth.w)<1e-7)return 1e8;
    float d=p.depth.z>0.5?(z-p.depth.y)/p.depth.x:p.depth.y/(z-p.depth.x);
    return (isnan(d)||isinf(d)||d<=0)?1e8:d;
}
vec3 position_at(ivec2 at) {
    vec2 ndc=(vec2(at)-p.viewport.xy+0.5)/p.viewport.zw*2.0-1.0;
    float d=distance_at(at);
    return vec3((ndc.y+p.projection.w)/p.projection.y*d,
                (ndc.x+p.projection.z)/p.projection.x*d,-d);
}
ivec2 bounded(ivec2 at) {return clamp(at,p.rect.xy,p.rect.xy+p.rect.zw-1);}
float occlusion_at(ivec2 at,float d) {
    if(p.ao.x<=0.0 || d>=1e7)return 1.0;
    vec3 center=position_at(at);
    // Choose the closer-depth derivative on each axis at silhouettes.
    vec3 left=center-position_at(bounded(at-ivec2(1,0)));
    vec3 right=position_at(bounded(at+ivec2(1,0)))-center;
    vec3 down=center-position_at(bounded(at-ivec2(0,1)));
    vec3 up=position_at(bounded(at+ivec2(0,1)))-center;
    vec3 dx=abs(left.z)<abs(right.z)?left:right;
    vec3 dy=abs(down.z)<abs(up.z)?down:up;
    vec3 n=cross(dx,dy);
    if(dot(n,n)<1e-12)return 1.0;
    n=normalize(n);if(n.z<0.0)n=-n;
    vec2 pixels=min(vec2(32.0*p.lens.w),
        abs(p.projection.xy)*p.viewport.zw*0.5*p.ao.y/max(d,1.0));
    float occlusion=0.0;
    for(int i=0;i<12;++i) {
        float angle=float(i)*2.39996323;
        vec2 offset=vec2(cos(angle),sin(angle))*pixels*sqrt((float(i)+0.5)/12.0);
        ivec2 q=at+ivec2(round(offset));
        if(any(lessThan(q,p.rect.xy))||any(greaterThanEqual(q,p.rect.xy+p.rect.zw)))continue;
        vec3 delta=position_at(q)-center;
        float len=length(delta);
        if(len<0.01||len>=p.ao.y)continue;
        float horizon=max(dot(n,delta/len)-0.08,0.0);
        occlusion+=horizon*(1.0-len/p.ao.y);
    }
    return clamp(1.0-p.ao.x*occlusion*(3.0/12.0),0.15,1.0);
}
void main() {
    ivec2 local=ivec2(gl_GlobalInvocationID.xy);
    if(any(greaterThanEqual(local,p.rect.zw)))return;
    ivec2 at=local+p.rect.xy;
    vec4 center=texelFetch(color_image,at,0);
    float d=distance_at(at);
    float radius=p.lens.z*smoothstep(p.lens.x,p.lens.x+p.lens.y,d);
    float ao=occlusion_at(at,d);
    if(radius<0.25) {imageStore(output_image,at,vec4(center.rgb*ao,center.a));return;}
    vec2 size=vec2(textureSize(color_image,0));
    vec3 sum=center.rgb*2.0;float weight=2.0;
    for(int i=0;i<16;++i) {
        float angle=float(i)*2.39996323;
        float r=sqrt((float(i)+0.5)/16.0)*radius;
        vec2 pos=clamp(vec2(at)+vec2(cos(angle),sin(angle))*r,
                       vec2(p.rect.xy),vec2(p.rect.xy+p.rect.zw-1));
        float other=distance_at(ivec2(round(pos)));
        // Never include a nearer object outside this pixel's focus plane.
        float w=smoothstep(d-max(20.0,d*0.05),d,other);
        sum+=textureLod(color_image,(pos+0.5)/size,0).rgb*w;weight+=w;
    }
    imageStore(output_image,at,vec4(sum/weight*ao,center.a));
}
)glsl";
struct Params {std::array<int,4> rect;std::array<float,4> depth,lens,projection,ao,viewport;};
static_assert(sizeof(Params)==96);
}
SceneEffects::SceneEffects(const Vulkan::Instance& i,Vulkan::Scheduler& s,
        Vulkan::RenderManager& m,Vulkan::DescriptorUpdateQueue& u):instance(i),scheduler(s),manager(m),updates(u) {}
void SceneEffects::Destroy(Target& t) {
    auto device=instance.GetDevice();device.destroyImageView(t.view);device.destroyImage(t.image);
    device.freeMemory(t.memory);t={};
}
SceneEffects::~SceneEffects() {
    if(!heap)return;
    manager.EndRendering();scheduler.Finish();
    for(auto& t:targets)Destroy(t);
    for(auto& r:retired)Destroy(r.target);
    auto d=instance.GetDevice();d.destroyPipeline(pipeline);d.destroyPipelineLayout(layout);
    d.destroySampler(color_sampler);d.destroySampler(depth_sampler);
}
void SceneEffects::Initialize() {
    if(heap)return;
    const std::array<vk::DescriptorSetLayoutBinding,3> bindings{{
        {0,vk::DescriptorType::eCombinedImageSampler,1,vk::ShaderStageFlagBits::eCompute},
        {1,vk::DescriptorType::eCombinedImageSampler,1,vk::ShaderStageFlagBits::eCompute},
        {2,vk::DescriptorType::eStorageImage,1,vk::ShaderStageFlagBits::eCompute}}};
    heap=std::make_unique<Vulkan::DescriptorHeap>(instance,scheduler.GetMasterSemaphore(),bindings,32);
    const auto d=instance.GetDevice();
    const vk::PushConstantRange range{vk::ShaderStageFlagBits::eCompute,0,sizeof(Params)};
    const auto set_layout=heap->Layout();
    layout=d.createPipelineLayout({.setLayoutCount=1,.pSetLayouts=&set_layout,
        .pushConstantRangeCount=1,.pPushConstantRanges=&range});
    auto sampler=vk::SamplerCreateInfo{};
    sampler.magFilter=sampler.minFilter=vk::Filter::eLinear;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=vk::SamplerAddressMode::eClampToEdge;
    color_sampler=d.createSampler(sampler);
    sampler.magFilter=sampler.minFilter=vk::Filter::eNearest;depth_sampler=d.createSampler(sampler);
    const auto shader=Vulkan::Compile(Shader,vk::ShaderStageFlagBits::eCompute,d);
    const auto result=d.createComputePipeline({},vk::ComputePipelineCreateInfo{
         .stage={.stage=vk::ShaderStageFlagBits::eCompute,.module=shader,.pName="main"},.layout=layout});
    d.destroyShaderModule(shader);
    if(result.result!=vk::Result::eSuccess)throw std::runtime_error("Unable to create depth of field pipeline");
    pipeline=result.value;
}
bool SceneEffects::Process(unsigned slot,const Input& in,const DepthOfField::Depth& depth,
                                 DepthOfField::Options options,AmbientOcclusion::Options ao) {
    options=DepthOfField::Sanitize(options);ao=AmbientOcclusion::Sanitize(ao);
    if(((!options.enabled||options.strength==0)&&(!ao.enabled||ao.strength==0))||slot>=targets.size()||!in.color||!in.depth||
       !in.color_view||!in.depth_view||!in.width||!in.height||in.width>8192||in.height>8192||
       in.rect[0]<0||in.rect[1]<0||in.rect[2]<=0||in.rect[3]<=0||
       unsigned(in.rect[0]+in.rect[2])>in.width||unsigned(in.rect[1]+in.rect[3])>in.height||
       !std::isfinite(in.scale)||in.scale<=0)return false;
    manager.EndRendering();scheduler.FlushReplay();Initialize();
    for(auto it=retired.begin();it!=retired.end();) {
        if(scheduler.IsFree(it->tick)){Destroy(it->target);it=retired.erase(it);}else ++it;
    }
    auto& t=targets[slot];const auto device=instance.GetDevice();
    const bool fresh=!t.image||t.w!=in.width||t.h!=in.height;
    if(fresh) {
        // A resize must not submit/retire the current tick: the rasterizer may
        // already hold uploaded vertices for the upcoming HUD draw. Retain old
        // images until their recorded GPU uses have completed instead of waiting.
        if(t.image)retired.push_back({t,scheduler.CurrentTick()});
        t={};t.w=in.width;t.h=in.height;
        t.image=device.createImage({.imageType=vk::ImageType::e2D,.format=vk::Format::eR8G8B8A8Unorm,
            .extent={in.width,in.height,1},.mipLevels=1,.arrayLayers=1,.samples=vk::SampleCountFlagBits::e1,
            .tiling=vk::ImageTiling::eOptimal,.usage=vk::ImageUsageFlagBits::eStorage|vk::ImageUsageFlagBits::eTransferSrc});
        const auto req=device.getImageMemoryRequirements(t.image);
        const auto props=instance.GetPhysicalDevice().getMemoryProperties();
        unsigned type=0;
        while(type<props.memoryTypeCount && (!(req.memoryTypeBits&(1u<<type))||
              !(props.memoryTypes[type].propertyFlags&vk::MemoryPropertyFlagBits::eDeviceLocal)))++type;
        if(type==props.memoryTypeCount)throw std::runtime_error("No depth of field image memory");
        t.memory=device.allocateMemory({.allocationSize=req.size,.memoryTypeIndex=type});device.bindImageMemory(t.image,t.memory,0);
        t.view=device.createImageView({.image=t.image,.viewType=vk::ImageViewType::e2D,
            .format=vk::Format::eR8G8B8A8Unorm,.subresourceRange={vk::ImageAspectFlagBits::eColor,0,1,0,1}});
    }
    const auto set=heap->Commit();
    updates.AddImageSampler(set,0,0,in.color_view,color_sampler);
    updates.AddImageSampler(set,1,0,in.depth_view,depth_sampler);
    updates.AddStorageImage(set,2,t.view);
    const Params params{in.rect,{depth.a,depth.b,depth.w_buffer?1.f:0.f,depth.clear},
        {options.focus,options.transition,(options.enabled?options.strength:0.f)*in.scale,in.scale},depth.projection,
        {ao.enabled?ao.strength:0.f,ao.radius,0.f,0.f},
        in.viewport[2]>0&&in.viewport[3]>0?in.viewport:
            std::array<float,4>{float(in.rect[0]),float(in.rect[1]),float(in.rect[2]),float(in.rect[3])}};
    scheduler.Record([in,params,set,fresh,out=t.image,layout=layout,pipeline=pipeline](vk::CommandBuffer cmd) {
        const auto barrier=[](vk::Image image,vk::ImageAspectFlags aspect,vk::AccessFlags src,vk::AccessFlags dst) {
            return vk::ImageMemoryBarrier{.srcAccessMask=src,.dstAccessMask=dst,
                .oldLayout=vk::ImageLayout::eGeneral,.newLayout=vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
                .image=image,.subresourceRange={aspect,0,1,0,1}};
        };
        std::array before{
            barrier(in.color,vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits::eMemoryWrite,vk::AccessFlagBits::eShaderRead),
            barrier(in.depth,in.depth_aspect,vk::AccessFlagBits::eMemoryWrite,vk::AccessFlagBits::eShaderRead),
            barrier(out,vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits::eTransferRead,vk::AccessFlagBits::eShaderWrite)};
        if(fresh){before[2].oldLayout=vk::ImageLayout::eUndefined;before[2].srcAccessMask={};}
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,vk::PipelineStageFlagBits::eComputeShader,{}, {},{},before);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute,pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,layout,0,set,{});
        cmd.pushConstants(layout,vk::ShaderStageFlagBits::eCompute,0,sizeof(params),&params);
        cmd.dispatch((in.rect[2]+7)/8,(in.rect[3]+7)/8,1);
        const std::array copying{
            barrier(out,vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits::eShaderWrite,vk::AccessFlagBits::eTransferRead),
            barrier(in.color,vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits::eShaderRead,vk::AccessFlagBits::eTransferWrite)};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,vk::PipelineStageFlagBits::eTransfer,{}, {},{},copying);
        const vk::ImageSubresourceLayers layer{vk::ImageAspectFlagBits::eColor,0,0,1};
        const vk::Offset3D offset{in.rect[0],in.rect[1],0};
        const vk::ImageCopy copy{layer,offset,layer,offset,{unsigned(in.rect[2]),unsigned(in.rect[3]),1}};
        cmd.copyImage(out,vk::ImageLayout::eGeneral,in.color,vk::ImageLayout::eGeneral,copy);
        const std::array after{
            barrier(in.color,vk::ImageAspectFlagBits::eColor,vk::AccessFlagBits::eTransferWrite,
                vk::AccessFlagBits::eMemoryRead|vk::AccessFlagBits::eMemoryWrite),
            barrier(in.depth,in.depth_aspect,vk::AccessFlagBits::eShaderRead,
                vk::AccessFlagBits::eMemoryRead|vk::AccessFlagBits::eMemoryWrite)};
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer|vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eAllCommands,{}, {},{},after);
    });
    return true;
}
}
