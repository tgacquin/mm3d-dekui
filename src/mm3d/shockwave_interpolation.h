// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include "mm3d/water_particle_interpolation.h"

namespace MM3D::ShockwaveInterpolation {
using ForestParticleInterpolation::Vertex;
using ForestParticleInterpolation::Quad;
using ForestParticleInterpolation::Near;
using ForestParticleInterpolation::Center;
using Key = std::array<std::uint32_t, 2>; // active texture, scene generation

inline std::optional<std::vector<Quad>> Decode(std::span<const Vertex> inputs,
                                              std::span<const std::uint32_t> order) {
    if (order.size() > 96) return {};
    const auto decoded = WaterParticleInterpolation::DecodeQuads(inputs, order);
    if (!decoded) return {};
    for (const auto& q : *decoded) {
        const auto& color = q.vertices[0][1];
        const bool pale = Near(color[0], 1) && Near(color[1], 1) &&
                          (Near(color[2], 1) || Near(color[2], 200.f/255));
        // Skulltula landing rings use a fixed brown tint instead of the pale
        // explosion tint. Keep this variant restricted to small ground quads.
        const bool landing = Near(color[0], .135332525f) &&
                             Near(color[1], .118415952f) &&
                             Near(color[2], .118415952f);
        if (!pale && !landing) return {};
        float aa=0,bb=0,ab=0;
        for(unsigned c=0;c<3;++c) {
            const float a=q.vertices[1][0][c]-q.vertices[0][0][c];
            const float b=q.vertices[2][0][c]-q.vertices[0][0][c];
            aa+=a*a;bb+=b*b;ab+=a*b;
        }
        // Large pale explosion rings, including the collapsed spawn frame.
        // The ground ring is slightly elliptical; the upright ring is square.
        if(aa>2048*2048 || bb>2048*2048 ||
           ((aa>.001f || bb>.001f) && (aa<16*16 || bb<16*16 ||
            aa/bb<.8f || aa/bb>1.25f || std::abs(ab)>.01f*std::sqrt(aa*bb))))return {};
        if (landing) {
            if (aa > 256*256 || bb > 256*256) return {};
            for (const auto& vertex : q.vertices)
                if (!Near(vertex[0][1], q.vertices[0][0][1])) return {};
        }
        for(unsigned k=0;k<4;++k)
            if(!Near(q.vertices[k][2][0], k<2?0.f:2.f) ||
               !Near(q.vertices[k][2][1], k%2?0.f:2.f))return {};
    }
    return decoded;
}
inline bool Same(const Quad& a, const Quad& b) {
    for (unsigned v = 0; v < 4; ++v)
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 4; ++c)
                if (!Near(a.vertices[v][r][c], b.vertices[v][r][c])) return false;
    return true;
}
inline bool Compatible(const Quad& current,const Quad& previous) {
    for(unsigned c=0;c<3;++c)
        if(!Near(current.vertices[0][1][c],previous.vertices[0][1][c]))return false;
    const float fade=previous.vertices[0][1][3]-current.vertices[0][1][3];
    if(fade<-.001f || fade>.15f)return false;
    const auto ac=Center(current),bc=Center(previous);
    float distance=0;for(unsigned c=0;c<3;++c)distance+=(ac[c]-bc[c])*(ac[c]-bc[c]);
    if(distance>.04f)return false; // rings remain centered on the explosion
    for(unsigned k:{1u,2u}) {
        float aa=0,bb=0,ab=0;
        for(unsigned c=0;c<3;++c) {
            const float a=current.vertices[k][0][c]-current.vertices[0][0][c];
            const float b=previous.vertices[k][0][c]-previous.vertices[0][0][c];
            aa+=a*a;bb+=b*b;ab+=a*b;
        }
        const float growth=std::sqrt(aa)-std::sqrt(bb);
        if(growth<-.01f || growth>128 || (bb>1 && ab<.8f*std::sqrt(aa*bb)))return false;
    }
    return true;
}
inline std::optional<unsigned> Match(const Quad& q, const std::vector<Quad>& candidates, bool newer) {
    float best = std::numeric_limits<float>::infinity(), second = best;
    std::optional<unsigned> match;
    const auto center = Center(q);
    for (unsigned i = 0; i < candidates.size(); ++i) {
        const auto& other = candidates[i];
        if (!(newer ? ShockwaveInterpolation::Compatible(q, other) : ShockwaveInterpolation::Compatible(other, q))) continue;
        const auto oc = Center(other);
        float distance = 0;
        for (unsigned c = 0; c < 3; ++c) distance += (center[c]-oc[c])*(center[c]-oc[c]);
        for(unsigned v=0;v<4;++v) for(unsigned c=0;c<3;++c)
            distance+=.01f*std::pow(q.vertices[v][0][c]-other.vertices[v][0][c],2);
        distance+=1024*std::pow(q.vertices[0][1][3]-other.vertices[0][1][3],2);
        if (distance < best) { second = best;best = distance;match = i; }
        else if (distance < second) second = distance;
    }
    return second > best*1.5f+.01f ? match : std::nullopt;
}
class History {
    struct Entry {
        std::uint64_t frame;
        std::vector<Quad> previous, current;
        bool overflow{};
    };
    std::map<Key, Entry> entries;
public:
    void Reset() { entries.clear(); }
    std::optional<std::vector<Quad>> Apply(std::uint64_t frame, const Key& key,
                                         const std::vector<Quad>& quads) {
        if (quads.empty() || quads.size() > 64) return {};
        for (auto it = entries.begin(); it != entries.end();)
            if (it->second.frame > frame || frame-it->second.frame > 1) it = entries.erase(it); else ++it;
        auto it = entries.find(key);
        if (it == entries.end()) {
            if (entries.size() >= 8) return {};
            it = entries.emplace(key, Entry{frame, {}, {}}).first;
        }
        auto& e = it->second;
        if (e.frame != frame) {
            e.previous = std::move(e.current);e.current.clear();e.frame = frame;e.overflow = false;
        }
        if (e.overflow) return {};
        // A ring can occur in overlapping submissions in one game frame.
        // Both use the same immutable previous-frame pool. Deduplicate exact
        // copies for the next frame, not their draw calls or native rendering.
        for (const auto& q : quads) {
            if (std::any_of(e.current.begin(), e.current.end(), [&](const auto& p) { return Same(q,p); })) continue;
            if (e.current.size() >= 128) {
                e.previous.clear();e.current.clear();e.overflow = true;return {};
            }
            e.current.push_back(q);
        }
        auto out = quads;
        bool changed = false;
        for (unsigned i = 0; i < quads.size(); ++i) {
            const auto match = Match(quads[i], e.previous, true);
            if (!match || Match(e.previous[*match], quads, false) != i) continue;
            for (unsigned v = 0; v < 4; ++v)
                for (unsigned a = 0; a < 2; ++a)
                    for (unsigned c = 0; c < 4; ++c)
                        out[i].vertices[v][a][c] = (quads[i].vertices[v][a][c]+e.previous[*match].vertices[v][a][c])*.5f;
            changed = true;
        }
        return changed ? std::optional{std::move(out)} : std::nullopt;
    }
};
}
