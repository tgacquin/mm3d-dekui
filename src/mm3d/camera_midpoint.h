// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>
#include "mm3d/widescreen_probe.h"

namespace MM3D::CameraMidpoint {
using Matrix = std::array<std::array<double,4>,4>;
using Quaternion = std::array<double,4>; // w,x,y,z
inline std::atomic<bool> enabled{false}, native_preview{false}, present_60{false};
inline std::atomic<bool> suppress_transform_feedback{false};
inline std::atomic<std::uint64_t> capture_pair_at_us{};
// Explicit local diagnostic: wait for horizontal menu motion, not a camera
// pair produced while the inventory is still closed or stationary.
inline std::atomic<std::uint64_t> capture_menu_pair_at_us{}, capture_menu_motion_frame{};
inline std::atomic<unsigned> generation{0}, copied_draws{0}, rejected_draws{0}, fallback_draws{0}, presented_pairs{0};
// Native memory fills establish the scene background before any geometry. Keep
// only same-frame, whole-target RGBA8 clears; depth/partial/old fills must not
// supply a color for an unrelated replay target.
struct ColorClears {
    struct Fill { std::uint32_t start{},end{},rgba{}; };
    std::array<Fill,16> fills{};
    std::optional<std::uint64_t> frame;
    void Reset() { fills={};frame.reset(); }
    void Invalidate(std::uint32_t start,std::uint64_t end) {
        for(auto& fill:fills)
            if(start<fill.end && fill.start<end)fill={};
    }
    void Observe(std::uint64_t current,std::uint32_t start,std::uint32_t end,
                 std::uint32_t rgba,unsigned bytes) {
        if(frame!=current) { Reset();frame=current; }
        if(end<=start)return;
        Invalidate(start,end);
        if(bytes!=4)return;
        for(auto& fill:fills)if(fill.end==0) { fill={start,end,rgba};return; }
        // A full tracker may lose a clear, but cannot retain an overwritten one.
    }
    std::array<float,4> Background(std::uint64_t current,std::uint32_t address,
                                   unsigned width,unsigned height,bool rgba8) const {
        if(frame==current && rgba8 && width && height) {
            const std::uint64_t end=std::uint64_t(address)+std::uint64_t(width)*height*4;
            for(const auto& fill:fills)if(fill.start==address && fill.end==end) {
                // PICA RGBA8 is stored with R in the high byte. Private scene
                // coverage stays opaque; the native clear's alpha is not HUD coverage.
                return {float((fill.rgba>>24)&255)/255.f,float((fill.rgba>>16)&255)/255.f,
                        float((fill.rgba>>8)&255)/255.f,1.f};
            }
        }
        return {0,0,0,1};
    }
};
inline Matrix Identity() { Matrix m{}; for(unsigned i=0;i<4;++i)m[i][i]=1; return m; }
inline Matrix Multiply(const Matrix& a,const Matrix& b) {
    Matrix r{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)r[i][j]+=a[i][k]*b[k][j];
    return r;
}
inline bool Near(const Matrix& a,const Matrix& b,double epsilon) {
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)
        if(!std::isfinite(a[i][j])||!std::isfinite(b[i][j])||std::abs(a[i][j]-b[i][j])>epsilon)return false;
    return true;
}
// Captured title-logo camera: a separate fixed view, not the world camera.
inline bool TitleLogoCamera(const Matrix& camera,const Matrix& projection) {
    auto expected=Identity();expected[2][3]=30.;
    Matrix lens{};lens[0][1]=2.390578508377075;lens[1][0]=-1.43434739112854;
    lens[2][2]=1.0003907680511475;lens[2][3]=5.001953601837158;lens[3][2]=-1.;
    return Near(camera,expected,.0001) && Near(projection,lens,.0001);
}
inline bool Rigid(const Matrix& m) {
    for(const auto& row:m)for(double v:row)if(!std::isfinite(v))return false;
    for(unsigned i=0;i<4;++i)if(std::abs(m[3][i]-(i==3?1.:0.))>1e-6)return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        double dot=0;for(unsigned k=0;k<3;++k)dot+=m[k][i]*m[k][j];
        if(std::abs(dot-(i==j?1.:0.))>.002)return false;
    }
    const double determinant=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-
        m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    return std::abs(determinant-1)<.003;
}
inline Matrix InverseRigid(const Matrix& m) {
    Matrix r=Identity();
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<3;++j)r[i][j]=m[j][i];
        for(unsigned j=0;j<3;++j)r[i][3]-=r[i][j]*m[j][3];
    }
    return r;
}
inline Quaternion Rotation(const Matrix& m) {
    Quaternion q{};
    const double trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0) {
        const double s=2*std::sqrt(trace+1);
        q={s/4,(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s};
    } else {
        unsigned i=0;for(unsigned k=1;k<3;++k)if(m[k][k]>m[i][i])i=k;
        const unsigned j=(i+1)%3,k=(i+2)%3;
        const double s=2*std::sqrt(1+m[i][i]-m[j][j]-m[k][k]);
        q[0]=(m[k][j]-m[j][k])/s; q[i+1]=s/4;
        q[j+1]=(m[j][i]+m[i][j])/s;q[k+1]=(m[k][i]+m[i][k])/s;
    }
    double length=0;for(double v:q)length+=v*v;length=std::sqrt(length);
    for(double& v:q)v/=length;
    return q;
}
inline Matrix Midpoint(const Matrix& a,const Matrix& b) {
    auto qa=Rotation(a),qb=Rotation(b);
    double dot=0;for(unsigned i=0;i<4;++i)dot+=qa[i]*qb[i];
    Quaternion q{};double length=0;
    for(unsigned i=0;i<4;++i){q[i]=qa[i]+(dot<0?-qb[i]:qb[i]);length+=q[i]*q[i];}
    length=std::sqrt(length);for(double& v:q)v/=length;
    const auto [w,x,y,z]=q;
    Matrix r=Identity();
    r[0][0]=1-2*(y*y+z*z);r[0][1]=2*(x*y-z*w);r[0][2]=2*(x*z+y*w);
    r[1][0]=2*(x*y+z*w);r[1][1]=1-2*(x*x+z*z);r[1][2]=2*(y*z-x*w);
    r[2][0]=2*(x*z-y*w);r[2][1]=2*(y*z+x*w);r[2][2]=1-2*(x*x+y*y);
    for(unsigned i=0;i<3;++i)r[i][3]=(a[i][3]+b[i][3])/2;
    return r;
}
// Sky and translucent world draws can use different clipping planes while
// sharing the same camera lens. The Z row must remain specific to each draw.
inline bool SameLens(const Matrix& a,const Matrix& b) {
    for(unsigned i : {0u,1u,3u})for(unsigned j=0;j<4;++j)
        if(!std::isfinite(a[i][j]) || !std::isfinite(b[i][j]) ||
           std::abs(a[i][j]-b[i][j])>1e-5)return false;
    return true;
}
inline bool WorldProjection(const Matrix& p) {
    return Near(p,p,0) && p[3]==std::array<double,4>{0,0,-1,0};
}
// Captured untextured sky dome. It precedes the scene background cover despite
// using opaque depth state. First-person camera movement can offset its center
// (over 12 units in the winter capture), so translation cannot identify it.
// Require its mesh count, sky clip range, texture state and 320-unit rigid basis.
inline bool OpaqueSkyDome(unsigned vertices,const Matrix& projection,const Matrix& model,unsigned textures) {
    if(vertices!=288 || textures!=0 || !WorldProjection(projection) || !Near(model,model,0) ||
       std::abs(projection[2][2]-1.0000892877578735)>1e-6 ||
       std::abs(projection[2][3]-5.000446796417236)>1e-5)return false;
    auto rotation=model;
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<3;++j)rotation[i][j]/=320.;
    }
    return Rigid(rotation);
}
// Accept gradual uniform lens scaling, not a changed aspect, skew or projection type.
// Rotated 3DS perspective matrices put the lens terms off the diagonal.
inline std::optional<double> GradualLensScale(const Matrix& previous,const Matrix& current) {
    if(!WorldProjection(previous)||!WorldProjection(current))return {};
    unsigned row=0,col=0;
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j)
        if(std::abs(previous[i][j])>std::abs(previous[row][col])){row=i;col=j;}
    if(std::abs(previous[row][col])<1e-6)return {};
    const double scale=current[row][col]/previous[row][col];
    if(scale<=0)return {};
    // A steady transformation zoom can exceed 5% per frame at a narrow FOV.
    // Bound its angular change too, rather than treating that smooth zoom as a
    // cut. Retain a relative cap so extremely narrow lenses cannot jump freely.
    const auto lens_angle=[](double term) {
        return 2.*std::atan(1./std::abs(term))*180./3.141592653589793;
    };
    const bool small_angle=scale>=.9 && scale<=1.1 &&
        std::abs(lens_angle(current[row][col])-lens_angle(previous[row][col]))<=2.5;
    if((scale<.95 || scale>1.05) && !small_angle)return {};
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j)
        if(std::abs(current[i][j]-previous[i][j]*scale)>1e-5)return {};
    return scale;
}
// Underwater world projection has a small independent X/Y wobble relative to
// the sky. Only share the camera delta within one frame, across distinct clip
// ranges, and for the captured rotated perspective layout. Keep this draw's
// native lens; applying the sky's zoom correction would distort the world.
inline bool AlternateWorldLens(const Matrix& a,const Matrix& b) {
    if(!WorldProjection(a)||!WorldProjection(b)||a[2]==b[2])return false;
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j) {
        if(j==1-i) {
            if(std::abs(a[i][j])<1e-6)return false;
            const double ratio=b[i][j]/a[i][j];
            if(ratio<.92 || ratio>1.08)return false;
        } else if(std::abs(a[i][j])>1e-6 || std::abs(b[i][j])>1e-6)return false;
    }
    return true;
}
// The underwater wobble also changes independently on each axis between
// frames, including scenes without a sky draw to seed a stable lens. Accept
// only small changes of the rotated perspective terms with unchanged clipping.
inline std::optional<std::array<double,2>> GradualWorldWobble(const Matrix& a,const Matrix& b) {
    if(!WorldProjection(a)||!WorldProjection(b)||a[2]!=b[2])return {};
    // Water entry animates the two lens axes independently by up to 4.8% per
    // frame. The far plane is scene-dependent (the Hall capture differs from
    // the coast), so an exact depth coefficient cannot identify this motion.
    // Recognize the native five-unit near-plane perspective family instead:
    // for this depth convention near = P[2][3] / P[2][2]. Keep clipping fixed
    // within the pair, and retain all axis, sign and camera-cut guards.
    const bool world_lens=a[0][1]>0 && a[1][0]<0 &&
        std::abs(a[2][0])<1e-6 && std::abs(a[2][1])<1e-6 &&
        a[2][2]>1. && a[2][2]<1.01 &&
        std::abs(a[2][3]/a[2][2]-5.)<.00001;
    const double limit=world_lens?.05:.005;
    std::array<double,2> scale{};
    for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c) {
        if(c==1-r) {
            if(std::abs(a[r][c])<1e-6)return {};
            scale[r]=b[r][c]/a[r][c];
            if(scale[r]<1.-limit || scale[r]>1.+limit)return {};
        } else if(std::abs(a[r][c])>1e-6 || std::abs(b[r][c])>1e-6)return {};
    }
    // Both axes animate in the captured distortion. A one-axis resize/aspect
    // transition must not use this exception to the normal lens guard.
    if(scale[0]==1. || scale[1]==1.)return {};
    return scale;
}
// Double Time and Inverted Time distort both lens axes and add tiny off-center
// terms. Their captured clipping ranges distinguish this from underwater wobble.
// Keep arbitrary skew/aspect changes and large jumps subject to the cut guard.
inline bool GradualTimeSkipDistortion(const Matrix& a,const Matrix& b) {
    const auto captured=[](const Matrix& p) {
        const bool shape=WorldProjection(p) && p[0][1]>0 && p[1][0]<0 &&
            std::abs(p[0][0])<1e-8 && std::abs(p[1][1])<1e-8 &&
            std::abs(p[0][3])<1e-8 && std::abs(p[1][3])<1e-8 &&
            std::abs(p[0][2])<=.00002 && std::abs(p[1][2])<=.0001 &&
            std::abs(p[2][0])<1e-8 && std::abs(p[2][1])<1e-8;
        if(!shape)return 0u;
        if(p[2][2]>=1.00015 && p[2][2]<=1.00027 &&
           std::abs(p[2][3]-5.001250267028809)<.000002)return 1u;
        // Inverted Time uses the same kind of distortion with a different
        // near/far range. Keep the two families separate across transitions.
        if(p[2][2]>=1.00032 && p[2][2]<=1.00040 &&
           std::abs(p[2][3]-5.001953601837158)<.000002)return 2u;
        // Double Time also retains this scene's shorter far plane. The white
        // cutscene animates the same lens offsets at this captured depth range.
        if(p[2][2]>=1.00043 && p[2][2]<=1.00051 &&
           std::abs(p[2][3]-5.002501487731934)<.000002)return 3u;
        return 0u;
    };
    const auto kind=captured(a);
    if(!kind || captured(b)!=kind)return false;
    if(a[0][2]==0 && a[1][2]==0 && b[0][2]==0 && b[1][2]==0)return false;
    for(unsigned r=0;r<2;++r) {
        const double ratio=b[r][1-r]/a[r][1-r];
        const double angle=std::abs(std::atan(1./std::abs(a[r][1-r]))-
                                    std::atan(1./std::abs(b[r][1-r])))*360./3.141592653589793;
        if(ratio<.87 || ratio>1.13 || angle>8)return false;
    }
    return true;
}
// Provenance for the native end-of-day scene-copy quad. Only bypass copies
// derived from the scene captured in this guest frame, never arbitrary textures.
struct SceneCopies {
    std::uint64_t frame{};
    std::array<std::uint32_t,8> addresses{};
    std::array<std::uint32_t,8> previous_addresses{};
    // The closing-day copy occupies the last 400 rows of a 512x512 RGBA8
    // texture. Its sampler base precedes the actual copy by 112 tiled rows.
    // The visible 480 columns may be copied in 8-row tile bands, skipping
    // the remaining 32 columns. Track this separately; do not promote arbitrary
    // containing textures (or this alias) to exact/previous-frame provenance.
    static constexpr std::uint32_t SceneBytes=512*400*4,PaddingBytes=512*112*4;
    std::uint32_t padded_source{},padded_destination{};
    void Begin(std::uint64_t current,std::uint32_t address,
               unsigned width=0,unsigned height=0,bool rgba8=false) {
        if(frame!=current){
            previous_addresses=current==frame+1?addresses:std::array<std::uint32_t,8>{};
            frame=current;addresses={};padded_destination=0;
        }
        padded_source=width==512 && height==400 && rgba8?address:0;
        Add(address);
    }
    bool PreviousContains(std::uint64_t current,std::uint32_t address) const {
        return current==frame && address &&
            std::find(previous_addresses.begin(),previous_addresses.end(),address)!=previous_addresses.end();
    }
    void Add(std::uint32_t address) {
        if(!address)return;
        for(auto& entry:addresses){if(entry==address)return;if(!entry){entry=address;return;}}
    }
    bool Contains(std::uint64_t current,std::uint32_t address) const {
        return current==frame && address &&
            std::find(addresses.begin(),addresses.end(),address)!=addresses.end();
    }
    void Transfer(std::uint64_t current,std::uint32_t source,std::uint32_t destination) {
        padded_destination=0;
        const bool known=Contains(current,source);
        for(auto& entry:addresses)if(entry==destination)entry=0;
        if(known)Add(destination);
    }
    void TextureTransfer(std::uint64_t current,std::uint32_t source,std::uint32_t destination,
                         std::uint32_t size,unsigned input_width,unsigned input_gap,
                         unsigned output_width,unsigned output_gap,bool succeeded) {
        // Widths/gaps are bytes, size has the PICA 16-byte alignment applied.
        // Gaps are ignored by the native transfer when its width spans size.
        const bool contiguous=size==SceneBytes && (!input_gap || input_width>=size) &&
            (!output_gap || output_width>=size);
        // Captured native copy: 50 bands of 480x8 RGBA8 pixels, each with
        // a 32x8-pixel gap. Both ends retain the same 512-pixel tiled stride.
        // NormalizedShrinkMesh samples only inside these copied 480 columns.
        const bool visible_rect=size==480*400*4 && input_width==480*8*4 &&
            input_gap==32*8*4 && output_width==input_width && output_gap==input_gap;
        const bool complete=succeeded && source==padded_source && Contains(current,source) &&
            (contiguous || visible_rect) && destination &&
            std::uint64_t(destination)+SceneBytes<=0x100000000ULL;
        Transfer(current,succeeded?source:0,destination);
        if(complete)padded_destination=destination;
    }
    void InvalidatePadded(std::uint32_t start,std::uint64_t end) {
        if(padded_destination && start<std::uint64_t(padded_destination)+SceneBytes &&
           padded_destination<end)padded_destination=0;
    }
    bool PaddedTextureContains(std::uint64_t current,std::uint32_t base,
                               unsigned width,unsigned height,bool rgba8) const {
        return frame==current && base && padded_destination && rgba8 && width==512 && height==512 &&
            std::uint64_t(base)+PaddingBytes==padded_destination;
    }
};
inline bool NormalizedShrinkMatrices(const Matrix& projection,const Matrix& model) {
    Matrix p{};p[0]={0,-1./120,0,1};p[1]={-1./200,0,0,1};
    p[2]={0,0,1./1800,1./3};p[3]={0,0,0,1};
    auto m=Identity();m[2][3]=-1200;
    return Near(projection,p,.00001) && Near(model,m,.00001);
}
inline bool NormalizedShrinkMesh(const Matrix& projection,const Matrix& model,
                                 const std::array<float,4>& uv) {
    // Verified native shrink mesh UVs stop one texel short of the scene edge.
    // Stereo mode uses 239/512 horizontally, mono uses twice that value.
    for(float value:uv)if(!std::isfinite(value))return false;
    constexpr float epsilon=.00001f;
    return NormalizedShrinkMatrices(projection,model) &&
        std::abs(uv[0])<epsilon && std::abs(uv[1])<epsilon &&
        (std::abs(uv[2]-478.f/512)<epsilon || std::abs(uv[2]-239.f/512)<epsilon) &&
        std::abs(uv[3]-399.f/512)<epsilon;
}
inline bool TransformationFeedback(const Matrix& projection,const Matrix& model,
                                   bool previous_scene,unsigned vertices,unsigned textures) {
    if(!previous_scene || vertices!=4 || textures!=1)return false;
    Matrix p{};p[0]={0,-1./120,0,1};p[1]={-1./200,0,0,1};
    p[2]={0,0,.5,-.5};p[3]={0,0,0,1};
    auto m=Identity();m[0][0]=m[1][1]=m[2][2]=1.0008;
    m[0][3]=-.16;m[1][3]=-.096;
    return Near(projection,p,.00001) && Near(model,m,.00001);
}
inline bool NormalizedSceneCopy(bool normalized,bool source_is_scene,bool ortho,
                               unsigned vertices,unsigned textures,bool opaque,bool stencil) {
    return normalized && source_is_scene && ortho && vertices==4 && textures==1 && opaque && !stencil;
}
inline bool NormalizedSceneClear(bool normalized,bool target_is_scene,bool ortho,
                                unsigned vertices,unsigned textures,bool opaque,bool stencil) {
    return normalized && target_is_scene && ortho && vertices==4 && textures==0 && opaque && !stencil;
}
// A native clear followed by the normalized scene copy is redundant in the
// private midpoint target. Defer the clear only until the next top-screen draw
// proves that pair; an unmatched clear must still make presentation fall back.
struct SceneClearPair {
    std::optional<std::uint64_t> frame;
    std::uint32_t target{};
    bool Observe(std::uint64_t current,std::uint32_t destination,bool clear,bool scene_copy) {
        const bool rejected=Pending(current) && !(scene_copy && destination==target);
        frame=clear?std::optional(current):std::nullopt;
        target=clear?destination:0;
        return rejected;
    }
    bool Pending(std::uint64_t current) const { return frame && *frame==current; }
    void Reset() { frame.reset();target=0; }
};
inline bool TransitionFade(const WidescreenProbe::Draw& draw,const Matrix& p) {
    // Captured screen-cover signature, shared by transitions and letterbox
    // bars. It does not by itself establish their order relative to the HUD;
    // paired presentation must preserve guest submission order.
    return draw == WidescreenProbe::TransitionOverlay && Near(p,p,0);
}
// Host-only sequencing: a pair is midpoint then current, never current then
// midpoint. Duplicate refreshes after a pair emit nothing; native 60 Hz scenes
// and discontinuities fall back to their original cadence.
enum class Presentation { Native, Pair, Skip };
enum class PresentationStatus : unsigned { Off, Settings, WaitingScene, FrameMismatch,
    NoMidpoint, NoHud, UnsupportedHud, Comparison, Cadence, Active };
