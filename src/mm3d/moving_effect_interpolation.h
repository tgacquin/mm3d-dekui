// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <map>
#include <vector>
#include "mm3d/link_pose_math.h"
#include "mm3d/song_effect_interpolation.h"
namespace MM3D::MovingEffectInterpolation {
using LinkPoseMath::Matrix;
using SongEffectInterpolation::Values;
using Tev=std::array<std::array<std::uint32_t,5>,6>;
struct Pattern {unsigned vertices,mask;std::array<float,6> basis,step,period;Tev material;};
// Bubble flames, beaver wakes and alien glow: material, UV basis and measured
// scroll velocity identify each independently moving model instance.
inline constexpr Pattern Patterns[]{
    {12,7,{1.0f,1.0f,2.0f,1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f,-0.03333333333333333f,0.0f,-0.016666666666666666f},{0.0f,0.0f,0.0f,2.0f,0.0f,1.0f},{{{238027845u,0u,65544u,4278203365u,0u},{250547775u,0u,65537u,4294967295u,0u},{236916463u,0u,1u,4278203365u,0u},{250547743u,0u,65536u,4278190080u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {24,3,{2.0f,2.0f,1.0f,1.0f,0.0f,0.0f},{0.0f,0.0f,0.0f,-0.03333333333333333f,0.0f,0.0f},{0.0f,0.0f,0.0f,2.0f,0.0f,0.0f},{{{249564739u,0u,65537u,4294967295u,1u},{236916463u,0u,1u,4281578188u,0u},{250547743u,0u,65536u,4278190080u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {180,7,{1.0f,1.0f,1.0f,2.0f,-2.0f,5.0f},{0.0f,0.0f,0.03333333333333333f,-0.06666666666666667f,-0.1f,-0.08333333333333333f},{0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},{{{55837524u,2236416u,589833u,4278190080u,0u},{250547743u,0u,65536u,1426063360u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {144,7,{4.0f,1.0f,6.0f,2.0f,8.0f,-3.0f},{-0.06666666666666667f,0.0f,-0.05f,-0.13333333333333333f,0.0f,0.1f},{0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},{{{239272788u,139264u,65545u,4278190080u,0u},{250547743u,0u,65536u,1426063360u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    // Romani Ranch alien beam: scrolling outer layer and inner glow.
    {324,7,{1.0f,1.0f,4.0f,1.0f,2.0f,2.0f},{-0.008333333333333333f,0.0f,-0.016666666666666666f,0.0f,0.0f,0.0f},{1.0f,0.0f,2.0f,0.0f,0.0f,0.0f},{{{89130307u,196608u,65537u,2724257279u,0u},{250547807u,0u,65545u,2724257279u,0u},{236916709u,0u,8u,4278205526u,0u},{250547807u,0u,65536u,889192448u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {180,3,{2.0f,2.0f,2.0f,2.0f,0.0f,0.0f},{0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f,0.0f,0.0f,0.0f},{{{239079139u,393216u,65537u,2717941631u,1u},{250547743u,0u,65536u,2717941631u,0u},{250547743u,0u,65536u,889192448u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    // Mirror-shield beam, rotating halo and end glow.
    {60,7,{},{},{},{{{240127555u,458752u,65537u,3118137343u,0u},{250547807u,0u,65545u,3118137343u,1u},{250547743u,0u,65536u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {90,7,{},{},{},{{{249564739u,0u,65537u,3959422976u,1u},{250547807u,0u,65538u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
    {30,7,{},{},{},{{{249564739u,0u,65537u,2181038080u,1u},{250547807u,0u,65537u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u},{268374015u,0u,0u,4294967295u,0u}}}},
};
inline bool Mirrored(unsigned kind){return kind==2 || kind==3;}
inline bool MirrorLight(unsigned kind){return kind>=6 && kind<=8;}
inline bool AnimatedColors(unsigned kind){return kind>=4 && kind<=8;}
inline unsigned Rows(unsigned kind){return Patterns[kind].mask==7?6:4;}
inline bool MirrorRotation(const Values& uv,unsigned row) {
    const auto& a=uv[row];const auto& b=uv[row+1];
    for(float f:a)if(!std::isfinite(f))return false;
    for(float f:b)if(!std::isfinite(f))return false;
    return std::abs(a[0]*a[0]+a[1]*a[1]-4)<.001f &&
        std::abs(b[0]+a[1])<1e-5 && std::abs(b[1]-a[0])<1e-5 &&
        std::abs(a[2])+std::abs(b[2])<1e-5 &&
        std::abs(a[3]-(1.f-.5f*(a[0]+a[1])))<1e-5 &&
        std::abs(b[3]-(1.f-.5f*(b[0]+b[1])))<1e-5;
}
inline bool MirrorUV(unsigned kind,const Values& uv) {
    if(!MirrorLight(kind))return false;
    for(const auto& row:uv)for(float f:row)if(!std::isfinite(f))return false;
    if(kind==6) {
        if(!MirrorRotation(uv,4))return false;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)
            if(std::abs(uv[r][c]-(c==r%2?(r==0?3.f:r==2?4.f:1.f):0.f))>1e-5)return false;
        return std::abs(uv[1][3])+std::abs(uv[3][3])<1e-5;
    }
    if(!MirrorRotation(uv,0) || !MirrorRotation(uv,2))return false;
    if(std::abs(uv[0][0]-uv[2][0])>1e-5 || std::abs(uv[0][1]+uv[2][1])>1e-5)return false;
    return uv[4]==std::array<float,4>{2,0,0,0} && uv[5]==std::array<float,4>{0,2,0,0};
}
inline std::optional<Values> MirrorUVHalf(unsigned kind,const Values& a,const Values& b) {
    if(!MirrorUV(kind,a) || !MirrorUV(kind,b))return {};
    Values out=b;const unsigned row=kind==6?4:0;
    const double old_angle=std::atan2(a[row+1][0],a[row][0]);
    const double angle=std::atan2(b[row+1][0],b[row][0]);
    const double delta=std::remainder(angle-old_angle,2*3.141592653589793);
    // The captured animation advances 0.75 degrees per native frame.
    if(std::abs(delta)>.00001 && std::abs(delta-3.141592653589793/240)>.0001)return {};
    const auto rotation=[&](unsigned r,double theta) {
        const float c=2*std::cos(theta),s=2*std::sin(theta);
        out[r]={c,-s,0,1.f-.5f*(c-s)};out[r+1]={s,c,0,1.f-.5f*(s+c)};
    };
    rotation(row,angle-delta*.5);
    if(kind!=6)rotation(2,-(angle-delta*.5));
    else for(unsigned r:{0u,2u}) {
        const float period=r==0?3.f:4.f,expected=r==0?-.025f:1.f/30;
        float step=b[r][3]-a[r][3];step-=std::round(step/period)*period;
        if(std::abs(step)>1e-6 && std::abs(step-expected)>1e-5)return {};
        out[r][3]-=step*.5f;
    }
    return out;
}
inline std::optional<unsigned> Identify(unsigned vertices,unsigned mask,const Values& uv,const Tev& material) {
    for(unsigned k=0;k<std::size(Patterns);++k){
        const auto& p=Patterns[k];
        if(vertices!=p.vertices || mask!=p.mask)continue;
        auto matched_material=material;
        if(AnimatedColors(k))for(unsigned stage=0;stage<(k==4?4u:k==5?3u:k==6?2u:1u);++stage)
            matched_material[stage][3]=p.material[stage][3];
        if(matched_material!=p.material)continue;
        if(MirrorLight(k)){if(MirrorUV(k,uv))return k;continue;}
        bool valid=true;
        for(unsigned r=0;r<Rows(k);++r)for(unsigned c=0;c<4;++c)
            valid &= std::isfinite(uv[r][c]) && (c==3 || std::abs(uv[r][c]-(c==r%2?p.basis[r]:0.f))<1e-5);
        if(valid)return k;
    }
    return {};
}
using Colors=std::array<std::array<float,4>,6>;
struct Sample {Matrix model;Values uv;Colors colors{};};
inline std::optional<Sample> Midpoint(unsigned kind,const Sample& old,const Sample& now) {
    if(kind>=std::size(Patterns))return {};
    const auto& p=Patterns[kind];Sample half=now;
    if(AnimatedColors(kind))for(unsigned stage=0;stage<6;++stage)for(unsigned c=0;c<4;++c) {
        const auto a=old.colors[stage][c],b=now.colors[stage][c];
        if(!std::isfinite(a)||!std::isfinite(b)||a<0||a>1||b<0||b>1)return {};
        half.colors[stage][c]=(a+b)*.5f;
    }
    const auto mirror=MirrorLight(kind)?MirrorUVHalf(kind,old.uv,now.uv):std::optional<Values>{};
    if(MirrorLight(kind) && !mirror)return {};
    if(mirror)half.uv=*mirror;
    bool moving=true,stationary=true;
    for(unsigned r=0;r<(MirrorLight(kind)?0:Rows(kind));++r){
        for(unsigned c=0;c<4;++c)if(!std::isfinite(old.uv[r][c]) || !std::isfinite(now.uv[r][c]) ||
            (c<3 && (std::abs(old.uv[r][c]-(c==r%2?p.basis[r]:0.f))>1e-5 ||
                     std::abs(now.uv[r][c]-(c==r%2?p.basis[r]:0.f))>1e-5)))return {};
        float step=now.uv[r][3]-old.uv[r][3];
        if(p.period[r])step-=std::round(step/p.period[r])*p.period[r];
        moving &= std::abs(step-p.step[r])<1e-5;
        stationary &= std::abs(step)<1e-6;
        half.uv[r][3]=now.uv[r][3]-step*.5f;
    }
    if(!moving && !stationary)return {};
    const auto ai=LinkPoseMath::Inverse(old.model),bi=LinkPoseMath::Inverse(now.model);
    if(!ai || !bi)return {};
    double distance=0;
    for(unsigned r=0;r<3;++r)distance+=std::pow(now.model[r][3]-old.model[r][3],2);
    if(distance>32*32)return {};
    // Reject scale resets, flips and unrelated instances, in both directions.
    for(const auto& relative:{LinkPoseMath::Multiply(*ai,now.model),LinkPoseMath::Multiply(*bi,old.model)}) {
        double change=0;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            change+=std::pow(relative[r][c]-(r==c?1.:0.),2);
        if(!std::isfinite(change) || change>(Mirrored(kind)?7.2:.5))return {};
    }
    auto old_basis=old.model,new_basis=now.model;
    // Both beaver wake meshes are mirrored. Remove that fixed reflection
    // before quaternion interpolation and restore it afterwards.
    if(Mirrored(kind))for(unsigned r=0;r<3;++r){old_basis[r][2]*=-1;new_basis[r][2]*=-1;}
    if(const auto rigid=LinkPoseMath::Half(old_basis,new_basis)) {
        half.model=*rigid;
        if(Mirrored(kind))for(unsigned r=0;r<3;++r)half.model[r][2]*=-1;
    }
    else {
        if(Mirrored(kind))return {};
        // The view-facing flame may carry nonuniform scale/shear. Only the
        // bounded, verified material above can take this affine fallback.
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
            half.model[r][c]=(old.model[r][c]+now.model[r][c])*.5;
        if(!LinkPoseMath::Inverse(half.model))return {};
    }
    return half;
}
class History {
public:
    using Key=std::array<std::uint32_t,5>; // textures, generation, kind
private:
    struct Current {Sample native;std::optional<Sample> half;};
    struct Entry {std::uint64_t frame{};std::vector<Sample> previous;std::vector<Current> current;std::vector<bool> used;};
    std::map<Key,Entry> entries;
public:
    void Reset(){entries.clear();}
    std::optional<Sample> Apply(std::uint64_t frame,const Key& key,const Sample& value) {
        if(key[4]>=std::size(Patterns) || !LinkPoseMath::Inverse(value.model))return {};
        for(auto it=entries.begin();it!=entries.end();)
            if(it->second.frame>frame || frame-it->second.frame>1)it=entries.erase(it);else ++it;
        auto it=entries.find(key);
        if(it==entries.end()){if(entries.size()>=32)return {};it=entries.emplace(key,Entry{}).first;}
        auto& e=it->second;
        if(e.frame!=frame){
            e.previous.clear();
            if(e.frame+1==frame)for(const auto& v:e.current)e.previous.push_back(v.native);
            e.current.clear();e.used.assign(e.previous.size(),false);e.frame=frame;
        }
        for(const auto& v:e.current)if(LinkPoseMath::Near(value.model,v.native.model,1e-6) && value.uv==v.native.uv && value.colors==v.native.colors)return v.half;
        if(e.current.size()>=32)return {};
        std::optional<unsigned> best;std::optional<Sample> half;double first=1e30,second=1e30;
        for(unsigned i=0;i<e.previous.size();++i){
            const auto candidate=Midpoint(key[4],e.previous[i],value);if(!candidate)continue;
            double score=0;
            for(unsigned r=0;r<3;++r)score+=std::pow(value.model[r][3]-e.previous[i].model[r][3],2);
            if(score<first){second=first;first=score;best=i;half=candidate;}else if(score<second)second=score;
        }
        if(!best || e.used[*best] || second<=first*1.5+.01)half.reset();
        if(half)e.used[*best]=true;
        e.current.push_back({value,half});return half;
    }
};
}
