// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include "mm3d/water_particle_interpolation.h"

namespace MM3D::BubbleInterpolation {
using ForestParticleInterpolation::Vertex;
using ForestParticleInterpolation::Quad;
using ForestParticleInterpolation::Near;
using ForestParticleInterpolation::Center;
using Key = std::array<std::uint32_t, 2>; // active texture, scene generation
using Tev = std::array<std::array<std::uint32_t,5>,6>;
// Dense swimming trails reach 100 bubbles in one draw. Keep the same bounded
// 128-quad capacity as the CPU strip decoder and the per-frame history pool.
inline constexpr unsigned MaxQuads=128;
inline bool Material(Tev tev) {
    // Scene lighting changes the second stage's constant color. Operations,
    // modifiers and the remaining stages must still match the bubble shader.
    tev[1][3]=0;
    constexpr Tev expected{{{249564899,0,0,0,2},{250482430,0,1,0,0},
        {249564976,0,8,0,0},{268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0}}};
    return tev==expected;
}

// Wart's burst uses the same two-frame bubble geometry with a cyan tint and
// fading vertex alpha, but a different combiner from underwater bubbles.
inline bool PopMaterial(const Tev& tev) {
    constexpr Tev expected{{{238030384,32,65544,6579455,0},
        {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0}}};
    return tev==expected;
}

inline float Size(const Quad& q) {
    float length=0;
    for(unsigned c=0;c<3;++c)length+=std::pow(q.vertices[1][0][c]-q.vertices[0][0][c],2);
    return std::sqrt(length);
}
inline std::optional<std::vector<Quad>> Decode(std::span<const Vertex> inputs,
                                              std::span<const std::uint32_t> order,bool popping=false) {
    if(order.size()>6*MaxQuads)return {};
    const auto quads=WaterParticleInterpolation::DecodeQuads(inputs,order);
    if(!quads)return {};
    for(const auto& q:*quads) {
        const float u=q.vertices[0][2][0];
        if(!Near(u,0) && !Near(u,.5f))return {};
        float aa=0,bb=0,ab=0;
        for(unsigned c=0;c<3;++c) {
            const float a=q.vertices[1][0][c]-q.vertices[0][0][c];
            const float b=q.vertices[2][0][c]-q.vertices[0][0][c];
            aa+=a*a;bb+=b*b;ab+=a*b;
        }
        // Captured underwater bubbles are small lit squares using either half
        // of a two-frame texture. Keep sprite selection native; blend geometry.
        if(aa<.25f || aa>1024 || std::abs(aa-bb)>.02f || std::abs(ab)>.02f)return {};
        for(unsigned k=0;k<4;++k) {
            const auto& v=q.vertices[k];
            if((popping ? (v[1][3]<0 || v[1][3]>1 || !Near(v[1][0],0) ||
                !Near(v[1][1],155.f/255) || !Near(v[1][2],155.f/255)) : !Near(v[1][3],1)) ||
               !Near(v[2][0],u+(k/2)*.5f) ||
               !Near(v[2][1],float(1-k%2)))return {};
            for(unsigned c=0;c<3;++c)if(v[1][c]<0 || v[1][c]>1)return {};
        }
    }
    return quads;
}
inline bool Same(const Quad& a, const Quad& b) {
    for (unsigned v = 0; v < 4; ++v)
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 4; ++c)
                if (!Near(a.vertices[v][r][c], b.vertices[v][r][c])) return false;
    return true;
}
inline bool SwimmingBatch(const std::vector<Quad>& quads) {
    return !quads.empty() && std::all_of(quads.begin(),quads.end(),[](const Quad& q) {
        const float size=Size(q);return size>=2.07f && size<=3.05f;
    });
}
inline bool Compatible(const Quad& current,const Quad& previous,bool swimming=false) {
    const auto a=Center(current),b=Center(previous);
    float distance=0;for(unsigned c=0;c<3;++c)distance+=(a[c]-b[c])*(a[c]-b[c]);
    // Swimming trail bubbles retain their random size while moving about 12
    // world units per native frame. Require that stable size to avoid matching
    // a newly spawned bubble to an unrelated nearby particle. Ordinary growing
    // bubbles retain their original, shorter movement limit.
    if(distance>(swimming?256:64) ||
       std::abs(Size(current)-Size(previous))>(swimming?.005f:1.f))return false;
    for(unsigned c=0;c<3;++c)
        if(std::abs(current.vertices[0][1][c]-previous.vertices[0][1][c])>.05f)return false;
    return true;
}
inline std::optional<unsigned> Match(const Quad& q, const std::vector<Quad>& candidates, bool newer, bool swimming=false) {
    float best = std::numeric_limits<float>::infinity(), second = best;
    std::optional<unsigned> match;
    const auto center = Center(q);
    for (unsigned i = 0; i < candidates.size(); ++i) {
        const auto& other = candidates[i];
        if (!(newer ? BubbleInterpolation::Compatible(q, other, swimming) : BubbleInterpolation::Compatible(other, q, swimming))) continue;
        const auto oc = Center(other);
        float distance = 0;
        for (unsigned c = 0; c < 3; ++c) distance += (center[c]-oc[c])*(center[c]-oc[c]);
        distance += 16*std::pow(Size(q)-Size(other),2);
        if (distance < best) { second = best;best = distance;match = i; }
        else if (distance < second) second = distance;
    }
    return second > best*1.5f+.01f ? match : std::nullopt;
}
class History {
    struct Entry {
        std::uint64_t frame;
        std::vector<Quad> previous, current;
        bool overflow{}, swimming{};
    };
    std::map<Key, Entry> entries;
public:
    void Reset() { entries.clear(); }
    std::optional<std::vector<Quad>> Apply(std::uint64_t frame, const Key& key,
                                         const std::vector<Quad>& quads,bool swimming=false) {
        if (quads.empty() || quads.size() > MaxQuads) return {};
        for (auto it = entries.begin(); it != entries.end();)
            if (it->second.frame > frame || frame-it->second.frame > 1) it = entries.erase(it); else ++it;
        auto it = entries.find(key);
        if (it == entries.end()) {
            if (entries.size() >= 8) return {};
            it = entries.emplace(key, Entry{frame, {}, {}}).first;
        }
        auto& e = it->second;
        if(e.swimming!=swimming) {
            e.previous.clear();e.current.clear();e.overflow=false;e.swimming=swimming;
        }
        if (e.frame != frame) {
            e.previous = std::move(e.current);e.current.clear();e.frame = frame;e.overflow = false;
        }
        if (e.overflow) return {};
        // A bubble can occur in overlapping submissions in one game frame.
        // Both use the same immutable previous-frame pool. Deduplicate exact
        // copies for the next frame, not their draw calls or native rendering.
        for (const auto& q : quads) {
            if (std::any_of(e.current.begin(), e.current.end(), [&](const auto& p) { return Same(q,p); })) continue;
            if (e.current.size() >= MaxQuads) {
                e.previous.clear();e.current.clear();e.overflow = true;return {};
            }
            e.current.push_back(q);
        }
        auto out = quads;
        bool changed = false;
        for (unsigned i = 0; i < quads.size(); ++i) {
            const auto match = Match(quads[i], e.previous, true, swimming);
            if (!match || Match(e.previous[*match], quads, false, swimming) != i) continue;
            for (unsigned v = 0; v < 4; ++v)
                for (unsigned a = 0; a < 2; ++a)
                    for (unsigned c = 0; c < 4; ++c)
                        out[i].vertices[v][a][c] = (quads[i].vertices[v][a][c]+e.previous[*match].vertices[v][a][c])*.5f;
            ForestParticleInterpolation::PreserveSpinSize(out[i], quads[i], e.previous[*match]);
            changed = true;
        }
        return changed ? std::optional{std::move(out)} : std::nullopt;
    }
};
}
