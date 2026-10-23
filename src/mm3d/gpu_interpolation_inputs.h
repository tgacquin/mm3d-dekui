// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/pica/shader_unit.h"
#include "mm3d/glare_interpolation.h"
#include "mm3d/screen_shrink.h"

namespace MM3D::GpuInterpolation {
// Shrink-copy recognition needs output UVs, including shader-generated UVs.
// Input attribute slots/registers are not interchangeable with those outputs.
// Shade this four-vertex screen batch on the CPU in GPU mode too. The rasterizer
// still proves texture provenance, TEV state and output UV bounds before treating
// it as a redundant copy; this predicate only selects vertex processing.
inline bool ScreenCopyDraw(const Pica::RegsInternal& regs,
                           const CameraMidpoint::Matrix& projection,
                           const CameraMidpoint::Matrix& model, bool normalized) {
    const auto& textures=regs.texturing.main_config;
    const auto& merger=regs.framebuffer.output_merger;
    const auto& blend=merger.alpha_blending;
    using BF=Pica::FramebufferRegs::BlendFactor;
    const bool opaque=merger.alphablend_enable && blend.factor_source_rgb==BF::One &&
        blend.factor_dest_rgb==BF::Zero &&
        blend.blend_equation_rgb==Pica::FramebufferRegs::BlendEquation::Add;
    const unsigned mask=unsigned(textures.texture0_enable)|
        (unsigned(textures.texture1_enable)<<1)|(unsigned(textures.texture2_enable)<<2);
    return CameraMidpoint::NormalizedSceneCopy(normalized,true,true,
        regs.pipeline.num_vertices,mask,opaque,merger.stencil_test.enable) &&
        !merger.depth_test_enable &&
        CameraMidpoint::NormalizedShrinkMatrices(projection,model);
}
// These screen passes need shaded outputs even in GPU mode. World effects use
// input uploads instead. This does not change the user's renderer preference.
inline bool NeedsShadedVertices(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup) {
    if((regs.pipeline.num_vertices!=72 && regs.pipeline.num_vertices!=4) ||
       setup.GetProgramCodeHash()!=0x89b1ab98da3d9241ULL ||
       setup.GetSwizzleDataHash()!=0x6f0b398b1fc186a9ULL)return false;
    CameraMidpoint::Matrix projection{},model=CameraMidpoint::Identity();
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)projection[r][c]=setup.uniforms.f[r][c].ToFloat32();
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)model[r][c]=setup.uniforms.f[20+r][c].ToFloat32();
    if(regs.pipeline.num_vertices==4)
        return ScreenCopyDraw(regs,projection,model,ScreenShrink::native_normalized.load());
    GlareInterpolation::Tev tev{};const auto stages=regs.texturing.GetTevStages();
    for(unsigned i=0;i<6;++i)tev[i]={stages[i].sources_raw,stages[i].modifiers_raw,
        stages[i].ops_raw,stages[i].const_color,stages[i].scales_raw};
    return GlareInterpolation::ScreenDraw(projection,model,tev);
}
// A bounded snapshot of shader inputs, without running a CPU vertex shader.
// Cache ordering mirrors PICA's CPU path so the SAME effect decoders, topology
// checks and interpolation histories operate on either rendering backend.
struct View {
    const std::vector<Pica::AttributeBuffer>& inputs;
    const std::vector<std::uint32_t>& order;
    const auto& Inputs() const { return inputs; }
    const auto& Order() const { return order; }
};
class Snapshot {
public:
    static constexpr unsigned Limit = 768;
    using PackedVertex = std::array<std::array<float,4>,16>;
    const auto& Inputs() const { return inputs; }
    const auto& Order() const { return order; }
    bool Ready() const { return !inputs.empty() && !order.empty(); }
    void Reset() { inputs.clear(); order.clear(); }

