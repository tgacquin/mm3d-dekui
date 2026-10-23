// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

namespace MM3D::GreatBayWaterInterpolation {
using Values=std::array<std::array<float,4>,6>;
using Tev=std::array<std::array<std::uint32_t,5>,6>;
struct Pattern {
    std::array<unsigned,6> meshes;
    unsigned mask,source,destination;
    bool depth_write;
    std::array<float,6> basis,step;
};
// Room pipe flow, water films/pool, wheel runoff, and m2_SEAhunsui20_ jets.
// The pipe's water lives in an opaque three-texture material: the ordinary
// transparent waterfall path deliberately cannot recognize it.
inline constexpr Pattern Patterns[]{
    {{36,126,144,150,234,402},7,1,0,true,{1,1,1,1,1,1},{0,0,-1.f/15,0,-1.f/60,0}},
    {{36,1500},3,6,1,false,{1,1,1,1},{-7.f/480,0,-11.f/1200,0}},
    {{195,1851,45},3,6,1,false,{10,10,9,9},{1.f/240,0,-3.f/800,0}},
    {{690},7,6,1,false,{5,5,6,5,10,10},{-1.f/160,0,1.f/200,0,-1.f/120,0}},
    {{138},3,6,1,false,{4,1,6,-2},{0,-1.f/30,0,1.f/10}},
    {{96,72},3,6,1,false,{3,1,5,-2},{0,-1.f/20,0,2.f/15}},
    {{216},7,6,7,false,{3,2,-4,3,2,1},{0,-1.f/15,0,-1.f/20,0,0}},
    {{108},7,6,7,false,{1,1,1,2,2,1},{-1.f/60,-1.f/60,1.f/60,-1.f/15,0,0}},
    {{108},7,6,1,false,{1,4,1,1,2,2},{0,-1.f/30,0,0,0,0}},
    {{252},3,6,1,false,{6,2,-8,2},{0,-1.f/10,0,-1.f/15}},
};
inline Values Basis(unsigned kind) {
    Values v{};
    for(unsigned r=0;r<6;++r)v[r][r%2]=Patterns[kind].basis[r];
    if(kind==2) {
        v[0]={7.0710678101f,-7.0710678101f,0,0};
        v[1]={7.0710678101f,7.0710678101f,0,0};
    } else if(kind==3) {
        v={{{3.5355339050f,3.5355339050f,0,0},{-3.5355339050f,3.5355339050f,0,0},
            {5.6378626823f,2.0520124435f,0,0},{-1.7100105286f,4.6982188225f,0,0},
            {4.9996633530f,-8.6596755981f,0,0},{8.6596755981f,4.9996633530f,0,0}}};
    }
    return v;
}
inline unsigned Rows(unsigned kind) {return Patterns[kind].mask==7?6:4;}
inline std::optional<unsigned> Identify(unsigned vertices,unsigned mask,const Values& v) {
    for(unsigned kind=0;kind<std::size(Patterns);++kind) {
        const auto& p=Patterns[kind];
        if(!vertices || mask!=p.mask || std::find(p.meshes.begin(),p.meshes.end(),vertices)==p.meshes.end())continue;
        const auto basis=Basis(kind);bool valid=true;
        for(unsigned r=0;r<Rows(kind);++r)for(unsigned c=0;c<4;++c)
            valid &= std::isfinite(v[r][c]) && (c==3 || std::abs(v[r][c]-basis[r][c])<1e-5f);
        if(valid)return kind;
    }
    return {};
}
inline Tev Material(unsigned kind) {
    Tev t{};for(auto& stage:t)stage={268374015,0,0,0xffffffff,0};
    if(kind==0) {
        t[0]={238030339,0,65537,0xffffffff,1};t[1]={236916308,0,1,0xffffffff,0};
        t[2]={236916495,0,1,0xffffffff,2};t[3]={236916287,0,1,0xffffffff,1};
        t[4]={236916207,32,8,0xffffffff,0};
    } else if(kind==1 || kind==2) {
        t[0]={238030403,0,kind==1?65538u:65545u,0xffffffff,0};
        t[1]={236916547,0,8,0xffffffff,kind==1?0u:1u};
        t[2]={236916255,0,1,0xffffffff,0};t[3]={236916239,0,1,0xffffffff,kind==1?1u:0u};
    } else if(kind==3) {
        t[0]={238030403,0,65537,0xffffffff,0};t[1]={236916517,0,8,0xffffffff,0};
        t[2]={236916511,0,1,0xffffffff,1};t[3]={236916239,0,1,0xffffffff,2};
    } else if(kind==4) {
        t[0]={3149379,131072,65537,0xff000000,1};t[1]={250547743,0,65536,0xffffffff,0};
    } else if(kind==5) {
        t[0]={249564739,0,65537,0xffffffff,2};
    } else if(kind==6 || kind==7) {
        t[0]={4392259,139264,589832,0xff000000,0};t[1]={250547743,0,65536,0xffffffff,0};
    } else if(kind==8) {
        t[0]={4392259,139264,589825,0xff000000,1};t[1]={241110767,196640,65537,0xffffffff,0};
    } else if(kind==9) {
        t[0]={238030403,131072,65537,0xff000000,65536};t[1]={250547743,0,65536,0xffffffff,0};
    }
    return t;
}
inline bool MatchesMaterial(unsigned kind,unsigned source,unsigned destination,bool depth_write,Tev tev) {
    if(kind>=std::size(Patterns))return false;
    const auto& p=Patterns[kind];
    if(source!=p.source || depth_write!=p.depth_write ||
       (destination!=p.destination && !(kind==6 && destination==1)))return false;
    // These layers animate alpha separately. UV interpolation neither changes
    // their opacity nor depends on its current value.
    for(auto& stage:tev)stage[3]|=0xff000000;
    return tev==Material(kind);
}
inline std::optional<Values> Midpoint(unsigned kind,const Values& old,const Values& now) {
    if(kind>=std::size(Patterns))return {};
    const auto& p=Patterns[kind];
    if(Identify(p.meshes[0],p.mask,old)!=kind || Identify(p.meshes[0],p.mask,now)!=kind)return {};
    auto half=now;bool moving=true,stationary=true;
    for(unsigned r=0;r<Rows(kind);++r) {
        const float delta=now[r][3]-old[r][3];
        // Captured phases are unwrapped. Resets / large jumps must seed anew.
        moving &= std::abs(delta-p.step[r])<1e-5f;
        stationary &= std::abs(delta)<1e-6f;
        half[r][3]=(old[r][3]+now[r][3])*.5f;
    }
    return moving||stationary?std::optional{half}:std::nullopt;
}
}
