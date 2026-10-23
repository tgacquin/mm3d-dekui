// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// User-supplied local captures are read only; no assets become test fixtures.
void CheckGpuCaptures(const char* directory) {
    using namespace Pica;
    using Snapshot=MM3D::GpuInterpolation::Snapshot;
    unsigned files=0,draws=0,vertices=0;
    for(const auto& name:QDir(QString::fromUtf8(directory)).entryList({"song-effect-*.json"},QDir::Files,QDir::Name)) {
        QFile file(QDir(QString::fromUtf8(directory)).filePath(name));
        Check(file.open(QIODevice::ReadOnly),"open archived capture");
        QJsonParseError error;
        const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
        Check(error.error==QJsonParseError::NoError,"parse archived capture");
        unsigned tested=0;
        for(const auto& value:doc.object()["draws"].toArray()) {
            const auto d=value.toObject();const auto raw=d["cpu_inputs"].toArray(),submitted=d["cpu_order"].toArray();
            const unsigned used=d["cpu_attribute_count"].toInt();
            if(d["shader"].toString()!="89b1ab98da3d9241" || raw.isEmpty() || submitted.isEmpty() ||
               raw.size()>Snapshot::Limit || submitted.size()>Snapshot::Limit || used<2 || used>5)continue;
            RegsInternal regs{};regs.pipeline.use_gs.Assign(PipelineRegs::UseGS::No);
            regs.pipeline.num_vertices=submitted.size();regs.pipeline.index_array.format.Assign(decltype(regs.pipeline.index_array)::SHORT);
            regs.vs.max_input_attribute_index.Assign(used-1);
            const auto mapping=d["cpu_input_registers"].toArray();
            for(unsigned a=0;a<used;++a)regs.vs.input_attribute_to_register_map_low|=u32(mapping[a].toInt())<<(a*4);
            auto& attrs=regs.pipeline.vertex_attributes;attrs.max_attribute_index.Assign(used-1);
            attrs.format0.Assign(PipelineRegs::VertexAttributeFormat::FLOAT);attrs.format1.Assign(PipelineRegs::VertexAttributeFormat::FLOAT);attrs.format2.Assign(PipelineRegs::VertexAttributeFormat::FLOAT);attrs.format3.Assign(PipelineRegs::VertexAttributeFormat::FLOAT);attrs.format4.Assign(PipelineRegs::VertexAttributeFormat::FLOAT);
            attrs.size0.Assign(3);attrs.size1.Assign(3);attrs.size2.Assign(3);attrs.size3.Assign(3);attrs.size4.Assign(3);
            auto& loader=attrs.attribute_loaders[0];loader.data_offset.Assign(4096);loader.byte_count.Assign(used*16);loader.component_count.Assign(used);
            loader.comp0.Assign(0);loader.comp1.Assign(1);loader.comp2.Assign(2);loader.comp3.Assign(3);loader.comp4.Assign(4);
            std::vector<u8> memory(4096+raw.size()*used*16);
            std::vector<AttributeBuffer> reference(raw.size());std::vector<u32> order;
            for(unsigned v=0;v<raw.size();++v) {
                const auto input=raw[v].toArray();
                for(unsigned a=0;a<used;++a)for(unsigned c=0;c<4;++c) {
                    const float x=float(input[a].toArray()[c].toDouble());
                    reference[v][a][c]=f24::FromFloat32(x);
                    std::memcpy(memory.data()+4096+v*used*16+a*16+c*4,&x,4);
                }
            }
            for(unsigned i=0;i<submitted.size();++i) {
                const auto index=u16(submitted[i].toInt());order.push_back(index);
                std::memcpy(memory.data()+i*2,&index,2);
            }
            const auto before=memory;
            Snapshot snapshot;AttributeBuffer defaults{};
            Check(snapshot.Capture(regs,true,defaults,[&](u32 at,void* out,unsigned size){
                if(at>memory.size() || size>memory.size()-at)return false;
                std::memcpy(out,memory.data()+at,size);return true;
            }),"captured draw loads through GPU snapshot");
            const auto cpu=Snapshot::Pack(regs,reference,order);
            const auto gpu=Snapshot::Pack(regs,snapshot.Inputs(),snapshot.Order());
            Check(cpu && gpu && *cpu==*gpu,"every captured submitted attribute matches CPU input order");
            auto edits=snapshot.Inputs();
            for(auto& input:edits) {input[0].x=f24::FromFloat32(input[0].x.ToFloat32()+.125f);input[1].w=f24::FromFloat32(.25f);}
            const auto packed=Snapshot::Pack(regs,edits,snapshot.Order());
            Check(packed.has_value(),"captured inputs accept isolated position/alpha changes");
            for(unsigned i=0;i<packed->size();++i)for(unsigned r=0;r<16;++r)for(unsigned c=0;c<4;++c) {
                const bool changed=(r==regs.vs.GetRegisterForAttribute(0) && c==0) ||
                                   (r==regs.vs.GetRegisterForAttribute(1) && c==3);
                Check(changed || (*packed)[i][r][c]==(*gpu)[i][r][c],"unmodified captured GPU attributes survive exactly");
            }
            Check(memory==before,"capture verification never writes source buffers");
            ++tested;vertices+=unsigned(submitted.size());
        }
        if(tested){++files;draws+=tested;std::cout<<name.toStdString()<<": "<<tested<<" input-equivalent draws\n";}
    }
    Check(files>0 && draws>0,"archive coverage is nonempty");
    std::cout<<"PASS archived GPU input parity: "<<files<<" files, "<<draws<<" draws, "<<vertices<<" submitted vertices\n";
}