    template<class Read>
    bool Capture(const Pica::RegsInternal& regs, bool indexed,
                 const Pica::AttributeBuffer& defaults, Read&& read) {
        Reset();
        const auto& p=regs.pipeline;
        const auto& attributes=p.vertex_attributes;
        const unsigned count=p.num_vertices, used=unsigned(regs.vs.max_input_attribute_index)+1;
        if(!count || count>Limit || used>12 || p.use_gs!=Pica::PipelineRegs::UseGS::No ||
           used>attributes.GetNumTotalAttributes())return false;
        if(!indexed && std::uint64_t(p.vertex_offset)+count-1>
                           std::numeric_limits<std::uint32_t>::max())return false;
        struct Source {std::uint64_t offset{}; unsigned stride{}, size{}, components{};
            Pica::PipelineRegs::VertexAttributeFormat format{};bool found{};};
        std::array<Source,12> sources{};
        const auto align=[](unsigned n,unsigned a){return (n+a-1)/a*a;};
        for(const auto& loader:attributes.attribute_loaders) {
            if(loader.component_count>12)return false;
            unsigned offset=0;
            for(unsigned c=0;c<loader.component_count;++c) {
                const auto attr=loader.GetComponent(c);
                if(attr>=12){offset=align(offset,4)+(attr-11)*4;continue;}
                const unsigned size=attributes.GetElementSizeInBytes(attr);
                if(!size)return false;
                offset=align(offset,size);
                auto& s=sources[attr];
                // Multiple definitions/retained attributes are outside the
                // verified effects. Do not guess their ordering or read memory.
                if(s.found)return false;
                s={std::uint64_t(loader.data_offset)+offset,unsigned(loader.byte_count),size,
                   attributes.GetNumElements(attr),attributes.GetFormat(attr),true};
                offset+=size*s.components;
                if(!s.components || s.components>4 || !s.stride || offset>s.stride)return false;
            }
        }
        std::array<unsigned,64> ids{},input_ids{};
        std::array<bool,64> valid{};
        unsigned cursor=0;
        const std::uint64_t base=attributes.GetPhysicalBaseAddress();
        const auto checked_read=[&](std::uint64_t at,void* out,unsigned size) {
            return at<=std::numeric_limits<std::uint32_t>::max() &&
                size<=0x100000000ull-at && read(std::uint32_t(at),out,size);
        };
        for(unsigned index=0;index<count;++index) {
            unsigned vertex=index+unsigned(p.vertex_offset);
            if(indexed) {
                const unsigned size=p.index_array.format?2:1;
                std::array<unsigned char,2> bytes{};
                if(!checked_read(base+p.index_array.offset+std::uint64_t(index)*size,bytes.data(),size)) {Reset();return false;}
                vertex=unsigned(bytes[0])|(unsigned(bytes[1])<<8);
            }
            unsigned found=64;
            if(indexed)for(unsigned i=0;i<64;++i)if(valid[i] && ids[i]==vertex){found=i;break;}
            if(found<64){order.push_back(input_ids[found]);continue;}
            Pica::AttributeBuffer input{};
            for(unsigned a=0;a<used;++a) {
                if(attributes.IsDefaultAttribute(a)){input[a]=defaults[a];continue;}
                const auto& s=sources[a];
                if(!s.found){Reset();return false;}
                std::array<unsigned char,16> bytes{};
                if(!checked_read(base+s.offset+std::uint64_t(vertex)*s.stride,
                                 bytes.data(),s.size*s.components)){Reset();return false;}
                for(unsigned c=0;c<4;++c) {
                    float value=c==3?1.f:0.f;
                    if(c<s.components) {
                        using F=Pica::PipelineRegs::VertexAttributeFormat;
                        switch(s.format) {
                        case F::BYTE: {std::int8_t v;std::memcpy(&v,bytes.data()+c,1);value=v;break;}
                        case F::UBYTE: value=bytes[c];break;
                        case F::SHORT: {std::int16_t v;std::memcpy(&v,bytes.data()+c*2,2);value=v;break;}
                        case F::FLOAT: std::memcpy(&value,bytes.data()+c*4,4);break;
                        }
                    }
                    input[a][c]=Pica::f24::FromFloat32(value);
                }
            }
            const auto id=unsigned(inputs.size());inputs.push_back(input);order.push_back(id);
            if(indexed){valid[cursor]=true;ids[cursor]=vertex;input_ids[cursor]=id;cursor=(cursor+1)%64;}
        }
        return Ready();
    }

    // Expand the submitted index order into a private non-indexed GPU buffer.
    // This preserves strip connectors, repeated vertices, and cache eviction.
    // No guest/native buffer is changed; all shader input registers survive.
    static std::optional<std::vector<PackedVertex>> Pack(
            const Pica::RegsInternal& regs, std::span<const Pica::AttributeBuffer> input,
            std::span<const std::uint32_t> order) {
        const unsigned used=unsigned(regs.vs.max_input_attribute_index)+1;
        if(input.empty() || input.size()>Limit || order.empty() || order.size()>Limit || used>16)return {};
        std::vector<PackedVertex> result;result.reserve(order.size());
        for(const auto index:order) {
            if(index>=input.size())return {};
            PackedVertex vertex{};for(auto& a:vertex)a[3]=1;
            for(unsigned a=0;a<used;++a)for(unsigned c=0;c<4;++c)
                vertex[regs.vs.GetRegisterForAttribute(a)][c]=input[index][a][c].ToFloat32();
            result.push_back(vertex);
        }
        return result;
    }
private:
    std::vector<Pica::AttributeBuffer> inputs;
    std::vector<std::uint32_t> order;
};
}
