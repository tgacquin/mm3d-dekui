// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include "mm3d/water_particle_interpolation.h"

namespace MM3D::BurstParticleInterpolation {
using Vertex = std::array<std::array<float, 4>, 5>; // position, color, up to three UVs
using ForestParticleInterpolation::Near;
enum class Kind { Dust, Blood, Flame, BombDebris, BlueBlood };
inline bool IsBlood(Kind kind) { return kind == Kind::Blood || kind == Kind::BlueBlood; }
inline bool BombMaterial(const std::array<std::array<std::uint32_t,5>,6>& tev) {
    for(unsigned i=0;i<6;++i)
        if(tev[i]!=std::array<std::uint32_t,5>{i?268374015u:238030384u,0,i?0u:65537u,i?0xffffffffu:0u,0})return false;
    return true;
}
struct Quad {
    std::array<Vertex, 4> vertices;
    std::array<std::uint32_t, 4> indices;
    Kind kind;
};
using Key = std::array<std::uint32_t, 5>; // textures, generation, kind
inline std::array<float, 3> Center(const Quad& q) {
    std::array<float, 3> out{};
    for (const auto& v : q.vertices)
        for (unsigned c = 0; c < 3; ++c) out[c] += v[0][c] * .25f;
    return out;
}
inline float Edge(const Quad& q, unsigned edge = 1) {
    float sum = 0;
    for (unsigned c = 0; c < 3; ++c) {
        const float d = q.vertices[edge][0][c] - q.vertices[0][0][c];
        sum += d*d;
    }
    return std::sqrt(sum);
}
inline int Tile(const Quad& q) {
    const auto& uv = q.vertices[0][2];
    return int(std::lround(uv[0]*4)) + 4*int(std::lround((1-uv[1])*4));
}
inline std::optional<std::vector<Quad>> Decode(std::span<const Vertex> inputs,
                                              std::span<const std::uint32_t> order,
                                              unsigned attributes, bool bomb_material = false) {
    if ((attributes != 3 && attributes != 5) || inputs.empty() || inputs.size() > 513) return {};
    std::vector<ForestParticleInterpolation::Vertex> base(inputs.size());
    for (unsigned i = 0; i < inputs.size(); ++i)
        for (unsigned a = 0; a < 3; ++a) base[i][a] = inputs[i][a];
    const auto decoded = WaterParticleInterpolation::DecodeQuads(base, order, false);
    if (!decoded || decoded->empty()) return {};
    std::vector<Quad> out;
    for (const auto& source : *decoded) {
        Quad q{};q.indices = source.indices;
        for (unsigned k = 0; k < 4; ++k) q.vertices[k] = inputs[q.indices[k]];
        const auto& color = q.vertices[0][1];
        if (attributes == 5) q.kind = Kind::Flame;
        else if(bomb_material && color==std::array<float,4>{1,1,1,1}) q.kind=Kind::BombDebris;
        else if (Near(color[0], 70.f/255) && Near(color[1], 70.f/255) && Near(color[2], 70.f/255))
            q.kind = Kind::Dust;
        else if (color[0] >= 0 && color[0] <= .04f && Near(color[0], color[2]) &&
                 color[1] >= .12f && color[1] <= .8f)
            q.kind = Kind::Blood;
        else if (bomb_material && color[0] >= 0 && color[0] <= .04f &&
                 Near(color[0], color[1]) && color[2] >= .12f && color[2] <= .8f)
            q.kind = Kind::BlueBlood;
        else return {};
        if (!out.empty() && out[0].kind != q.kind) return {};
        const float width = Edge(q, 2), height = Edge(q);
        float dot = 0;
        for (unsigned c = 0; c < 3; ++c)
            dot += (q.vertices[1][0][c]-q.vertices[0][0][c]) *
                   (q.vertices[2][0][c]-q.vertices[0][0][c]);
        if (std::abs(dot) > .001f*width*height) return {};
        if (q.kind == Kind::Flame) {
            // Captured Skulltula death flames: purple 32.4 x 48 quads with
            // two linked scrolling layers, the second at four times the rate.
            if (std::abs(width-32.4f) > .01f || std::abs(height-48) > .01f ||
                color[0] < .65f || color[0] > .91f || color[1] < .35f || color[1] > .71f ||
                !Near(color[2]-color[0], .1f)) return {};
        } else if(q.kind==Kind::BombDebris) {
            // Captured explosion fragments: rotating 7:6 rectangles whose
            // random size remains fixed throughout each particle's lifetime.
            if(height<1||height>12||std::abs(width-height*7.f/6)>.005f)return {};
        } else {
            if (std::abs(width-height) > .01f || height < (q.kind == Kind::Dust ? 4.f : 2.f) ||
                height > (q.kind == Kind::Dust ? 40.f : 6.1f)) return {};
        }
        const auto& first_uv = q.vertices[0][2];
        const float phase = q.vertices[0][3][1]-1;
        for (unsigned k = 0; k < 4; ++k) {
            const auto& v = q.vertices[k];
            for (unsigned a = 0; a < attributes; ++a)
                for (float f : v[a]) if (!std::isfinite(f)) return {};
            const float u = float(k/2), t = float(1-k%2);
            if (IsBlood(q.kind)) {
                // Green and blue blood share the captured corner gradient and
                // lifetime fade. Blue additionally requires the sprite material;
                // a blue tint alone must not admit unrelated particles.
                const float a = color[3];
                std::array<float, 4> expected{
                    k == 0 ? a*10.f/255 : 0,
                    k == 0 ? (32+168*a)/255 : k == 2 ? (32+96*a)/255 : (64+64*a)/255,
                    k == 0 ? a*10.f/255 : 0, a};
                if (q.kind == Kind::BlueBlood) std::swap(expected[1], expected[2]);
                for (unsigned c = 0; c < 4; ++c) if (!Near(v[1][c], expected[c])) return {};
            } else {
                for (unsigned c = 0; c < 4; ++c) if (!Near(v[1][c], color[c])) return {};
            }
            if (q.kind == Kind::Dust) {
                if (first_uv[0] < 0 || first_uv[0] > .75f || first_uv[1] < .25f || first_uv[1] > 1 ||
                    !Near(first_uv[0]*4, std::round(first_uv[0]*4)) ||
                    !Near(first_uv[1]*4, std::round(first_uv[1]*4)) ||
                    !Near(v[2][0], first_uv[0]+u*.25f) ||
                    !Near(v[2][1], first_uv[1]-(1-t)*.25f)) return {};
            } else if (!Near(v[2][0], u) || !Near(v[2][1], t)) return {};
            if (q.kind == Kind::Flame) {
                if (phase < -1.001f || phase > .001f ||
                    !Near(v[3][0], u) || !Near(v[3][1], t+phase) ||
                    !Near(v[4][0], -2*u) || !Near(v[4][1], 2*t+4*phase)) return {};
                for (unsigned a : {3u, 4u})
                    if (!Near(v[a][2], 0) || !Near(v[a][3], 1)) return {};
            }
        }
        out.push_back(q);
    }
    return out;
}
// a is newer than b. Spawn/reset discontinuities never inherit old motion.
inline bool Compatible(const Quad& a, const Quad& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == Kind::BlueBlood && a.vertices[0][1][3] > b.vertices[0][1][3] + .001f)
        return false;
    if(a.kind==Kind::BombDebris && (std::abs(Edge(a)-Edge(b))>.005f ||
       std::abs(Edge(a,2)-Edge(b,2))>.005f))return false;
    const auto ac = Center(a), bc = Center(b);
    float distance = 0;
    for (unsigned c = 0; c < 3; ++c) distance += (ac[c]-bc[c])*(ac[c]-bc[c]);
    if (distance > (IsBlood(a.kind) || a.kind == Kind::BombDebris ? 256.f : a.kind == Kind::Flame ? 144.f : 100.f)) return false;
    for (unsigned c = 0; c < 4; ++c)
        if (std::abs(a.vertices[0][1][c]-b.vertices[0][1][c]) >
            (a.kind == Kind::Dust ? .26f : IsBlood(a.kind) ? .05f : .15f)) return false;
    if (a.kind == Kind::Dust) {
        const int step = Tile(a)-Tile(b);
        if (step != 0 && step != 1) return false;
        const float growth = Edge(a)-Edge(b);
        if (growth < -.01f || growth > 2.f) return false;
    }
    if (a.kind == Kind::Flame) {
        float delta = a.vertices[0][3][1]-b.vertices[0][3][1];
        delta -= std::round(delta);
        if (!Near(delta, 0) && !Near(delta, -1.f/60)) return false;
    }
    return true;
}
inline std::optional<unsigned> Match(const Quad& q, const std::vector<Quad>& candidates, bool newer) {
    float best = std::numeric_limits<float>::infinity(), second = best;
    std::optional<unsigned> match;
    const auto center = Center(q);
    for (unsigned i = 0; i < candidates.size(); ++i) {
        const auto& other = candidates[i];
        if (!(newer ? Compatible(q, other) : Compatible(other, q))) continue;
        const auto oc = Center(other);
        float distance = 0;
        for (unsigned c = 0; c < 3; ++c) distance += (center[c]-oc[c])*(center[c]-oc[c]);
        if (distance < best) { second = best;best = distance;match = i; }
        else if (distance < second) second = distance;
    }
    return second > best*1.5f+.01f ? match : std::nullopt;
}
inline std::optional<std::vector<unsigned>> BloodMatches(const std::vector<Quad>& current,
                                                        const std::vector<Quad>& previous) {
    // The captured 27-drop burst expands uniformly while gravity translates
    // its center. Recover that shared motion before matching close neighbors.
    // Validate every drop independently; never assume submission order is ID.
    if (current.size() != 27 || previous.size() != 27 ||
        !IsBlood(current[0].kind) || previous[0].kind != current[0].kind) return {};
    std::array<double, 3> ac{}, bc{};
    for (unsigned i = 0; i < 27; ++i) {
        const auto a = Center(current[i]), b = Center(previous[i]);
        for (unsigned c = 0; c < 3; ++c) { ac[c] += a[c]/27.;bc[c] += b[c]/27.; }
    }
    double aa = 0, bb = 0;
    for (unsigned i = 0; i < 27; ++i) {
        const auto a = Center(current[i]), b = Center(previous[i]);
        for (unsigned c = 0; c < 3; ++c) {
            aa += (a[c]-ac[c])*(a[c]-ac[c]);bb += (b[c]-bc[c])*(b[c]-bc[c]);
        }
    }
    if (bb < 1 || aa < 1) return {};
    const double scale = std::sqrt(aa/bb);
    if (scale < .999 || scale > 2.01) return {};
    std::vector<unsigned> matches;
    std::array<bool, 27> used{};
    for (const auto& q : current) {
        const auto center = Center(q);
        std::optional<unsigned> match;
        for (unsigned i = 0; i < 27; ++i) {
            if (!Compatible(q, previous[i])) continue;
            const auto other = Center(previous[i]);
            double error = 0;
            for (unsigned c = 0; c < 3; ++c) {
                const double d = center[c]-ac[c]-(other[c]-bc[c])*scale;
                error += d*d;
            }
            if (error > .0001) continue;
            if (match || used[i]) return {};
            match = i;
        }
        if (!match) return {};
        used[*match] = true;matches.push_back(*match);
    }
    return matches;
}
inline std::vector<std::optional<unsigned>> DustMatches(const std::vector<Quad>& current,
                                                       const std::vector<Quad>& previous) {
    std::vector<std::optional<unsigned>> matches(current.size());
    if (current.empty() || previous.empty() || current[0].kind != Kind::Dust || previous[0].kind != Kind::Dust)
        return matches;
    // The flower emits thirty puffs together. Several bursts can share one
    // draw and depth sorting changes their order. Atlas age separates bursts;
    // their radial expansion predicts correspondence before nearest matching.
    for (int age = 1; age < 16; ++age) {
        std::vector<unsigned> newer, older;
        for (unsigned i = 0; i < current.size(); ++i)
            if (Tile(current[i]) == age && std::abs(Edge(current[i])-(9.7f+1.7f*age)) < .02f) newer.push_back(i);
        for (unsigned i = 0; i < previous.size(); ++i)
            if (Tile(previous[i]) == age-1 && std::abs(Edge(previous[i])-(9.7f+1.7f*(age-1))) < .02f) older.push_back(i);
        if (newer.size() != 30 || older.size() != 30) continue;
        std::array<double, 3> ac{}, bc{};
        for (unsigned i = 0; i < 30; ++i) {
            const auto a = Center(current[newer[i]]), b = Center(previous[older[i]]);
            for (unsigned c = 0; c < 3; ++c) { ac[c] += a[c]/30.;bc[c] += b[c]/30.; }
        }
        const double scale = double(age+1)/age;
        std::vector<unsigned> group;
        for (auto index : newer) {
            const auto& q = current[index];const auto center = Center(q);
            double best = std::numeric_limits<double>::infinity(), second = best;
            std::optional<unsigned> match;
            for (auto candidate : older) {
                if (!Compatible(q, previous[candidate])) continue;
                const auto other = Center(previous[candidate]);
                double error = 0;
                for (unsigned c = 0; c < 3; ++c) {
                    const double d = center[c]-ac[c]-(other[c]-bc[c])*scale;error += d*d;
                }
                if (error < best) { second = best;best = error;match = candidate; }
                else if (error < second) second = error;
            }
            if (!match || best > .25 || second <= best*4+.01 ||
                std::find(group.begin(),group.end(),*match) != group.end()) break;
            group.push_back(*match);
        }
        // A partial fit could be a different burst or a spawn/despawn boundary.
        if (group.size() == 30)
            for (unsigned i = 0; i < 30; ++i) matches[newer[i]] = group[i];
    }
    return matches;
}
class History {
    struct Entry { std::uint64_t frame; std::vector<Quad> quads; };
    std::map<Key, Entry> entries;
public:
    void Reset() { entries.clear(); }
    std::optional<std::vector<Quad>> Apply(std::uint64_t frame, const Key& key, const std::vector<Quad>& quads) {
        if (quads.empty() || quads.size() > 128) return {};
        for (auto it = entries.begin(); it != entries.end();)
            if (it->second.frame > frame || frame-it->second.frame > 1) it = entries.erase(it); else ++it;
        auto it = entries.find(key);
        if (it == entries.end()) {
            if (entries.size() < 8) entries.emplace(key, Entry{frame, quads});
            return {};
        }
        auto& e = it->second;
        if (e.frame == frame) { e.quads.clear();return {}; }
        auto out = quads;
        bool changed = false;
        const auto blood = BloodMatches(quads, e.quads);
        const auto dust = DustMatches(quads, e.quads);
        if (e.frame+1 == frame) for (unsigned i = 0; i < quads.size(); ++i) {
            const auto match = blood ? std::optional{(*blood)[i]} : dust[i] ? dust[i] : Match(quads[i], e.quads, true);
            if (!match || (!blood && !dust[i] && Match(e.quads[*match], quads, false) != i)) continue;
            const auto& previous = e.quads[*match];
            for (unsigned v = 0; v < 4; ++v)
                for (unsigned a = 0; a < 2; ++a)
                    for (unsigned c = 0; c < 4; ++c)
                        out[i].vertices[v][a][c] = (quads[i].vertices[v][a][c]+previous.vertices[v][a][c])*.5f;
            // Atlas frames stay discrete: averaging their UVs would sample
            // neighboring tiles. Flames instead have continuous linked scroll.
            if (quads[i].kind == Kind::Flame) {
                float delta = quads[i].vertices[0][3][1]-previous.vertices[0][3][1];
                delta -= std::round(delta);
                for (unsigned v = 0; v < 4; ++v) {
                    out[i].vertices[v][3][1] -= delta*.5f;
                    out[i].vertices[v][4][1] -= delta*2;
                }
            }
            changed = true;
        }
        e = {frame, quads};
        return changed ? std::optional{std::move(out)} : std::nullopt;
    }
};
}
