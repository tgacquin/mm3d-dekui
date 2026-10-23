// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/gpu_interpolation_inputs.h"
#include "video_core/renderer_vulkan/vk_interpolation_inputs.h"

void CheckGpuInputs() {
    using namespace Pica;
    using Snapshot=MM3D::GpuInterpolation::Snapshot;
    using F=PipelineRegs::VertexAttributeFormat;
    RegsInternal regs{};
    regs.pipeline.use_gs.Assign(PipelineRegs::UseGS::No);
    regs.pipeline.num_vertices=6;
    regs.vs.max_input_attribute_index.Assign(4);
    regs.vs.input_attribute_to_register_map_low=0x86530;
    auto& a=regs.pipeline.vertex_attributes;
    a.max_attribute_index.Assign(4);
    a.format0.Assign(F::FLOAT);a.size0.Assign(2);
    a.format1.Assign(F::UBYTE);a.size1.Assign(3);
    a.format2.Assign(F::SHORT);a.size2.Assign(1);
    a.format3.Assign(F::BYTE);a.size3.Assign(2);
    a.attribute_mask.Assign(1u<<4);
    auto& l=a.attribute_loaders[0];l.data_offset.Assign(128);l.byte_count.Assign(32);
    l.component_count.Assign(5);l.comp0.Assign(0);l.comp1.Assign(12);l.comp2.Assign(1);l.comp3.Assign(2);l.comp4.Assign(3);
    regs.pipeline.index_array.offset.Assign(0);
    std::vector<unsigned char> bytes(128+32*128);
    const auto put=[&](unsigned at,const auto& v){std::memcpy(bytes.data()+at,&v,sizeof(v));};
    AttributeBuffer defaults{};defaults[4]={f24::FromFloat32(7),f24::FromFloat32(8),f24::FromFloat32(9),f24::FromFloat32(10)};
    for(unsigned v=0;v<128;++v) {
        const unsigned at=128+32*v;
        put(at,float(v));put(at+4,float(v)+.25f);put(at+8,float(v)+.5f);
        put(at+16,std::array<unsigned char,4>{1,128,255,64});
        put(at+20,std::array<std::int16_t,2>{-300,500});
        put(at+24,std::array<std::int8_t,3>{-127,25,126});
    }
    unsigned denied=~0u;
    const auto read=[&](u32 at,void* out,unsigned size){
        if(at==denied || at>bytes.size() || size>bytes.size()-at)return false;
        std::memcpy(out,bytes.data()+at,size);return true;
    };
    Snapshot snapshot;
    for(bool wide:{false,true}) {
        regs.pipeline.index_array.format.Assign(wide?decltype(regs.pipeline.index_array)::SHORT:decltype(regs.pipeline.index_array)::BYTE);
        const std::array<unsigned,6> sequence{2,0,1,1,0,3};
        for(unsigned i=0;i<6;++i) {
            if(wide)put(i*2,std::uint16_t(sequence[i]));else bytes[i]=sequence[i];
        }
        Check(snapshot.Capture(regs,true,defaults,read),"GPU inputs capture 8/16-bit indices");
        Check(snapshot.Inputs().size()==4 && snapshot.Order()==std::vector<u32>({0,1,2,2,1,3}),"GPU snapshot mirrors CPU cache order");
        for(const auto& input:snapshot.Inputs()) {
            Check(input[0].w.ToFloat32()==1 && input[2].z.ToFloat32()==0 && input[2].w.ToFloat32()==1,"missing components use native defaults");
            Check(input[1].y.ToFloat32()==128 && input[1].z.ToFloat32()==255,"byte colors stay unnormalized");
            Check(input[2].x.ToFloat32()==-300 && input[2].y.ToFloat32()==500,"signed short preserved");
            Check(input[3].x.ToFloat32()==-127 && input[4].w.ToFloat32()==10,"signed bytes and fixed attributes preserved");
        }
        const auto original=bytes;
        auto changed=snapshot.Inputs();changed[0][0].x=f24::FromFloat32(77);
        auto packed=Snapshot::Pack(regs,changed,snapshot.Order());
        Check(packed && packed->size()==6 && (*packed)[0][0][0]==77,"modified inputs packed");
        Check((*packed)[0][3][1]==128 && (*packed)[0][5][0]==-300 && (*packed)[0][6][0]==-127 && (*packed)[0][8][3]==10,"all mapped GPU registers retained");
        Check((*packed)[0][15]==std::array<float,4>{0,0,0,1},"unused GPU registers retain defaults");
        Check(bytes==original && snapshot.Inputs()[0][0].x.ToFloat32()==2,"native memory and captured inputs untouched");
        auto invalid=snapshot.Order();invalid.back()=999;
        Check(!Snapshot::Pack(regs,changed,invalid),"invalid packed indices rejected before upload");
    }
    regs.pipeline.num_vertices=6;regs.pipeline.vertex_offset=3;
    Check(snapshot.Capture(regs,false,defaults,read) && snapshot.Inputs().front()[0].x.ToFloat32()==3,"non-indexed vertex offset");
    regs.pipeline.num_vertices=72;regs.pipeline.index_array.format.Assign(decltype(regs.pipeline.index_array)::BYTE);
    for(unsigned i=0;i<70;++i)bytes[i]=i;bytes[70]=0;bytes[71]=1;
    Check(snapshot.Capture(regs,true,defaults,read) && snapshot.Inputs().size()==72,"cache evictions produce same duplicates as CPU");
    denied=128;
    Check(!snapshot.Capture(regs,true,defaults,read) && !snapshot.Ready(),"unreadable input discards entire snapshot");denied=~0u;
    auto bad=regs;bad.pipeline.num_vertices=769;
    Check(!snapshot.Capture(bad,true,defaults,read),"capture allocation bounded");
    bad=regs;bad.pipeline.vertex_attributes.attribute_loaders[0].byte_count.Assign(4);
    Check(!snapshot.Capture(bad,true,defaults,read),"malformed stride rejected");
    bad=regs;bad.pipeline.use_gs.Assign(PipelineRegs::UseGS::Yes);
    Check(!snapshot.Capture(bad,true,defaults,read),"geometry shader state retention excluded");
    bad=regs;bad.pipeline.vertex_offset=~0u;
    Check(!snapshot.Capture(bad,false,defaults,read),"non-indexed vertex offset overflow rejected");
    const auto layout=Vulkan::InterpolatedInputLayout();
    Check(layout.binding_count==1 && layout.bindings[0].byte_count==sizeof(Snapshot::PackedVertex) && layout.attribute_count==16,"GPU layout matches packed size");
    for(unsigned i=0;i<16;++i)Check(layout.attributes[i].location==i && layout.attributes[i].offset==i*16 && layout.attributes[i].size==4 && layout.attributes[i].type==F::FLOAT,"GPU layout preserves every register");
    {
        using namespace MM3D::CameraMidpoint;
        RegsInternal screen{};screen.pipeline.num_vertices=4;
        screen.texturing.main_config.texture0_enable.Assign(1);
        auto& merger=screen.framebuffer.output_merger;
        merger.alphablend_enable.Assign(1);
        merger.alpha_blending.factor_source_rgb.Assign(FramebufferRegs::BlendFactor::One);
        merger.alpha_blending.factor_dest_rgb.Assign(FramebufferRegs::BlendFactor::Zero);
        Matrix projection{};projection[0]={0,-1./120,0,1};projection[1]={-1./200,0,0,1};
        projection[2]={0,0,1./1800,1./3};projection[3]={0,0,0,1};
        auto model=Identity();model[2][3]=-1200;
        const auto candidate=[&]{return MM3D::GpuInterpolation::ScreenCopyDraw(screen,projection,model,true);};
        Check(candidate(),"GPU normalized screen copy requests shaded output UVs");
        Check(!MM3D::GpuInterpolation::ScreenCopyDraw(screen,projection,model,false),"ordinary screen quads retain GPU shading");
        screen.pipeline.num_vertices=6;Check(!candidate(),"other screen geometry excluded");screen.pipeline.num_vertices=4;
        screen.texturing.main_config.texture1_enable.Assign(1);Check(!candidate(),"multitexture effects excluded");screen.texturing.main_config.texture1_enable.Assign(0);
        merger.depth_test_enable.Assign(1);Check(!candidate(),"depth-tested world geometry excluded");merger.depth_test_enable.Assign(0);
        merger.stencil_test.enable.Assign(1);Check(!candidate(),"stencil effects excluded");merger.stencil_test.enable.Assign(0);
        merger.alpha_blending.factor_source_rgb.Assign(FramebufferRegs::BlendFactor::SourceAlpha);
        Check(!candidate(),"alpha fades excluded");merger.alpha_blending.factor_source_rgb.Assign(FramebufferRegs::BlendFactor::One);
        model[0][0]=.95;Check(!candidate(),"unnormalized shrink geometry excluded");model[0][0]=1;
        // Reproduce the logged rejection: raw/unassigned input slots give zero
        // bounds, while shaded texture coordinates identify the copied scene.
        for(float width:{478.f/512,239.f/512}) {
            SceneClearPair pair;
            pair.Observe(20,0x18237fc0,true,false);
            Check(pair.Observe(20,0x18237fc0,false,NormalizedShrinkMesh(projection,model,{0,0,0,0})),"raw input bounds reproduce day-close 30fps fallback");
            pair.Observe(21,0x18237fc0,true,false);
            Check(candidate() && !pair.Observe(21,0x18237fc0,false,
                NormalizedShrinkMesh(projection,model,{0,0,width,399.f/512})) && !pair.Pending(21),
                "shaded mono/stereo UVs preserve clear-copy pair in GPU mode");
        }
        std::cout<<"PASS GPU day-close screen-copy routing and mono/stereo clear-pair regression\n";
    }
    std::cout<<"PASS GPU input bridge: formats, padding, defaults, mapping, indices, cache eviction, bounds and isolation\n";
}
