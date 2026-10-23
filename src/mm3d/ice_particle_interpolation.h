// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/bubble_interpolation.h"

namespace MM3D::IceParticleInterpolation {
using ForestParticleInterpolation::Vertex;
using ForestParticleInterpolation::Quad;
using ForestParticleInterpolation::Center;
using ForestParticleInterpolation::Near;
using Tev = std::array<std::array<std::uint32_t, 5>, 6>;
enum class Kind : unsigned { None, Breath, Pop, FreezeMist, FreezeShard };
inline Kind Material(const Tev& tev) {
    constexpr Tev breath{{{249564723,0,2,0,2}, {250482430,0,1,7356446,0},
        {249564979,0,2,0,1}, {249564656,0,8,0,0},
        {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0}}};
    constexpr Tev pop{{{238030384,32,65544,9868950,0},
        {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0}}};
    constexpr Tev mist{{{249564899,0,0,0,2}, {250482430,0,1,9211040,0},
        {249564976,0,8,0,0}, {268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0}}};
    constexpr Tev shard{{{238030384,0,65537,0,0},
        {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0}}};
    return tev == breath ? Kind::Breath : tev == pop ? Kind::Pop :
        tev == mist ? Kind::FreezeMist : tev == shard ? Kind::FreezeShard : Kind::None;
}
inline bool Atlas(Kind kind) { return kind==Kind::Breath || kind==Kind::FreezeMist; }
inline float Size(const Quad& q) { return BubbleInterpolation::Size(q); }
inline int Tile(const Quad& q) {
    return int(std::lround(q.vertices[0][2][0]*4)) +
        4*int(std::lround((1-q.vertices[0][2][1])*4));
}
inline std::optional<std::vector<Quad>> Decode(std::span<const Vertex> inputs,
        std::span<const std::uint32_t> order, Kind kind) {
    if(kind == Kind::None)return {};
    auto quads = WaterParticleInterpolation::DecodeQuads(inputs,order);
    if(!quads || quads->empty())return {};
    for(const auto& q:*quads) {
        const float size=Size(q);
        if(kind==Kind::FreezeMist) { if(std::abs(size-78.08f)>.01f)return {}; }
        else if(kind==Kind::FreezeShard) { if(size<3.99f || size>7.61f)return {}; }
        else if(size < (kind==Kind::Breath?24.f:8.f) || size > (kind==Kind::Breath?128.f:16.f))return {};
        float width=0,dot=0;
        for(unsigned c=0;c<3;++c) {
            const float a=q.vertices[1][0][c]-q.vertices[0][0][c];
            const float b=q.vertices[2][0][c]-q.vertices[0][0][c];
            width+=b*b;dot+=a*b;
        }
        if(std::abs(std::sqrt(width)-size)>.01f || std::abs(dot)>.001f*size*size)return {};
        const auto uv=q.vertices[0][2];
        if(Atlas(kind)) {
            if(uv[0]<0 || uv[0]>.75f || uv[1]<.25f || uv[1]>1 ||
               !Near(uv[0]*4,std::round(uv[0]*4)) || !Near(uv[1]*4,std::round(uv[1]*4)))return {};
        } else if((!Near(uv[0],0) && !(kind==Kind::Pop && Near(uv[0],.5f))) || !Near(uv[1],1))return {};
        const std::array<float,3> color=kind==Kind::Breath?
            std::array<float,3>{120.f/255,96.f/255,48.f/255}:
            kind==Kind::FreezeMist?std::array<float,3>{-.1476354897f,-.0430603027f,-.0430603027f}:
            kind==Kind::FreezeShard?std::array<float,3>{1,1,1}:
            std::array<float,3>{105.f/255,105.f/255,105.f/255};
        // The mist uses signed color offsets in its native combiner. Preserve
        // those values; white square shards share a material with rectangular
        // bomb debris, which the strict shape/size checks exclude here.
        if(kind==Kind::FreezeShard && !Near(q.vertices[0][1][3],1))return {};
        for(unsigned k=0;k<4;++k) {
            const auto& v=q.vertices[k];
            for(unsigned c=0;c<3;++c)if(!Near(v[1][c],color[c]))return {};
            if(!Near(v[2][0],uv[0]+(k/2)*(Atlas(kind)?.25f:kind==Kind::Pop?.5f:1.f)) ||
               !Near(v[2][1],uv[1]-(k%2)*(Atlas(kind)?.25f:1.f)))return {};
        }
    }
    return quads;
}
// Each captured puff/fragment keeps its size. Atlas progression and opacity
// distinguish births from older particles even when submission order changes.
inline bool Compatible(const Quad& a,const Quad& b,Kind kind) {
    if(std::abs(Size(a)-Size(b))>.01f)return false;
    const float alpha=a.vertices[0][1][3]-b.vertices[0][1][3];
    if(std::abs(alpha)>.03f || (kind==Kind::Pop && alpha>.001f))return false;
    if(Atlas(kind)) {
        const int step=(Tile(a)-Tile(b)+16)%16;
        if(step!=0 && step!=1)return false;
    } else if(!Near(a.vertices[0][2][0],b.vertices[0][2][0]))return false;
    const auto ac=Center(a),bc=Center(b);float distance=0;
    for(unsigned c=0;c<3;++c)distance+=(ac[c]-bc[c])*(ac[c]-bc[c]);
    return distance<=(kind==Kind::Breath?1024.f:144.f);
}
inline std::optional<unsigned> Match(const Quad& q,const std::vector<Quad>& candidates,
        Kind kind,bool newer) {
    std::optional<unsigned> match;float best=std::numeric_limits<float>::infinity(),second=best;
    const auto center=Center(q);
    for(unsigned i=0;i<candidates.size();++i) {
        if(!(newer?Compatible(q,candidates[i],kind):Compatible(candidates[i],q,kind)))continue;
        const auto c=Center(candidates[i]);float distance=0;
        for(unsigned k=0;k<3;++k)distance+=(c[k]-center[k])*(c[k]-center[k]);
        if(distance<best){second=best;best=distance;match=i;}else if(distance<second)second=distance;
    }
    return second>best*1.5f+.01f?match:std::nullopt;
}
class History {
    using Key=std::array<std::uint32_t,3>; // active texture, generation, kind
    struct Entry {std::uint64_t frame;std::vector<Quad> previous,current;bool overflow{};};
    std::map<Key,Entry> entries;
public:
    void Reset(){entries.clear();}
    std::optional<std::vector<Quad>> Apply(std::uint64_t frame,std::uint32_t texture,
            std::uint32_t generation,Kind kind,const std::vector<Quad>& quads,
            std::vector<Quad>* previous_tiles=nullptr) {
        if(previous_tiles)previous_tiles->clear();
        if(kind==Kind::None || quads.empty() || quads.size()>128)return {};
        for(auto it=entries.begin();it!=entries.end();)
            if(it->second.frame>frame || frame-it->second.frame>1)it=entries.erase(it);else ++it;
        const Key key{texture,generation,static_cast<unsigned>(kind)};
        auto it=entries.find(key);
        if(it==entries.end()) {
            if(entries.size()>=8)return {};
            it=entries.emplace(key,Entry{frame,{},{}}).first;
        }
        auto& e=it->second;
        if(e.frame!=frame){e.previous=std::move(e.current);e.current.clear();e.frame=frame;e.overflow=false;}
        if(e.overflow)return {};
        // Several ice enemies submit this same material each frame. Keep their
        // shared previous pool immutable and collect all native batches.
        for(const auto& q:quads) {
            if(std::any_of(e.current.begin(),e.current.end(),[&](const auto& p){return BubbleInterpolation::Same(q,p);}))continue;
            if(e.current.size()>=256){e.previous.clear();e.current.clear();e.overflow=true;return {};}
            e.current.push_back(q);
        }
        auto out=quads;bool changed=false;
        for(unsigned i=0;i<quads.size();++i) {
            const auto match=Match(quads[i],e.previous,kind,true);
            if(!match)continue;
            const auto reverse=Match(e.previous[*match],e.current,kind,false);
            if(!reverse || !BubbleInterpolation::Same(e.current[*reverse],quads[i]))continue;
            const auto& old=e.previous[*match];
            for(unsigned v=0;v<4;++v) {
                for(unsigned c=0;c<3;++c)out[i].vertices[v][0][c]=(quads[i].vertices[v][0][c]+old.vertices[v][0][c])*.5f;
                out[i].vertices[v][1][3]=(quads[i].vertices[v][1][3]+old.vertices[v][1][3])*.5f;
            }
            ForestParticleInterpolation::PreserveSpinSize(out[i],quads[i],old);
            // Breath uses additive SourceAlpha/One blending and passes the
            // vertex alpha through its verified combiner. Two half-weight
            // samples blend the atlas images without sliding UVs across tiles.
            // Other particle materials retain their native atlas behavior.
            if(previous_tiles && kind==Kind::Breath && Tile(quads[i])!=Tile(old)) {
                if(previous_tiles->empty()) {
                    *previous_tiles=quads;
                    for(auto& q:*previous_tiles)for(auto& v:q.vertices)v[1][3]=0;
                }
                out[i].vertices[0][1][3]*=.5f;
                for(unsigned v=1;v<4;++v)out[i].vertices[v][1][3]=out[i].vertices[0][1][3];
                (*previous_tiles)[i]=out[i];
                for(unsigned v=0;v<4;++v)(*previous_tiles)[i].vertices[v][2]=old.vertices[v][2];
            }
            changed=true;
        }
        return changed?std::optional{std::move(out)}:std::nullopt;
    }
};
}
