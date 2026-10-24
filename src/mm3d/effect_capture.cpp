// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/effect_capture.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cmath>
namespace MM3D::EffectCapture {
bool Export(const std::string& path) {
    std::vector<Draw> samples;std::vector<Fill> clear_samples;bool capped,geometry_capped,fill_capped;
    std::vector<LayoutReplay> replay_samples;std::vector<Presentation> presentation_samples;
    std::vector<MapReplay> map_samples;std::vector<MapShadow> shadow_samples;
    {std::scoped_lock lock(mutex);samples.swap(draws);clear_samples.swap(fills);
        replay_samples.swap(layout_replays);presentation_samples.swap(presentations);
        map_samples.swap(map_replays);shadow_samples.swap(map_shadows);
        capped=truncated;geometry_capped=geometry_truncated;fill_capped=fills_truncated;}
    QJsonArray output,fill_output;
    QJsonArray replay_output,presentation_output;
    QJsonArray map_output,shadow_output;
    for(const auto& sample:shadow_samples) {
        QJsonArray words;for(auto word:sample.words)words.append(QString::number(word,16));
        shadow_output.append(QJsonObject{{"frame",double(sample.frame)},{"words",words}});
    }
    for(const auto& r:map_samples)
        map_output.append(QJsonObject{{"frame",double(r.frame)},
            {"texture",QString::number(r.texture,16)},{"vertices",int(r.vertices)},
            {"owner",QString::number(r.owner,16)},{"flags",int(r.flags)},{"gates",int(r.gates)},
            {"position_delta",double(r.position_delta)},{"uniform_delta",double(r.uniform_delta)}});
    for(const auto& r:replay_samples)
        replay_output.append(QJsonObject{{"frame",double(r.frame)},
            {"texture",QString::number(r.texture,16)},{"vertices",int(r.vertices)},
            {"owner",QString::number(r.owner,16)},{"flags",int(r.flags)},
            {"position_delta",double(r.position_delta)},{"uniform_delta",double(r.uniform_delta)}});
    for(const auto& p:presentation_samples)
        presentation_output.append(QJsonObject{{"frame",double(p.frame)},
            {"status",int(p.status)},{"plan",int(p.plan)},{"enabled",p.enabled}});
    for(const auto& f:clear_samples)
        fill_output.append(QJsonObject{{"frame",double(f.frame)},{"sequence",double(f.sequence)},
            {"ms",double(f.time)},{"start",QString::number(f.start,16)},
            {"end",QString::number(f.end,16)},{"value",QString::number(f.value,16)},
            {"bytes",int(f.bytes)}});
    for(const auto& d:samples) {
        QJsonArray uniforms,textures,state,buffers,formats,cpu_inputs,cpu_order,cpu_input_registers;
        for(auto reg:d.cpu_input_registers)cpu_input_registers.append(int(reg));
        for(const auto& input:d.cpu_inputs) {
            QJsonArray attributes;
            for(const auto& attribute:input) {
                QJsonArray values;
                for(float v:attribute)values.append(std::isfinite(v)?QJsonValue(double(v)):QJsonValue());
                attributes.append(values);
            }
            cpu_inputs.append(attributes);
        }
        for(auto index:d.cpu_order)cpu_order.append(double(index));
        for(const auto& f:d.formats)formats.append(QJsonArray{int(f[0]),int(f[1]),int(f[2])});
        for(const auto& b:d.buffers) {
            QJsonArray components;for(auto c:b.components)components.append(int(c));
            buffers.append(QJsonObject{{"stride",int(b.stride)},{"first",int(b.first)},{"last",int(b.last)},
                {"components",components},{"hex",QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.bytes.data()),int(b.bytes.size())).toHex())}});
        }
        for(const auto& row:d.uniforms) {
            QJsonArray r;for(float v:row)r.append(std::isfinite(v)?QJsonValue(double(v)):QJsonValue());
            uniforms.append(r);
        }
        for(auto t:d.textures)textures.append(QString::number(t,16));
        for(auto v:d.state)state.append(double(v));
        QJsonArray tev,alpha_blend,alpha_test,stencil,clip_positions,clip_uvs,map_identity,map_ownership;
        for(auto v:d.dungeon_map_identity)map_identity.append(QString::number(v,16));
        for(auto v:d.dungeon_map_ownership)map_ownership.append(QString::number(v,16));
        for(const auto& uv:d.clip_uvs) {
            QJsonArray row;for(float v:uv)row.append(std::isfinite(v)?QJsonValue(double(v)):QJsonValue());
            clip_uvs.append(row);
        }
        for(auto v:d.stencil)stencil.append(int(v));
        for(const auto& p:d.clip_positions) {
            QJsonArray row;for(float v:p)row.append(std::isfinite(v)?QJsonValue(double(v)):QJsonValue());
            clip_positions.append(row);
        }
        for(const auto& stage:d.tev) {
            QJsonArray row;for(auto v:stage)row.append(double(v));tev.append(row);
        }
        for(auto v:d.alpha_blend)alpha_blend.append(int(v));
        for(auto v:d.alpha_test)alpha_test.append(int(v));
        output.append(QJsonObject{{"frame",double(d.frame)},{"ms",double(d.time)},
            {"sequence",double(d.sequence)},{"framebuffer",QString::number(d.framebuffer,16)},
            {"framebuffer_size",QJsonArray{int(d.framebuffer_width),int(d.framebuffer_height)}},
            {"framebuffer_format",int(d.framebuffer_format)},{"texture_mask",int(d.texture_mask)},
            {"alpha_blend",alpha_blend},{"alpha_test",alpha_test},
            {"stencil",stencil},{"clip_scale",QJsonArray{d.clip_scale[0],d.clip_scale[1]}},
            {"clip_positions",clip_positions},{"clip_uvs",clip_uvs},
            {"dungeon_map_identity",map_identity},{"dungeon_map_ownership",map_ownership},
            {"dungeon_map_gates",int(d.dungeon_map_gates)},{"dungeon_map_result",int(d.dungeon_map_result)},
            {"dungeon_map_mismatch",int(d.dungeon_map_mismatch)},
            {"dungeon_map_delta",QJsonArray{d.dungeon_map_delta[0],d.dungeon_map_delta[1]}},
            {"blend_constant",QString::number(d.blend_constant,16)},{"tev",tev},
            {"shader",QString::number(d.shader,16)},{"swizzle",QString::number(d.swizzle,16)},
            {"vertices",double(d.vertices)},{"vertex_base",QString::number(d.base,16)},
            {"index_offset",double(d.offset)},{"indexed",d.indexed},{"accelerated",d.accelerated},
            {"textures",textures},{"state",state},{"uniforms",uniforms},{"formats",formats},{"buffers",buffers},
            {"cpu_inputs",cpu_inputs},{"cpu_order",cpu_order},
            {"cpu_input_registers",cpu_input_registers},{"cpu_topology",int(d.cpu_topology)},
            {"cpu_attribute_count",int(d.cpu_attribute_count)},{"depth_write",d.depth_write}});
    }
    const auto data=QJsonDocument(QJsonObject{{"schema",9},{"kind","effect-render-capture"},
        {"map_replays",map_output},{"map_shadows",shadow_output},
        {"map_shadow_fields","pass,border-overlay,minimap-hidden,root-replaced,child-count,source-pane,flags,alpha-bits,xx-bits,yy-bits,xy-bits,yx-bits,x-bits,y-bits,description-window,map-mode-word"},
        {"dungeon_map_gates","1=screen-material,2=owner,4=CPU-replay,8=native-succeeded,16=render-state,32=midpoint-uploaded,64=midpoint-submitted,128=frame-interpolated,256=HUD-rejected,512=60fps-enabled"},
        {"dungeon_map_result","0=not-attempted,1=invalid-batch,2=nonfinite,3=homogeneous-w,4=capacity,5=no-previous-key,6=ambiguous,7=vertex-count,8=teleport,9=depth,10=UV,11=nonuniform-pan,12=accepted"},
        {"dungeon_map_ownership_fields","page,layout,submitted-layout,controller,resource,room-word,floor-word,zoom-word"},
        {"map_replay_flags","1=owned-map-camera,2=midpoint-calculated,4=CPU-ready,8=CPU-replayed,16=midpoint-submitted,32=CPU-mode,64=pause-texture,128=native-draw-succeeded"},
        {"map_replay_gates","1=enabled,2=replayable,4=supported-layout,8=color-target,16=top-viewport,32=stencil-compatible,64=no-guest-region,128=full-viewport-origin,256=valid-camera-depth"},
        {"layout_replays",replay_output},{"presentations",presentation_output},
        {"layout_replay_flags","1=candidate,2=matched,4=CPU-ready,8=CPU-replayed,16=midpoint-submitted,32=CPU-mode,64=pause-texture,128=native-draw-succeeded,256=native-message-pane,512=message-midpoint-calculated,1024=message-midpoint-submitted"},
        {"presentation_status","0=off,1=settings,2=waiting-scene,3=frame-mismatch,4=no-midpoint,5=no-HUD,6=unsupported-HUD,7=comparison,8=cadence,9=active"},
        {"presentation_plan","0=native,1=pair,2=skip"},
        {"fills",fill_output},{"fills_truncated",fill_capped},
        {"tev_fields","sources,modifiers,operations,constant_color,scales (raw registers)"},
        {"alpha_blend_fields","source,destination,equation"},
        {"alpha_test_fields","enabled,function,reference"},
        {"stencil_fields","enabled,function,reference,compare_mask,write_mask,stencil_fail,depth_fail,pass (effective pipeline state)"},
        {"clip_positions_fields","Native CPU triangle positions before host clip scaling; bounded small stencil draws and dungeon-map draws; map clip_uvs retain matching UVs"},
        {"clip_scale_fields","host clip X,Y multipliers; display horizontal axis is clip Y"},
        {"truncated",capped},{"geometry_truncated",geometry_capped},{"draws",output},
        {"state_fields","blend,source,destination,equation,color_mask,depth_test,stencil_test"},
        {"sampling","Three consecutive guest frames per ten, 20 seconds after 3 second delay; top-screen native draws only."},
        {"limits","Transparent world draws and stencil draws up to 144 vertices, screen quads, or CPU particle strips up to 768 vertices, include bounded vertex buffers or resolved CPU inputs and submission order. Total geometry is capped at 32 MiB. Fill metadata includes all frames (16000 maximum); sequence orders fills and sampled draws. Fragment combiner, blending and stencil state included; no guest index payloads or texture pixels. Nonfinite values are null. Local diagnostic; do not package."}}).toJson(QJsonDocument::Compact);
    QSaveFile file(QString::fromStdString(path));
    return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&file.commit();
}
}
