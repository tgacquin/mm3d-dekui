// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstring>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include "mm3d/scene_effects.h"
#include "video_core/rasterizer_cache/utils.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"
namespace MM3D {
namespace {
void Check(bool b,const char* text) {if(!b)throw std::runtime_error(text);}
}
void RunSceneEffectsFixture(const Vulkan::Instance& instance,Vulkan::Scheduler& scheduler) {
    Vulkan::RenderManager manager(instance,scheduler);
    Vulkan::DescriptorUpdateQueue updates(instance);
    Vulkan::TextureRuntime runtime(instance,scheduler,manager,updates,3);
    scheduler.RegisterOnSubmit([&]{manager.EndRendering();});
    scheduler.RegisterOnDispatch([&]{updates.Flush();});
    SceneEffects effect(instance,scheduler,manager,updates);
    for(unsigned scale:{1u,2u}) {
        VideoCore::SurfaceParams p;
        p.addr=0x18000000;p.width=p.stride=64;p.height=64;p.res_scale=scale;p.is_tiled=true;
        p.pixel_format=VideoCore::PixelFormat::RGBA8;p.UpdateParams();
        Vulkan::Surface color(runtime,p);
        p.addr=0x19000000;p.pixel_format=VideoCore::PixelFormat::D24S8;p.UpdateParams();
        Vulkan::Surface depth(runtime,p);
        const auto paint=[&](bool checker,const DepthOfField::Depth& transform,bool contact=false) {
            for(unsigned x=0;x<64;++x) {
                VideoCore::TextureClear c{};
                c.texture_rect=Common::Rectangle<u32>{x,64,x+1,0}*scale;
                const float v=checker?float(x%2):1.f;
                c.value.color={v,v,v,1.f};runtime.ClearTexture(color,c);
                const float distance=contact?(x<32?500.f:550.f):(x<32?100.f:2500.f);
                c.value.depth=transform.w_buffer?transform.a*distance+transform.b:transform.a+transform.b/distance;
                c.value.stencil=7;runtime.ClearTexture(depth,c);
            }
        };
        const auto read=[&] {
            const auto staging=runtime.FindStaging(64*64*4,false);
            color.Download({.buffer_offset=staging.offset,.buffer_size=staging.size,
                .texture_rect=color.GetRect(),.texture_level=0},staging);
            return std::vector<u8>(staging.mapped.begin(),staging.mapped.begin()+64*64*4);
        };
        const auto input=[&] {
            return SceneEffects::Input{color.Image(),depth.Image(),color.FramebufferView(),depth.DepthView(),
                depth.Aspect(),64*scale,64*scale,{0,0,int(64*scale),int(64*scale)},float(scale)};
        };
        for(const auto transform:std::array{
                DepthOfField::Depth{1.f,-5.f,false,1.f,{2.f,-1.2f,0,0}},
                DepthOfField::Depth{0.f,5.f,false,0.f,{2.f,-1.2f,0,0}},
                DepthOfField::Depth{1.f/4000,0,true,1.f,{2.f,-1.2f,0,0}}}) {
            paint(true,transform);const auto native=read();
            Check(!effect.Process(0,input(),transform,{}),"disabled effects must skip all GPU work");
            Check(read()==native,"disabled effects preserve every pixel");
            const auto tick=scheduler.CurrentTick();
            Check(effect.Process(0,input(),transform,{true,500,1000,3}),"native scene processing");
            Check(scheduler.CurrentTick()==tick,"effect must not submit or retire pending draw uploads");
            const auto blurred=read();
            unsigned changes=0;
            for(unsigned y=8;y<56;++y)for(unsigned x=8;x<56;++x) {
                const auto i=(y*64+x)*4;
                if(x<24)Check(std::memcmp(&native[i],&blurred[i],4)==0,"foreground must stay sharp");
                if(x>40 && native[i]!=blurred[i])++changes;
                Check(blurred[i+3]==native[i+3],"effect preserves alpha");
            }
            Check(changes>400,"far plane must actually blur");
            paint(true,transform);
            Check(effect.Process(1,input(),transform,{true,500,1000,3}),"midpoint scene processing");
            Check(read()==blurred,"native and midpoint slots must agree");
            // Simulate a HUD plate drawn after the scene effect.
            VideoCore::TextureClear hud{};hud.texture_rect=Common::Rectangle<u32>{4,12,12,4}*scale;
            hud.value.color={1,1,1,1};runtime.ClearTexture(color,hud);
            const auto composed=read();
            Check(composed[(8*64+8)*4]==255,"later HUD overlay remains sharp");
        }
        const DepthOfField::Depth z{1,-5,false,1,{2,-1.2f,0,0}};
        paint(false,z,true);
        Check(effect.Process(0,input(),z,{}, {true,1.5f,150.f}),"AO-only processing");
        const auto shaded=read();unsigned dark=0;
        for(unsigned y=8;y<56;++y)for(unsigned x=32;x<45;++x)
            if(shaded[(y*64+x)*4]<250)++dark;
        Check(dark>20,"depth contact must receive occlusion");
        Check(shaded[(32*64+8)*4]>=250,"unoccluded foreground must stay bright");
        paint(true,z,true);
        Check(effect.Process(0,input(),z,{true,100,200,2},{true,.6f,100}),"combined effects");
        scheduler.Finish();
        std::cout<<"PASS scene effects: scale="<<scale<<" forward/reversed/W depth, sharp foreground/HUD, blurred background, AO contacts, combined, disabled, paired targets\n";
    }
    // Isolated full-screen cost, including submission/wait. This is a
    // synthetic upper-workload sample, not a gameplay FPS prediction.
    for(unsigned scale:{1u,3u,5u}) {
        VideoCore::SurfaceParams p;
        p.addr=0x18000000;p.width=p.stride=512;p.height=400;p.res_scale=scale;p.is_tiled=true;
        p.pixel_format=VideoCore::PixelFormat::RGBA8;p.UpdateParams();
        Vulkan::Surface color(runtime,p);
        p.addr=0x19000000;p.pixel_format=VideoCore::PixelFormat::D24S8;p.UpdateParams();
        Vulkan::Surface depth(runtime,p);
        VideoCore::TextureClear clear{};clear.texture_rect=color.GetScaledRect();
        clear.value.color={.5f,.5f,.5f,1};runtime.ClearTexture(color,clear);
        clear.value.depth=1.f-5.f/2500;clear.value.stencil=0;runtime.ClearTexture(depth,clear);
        const DepthOfField::Depth transform{1,-5,false,1,{2,-1.2f,0,0}};
        const SceneEffects::Input input{color.Image(),depth.Image(),color.FramebufferView(),depth.DepthView(),
            depth.Aspect(),512*scale,400*scale,{0,0,int(480*scale),int(400*scale)},float(scale)};
        for(unsigned mode=0;mode<3;++mode) {
            double total=0;
            for(unsigned sample=0;sample<9;++sample) {
                const auto start=std::chrono::steady_clock::now();
                effect.Process(0,input,transform,{mode!=1,600,1200,1.5f},{mode!=0,.6f,40});
                scheduler.Finish();
                if(sample)total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            }
            std::cout<<"Scene effects synthetic: scale="<<scale<<" mode="<<mode<<" wall_mean_ms="<<total/8<<"\n";
        }
    }
    scheduler.Finish();
    scheduler.RegisterOnSubmit([]{});scheduler.RegisterOnDispatch([]{});
}
}
