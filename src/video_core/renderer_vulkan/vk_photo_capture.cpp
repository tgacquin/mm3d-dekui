// SPDX-License-Identifier: GPL-2.0-or-later
#include <cryptopp/sha.h>
#include "mm3d/widescreen.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/frontend/image_interface.h"
#include "core/memory.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_photo_transfer.h"

namespace Vulkan {
namespace {
std::string PhotoPath(std::span<const u8> native,unsigned eye) {
    std::array<u8,CryptoPP::SHA256::DIGESTSIZE> digest{};
    CryptoPP::SHA256{}.CalculateDigest(digest.data(),native.data(),native.size());
    constexpr char hex[]="0123456789abcdef";
    std::string key;
    for(auto byte:digest){key+=hex[byte>>4];key+=hex[byte&15];}
    return FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir)+"mm3d-local/pictographs/"+
        key+(eye?"-right.png":"-left.png");
}
}
void RasterizerVulkan::ResetPhoto() {
    photo_textures={};photo_orientation={};MM3D::PhotoCapture::layout=0;
    // Private surfaces may still be sampled by queued draws.
    if(photo_images[0] || photo_images[1]){runtime.Finish();photo_images={};}
}
void RasterizerVulkan::CapturePhoto(u32 address,unsigned eye) {
    if(!MM3D::PhotoCapture::enabled.load() || eye>1)return;
    if(eye==0)ResetPhoto();
    if(!MM3D::Widescreen::Active())return;
    VideoCore::SurfaceParams params;
    params.addr=address;params.width=params.stride=240;params.height=400;
    params.pixel_format=VideoCore::PixelFormat::RGB8;params.is_tiled=false;params.UpdateParams();
    const auto [id,rect]=res_cache.GetSurfaceSubRect(params,VideoCore::ScaleMatch::Ignore,true);
    if(!id)return;
    auto& source=res_cache.GetSurface(id);const auto scale=source.GetResScale();
    if(!scale || scale>16 || source.IsCustom() || rect.GetWidth()!=240*scale || rect.GetHeight()!=400*scale)return;
    // Native dimensions equal the full GPU extent: Download must not downsample.
    params.addr=0;params.width=params.stride=240*scale;params.height=400*scale;
    params.res_scale=1;params.pixel_format=VideoCore::PixelFormat::RGBA8;params.UpdateParams();
    auto image=std::make_unique<Surface>(runtime,params);
    const VideoCore::TextureBlit copy{.src_level=source.LevelOf(address),.dst_level=0,
        .src_rect=rect,.dst_rect={0,params.height,params.width,0}};
    if(!runtime.BlitTextures(source,*image,copy)){runtime.Finish();return;}
    photo_images[eye]=std::move(image);
    LOG_INFO(Render_Vulkan,"Pictograph captured eye {} at {}x{}",eye,400*scale,240*scale);
}
bool RasterizerVulkan::BindPhoto(u32 texture,unsigned eye,const MM3D::PhotoCapture::Probes& probes) {
    if(!MM3D::PhotoCapture::enabled.load() || eye>1 || !MM3D::Widescreen::Active() || !(photo_images[eye] || photo_images[0]))return false;
    const auto data=memory.GetPhysicalRef(texture);
    if(!data || data.GetSize()<0x40000)return false;
    const auto bytes=data.GetReadBytes<u8>(0x40000);
    const auto orientation=MM3D::PauseCapture::Orientation([&](u32 offset)->std::optional<u32>{
        return u32(bytes[offset])|(u32(bytes[offset+1])<<8);
    },probes);
    if(!orientation)return false;
    // Private surfaces always use the display-buffer rotation: upright PNG
    // (x,y) corresponds to texture (1-y,x). CapturePhoto, LoadPhoto, and the
    // retained-pixel path all preserve that convention. JPEG upload orientation
    // validates ownership above, but must not choose the private image's flip.
    photo_textures[eye]=texture;
    photo_orientation[eye]=MM3D::PhotoCapture::SurfaceOrientation;
    return true;
}
void RasterizerVulkan::KeepPhoto(std::span<const u8> native) {
    if(!MM3D::PhotoCapture::enabled.load())return;
    auto codec=Core::System::GetInstance().GetImageInterface();
    if(!codec || !photo_images[0] || native.empty())return;
    std::array<std::shared_ptr<const PhotoTransfer::Pixels>,2> snapshots;
    const std::array paths{PhotoPath(native,0),PhotoPath(native,1)};
    for(unsigned eye=0;eye<2;++eye) {
        if(eye==1 && !photo_images[1]){snapshots[1]=snapshots[0];continue;}
        const auto& surface=photo_images[eye];
        auto pixels=std::make_shared<PhotoTransfer::Pixels>();
        pixels->width=surface->width;pixels->height=surface->height;
        pixels->bytes=PhotoTransfer::Read(runtime,*surface,{0,surface->height,surface->width,0});
        if(pixels->bytes.empty())return;
        snapshots[eye]=std::move(pixels);
    }
    // Keep immutable pixels available for an immediate Gear reopen while the
    // PNG writer is still running. No guest/GPU objects enter the worker.
    photo_saved_key=paths[0];photo_saved_pixels=snapshots;
    photo_saves.Submit([codec=std::move(codec),snapshots=std::move(snapshots),paths] {
      try {
        for(unsigned eye=0;eye<2;++eye) {
            const auto& source=*snapshots[eye];
            const u32 h=source.width,source_w=source.height,w=(h*16+4)/9;
            const auto& pixels=source.bytes;
            // Rotate the framebuffer and correct pixel aspect off the emulation thread.
            std::vector<u8> rgba(std::size_t(w)*h*4);
            for(u32 y=0;y<h;++y)for(u32 x=0;x<w;++x) {
                const double sx=std::clamp((double(x)+.5)*source_w/w-.5,0.,double(source_w-1));
                const u32 a=u32(sx),b=std::min(a+1,source_w-1);
                for(unsigned c=0;c<3;++c)
                    rgba[(std::size_t(y)*w+x)*4+c]=u8(std::lround(
                        pixels[(std::size_t(a)*h+(h-1-y))*4+c]*(1-(sx-a))+
                        pixels[(std::size_t(b)*h+(h-1-y))*4+c]*(sx-a)));
                rgba[(std::size_t(y)*w+x)*4+3]=255;
            }
            const auto& path=paths[eye];const auto temporary=path+".tmp";
            if(!FileUtil::CreateFullPath(path) || !codec->EncodePNG(temporary,w,h,rgba) ||
               !FileUtil::Rename(temporary,path)) {
                FileUtil::Delete(temporary);
                LOG_WARNING(Render_Vulkan,"Could not save high-resolution pictograph {}",path);
            } else LOG_INFO(Render_Vulkan,"Saved high-resolution pictograph {} ({}x{})",path,w,h);
        }
      } catch(const std::exception& e) {
        LOG_WARNING(Render_Vulkan,"Could not save high-resolution pictograph: {}",e.what());
      }
    });
}
void RasterizerVulkan::LoadPhoto(std::span<const u8> native) {
    ResetPhoto();if(!MM3D::PhotoCapture::enabled.load())return;
    auto codec=Core::System::GetInstance().GetImageInterface();if(!codec)return;
    if(photo_saved_key==PhotoPath(native,0) && photo_saved_pixels[0] && photo_saved_pixels[1]) {
        for(unsigned eye=0;eye<2;++eye) {
            const auto& pixels=*photo_saved_pixels[eye];
            VideoCore::SurfaceParams params;
            params.width=params.stride=pixels.width;params.height=pixels.height;params.res_scale=1;
            params.pixel_format=VideoCore::PixelFormat::RGBA8;params.is_tiled=false;params.UpdateParams();
            auto image=std::make_unique<Surface>(runtime,params);
            const auto staging=runtime.FindStaging(u32(pixels.bytes.size()),true);
            std::copy(pixels.bytes.begin(),pixels.bytes.end(),staging.mapped.begin());
            image->Upload({.buffer_offset=staging.offset,.buffer_size=staging.size,
                .texture_rect={0,params.height,params.width,0},.texture_level=0},staging);
            photo_images[eye]=std::move(image);
        }
        return;
    }
    for(unsigned eye=0;eye<2;++eye) {
        const auto path=PhotoPath(native,eye);FileUtil::IOFile file(path,"rb");
        if(!file.IsOpen() || file.GetSize()<24 || file.GetSize()>128*1024*1024)continue;
        std::vector<u8> png(file.GetSize());
        if(file.ReadBytes(png.data(),png.size())!=png.size())continue;
        // Bound dimensions before invoking the decoder, including corrupt files.
        constexpr std::array<u8,8> signature{137,80,78,71,13,10,26,10};
        if(!std::equal(signature.begin(),signature.end(),png.begin()))continue;
        const auto be=[&](unsigned at){return (u32(png[at])<<24)|(u32(png[at+1])<<16)|(u32(png[at+2])<<8)|png[at+3];};
        const u32 expected_w=be(16),expected_h=be(20);
        if(!expected_h || expected_h>3840 || expected_w!=(expected_h*16+4)/9 || expected_h%240)continue;
        u32 w=0,h=0;std::vector<u8> rgba;
        if(!codec->DecodePNG(rgba,w,h,png) || w!=expected_w || h!=expected_h || rgba.size()!=std::size_t(w)*h*4)continue;
        VideoCore::SurfaceParams params;
        params.width=params.stride=h;params.height=w;params.res_scale=1;
        params.pixel_format=VideoCore::PixelFormat::RGBA8;params.is_tiled=false;params.UpdateParams();
        auto image=std::make_unique<Surface>(runtime,params);
        const auto staging=runtime.FindStaging(u32(rgba.size()),true);
        for(u32 y=0;y<h;++y)for(u32 x=0;x<w;++x)
            std::copy_n(rgba.data()+(std::size_t(y)*w+x)*4,4,staging.mapped.data()+(std::size_t(x)*h+(h-1-y))*4);
        image->Upload({.buffer_offset=staging.offset,.buffer_size=u32(rgba.size()),
            .texture_rect={0,w,h,0},.texture_level=0},staging);
        photo_images[eye]=std::move(image);
    }
}
}
