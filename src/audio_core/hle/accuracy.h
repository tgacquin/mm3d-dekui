// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
namespace AudioCore::HLE {
enum class AccuracyMode : int { Standard, Rupees, All };
// Latched per voice/buffer, never halfway through decoding. Other voices and
// later reuse of the same DSP channel cannot inherit a rupee classification.
inline std::atomic<AccuracyMode> accuracy_mode{AccuracyMode::All};
namespace RupeeAudio {
struct Signature {
    std::uint64_t coefficients, attack, loop;
    unsigned samples, loop_samples;
};
// Identification metadata only, no game samples. JokerSound bank 3 programs
// 9 (fall), 11 (counter), 29 (pickup): wave archive 6 waves 11, 13, 33.
// Initial buffers and their queued tails are identified independently.
// These waveforms can also be reused by other native effects; those instances
// receive the same correction. Shared bank-1 accompaniment is left standard.
inline constexpr std::array<Signature,3> Signatures{{
    {0x21d2b9998236864bULL,0xd074237cdf2ff474ULL,0x3c93926176030731ULL,1698,802},
    {0x2173f84dbe072fcbULL,0xb6f64a8d92fbf290ULL,0x1ef76c7a5221acaeULL,946,50},
    {0x3d4066fb8a7fbb5dULL,0x18e9eec9b9b754c9ULL,0x54ca923054b562ecULL,905,9},
}};
constexpr std::uint64_t Hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t result=14695981039346656037ULL;
    for(auto byte:bytes)result=(result^byte)*1099511628211ULL;
    return result;
}
inline std::uint64_t Coefficients(const std::array<std::int16_t,16>& coefficients) {
    std::array<std::uint8_t,32> bytes{};
    for(unsigned i=0;i<16;++i) {
        const auto value=std::uint16_t(coefficients[i]);
        bytes[2*i]=std::uint8_t(value);bytes[2*i+1]=std::uint8_t(value>>8);
    }
    return Hash(bytes);
}
inline const Signature* Find(const std::array<std::int16_t,16>& coefficients,unsigned samples) {
    const auto hash=Coefficients(coefficients);
    for(const auto& s:Signatures)
        if(hash==s.coefficients && (samples==s.samples || samples==s.loop_samples || samples==896))return &s;
    return nullptr;
}
constexpr unsigned PrefixBytes(unsigned samples) {
    return samples>=112?64:((samples+13)/14)*8;
}
inline bool Matches(const Signature& s,unsigned samples,std::span<const std::uint8_t> bytes) {
    if(samples!=s.samples && samples!=s.loop_samples && samples!=896)return false;
    const auto count=PrefixBytes(samples);
    return bytes.size()>=count && Hash(bytes.first(count))==(samples==s.loop_samples?s.loop:s.attack);
}
}
}