inline std::atomic<PresentationStatus> presentation_status{PresentationStatus::Off};
inline const char* StatusText(PresentationStatus status) {
    switch(status) {
    case PresentationStatus::Off:return "Off";
    case PresentationStatus::Settings:return "Waiting: enable VSync and preview; use one landscape screen view";
    case PresentationStatus::WaitingScene:return "Native: waiting for supported scene";
    case PresentationStatus::FrameMismatch:return "Native: captured scene is not the completed frame";
    case PresentationStatus::NoMidpoint:return "Native: camera history not ready";
    case PresentationStatus::NoHud:return "Native: waiting for HUD capture";
    case PresentationStatus::UnsupportedHud:return "Native: unsupported HUD/scene draw";
    case PresentationStatus::Comparison:return "Native: disable current-camera comparison";
    case PresentationStatus::Cadence:return "Native: waiting for stable 30 Hz scene cadence";
    case PresentationStatus::Active:return "Active: presenting midpoint/native pairs";
    }
    return "Unknown";
}
inline bool IsCompletedCapture(std::optional<std::uint64_t> capture,std::uint64_t completed) {
    // Draws use the count before SetBufferSwap/EndGameFrame increments it.
    return capture && completed>0 && *capture==completed-1;
}
// Pace frame starts on a fixed timeline. Submission/GPU work consumes this budget;
// it must not be followed by a fresh full-frame sleep.
class PresentationPacer {
    std::optional<std::int64_t> next;
public:
    static constexpr std::int64_t Period=16666667;
    std::int64_t Deadline(std::int64_t now,bool paired) {
        if(!paired){next.reset();return now;}
        // Late frames resume immediately; adding a new buffer delay here creates
        // visible hitches and grows queue latency. Rebase to avoid rapid catch-up.
        if(!next || now-*next>=Period/4)next=now;
        const auto deadline=*next;
        *next+=Period;
        return deadline;
    }
};
class PresentationClock {
    std::optional<std::uint64_t> frame;
    std::int64_t time{};
    unsigned stable{};
    bool paired{};
    bool guest_timed{};
public:
    void Reset() { frame.reset();stable=0;paired=false;guest_timed=false; }
    Presentation Next(std::uint64_t next,std::int64_t now,bool eligible,
                      std::optional<std::int64_t> guest_ms=std::nullopt) {
        if(frame && *frame==next)return paired?Presentation::Skip:Presentation::Native;
        const auto timestamp=guest_ms.value_or(now);
        const auto elapsed=timestamp-time;
        const bool continuous=frame && next==*frame+1 && guest_timed==guest_ms.has_value() &&
            elapsed>=25 && elapsed<=50;
        // Cadence describes the guest frames, including frames whose private
        // replay is incomplete. A pause/menu transition can temporarily lose
        // its midpoint while continuing at 30 Hz; requiring two more eligible
        // frames afterward hides most of a short opening/closing animation.
        // Use completed-buffer emulated timestamps when available. Host waits
        // (notably the pause backdrop capture) do not change the game's rate.
        // Eligibility still gates every pair; a guest 60 Hz interval, frame
        // gap, guest-time stall/rewind or clock-source change resets cadence.
        stable=continuous?std::min(stable+1,2u):0;
        frame=next;time=timestamp;guest_timed=guest_ms.has_value();paired=eligible && stable>=2;
        return paired?Presentation::Pair:Presentation::Native;
    }
};
// Diagnostic counts are draw observations, not completed game-frame counts.
inline std::atomic<unsigned> camera_cpu_mismatch{},camera_lens_change{},camera_cut{},camera_gap{};
// Render-thread history only. It never writes guest camera state or advances time.
class History {
    Matrix current{}, projection{}, camera_delta{};
    std::array<double,2> midpoint_lens_scale{1,1};
    std::array<std::array<double,4>,2> midpoint_lens_offset{};
    Matrix Project(const Matrix& p) const {
        auto midpoint=p;
        for(unsigned i=0;i<2;++i)for(unsigned j=0;j<4;++j)
            midpoint[i][j]=midpoint[i][j]*midpoint_lens_scale[i]+midpoint_lens_offset[i][j];
        return Multiply(midpoint,camera_delta);
    }
    std::uint64_t frame{};
    std::int64_t time{};
    bool seen{}, ready{};
public:
    void Reset() { seen=ready=false; }
    // Reacquisition must not blank a valid native scene. Never interpolate when
    // the CPU camera is unavailable/mismatched, or across cuts/projection changes.
    Matrix Preview(std::uint64_t next_frame,std::int64_t now,const Matrix& camera,
                   const Matrix& p,bool cpu_matches,bool comparison,bool& fallback) {
        fallback=true;
        // A rejected draw must not erase the validated world camera from this
        // frame (e.g. auxiliary title geometry). It still renders natively and
        // never advances history. If a whole frame lacks a matching camera,
        // Observe's consecutive-frame guard forces fresh acquisition afterward.
        if(!cpu_matches) { ++camera_cpu_mismatch;return p; }
        const auto result=Observe(next_frame,now,camera,p);
        if(comparison || !result)return p;
        fallback=false;
        return *result;
    }

