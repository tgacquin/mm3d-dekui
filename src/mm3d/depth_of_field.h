// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include "mm3d/camera_midpoint.h"

namespace MM3D::DepthOfField {
struct Options {
    bool enabled = false;
    float focus = 600.f;
    float transition = 1200.f;
    float strength = 1.5f;
};
inline std::atomic<bool> enabled{false};
inline std::atomic<float> focus{600.f}, transition{1200.f}, strength{1.5f};
inline float Bounded(float x,float lo,float hi,float fallback) {
    return std::isfinite(x)?std::clamp(x,lo,hi):fallback;
}
inline Options Sanitize(Options o) {
    o.focus=Bounded(o.focus,50.f,4000.f,600.f);
    o.transition=Bounded(o.transition,100.f,6000.f,1200.f);
    o.strength=Bounded(o.strength,0.f,4.f,1.5f);
    return o;
}
inline void Apply(Options o) {
    o=Sanitize(o);focus=o.focus;transition=o.transition;strength=o.strength;enabled=o.enabled;
}
inline Options Get() {return Sanitize({enabled.load(),focus.load(),transition.load(),strength.load()});}
// PICA stores A+B/d for Z buffering, or A*d+B for W buffering.
// Retain the actual viewport depth transform; reversed depth is not assumed.
struct Depth {float a{},b{};bool w_buffer{};float clear{};std::array<float,4> projection{};};
inline std::optional<Depth> Decode(const CameraMidpoint::Matrix& p,float scale,float offset,
                                   bool w_buffer,bool reversed) {
    if(!CameraMidpoint::WorldProjection(p)||!std::isfinite(scale)||!std::isfinite(offset))return {};
    Depth d{offset-scale*float(p[2][2]),scale*float(p[2][3]),w_buffer,reversed?0.f:1.f,{float(p[0][1]),float(p[1][0]),float(p[0][2]),float(p[1][2])}};
    if(!std::isfinite(d.a)||!std::isfinite(d.b)||
       std::abs(w_buffer?d.a:d.b)<1e-8f)return {};
    if(std::abs(d.projection[0])<1e-5f||std::abs(d.projection[1])<1e-5f)return {};
    return d;
}
inline float Distance(float z,const Depth& d) {
    if(std::abs(z-d.clear)<1e-7f)return 1e8f;
    const float x=d.w_buffer?(z-d.b)/d.a:d.b/(z-d.a);
    return std::isfinite(x)&&x>0?x:1e8f;
}
inline float Radius(float distance,const Options& o) {
    const auto s=Sanitize(o);
    const float t=std::clamp((distance-s.focus)/s.transition,0.f,1.f);
    return s.strength*t*t*(3.f-2.f*t);
}
}
