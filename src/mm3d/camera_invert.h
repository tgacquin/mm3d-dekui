// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>

namespace MM3D::CameraInvert {
// Host-side inversion of the C-stick (right stick), which controls the free
// camera. Applied where the emulated IR/RST and Circle Pad Pro read the stick,
// so it works for every input device without changing controller bindings.
inline std::atomic<bool> invert_x{false}, invert_y{false};
inline void Configure(bool x, bool y) {
    invert_x.store(x);
    invert_y.store(y);
}
inline void Apply(float& x, float& y) {
    if (invert_x.load()) x = -x;
    if (invert_y.load()) y = -y;
}
} // namespace MM3D::CameraInvert
