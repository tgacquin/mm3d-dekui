// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <span>
#include <vector>
#include "mm3d/pause_capture.h"

namespace MM3D::PhotoCapture {
using U32=std::uint32_t;
using Probes=PauseCapture::Probes;
// Set once when the overlay loads settings; changing the preference requires a
// game restart so existing GPU surfaces and queued saves finish normally.
inline std::atomic<bool> enabled{true};
// Private display surfaces have a fixed rotation, including after PNG reload.
// LayoutShader supplies V=1-y*240/256; binding 4 maps it to texture X=1-y.
inline constexpr unsigned SurfaceOrientation=2;
inline constexpr int TextureBinding=SurfaceOrientation+2;
inline bool HasPhotoBinding(const std::array<int,4>& bindings) {
    return std::any_of(bindings.begin(),bindings.begin()+3,[](int value){return value==3 || value==4;});
}
constexpr U32 FirstSVC=0xfffa98;
constexpr std::array<U32,4> PCs{0x188de0,0x188fb0,0x609fbc,0x1b6d38};
constexpr std::array<U32,4> Words{0xe59d3004,0xe59d000c,0xe284200c,0xe1a05001};
inline U32 Instruction(U32 pc,U32 word) {
    for(unsigned i=0;i<PCs.size();++i)if(pc==PCs[i] && word==Words[i])return 0xef000000|FirstSVC+i;
    return word;
}
template<class M> bool CodeMatches(M& m) {
    for(unsigned i=0;i<PCs.size();++i)if(m.Read(PCs[i])!=std::optional<U32>(Words[i]))return false;
    return PauseCapture::CodeMatches(m) && m.Read(0x188fac)==0xeb05dc68 &&
        m.Read(0x609fc4)==0xe8820060 && m.Read(0x17dafc)==0xe585004c &&
        m.Read(0x180074)==0x6754 && m.Read(0x180058)==0x8363;
}
inline bool Pointer(U32 p,U32 size) {return !(p&3) && p>=0x100000 && p<0x40000000 && size<=0x40000000-p;}
inline std::atomic<U32> layout{0};
struct Backend {
    virtual ~Backend()=default;
    virtual void ResetPhoto()=0;
    virtual void CapturePhoto(U32 source,unsigned eye)=0;
    virtual bool BindPhoto(U32 texture,unsigned eye,const Probes& probes)=0;
    virtual void KeepPhoto(std::span<const std::uint8_t> native)=0;
    virtual void LoadPhoto(std::span<const std::uint8_t> native)=0;
};
inline std::atomic<Backend*> backend{nullptr};
// Native MPO/JPEG bytes remain untouched; they identify an optional host image.
template<class M> std::vector<std::uint8_t> Bytes(M& m,U32 pointer,U32 size) {
    if(size<4 || size>65352 || !Pointer(pointer,(size+3)&~3u))return {};
    std::vector<std::uint8_t> bytes(size);
    for(U32 i=0;i<size;i+=4) {
        const auto word=m.Read(pointer+i);if(!word)return {};
        for(U32 c=0;c<4 && i+c<size;++c)bytes[i+c]=std::uint8_t(*word>>(8*c));
    }
    if(bytes[0]!=0xff || bytes[1]!=0xd8)return {};
    return bytes;
}
struct Hook {
    U32 owner=0,manager=0;
    bool pending=false;
    void Reset() {owner=manager=0;pending=false;layout=0;if(auto* b=backend.load())b->ResetPhoto();}
    void Destroy(U32 object) {if(owner==object)Reset();}
    void Begin(U32 object) {Reset();if(enabled.load() && Pointer(object,0x1c)){owner=object;pending=true;}}
    template<class M> void Import(M& m,U32 object,U32 data,U32 size) {
        Reset();if(!enabled.load() || !Pointer(object,0x1c))return;
        const auto bytes=Bytes(m,data,size);if(bytes.empty())return;
        owner=object;if(auto* b=backend.load())b->LoadPhoto(bytes);
    }
    template<class M> void Convert(M& m,U32 capture,U32 index,U32 eye,U32 mode) {
        if(!enabled.load() || !pending || !owner || mode!=0 || eye>1 || index>1 || !Pointer(capture,0x20))return;
        if(eye==0)manager=capture;
        if(manager!=capture)return;
        const auto src=m.Read(capture+0xc+index*4+eye*8);
        if(auto* b=backend.load();b && src && PauseCapture::Physical(*src))
            b->CapturePhoto(PauseCapture::Physical(*src),eye);
    }
    template<class M> void Keep(M& m) {
        if(!enabled.load() || !owner || !pending)return;
        const auto size=m.Read(0x7788a4);if(!size)return;
        const auto bytes=Bytes(m,0x7788a8,*size);if(bytes.empty())return;
        if(auto* b=backend.load())b->KeepPhoto(bytes);
        pending=false;
    }
    template<class M> void Bind(M& m,U32 photo_layout,U32 left,U32 right) {
        auto* b=backend.load();
        if(!enabled.load() || !b || !owner || !Pointer(photo_layout,0x148) || !Pointer(left,0xb0) ||
           right!=left+0x58 || m.Read(owner+8)!=left)return;
        for(unsigned eye=0;eye<2;++eye) {
            const auto descriptor=m.Read(left+eye*0x58+0x4c);
            if(!descriptor || !Pointer(*descriptor,0x24))continue;
            const U32 d=*descriptor;
            const bool tiled=m.Read(d+8)==0x6754; // GL_NATIVE_RGB_DMP from the JPEG decoder.
            if((!tiled && m.Read(d+8)!=0x1907) || m.Read(d+12)!=0x8363 ||
               m.Read(d+16)!=0x1000200)continue;
            const auto raw=m.Read(d+0x14),data=m.Read(d+0x20);
            if(!raw || !data || !Pointer(*raw,0x40000) || !PauseCapture::Physical(*data))continue;
            Probes probes{};bool valid=true;
            for(unsigned i=0;i<probes.size();++i) {
                const auto point=PauseCapture::ProbePoints[i];
                U32 index=point[1]*512+point[0];
                if(tiled) {
                    U32 morton=0;
                    for(unsigned bit=0;bit<3;++bit)morton|=((point[0]>>bit)&1)<<(2*bit)|((point[1]>>bit)&1)<<(2*bit+1);
                    index=(point[1]/8)*4096+(point[0]/8)*64+morton;
                }
                const U32 at=*raw+2*index;
                const auto word=m.Read(at&~3u);if(!word){valid=false;break;}
                probes[i]=(*word>>((at&2)*8))&0xffff;
            }
            if(valid && b->BindPhoto(PauseCapture::Physical(*data),eye,probes))layout=photo_layout;
        }
    }
};
}
