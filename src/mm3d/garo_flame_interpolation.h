// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <span>
#include <vector>
#include "mm3d/fire_effect_interpolation.h"
#include "mm3d/link_pose_math.h"

namespace MM3D::GaroFlameInterpolation {
using Matrix=LinkPoseMath::Matrix;
using Vertex=std::array<std::array<float,4>,5>;
using Points=std::array<std::array<float,3>,20>;
using Key=std::array<std::uint32_t,4>; // three textures and scene generation
struct Ribbon {Points points;std::array<std::uint32_t,20> indices;};
inline std::optional<Ribbon> Decode(std::span<const Vertex> inputs,
        std::span<const std::uint32_t> order) {
    if(inputs.size()!=20 || order.size()!=54)return {};
    Ribbon out{};
    for(unsigned pair=0;pair<10;++pair)for(unsigned side=0;side<2;++side) {
        const auto at=order[pair<9?pair*6+side:50+side];
        const auto i=2*pair+side;
        if(at>=inputs.size() || std::find(out.indices.begin(),out.indices.begin()+i,at)!=out.indices.begin()+i)return {};
        out.indices[i]=at;
        const auto& v=inputs[at];
        for(float p:v[0])if(!std::isfinite(p))return {};
        if(std::abs(v[0][3]-1)>1e-5)return {};
        // Three UV sets share the strip's ten paired cross-sections. Color is
        // unused by this material and may contain stale guest attribute data.
        for(unsigned a=2;a<5;++a)for(unsigned c=0;c<4;++c) {
            const float expected=c==0?float(pair)/9:c==1?float(1-side):c==3?1.f:0.f;
            if(!std::isfinite(v[a][c]) || std::abs(v[a][c]-expected)>1e-5)return {};
        }
        for(unsigned c=0;c<3;++c)out.points[i][c]=v[0][c];
    }
    for(unsigned pair=0;pair<9;++pair) {
        const std::array<unsigned,6> pattern{2*pair,2*pair+1,2*pair+2,2*pair+3,2*pair+3,2*pair+2};
        for(unsigned k=0;k<6;++k)if(order[pair*6+k]!=out.indices[pattern[k]])return {};
    }
    return out;
}
inline Points Transform(const Points& points,const Matrix& m) {
    Points out{};
    for(unsigned i=0;i<points.size();++i)for(unsigned r=0;r<3;++r) {
        double value=m[r][3];
        for(unsigned c=0;c<3;++c)value+=m[r][c]*points[i][c];
        out[i][r]=float(value);
    }
    return out;
}
inline double Distance(const Points& a,const Points& b) {
    double total=0;
    for(unsigned i=0;i<a.size();++i) {
        double point=0;
        for(unsigned c=0;c<3;++c)point+=std::pow(double(a[i][c])-b[i][c],2);
        if(!std::isfinite(point) || point>10000)return INFINITY;
        total+=point;
    }
    return total/a.size();
}
class History {
    struct Item {Points points;std::optional<Points> half;};
    struct Entry {std::uint64_t frame;std::vector<Item> current,previous;std::vector<bool> used;};
    std::map<Key,Entry> entries;
public:
    void Reset(){entries.clear();}
    std::optional<Ribbon> Apply(std::uint64_t frame,const Key& key,const Ribbon& ribbon,const Matrix& world) {
        if(!LinkPoseMath::Rigid(world))return {};
        for(auto it=entries.begin();it!=entries.end();)
            if(it->second.frame>frame || frame-it->second.frame>1)it=entries.erase(it);else ++it;
        auto it=entries.find(key);
        if(it==entries.end()) {
            if(entries.size()>=16)return {};
            it=entries.emplace(key,Entry{frame,{},{},{}}).first;
        }
        auto& e=it->second;
        if(frame!=e.frame) {
            e.previous=std::move(e.current);e.current.clear();e.used.assign(e.previous.size(),false);e.frame=frame;
        }
        const auto points=Transform(ribbon.points,world);
        const auto convert=[&](const std::optional<Points>& half)->std::optional<Ribbon> {
            if(!half)return {};
            auto out=ribbon;out.points=Transform(*half,LinkPoseMath::InverseRigid(world));return out;
        };
        for(const auto& item:e.current)if(Distance(item.points,points)<1e-8)return convert(item.half);
        if(e.current.size()>=8)return {};
        double best=INFINITY,second=INFINITY;unsigned selected=0;
        for(unsigned i=0;i<e.previous.size();++i) {
            const double distance=Distance(e.previous[i].points,points);
            if(distance<best){second=best;best=distance;selected=i;}
            else second=std::min(second,distance);
        }
        std::optional<Points> half;
        // Match the whole strip, not draw order or just the sword's tip.
        // Ambiguous crossings and jumps stay native; a previous sword cannot
        // supply the midpoint for two different current strips.
        if(std::isfinite(best) && best<2500 && second>best*2+1 && !e.used[selected]) {
            e.used[selected]=true;half=points;
            for(unsigned i=0;i<points.size();++i)for(unsigned c=0;c<3;++c)
                (*half)[i][c]=(points[i][c]+e.previous[selected].points[i][c])*.5f;
        }
        e.current.push_back({points,half});return convert(half);
    }
};
}
