// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>
#include "mm3d/profiles.h"

namespace MM3D::Atlas {
constexpr unsigned Limit = 64;
struct alignas(16) Uniforms {
    std::array<int,4> info{}; // count, inverse (keep unselected), normalized scene copy, reserved
    std::array<std::array<float,4>,Limit> regions{};
    std::array<int,4> pause{}; // Texture bindings: 1/2 dark pause, 3/4 full-bright photo; odd values flip Y.
};
static_assert(sizeof(Uniforms)==32+16*Limit);
inline bool Add(Uniforms& u,std::array<float,4> r) {
    for(float v:r) if(!std::isfinite(v) || v<0 || v>1) return false;
    if(r[0]>=r[2] || r[1]>=r[3]) return false;
    for(int i=0;i<u.info[0];++i) if(u.regions[i]==r) return true;
    if(u.info[0]>=int(Limit)) return false;
    u.regions[u.info[0]++]=r; return true;
}
inline bool Build(const Profiles::Profile& p,Uniforms& u) {
    u={};
    for(const auto& g:p.atlas_grids) {
        if(g.columns<1 || g.rows<1) return false;
        for(int cell:g.cells) {
            if(cell<0 || cell>=g.columns*g.rows) return false;
            const int x=cell%g.columns,y=cell/g.columns;
            if(!Add(u,{float(x)/g.columns,float(y)/g.rows,float(x+1)/g.columns,float(y+1)/g.rows})) return false;
        }
        for(auto r:g.rectangles) if(!Add(u,r)) return false;
    }
    return u.info[0]>0;
}
inline bool Overlaps(const Uniforms& a,const Uniforms& b) {
    for(int i=0;i<a.info[0];++i) for(int j=0;j<b.info[0];++j) {
        const auto& x=a.regions[i]; const auto& y=b.regions[j];
        if(x[0]<y[2] && y[0]<x[2] && x[1]<y[3] && y[1]<x[3]) return true;
    }
    return false;
}
// Specialize only the runtime Vulkan shader variant, leaving upstream shader caches intact.
inline std::string Shader(std::string source) {
    const auto block=source.find("vec4 blend_color;");
    const auto main=source.find("void main() {");
    if(block==std::string::npos || main==std::string::npos) throw std::runtime_error("Atlas shader interface changed");
    source.insert(main+13,R"(
    bool atlas_selected = false;
    vec2 atlas_uv = vec2(texcoord0.x, 1.0 - texcoord0.y);
    for (int i=0; i<mm3d_atlas_info.x; ++i) {
        vec4 r = mm3d_atlas_regions[i];
        atlas_selected = atlas_selected || (all(greaterThanEqual(atlas_uv,r.xy)) && all(lessThan(atlas_uv,r.zw)));
    }
    if (mm3d_atlas_info.x > 0 && (mm3d_atlas_info.y != 0 ? atlas_selected : !atlas_selected)) discard;
)");
    source.insert(block+17,"\n    ivec4 mm3d_atlas_info;\n    vec4 mm3d_atlas_regions[64];\n    ivec4 mm3d_pause_info;\n");
    for(unsigned unit=0;unit<3;++unit) {
        // Cube/shadow tex0 has a different sampler signature; it never qualifies.
        if(unit==0&&source.find("uniform sampler2D tex0;")==std::string::npos)continue;
        const std::string name="vec4 sampleTexUnit"+std::to_string(unit)+"() {";
        const auto at=source.find(name);if(at==std::string::npos)continue;
        if(unit==0) {
            // The native shrink mesh samples 478x399 pixels of a 480x400
            // scene. At normalized scale this resamples/shifts only native
            // frames, while midpoint replay preserves the original pixels.
            // Expand to the full copied rectangle for this verified draw only.
            source.insert(at+name.size(),
                "\nif (mm3d_atlas_info.z != 0) return textureLod(tex0, "
                "texcoord0 * vec2(240.0/239.0, 400.0/399.0), 0.0);\n");
        }
        const auto index=std::to_string(unit);
        std::string coord="texcoord"+index;
        if(unit==2&&source.find("textureLod(tex2, texcoord1")!=std::string::npos)coord="texcoord1";
        source.insert(at+name.size(),"\nif (mm3d_pause_info["+index+"] != 0) {\n"
            // LayoutShader flips V at instruction 0xfc: the photo's incoming
            // V range is 16/256..1, not the MFL resource's 0..240/256.
            // Resolve orientation within those 240 image rows in both cases.
            "float v_start = mm3d_pause_info["+index+"] > 2 ? 16.0/256.0 : 0.0;\n"
            "vec2 raw = vec2("+coord+".x * 512.0, (mm3d_pause_info["+index+"] % 2 == 1 ? 1.0-"+coord+".y : "+coord+".y-v_start) * 256.0);\n"
            "if (raw.x < 0.0 || raw.x > 400.0 || raw.y < 0.0 || raw.y > 240.0) return vec4(0,0,0,1);\n"
            "vec3 rgb = textureLod(tex"+index+", clamp(raw.yx / vec2(240.0,400.0), vec2(0.0), vec2(1.0)), 0.0).rgb;\n"
            "return vec4(rgb * (mm3d_pause_info["+index+"] > 2 ? 1.0 : 0.25), 1.0);\n}\n");
    }
    return source;
}
}
