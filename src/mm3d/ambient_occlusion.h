// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/depth_of_field.h"
namespace MM3D::AmbientOcclusion {
struct Options {bool enabled=false;float strength=.6f;float radius=40.f;};
inline std::atomic<bool> enabled{false};
inline std::atomic<float> strength{.6f},radius{40.f};
inline Options Sanitize(Options o) {
    o.strength=DepthOfField::Bounded(o.strength,0.f,1.5f,.6f);
    o.radius=DepthOfField::Bounded(o.radius,5.f,150.f,40.f);
    return o;
}
inline void Apply(Options o) {o=Sanitize(o);strength=o.strength;radius=o.radius;enabled=o.enabled;}
inline Options Get(){return Sanitize({enabled.load(),strength.load(),radius.load()});}
}
