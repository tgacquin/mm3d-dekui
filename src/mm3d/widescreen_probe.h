// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace MM3D::WidescreenProbe {
struct Draw {
    std::uint64_t shader{},swizzle{};
    unsigned vertices{},width{},height{},textures{},blend{},depth{};
    bool operator==(const Draw&) const = default;
};
struct Candidate {
    Draw draw;
    unsigned observations{};
    std::array<int,4> scissor{};
};
// A saved fullscreen rule identifies draw state, not the size of its geometry.
// The layout shader also uses that state for small solid UI panes (including
// both Schedule portrait flashes). Keep those in the same space as their icons.
inline bool LocalLayoutPane(std::uint64_t shader,std::uint64_t swizzle,unsigned vertices,
    unsigned textures,const std::array<float,4>& control,const std::array<float,4>& x,
    const std::array<float,4>& y) {
    if(shader!=0x9b343fea686d98e0ULL || swizzle!=0xac22b79e29209c01ULL ||
       vertices!=6 || textures!=0 || (control[0]!=0 && control[0]!=1) ||
       control[1]!=3 || control[2]!=0 || control[3]!=1)return false;
    for(const auto& row:{x,y})for(float v:row)if(!std::isfinite(v))return false;
    return x[1]==0 && x[2]==0 && y[1]==0 && y[3]==0 &&
        x[3]>0 && x[3]<400 && y[2]>0 && y[2]<240;
}
// UpCollect can draw the frame alone or batch it with the blackboard. Native
// packing changes the gradient's register index as panes fade in/out. Identify
// the frame by its geometry and referenced colors, not a fixed register slot.
template<class ReadRow> bool PhotoFrameBatch(std::uint64_t shader,std::uint64_t swizzle,
    unsigned vertices,unsigned textures,ReadRow row) {
    if(shader!=0x9b343fea686d98e0ULL || swizzle!=0xac22b79e29209c01ULL ||
       (vertices!=6 && vertices!=12) || textures!=0)return false;
    constexpr std::array<std::array<float,3>,4> colors{{
        {255,255,255},{214,215,211},{142,147,134},{100,106,87}}}; // BGR
    bool frame_found=false;
    for(unsigned pane=0;pane<vertices/6;++pane) {
        const auto c=row(10+4*pane),x=row(11+4*pane),y=row(12+4*pane);
        for(const auto& r:{c,x,y})for(float v:r)if(!std::isfinite(v))return false;
        if((c[0]!=0 && c[0]!=1) || c[1]!=3 || c[2]!=0 ||
           x[1]!=0 || x[2]!=0 || y[1]!=0 || y[3]!=0 || x[3]<=0 || y[2]<=0)return false;
        const float index=std::abs(c[3]);
        if(index<18 || index>95 || index!=std::floor(index))return false;
        if(c[3]<0) {
            const auto color=row(unsigned(index));
            // Only a black backdrop may share the frame's corrected draw.
            if(!std::isfinite(color[0]) || color[0]<0 || color[0]>1 ||
               color[1]!=0 || color[2]!=0 || color[3]!=0)return false;
            continue;
        }
        if(frame_found || index>92)return false;
        const float ratio=x[3]/y[2];
        if(ratio<207.49f/120.f || ratio>208.01f/120.f)return false;
        for(unsigned corner=0;corner<4;++corner) {
            const auto color=row(unsigned(index)+corner);
            for(float v:color)if(!std::isfinite(v))return false;
            if(color[0]<0 || color[0]>1)return false;
            for(unsigned channel=0;channel<3;++channel)
                if(std::abs(color[channel+1]*255-colors[corner][channel])>.1f)return false;
        }
        frame_found=true;
    }
    return frame_found;
}
// MM3D transition/title overlay, recovered from the user's tested capture.
// Observe is called only for widescreen top-screen draws without depth writes.
inline constexpr Draw TransitionOverlay{0x89b1ab98da3d9241ULL,0x6f0b398b1fc186a9ULL,
    4,480,400,0,65654,3856};
inline std::mutex mutex;
inline std::vector<Candidate> candidates;
inline std::vector<Draw> saved_draws;
inline int selected=-1;
inline unsigned bypassed=0;
inline std::int64_t capture_until=0,test_until=0;
inline auto Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void Capture() {
    std::scoped_lock lock(mutex);
    candidates.clear(); selected=-1; bypassed=0; test_until=0; capture_until=Now()+30000;
}
inline void Stop() {
    std::scoped_lock lock(mutex);
    capture_until=test_until=0; selected=-1;
}
inline bool Observe(const Draw& draw,const std::array<int,4>& scissor) {
    std::scoped_lock lock(mutex);
    const auto now=Now();
    if(now<capture_until) {
        auto it=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.draw==draw;});
        if(it!=candidates.end()) { ++it->observations; it->scissor=scissor; }
        else if(candidates.size()<128) candidates.push_back({draw,1,scissor});
    }
    const bool bypass=draw==TransitionOverlay || (std::find(saved_draws.begin(),saved_draws.end(),draw)!=saved_draws.end()) ||
        (now<test_until && selected>=0 && selected<int(candidates.size()) &&
        candidates[selected].draw==draw);
    if(bypass) ++bypassed;
    return bypass;
}
}
