// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>

namespace MM3D::FireEffectInterpolation {
inline constexpr std::array<unsigned,6> Rows{10,11,14,15,17,18};
using Values=std::array<std::array<float,4>,6>;
using Tev=std::array<std::array<std::uint32_t,5>,6>;
using Key=std::array<std::uint32_t,5>; // enabled textures, scene generation, kind
struct Pattern {
    unsigned vertices,mask,destination,phase_row;
    std::array<float,6> basis,multipliers;
    float step;
    Tev material;
};
// Verified OB_F_firecircle, map_updraft fire core/sheet/embers, and Garo
// Master's CPU sword flames. Texture addresses are not recognition criteria.
inline constexpr Pattern Patterns[]{
    {144,7,1,0,{-6.0f,-2.0f,4.0f,1.0f,3.0f,1.0f},{3.0f,6.0f,2.0f,-4.0f,-6.0f,0.0f},0.008333333333333333f,{{{88278339u,2236416u,589832u,4294967295u,0u},{256050671u,2236416u,589825u,4281565183u,0u},{250545503u,0u,65537u,4294967295u,1u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {144,3,7,1,{5.0f,1.0f,4.0f,2.0f,0.0f,0.0f},{5.0f,1.0f,0.0f,2.0f,0.0f,0.0f},0.011111111111111112f,{{{239275587u,139264u,65545u,4279848191u,65537u},{250547743u,0u,65536u,4278190080u,0u},{235867887u,0u,65536u,4279848191u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {144,7,7,0,{4.0f,4.0f,3.0f,5.0f,-4.0f,6.0f},{2.0f,-2.0f,3.0f,-10.0f,2.0f,-6.0f},0.011111111111111112f,{{{55837524u,2236416u,65538u,4278209791u,65536u},{235867711u,0u,65545u,4278209791u,65537u},{250547951u,0u,65536u,4278190080u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {144,3,7,0,{5.0f,3.0f,-4.0f,3.0f,0.0f,0.0f},{1.0f,-6.0f,2.0f,-3.0f,0.0f,0.0f},0.011111111111111112f,{{{54723395u,2236416u,65538u,4278209791u,65536u},{235867887u,0u,65537u,4278209791u,65536u},{250547951u,0u,65536u,4278190080u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {54,7,1,2,{1.0f,1.0f,1.0f,1.0f,2.0f,-2.0f},{0.0f,0.0f,-1.0f,1.0f,-2.0f,0.0f},0.03333333333333333f,{{{54723412u,2236416u,524296u,4278190080u,0u},{250547951u,0u,1u,4281578239u,0u},{250547951u,0u,65536u,4278190080u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
};
inline unsigned ActiveRows(unsigned kind){return Patterns[kind].mask==7?6:4;}
inline std::optional<float> Phase(unsigned kind,const Values& uv) {
    if(kind>=std::size(Patterns))return {};
    const auto& p=Patterns[kind];
    const float phase=uv[p.phase_row][3]/p.multipliers[p.phase_row];
    if(!std::isfinite(phase) || phase<-.00001f || phase>1.00001f)return {};
    for(unsigned r=0;r<ActiveRows(kind);++r)for(unsigned c=0;c<4;++c) {
        const float value=uv[r][c];
        if(!std::isfinite(value))return {};
        // The fire ring also pulses the height of its third texture layer.
        if(kind==0 && r==5 && c==1) {
            if(value<1 || value>2)return {};
        } else {
            const float expected=c==3?phase*p.multipliers[r]:(c==r%2?p.basis[r]:0.f);
            if(std::abs(value-expected)>1e-5f)return {};
        }
    }
    return phase;
}
inline std::optional<unsigned> Identify(unsigned vertices,unsigned mask,unsigned source,
        unsigned destination,bool depth_write,const Values& uv,const Tev& material) {
    if(source!=6 || depth_write)return {};
    for(unsigned kind=0;kind<std::size(Patterns);++kind) {
        const auto& p=Patterns[kind];
        if(vertices==p.vertices && mask==p.mask && destination==p.destination &&
           material==p.material && Phase(kind,uv))return kind;
    }
    return {};
}
inline std::optional<Values> Midpoint(unsigned kind,const Values& a,const Values& b) {
    const auto old=Phase(kind,a),now=Phase(kind,b);
    if(!old || !now)return {};
    float step=*now-*old;step-=std::round(step);
    if(std::abs(step)>1e-5f && std::abs(step-Patterns[kind].step)>1e-5f)return {};
    auto out=b;
    // One shared phase keeps all layers synchronized at their common wrap.
    float phase=*now-step*.5f;
    if(phase<0)phase+=1;
    if(phase>1)phase-=1;
    for(unsigned r=0;r<ActiveRows(kind);++r)out[r][3]=phase*Patterns[kind].multipliers[r];
    if(kind==0) {
        if(std::abs(a[5][1]-b[5][1])>.15f)return {};
        out[5][1]=(a[5][1]+b[5][1])*.5f;
    }
    return out;
}
class History {
    struct Entry {std::uint64_t frame;Values value;std::optional<Values> half;bool ambiguous=false;};
    std::map<Key,Entry> entries;
public:
    void Reset(){entries.clear();}
    std::optional<Values> Apply(std::uint64_t frame,const Key& key,const Values& value,bool allow) {
        const auto kind=key[4];
        if(!Phase(kind,value))return {};
        for(auto it=entries.begin();it!=entries.end();)
            if(it->second.frame>frame || frame-it->second.frame>1)it=entries.erase(it);else ++it;
        auto it=entries.find(key);
        if(it==entries.end()) {
            if(entries.size()<32)entries.emplace(key,Entry{frame,value,{}});
            return {};
        }
        auto& e=it->second;
        if(e.frame==frame) {
            for(unsigned r=0;r<ActiveRows(kind);++r)if(value[r]!=e.value[r]) {
                e.ambiguous=true;e.half.reset();return {};
            }
        } else {
            const auto half=!e.ambiguous && frame==e.frame+1?Midpoint(kind,e.value,value):std::nullopt;
            e={frame,value,half};
        }
        if(!allow || !e.half)return {};
        auto out=value; // Inactive UV rows belong to the native draw, not history.
        for(unsigned r=0;r<ActiveRows(kind);++r)out[r]=(*e.half)[r];
        return out;
    }
};
}
