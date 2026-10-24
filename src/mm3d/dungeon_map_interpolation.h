// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/glare_interpolation.h"
#include <map>
#include <mutex>

namespace MM3D::DungeonMapInterpolation {
using U32=std::uint32_t;
using U64=std::uint64_t;
// Page, map resource, room/floor words, zoom mode. No animated pan coordinates.
using Identity=std::array<U32,5>;
// Ownership evidence is retained even when the conservative matcher rejects it.
using Ownership=std::array<U32,8>; // owner,panel,submitted,controller,resource,room,floor,mode
struct Stamp {U64 frame;U32 generation;std::optional<Identity> identity;Ownership ownership{};};
inline std::mutex mutex;
inline std::vector<Stamp> stamps;
inline void Publish(U64 frame,U32 generation,std::optional<Identity> identity,Ownership ownership={}) {
    std::scoped_lock lock(mutex);
    if(!stamps.empty()&&(frame<stamps.back().frame||generation!=stamps.back().generation))stamps.clear();
    if(!stamps.empty()&&stamps.back().frame==frame)stamps.pop_back();
    if(stamps.size()==8)stamps.erase(stamps.begin());
    stamps.push_back({frame,generation,identity,ownership});
}
inline std::optional<Stamp> CurrentStamp(U64 frame,U32 generation) {
    std::scoped_lock lock(mutex);
    for(auto it=stamps.rbegin();it!=stamps.rend();++it)
        if(it->generation==generation&&(it->frame==frame||it->frame+1==frame))return *it;
    return {};
}
inline std::optional<Identity> Current(U64 frame,U32 generation) {
    const auto stamp=CurrentStamp(frame,generation);
    return stamp?stamp->identity:std::nullopt;
}
// Recorded dungeon floor and Radar draws share this fixed screen projection
// and material. Panning lives in vertex arrays, not layout-shader transforms.
inline bool ScreenDraw(U64 shader,U64 swizzle,unsigned vertices,
        const CameraMidpoint::Matrix& projection,const CameraMidpoint::Matrix& model,
        const GlareInterpolation::Tev& tev) {
    if(shader!=0x89b1ab98da3d9241ULL||swizzle!=0x6f0b398b1fc186a9ULL||
       !vertices||vertices>4096||vertices%6)return false;
    CameraMidpoint::Matrix p{};
    p[0]={0,-1./120,0,1};p[1]={-1./200,0,0,1};
    p[2]={0,0,1./1800,1./3};p[3][3]=1;
    auto m=CameraMidpoint::Identity();m[2][3]=-1200;
    if(!CameraMidpoint::Near(projection,p,1e-6)||!CameraMidpoint::Near(model,m,1e-6))return false;
    const GlareInterpolation::Tev expected{{{238030384,0,65537,0,0},
        {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0}}};
    return tev==expected;
}
using Vertex=GlareInterpolation::ScreenVertex;
using Vertices=std::vector<Vertex>;
struct Key {
    Identity identity{};
    U32 texture{},framebuffer{},generation{};
    auto operator<=>(const Key&) const = default;
};
enum class Result : unsigned {
    NotAttempted, InvalidBatch, Nonfinite, HomogeneousW, Capacity,
    NoPreviousKey, Ambiguous, VertexCount, Teleport, Depth, UV, NonuniformPan, Accepted
};
inline bool Degenerate(const Vertices& v,unsigned i) {
    return v[i].position==v[i+1].position||v[i].position==v[i+2].position||
        v[i+1].position==v[i+2].position;
}
// Radar's native depth sort can swap icons inside one batch (captured on Great
// Bay 1F). Identify a visible triangle by its atlas cell, depth and rigid shape,
// not its ordinal. Preserve the current order/depth; only interpolate XY.
inline std::optional<Vertices> ReorderedPan(const Vertices& prior,const Vertices& current) {
    using Signature=std::array<float,9>;
    const auto signature=[](const Vertices& v,unsigned i) {
        Signature key{};
        for(unsigned j=0;j<3;++j) {
            key[j*3]=v[i+j].position[2];key[j*3+1]=v[i+j].uv[0];key[j*3+2]=v[i+j].uv[1];
        }
        return key;
    };
    std::map<Signature,std::vector<unsigned>> index;
    unsigned old_visible=0;
    for(unsigned i=0;i<prior.size();i+=3)if(!Degenerate(prior,i)) {
        index[signature(prior,i)].push_back(i);++old_visible;
    }
    struct Match {unsigned at;std::vector<unsigned> candidates;std::optional<unsigned> old;};
    std::vector<Match> matches;
    for(unsigned i=0;i<current.size();i+=3) {
        if(Degenerate(current,i))continue;
        const auto found=index.find(signature(current,i));if(found==index.end())return {};
        Match match{i,{},std::nullopt};
        for(unsigned old:found->second) {
            const float dx=current[i].position[0]-prior[old].position[0],
                        dy=current[i].position[1]-prior[old].position[1];
            if(std::abs(dx)>32.f/120||std::abs(dy)>32.f/200)continue;
            bool rigid=true;
            for(unsigned j=1;j<3;++j)
                rigid &= std::abs(current[i+j].position[0]-prior[old+j].position[0]-dx)<=.0002f&&
                         std::abs(current[i+j].position[1]-prior[old+j].position[1]-dy)<=.0002f;
            if(rigid)match.candidates.push_back(old);
        }
        if(match.candidates.empty())return {};
        matches.push_back(std::move(match));
    }
    if(matches.size()!=old_visible)return {};
    // Resolve only forced one-to-one matches. Identical nearby markers may
    // initially have two candidates; a uniquely owned neighbor can resolve it.
    // Never choose the nearest arbitrarily or reuse an old triangle twice.
    std::vector<bool> used(prior.size()/3);
    unsigned remaining=matches.size();
    while(remaining) {
        bool progress=false;
        for(auto& match:matches)if(!match.old) {
            std::erase_if(match.candidates,[&](unsigned old){return used[old/3];});
            if(match.candidates.empty())return {};
            if(match.candidates.size()!=1)continue;
            match.old=match.candidates[0];used[*match.old/3]=true;--remaining;progress=true;
        }
        if(!progress)return {};
    }
    auto half=current;
    for(const auto& match:matches)for(unsigned j=0;j<3;++j)for(unsigned c=0;c<2;++c)
        half[match.at+j].position[c]=(prior[*match.old+j].position[c]+current[match.at+j].position[c])*.5f;
    return half;
}
class History {
    struct Entry {Vertices vertices;bool ambiguous=false;};
    std::map<Key,Entry> previous,current;
    std::optional<U64> frame;
public:
    Result result=Result::NotAttempted;
    unsigned mismatch=0;
    void Reset(){previous.clear();current.clear();frame.reset();}
    std::optional<Vertices> Apply(U64 next,const Key& key,const Vertices& vertices) {
        result=Result::NotAttempted;mismatch=0;
        const auto reject=[&](Result why)->std::optional<Vertices>{result=why;return {};};
        if(!frame||next!=*frame) {
            if(frame&&next==*frame+1)previous=std::move(current);else previous.clear();
            current.clear();frame=next;
        }
        if(vertices.empty()||vertices.size()>4096||vertices.size()%6||!key.identity[0])return reject(Result::InvalidBatch);
        for(const auto& v:vertices) {
            for(float x:v.position)if(!std::isfinite(x))return reject(Result::Nonfinite);
            for(float x:v.uv)if(!std::isfinite(x))return reject(Result::Nonfinite);
            if(v.position[3]!=1)return reject(Result::HomogeneousW);
        }
        if(current.size()>=8)return reject(Result::Capacity);
        auto [stored,added]=current.emplace(key,Entry{vertices});
        if(!added&&stored->second.vertices!=vertices)stored->second.ambiguous=true;
        const auto old=previous.find(key);
        if(old==previous.end())return reject(Result::NoPreviousKey);
        if(old->second.ambiguous||stored->second.ambiguous)return reject(Result::Ambiguous);
        if(old->second.vertices.size()!=vertices.size())return reject(Result::VertexCount);
        const auto& prior=old->second.vertices;
        for(unsigned i=0;i<vertices.size();++i) {
            const auto& a=prior[i];const auto& b=vertices[i];
            mismatch=i;
            if(a.position[2]!=b.position[2]||a.uv!=b.uv) {
                if(auto reordered=ReorderedPan(prior,vertices)) {
                    result=Result::Accepted;mismatch=0;return reordered;
                }
                return reject(a.position[2]!=b.position[2]?Result::Depth:Result::UV);
            }
        }
        auto half=vertices;
        // The shaded triangle list contains strip connectors and a stationary
        // sentinel vertex. These invisible triangles do not share the pan.
        // Icons also round their positions independently to native pixels.
        // Validate translation per visible triangle, keeping every connector
        // untouched so it cannot acquire area on the intermediate frame.
        for(unsigned i=0;i<vertices.size();i+=3) {
            mismatch=i;
            const bool was_hidden=Degenerate(prior,i),hidden=Degenerate(vertices,i);
            if(was_hidden&&hidden)continue;
            if(was_hidden!=hidden)return reject(Result::NonuniformPan);
            const float dx=vertices[i].position[0]-prior[i].position[0],
                        dy=vertices[i].position[1]-prior[i].position[1];
            if(std::abs(dx)>32.f/120||std::abs(dy)>32.f/200)return reject(Result::Teleport);
            for(unsigned j=i;j<i+3;++j) {
                mismatch=j;
                if(std::abs(vertices[j].position[0]-prior[j].position[0]-dx)>.0002f||
                   std::abs(vertices[j].position[1]-prior[j].position[1]-dy)>.0002f)
                    return reject(Result::NonuniformPan);
                for(unsigned c=0;c<2;++c)
                    half[j].position[c]=(prior[j].position[c]+vertices[j].position[c])*.5f;
            }
        }
        result=Result::Accepted;mismatch=0;
        return half;
    }
};
}
