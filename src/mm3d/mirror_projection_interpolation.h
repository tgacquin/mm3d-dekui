// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <map>
#include "mm3d/camera_midpoint.h"

namespace MM3D::MirrorProjectionInterpolation {
using CameraMidpoint::Matrix;
using Tev = std::array<std::array<std::uint32_t, 5>, 6>;
// The reflected face is composited into opaque receiver materials, not drawn
// with the translucent beam. Texture unit 1 uses a world-space projector in
// c14..c16; its first two rows carry a fixed 0.03 scale.
inline constexpr Tev Material{{
    {238030385u, 0, 65537, 4278190080u, 0},
    {236916239u, 0, 1, 4278190080u, 2},
    {236916708u, 32, 8, 4294967295u, 0},
    {268374015u, 0, 0, 4294967295u, 0},
    {268374015u, 0, 0, 4294967295u, 0},
    {268374015u, 0, 0, 4294967295u, 0},
}};
struct Sample {
    Matrix projection;
    float alpha{};
};
inline std::optional<Matrix> Pose(const Matrix& projection) {
    auto view = projection;
    for (unsigned r = 0; r < 2; ++r)
        for (unsigned c = 0; c < 4; ++c) view[r][c] /= .03;
    if (!CameraMidpoint::Rigid(view)) return {};
    return CameraMidpoint::InverseRigid(view);
}
inline bool Recognized(unsigned mask, Tev material, const Matrix& projection) {
    // RGB is white; only the reflected light's intensity changes.
    material[2][3] |= 0xff000000u;
    return mask == 3 && material == Material && Pose(projection).has_value();
}
inline bool Valid(const Sample& sample) {
    return std::isfinite(sample.alpha) && sample.alpha >= 0 && sample.alpha <= 1 &&
           Pose(sample.projection).has_value();
}
inline std::optional<Sample> Half(const Sample& old, const Sample& now) {
    if (!Valid(old) || !Valid(now)) return {};
    const auto a = *Pose(old.projection), b = *Pose(now.projection);
    double distance = 0, rotation = 0;
    for (unsigned r = 0; r < 3; ++r) {
        distance += std::pow(a[r][3] - b[r][3], 2);
        for (unsigned c = 0; c < 3; ++c)
            rotation += std::pow(a[r][c] - b[r][c], 2);
    }
    // No blending across teleports, a new reflector, or a sudden direction cut.
    if (distance > 32 * 32 || rotation > .5) return {};
    auto projection = CameraMidpoint::InverseRigid(CameraMidpoint::Midpoint(a, b));
    for (unsigned r = 0; r < 2; ++r)
        for (unsigned c = 0; c < 4; ++c) projection[r][c] *= .03;
    return Sample{projection, (old.alpha + now.alpha) * .5f};
}
class History {
public:
    // Share the projector across all wall meshes, independent of their base
    // textures, triangle counts, visibility and submission order.
    using Key = std::array<std::uint32_t, 2>; // projection texture, generation
    void Reset() { entries.clear(); }
    std::optional<Sample> Apply(std::uint64_t frame, const Key& key, const Sample& sample) {
        if (!Valid(sample)) { entries.erase(key); return {}; }
        for (auto it = entries.begin(); it != entries.end();)
            if (it->second.frame > frame || frame - it->second.frame > 1)
                it = entries.erase(it);
            else ++it;
        auto it = entries.find(key);
        if (it == entries.end()) {
            if (entries.size() >= 8) return {};
            entries.emplace(key, Entry{frame, sample, {}, false});
            return {};
        }
        auto& entry = it->second;
        if (entry.frame == frame) {
            if (!CameraMidpoint::Near(entry.native.projection, sample.projection, 1e-6) ||
                entry.native.alpha != sample.alpha) entry.ambiguous = true;
            return entry.ambiguous ? std::nullopt : entry.half;
        }
        const auto half = !entry.ambiguous && entry.frame + 1 == frame ?
            Half(entry.native, sample) : std::nullopt;
        entry = {frame, sample, half, false};
        return half;
    }
private:
    struct Entry {
        std::uint64_t frame;
        Sample native;
        std::optional<Sample> half;
        bool ambiguous;
    };
    std::map<Key, Entry> entries;
};
} // namespace MM3D::MirrorProjectionInterpolation
