// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
namespace MM3D::EffectCapture {
struct Buffer {
    unsigned stride{},first{},last{};
    std::vector<unsigned> components;
    std::vector<std::uint8_t> bytes;
};
struct Draw {
    std::uint64_t sequence{};
    std::uint64_t frame{},shader{},swizzle{};
    std::int64_t time{};
    std::uint32_t vertices{},base{},offset{};
    bool indexed{},accelerated{};
    std::array<std::array<unsigned,3>,12> formats{};
    std::vector<Buffer> buffers;
    // Resolved shader inputs in cache-miss order, plus the submitted vertex order.
    // CPU draws cannot use the accelerated path's vertex-buffer/index metadata.
    std::vector<std::array<std::array<float,4>,16>> cpu_inputs;
    std::vector<std::uint32_t> cpu_order;
    std::array<unsigned,16> cpu_input_registers{};
    unsigned cpu_topology{},cpu_attribute_count{};
    bool depth_write{};
    std::uint32_t framebuffer{},framebuffer_width{},framebuffer_height{},framebuffer_format{};
    unsigned texture_mask{};
    std::array<unsigned,3> alpha_blend{},alpha_test{};
    std::array<unsigned,8> stencil{};
    std::array<float,2> clip_scale{1,1};
    std::vector<std::array<float,4>> clip_positions;
    std::vector<std::array<float,2>> clip_uvs;
    std::array<std::uint32_t,5> dungeon_map_identity{};
    std::array<std::uint32_t,8> dungeon_map_ownership{};
    unsigned dungeon_map_gates{},dungeon_map_result{},dungeon_map_mismatch{};
    std::array<float,2> dungeon_map_delta{};
    std::uint32_t blend_constant{};
    std::array<std::array<std::uint32_t,5>,6> tev{};
    std::array<std::uint32_t,3> textures{};
    std::array<unsigned,7> state{};
    std::array<std::array<float,4>,96> uniforms{};
};
struct Fill {
    std::uint64_t frame{},sequence{};
    std::int64_t time{};
    std::uint32_t start{},end{},value{},bytes{};
};
// Local capture evidence from after matching and CPU replay, rather than just
// the native uniforms. Flags: candidate, matched, CPU-ready, CPU-replayed,
// midpoint-submitted, CPU-mode, pause-texture, native-draw-succeeded.
struct LayoutReplay {
    std::uint64_t frame{};
    std::uint32_t texture{},vertices{},owner{},flags{};
    float position_delta{},uniform_delta{};
};
struct MapReplay : LayoutReplay {std::uint32_t gates{};};
struct MapShadow {std::uint64_t frame{};std::array<std::uint32_t,16> words{};};
struct Presentation {
    std::uint64_t frame{};
    unsigned status{},plan{};
    bool enabled{};
};
inline std::mutex mutex;
inline std::atomic<bool> active{};
inline std::atomic<unsigned> count{};
inline std::int64_t begin{},end{};
inline bool truncated{};
inline bool geometry_truncated{};
inline std::size_t geometry_bytes{};
inline std::vector<Draw> draws;
inline std::vector<Fill> fills;
inline std::vector<LayoutReplay> layout_replays;
inline std::vector<MapReplay> map_replays;
inline std::vector<MapShadow> map_shadows;
inline std::vector<Presentation> presentations;
inline std::uint64_t sequence{};
inline bool fills_truncated{};
inline std::int64_t Now() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void Start() {
    std::scoped_lock lock(mutex);active=false;draws.clear();count=0;truncated=false;geometry_truncated=false;geometry_bytes=0;
    fills.clear();sequence=0;fills_truncated=false;
    layout_replays.clear();map_replays.clear();map_shadows.clear();presentations.clear();
    begin=Now()+3000;end=begin+20000;active=true;
}
inline void Poll() {std::scoped_lock lock(mutex);if(active && Now()>=end)active=false;}
inline bool Recording(std::uint64_t frame) {
    if(!active)return false;
    std::scoped_lock lock(mutex);
    const auto now=Now();return active && now>=begin && now<end && frame%10<3;
}
inline void ObserveLayoutReplay(const LayoutReplay& replay) {
    if(!active)return;
    std::scoped_lock lock(mutex);
    const auto now=Now();
    if(active && now>=begin && now<end && layout_replays.size()<16000)
        layout_replays.push_back(replay);
}
inline void ObserveMapReplay(const MapReplay& replay) {
    if(!active)return;
    std::scoped_lock lock(mutex);
    const auto now=Now();
    if(active && now>=begin && now<end && map_replays.size()<16000)
        map_replays.push_back(replay);
}
inline void ObserveMapShadow(const MapShadow& sample) {
    if(!active)return;
    std::scoped_lock lock(mutex);
    const auto now=Now();
    if(active&&now>=begin&&now<end&&map_shadows.size()<2000)map_shadows.push_back(sample);
}
inline void ObservePresentation(const Presentation& presentation) {
    if(!active)return;
    std::scoped_lock lock(mutex);
    const auto now=Now();
    if(active && now>=begin && now<end && presentations.size()<4000)
        presentations.push_back(presentation);
}
inline void Observe(Draw draw) {
    std::scoped_lock lock(mutex);
    if(!active || Now()<begin || Now()>=end)return;
    if(draws.size()>=16000){truncated=true;active=false;return;}
    std::size_t bytes=0;for(const auto& b:draw.buffers)bytes+=b.bytes.size();
    bytes+=draw.cpu_inputs.size()*sizeof(draw.cpu_inputs.front())+
        draw.cpu_order.size()*sizeof(std::uint32_t)+
        draw.clip_positions.size()*sizeof(std::array<float,4>)+draw.clip_uvs.size()*sizeof(std::array<float,2>);
    if(geometry_bytes+bytes>32*1024*1024){
        draw.buffers.clear();draw.cpu_inputs.clear();draw.cpu_order.clear();draw.clip_positions.clear();draw.clip_uvs.clear();geometry_truncated=true;
    }
    else geometry_bytes+=bytes;
    draw.sequence=sequence++;draw.time=Now()-begin;draws.push_back(std::move(draw));count=unsigned(draws.size());
}
// Include fills from all frames: a clear can precede the sampled draw's frame.
// Only metadata is retained; this never reads image pixels or waits for the GPU.
inline void ObserveFill(Fill fill) {
    if(!active)return;
    std::scoped_lock lock(mutex);
    const auto now=Now();
    if(!active || now<begin || now>=end)return;
    if(fills.size()>=16000){fills_truncated=true;return;}
    fill.sequence=sequence++;fill.time=now-begin;fills.push_back(fill);
}
bool Export(const std::string& path);
}