    std::optional<Matrix> Observe(std::uint64_t next_frame,std::int64_t now,
                                  const Matrix& camera,const Matrix& p) {
        if(!Rigid(camera)||!Near(p,p,0)) { Reset();return {}; }
        if(seen && next_frame==frame) {
            if(!Near(current,camera,.001))return {};
            if(SameLens(projection,p))
                return ready?std::optional<Matrix>(Project(p)):std::nullopt;
            if(ready && AlternateWorldLens(projection,p))return Multiply(p,camera_delta);
            return {};
        }
        const bool frame_ok=seen && next_frame==frame+1 && now>=time && now-time<=200;
        const auto lens_scale=GradualLensScale(projection,p);
        const auto wobble=GradualWorldWobble(projection,p);
        const bool time_skip=GradualTimeSkipDistortion(projection,p);
        const bool lens_ok=SameLens(projection,p) || lens_scale.has_value() || wobble.has_value() || time_skip;
        if(!frame_ok)++camera_gap;
        else if(!lens_ok)++camera_lens_change;
        bool continuous=frame_ok && lens_ok;
        if(continuous) {
            double distance=0,trace=0;
            for(unsigned i=0;i<3;++i) {
                distance+=(current[i][3]-camera[i][3])*(current[i][3]-camera[i][3]);
                for(unsigned j=0;j<3;++j)trace+=current[i][j]*camera[i][j];
            }
            continuous=distance<=250*250 && (trace-1)/2>=std::cos(45.*3.141592653589793/180.);
            if(!continuous)++camera_cut;
        }
        ready=continuous;
        if(ready) {
            // Original vertices are in current view space. Change only the host
            // projection: P * V_mid * C_current * view_vertex.
            camera_delta=Multiply(InverseRigid(Midpoint(current,camera)),camera);
            for(unsigned r=0;r<2;++r) {
                midpoint_lens_scale[r]=time_skip?1.:lens_scale?(1.+1. / *lens_scale)*.5:
                    wobble?(1.+1. / (*wobble)[r])*.5:1.;
                for(unsigned c=0;c<4;++c)
                    midpoint_lens_offset[r][c]=time_skip?(projection[r][c]-p[r][c])*.5:0.;
            }
        }
        current=camera;projection=p;frame=next_frame;time=now;seen=true;
        return ready?std::optional<Matrix>(Project(p)):std::nullopt;
    }
};
}
