// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "mm3d/link_pose_math.h"
namespace MM3D::LinkPoseCapture {
using U32=std::uint32_t;
using Matrix=std::array<float,12>;
// Human Link's skeleton topology/name hashes; no model or animation payload.
inline constexpr std::array<U32,26> BoneWords{
0xffff0000,0x1,0x11002,0x21003,0x31004,0x41005,0x21006,0x61007,0x71008,
0x11009,0x9100a,0xa100b,0xb100c,0xc100d,0xa100e,0xe100f,0xf1010,0x101011,
0xa1012,0x121013,0x131014,0x141015,0xa1016,0x17,0x170018,0x170019};
inline constexpr std::array<U32,26> Hashes{
0xb801f960,0xfc29f700,0xb29ce26f,0x64feead5,0x64e15c5a,0x64fff60b,
0x64feea12,0x64e15c1d,0x64fff6cc,0x1205e2f8,0x1205e440,0x453279f3,
0x4534ee22,0x891bcbe4,0x238c3e48,0xdefe7b07,0xdeff87dc,0xb255daf4,
0x238c3980,0xdefe7bc0,0xdeff871b,0xb255daad,0xb306e1a0,0x7fbee60e,
0x20f24568,0x10d8f708};
struct Pose { std::uint64_t frame{}; U32 instance{}, output{}; std::vector<Matrix> bones=std::vector<Matrix>(26); };
struct Draw { std::uint64_t frame{}; U32 vertices{},vertex_base{},index_offset{};
    std::array<Matrix,10> palette{}; };
// Renderer results paired with native Draw samples. This distinguishes a valid
// offline skeleton midpoint from a midpoint actually submitted during gameplay.
struct Replay {
    std::uint64_t frame{};
    U32 vertices{},vertex_base{},index_offset{},gates{},flags{},overrides{},changed{},poses{};
    float uniform_delta{};
    Matrix camera{};
};
inline std::mutex mutex;
inline std::atomic<bool> active{false}, interpolate{false}, interpolate_actors{false};
inline std::atomic<unsigned> interpolation_generation{0}, replaced_draws{0}, replaced_bones{0};
struct LivePose {
    Pose pose;
    std::vector<Matrix> bind=std::vector<Matrix>(26);
    U32 model{}; unsigned generation{};
    std::vector<U32> words=std::vector<U32>(BoneWords.begin(),BoneWords.end());
    std::uint64_t signature{};
};
inline bool Enabled() { return interpolate.load() || interpolate_actors.load(); }
inline std::vector<LivePose> live;
inline std::vector<LivePose> ForFrame(std::uint64_t frame) {
    std::scoped_lock lock{mutex};
    std::vector<LivePose> result;
    for(const auto& p:live)if(p.pose.frame+1==frame && p.generation==interpolation_generation.load())result.push_back(p);
    return result;
}
inline std::atomic<std::int64_t> begins{},ends{};
inline std::atomic<unsigned> pose_count{},draw_count{};
inline std::vector<Pose> poses;
inline std::vector<LivePose> actor_poses;
inline std::vector<Draw> draws;
inline std::vector<Replay> replays;
inline bool truncated{};
inline auto Now() { return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline bool Recording() { if(!active.load())return false;const auto now=Now();return now>=begins && now<ends; }
inline void Start() {
    std::scoped_lock lock{mutex};active=false;poses.clear();actor_poses.clear();draws.clear();replays.clear();truncated=false;
    pose_count=0;draw_count=0;begins=Now()+3000;ends=begins.load()+10000;active=true;
}
inline void Poll() { if(active && Now()>=ends)active=false; }
inline bool Pointer(U32 p,U32 size) { return p>=0x08000000 && !(p&3) && p<0x40000000 && size<=0x40000000-p; }
struct Hook {
    Pose current;
    U32 seen{},model{};
    unsigned generation{};
    std::vector<Matrix> bind;
    std::vector<U32> words;
    std::vector<bool> received;
    std::uint64_t signature{};
    bool bind_valid{},human{};
    template<class M> void Enter(M& m,U32 candidate) {
        current.instance=0;seen=0;bind_valid=false;
        if((!Recording() && !Enabled()) || !Pointer(candidate,0x28))return;
        auto wrapper=m.Read(candidate+0xc),out=m.Read(candidate+0x24);
        if(!wrapper||!out||!Pointer(*wrapper,4))return;
        auto data=m.Read(*wrapper);
        if(!data||!Pointer(*data,16)||m.Read(*data)!=std::optional<U32>(0x206c6b73))return;
        auto count=m.Read(*data+8),size=m.Read(*data+4);
        if(!count||!*count||*count>128||!size||*size!=16+44*(*count)||
           !Pointer(*data,*size)||!Pointer(*out,*count*48))return;
        words.resize(*count);received.assign(*count,false);
        human=*count==26;signature=14695981039346656037ull;
        const auto hash=[&](U32 value){signature^=value;signature*=1099511628211ull;};
        for(unsigned i=0;i<*count;++i) {
            auto word=m.Read(*data+16+44*i),name=m.Read(*data+56+44*i);
            if(!word||!name||(*word&0xfff)!=i)return;
            const auto parent=*word>>16;
            if(parent!=65535 && parent>=i)return;
            words[i]=*word;hash(*word);hash(*name);
            if(human && (*word!=BoneWords[i]||*name!=Hashes[i]))human=false;
        }
        if(!human && !interpolate_actors.load())return;
        if(human && !Recording() && !interpolate.load())return;
        current={};current.bones.resize(*count);current.output=*out;
        model=*data;generation=interpolation_generation.load();
        if((human && interpolate.load()) || (!human && interpolate_actors.load())) {
            using namespace LinkPoseMath;
            std::vector<CameraMidpoint::Matrix> rest(*count);bind.resize(*count);
            for(unsigned i=0;i<*count;++i) {
                std::array<float,9> values{};
                for(unsigned j=0;j<9;++j) {
                    const auto word=m.Read(*data+20+44*i+j*4);
                    if(!word || !std::isfinite(std::bit_cast<float>(*word)))return;
                    hash(*word);values[j]=std::bit_cast<float>(*word);
                }
                const auto parent=words[i]>>16;
                rest[i]=Rest(values);
                if(parent!=65535)rest[i]=Multiply(rest[parent],rest[i]);
                const auto inverse=Inverse(rest[i]);if(!inverse)return;
                bind[i]=Flatten(*inverse);
            }
            bind_valid=true;
        }
        current.instance=candidate;
    }
    template<class M> void MatrixBuilt(M& m,U32 candidate,unsigned index,std::uint64_t frame) {
        if((!Recording() && !Enabled()) || !current.instance || candidate!=current.instance || index>=current.bones.size())return;
        for(unsigned i=0;i<12;++i) {
            auto word=m.Read(current.output+index*48+i*4);
            if(!word || !std::isfinite(std::bit_cast<float>(*word))) {current.instance=0;return;}
            current.bones[index][i]=std::bit_cast<float>(*word);
        }
        if(!seen)current.frame=frame;
        if(current.frame!=frame){current.instance=0;return;}
        if(!received[index]){received[index]=true;++seen;}
        if(seen==current.bones.size()) {
            std::scoped_lock lock{mutex};
            if(Enabled() && bind_valid && generation==interpolation_generation.load()) {
                // Keep a bounded recent window; preserve duplicate instances for ambiguity checks.
                std::erase_if(live,[&](const auto& p){return p.generation!=generation ||
                    p.pose.frame>frame || frame-p.pose.frame>3;});
                if(live.size()>=512)live.erase(live.begin());
                live.push_back({current,bind,model,generation,words,signature});
            }
            if(Recording() && !human && bind_valid && (frame+1)%10<3) {
                if(actor_poses.size()<4096)
                    actor_poses.push_back({current,bind,model,generation,words,signature});
                else truncated=true;
            }
            if(active && human) {
                if(poses.size()<4096){poses.push_back(current);pose_count=unsigned(poses.size());}
                else truncated=true;
            }
            current.instance=0;
        }
    }
};
inline void Observe(Draw draw) {
    if(!Recording())return;
    // Three consecutive frames every ten retain matching animation samples,
    // without copying every material draw throughout the whole ten seconds.
    if(draw.frame%10>=3)return;
    std::scoped_lock lock{mutex};
    if(!active)return;
    if(draws.size()<8192){draws.push_back(std::move(draw));draw_count=unsigned(draws.size());}
    else truncated=true;
}
bool Export(const std::string& path);
inline void ObserveReplay(const Replay& replay) {
    if (!Recording() || replay.frame%10>=3) return;
    const std::scoped_lock lock{mutex};
    if (!active) return;
    if (replays.size()<8192) replays.push_back(replay);
    else truncated=true;
}
}
