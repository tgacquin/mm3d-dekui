// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace MM3D::Translation {
using U32 = std::uint32_t;
enum class Kind { Controls, Owl, Song, Price };
struct Variant {
    std::string text;
    U32 metadata{}, flags{};
};
struct Entry {
    Kind kind{};
    U32 offset{}, capacity{};
    std::vector<Variant> variants;
};
struct Archive {
    std::map<U32, Entry> entries;
    U32 archive_size{}, count{};
};
struct Pack {
    std::map<U32, Archive> archives;
    std::string romfs;
};
// Published only when mounting the game, never while a dialogue is open.
class SessionPack {
public:
    std::shared_ptr<const Pack> load() const {
        const std::lock_guard lock(mutex);
        return value;
    }
    void store(std::shared_ptr<const Pack> pack) {
        const std::lock_guard lock(mutex);
        value = std::move(pack);
    }
private:
    mutable std::mutex mutex;
    std::shared_ptr<const Pack> value;
};
inline SessionPack active;
inline std::atomic<bool> import_requested{false};
inline std::atomic<unsigned> preferences_revision{1};
std::string Import(const std::string& source, const std::string& directory);
bool SetEnabled(const std::string& directory, bool enabled);
bool Requested(const std::string& directory);
bool Installed(const std::string& directory);
// Returns a validated, immutable RomFS layer, or nothing when disabled.
std::string Mount(const std::string& directory);
inline std::string RomFS() {
    const auto pack = active.load();
    return pack ? pack->romfs : std::string{};
}

inline bool Pointer(U32 p, U32 size) {
    return p >= 0x100000 && size <= 0x1000000 && p <= 0x40000000 - size;
}
template<class M> bool Owns(M& m, U32 self) {
    const auto pack = active.load();
    if (!pack || !Pointer(self, 20)) return false;
    const auto base = m.Read(self + 4);
    if (!base || !Pointer(*base, 16)) return false;
    const auto language = m.Read(*base + 12);
    if (!language) return false;
    const auto slot = pack->archives.find(*language);
    if (slot == pack->archives.end()) return false;
    const auto& a = slot->second;
    return Pointer(*base, a.archive_size) &&
        m.Read(self + 8) == *base + 16 && m.Read(self + 12) == a.archive_size &&
        m.Read(self + 16) == 20 && m.Read(*base) == 0x47534d47 &&
        m.Read(*base + 4) == 1 && m.Read(*base + 8) == a.count;
}
template<class M> bool Apply(M& m, U32 self, U32 id, Kind kind, unsigned variant) {
    const auto pack = active.load();
    if (!pack || !Pointer(self, 20)) return false;
    const auto base = m.Read(self + 4);
    if (!base || !Pointer(*base, 16)) return false;
    const auto language = m.Read(*base + 12);
    if (!language) return false;
    const auto slot = pack->archives.find(*language);
    if (slot == pack->archives.end()) return false;
    const auto& archive = slot->second;
    const auto found = archive.entries.find(id);
    if (found == archive.entries.end()) return false;
    const auto& e = found->second;
    if (e.kind != kind || variant >= e.variants.size()) return false;
    if (!Pointer(*base, archive.archive_size) ||
        m.Read(self + 8) != *base + 16 || m.Read(self + 12) != archive.archive_size ||
        m.Read(self + 16) != 20 || m.Read(*base) != 0x47534d47 ||
        m.Read(*base + 4) != 1 || m.Read(*base + 8) != archive.count) return false;
    U32 lo = 0, hi = archive.count;
    while (lo < hi) {
        const U32 mid = lo + (hi - lo) / 2, entry = *base + 16 + mid * 20;
        const auto key = m.Read(entry);
        if (!key) return false;
        if ((*key & 65535) < id) { lo = mid + 1; continue; }
        if ((*key & 65535) > id) { hi = mid; continue; }
        if (m.Read(entry + 12) != e.offset || m.Read(entry + 16) != e.capacity ||
            e.capacity % 4 || e.offset % 4 || e.offset < 16 + archive.count * 20 ||
            e.offset > archive.archive_size || e.capacity > archive.archive_size - e.offset)
            return false;
        const auto metadata = m.Read(entry + 4), flags = m.Read(entry + 8);
        if (!metadata || !flags) return false;
        std::string current(e.capacity, '\0');
        for (U32 i = 0; i < e.capacity; i += 4) {
            const auto word = m.Read(*base + e.offset + i);
            if (!word) return false;
            for (unsigned b = 0; b < 4; ++b) current[i + b] = char(*word >> (8 * b));
        }
        bool recognized = false;
        for (const auto& v : e.variants)
            recognized |= current == v.text && *metadata == v.metadata && *flags == v.flags;
        if (!recognized) return false;
        const auto& desired = e.variants[variant];
        if (desired.text.size() != e.capacity) return false;
        if (current == desired.text && *metadata == desired.metadata && *flags == desired.flags)
            return true;
        // Every destination was checked before the first write. Entry attributes
        // and text change together; unknown/custom payloads are never overwritten.
        for (U32 i = 0; i < e.capacity; i += 4) {
            U32 word = 0;
            for (unsigned b = 0; b < 4; ++b)
                word |= U32(static_cast<unsigned char>(desired.text[i + b])) << (8 * b);
            m.Write(*base + e.offset + i, word);
        }
        m.Write(entry + 4, desired.metadata);
        m.Write(entry + 8, desired.flags);
        return true;
    }
    return false;
}
}
