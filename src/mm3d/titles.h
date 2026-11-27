// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

// Majora's Mask 3D program IDs the mod supports. Europe Rev 1 ships a code.bin
// identical to USA Rev 1 (same CRC32), so every code hook applies to both.
namespace MM3D::Titles {
inline constexpr std::uint64_t Usa = 0x0004000000125500ULL;
inline constexpr std::uint64_t Europe = 0x0004000000125600ULL;

constexpr bool IsSupported(std::uint64_t program_id) {
    return program_id == Usa || program_id == Europe;
}

// Title whose `load/mods/<id>` and `load/textures/<id>` folders this session uses.
// Defaults to USA (existing installations); set once the game is identified.
inline std::atomic<std::uint64_t>& Selected() {
    static std::atomic<std::uint64_t> selected{Usa};
    return selected;
}
inline void Select(std::uint64_t program_id) {
    if (IsSupported(program_id)) Selected().store(program_id);
}
inline std::string Hex(std::uint64_t program_id) {
    char text[17];
    std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(program_id));
    return text;
}
inline std::string SelectedHex() { return Hex(Selected().load()); }
} // namespace MM3D::Titles
