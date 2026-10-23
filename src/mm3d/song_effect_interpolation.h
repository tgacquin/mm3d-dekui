// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include "mm3d/great_bay_water_interpolation.h"
#include "mm3d/coastal_water_interpolation.h"
namespace MM3D::SongEffectInterpolation {
inline constexpr std::array<unsigned,6> Rows{10,11,14,15,17,18};
using Values=std::array<std::array<float,4>,6>;
using Key=std::array<std::uint32_t,7>;
struct MoonPattern {
    unsigned vertices,texture_mask,phase_row;
    std::array<float,6> basis,phase_scale;
    std::array<float,6> offset{};
    float step=-1.f/160;
    unsigned equation=0;
    bool additive=false;
};
// moon_fall_model and moon_start_anm: verified scrolling layers with a shared
// phase per model. Validate every active UV row; inactive rows are stale state.
inline const MoonPattern* MoonEffect(unsigned vertices,const Values& v,unsigned texture_mask) {
    static constexpr MoonPattern patterns[]{
        {720,7,0,{5,1,10,6,-8,2},{1,0,0,3,0,2}},
        {288,7,3,{6,1,16,4,-24,7},{0,0,0,2,0,7}},
        {336,3,1,{1,2,-2,3,0,0},{0,1,0,3,0,0}},
        {1440,7,2,{1,1,4,3,2,2},{0,1,2,3,0,0},{0,-.1f,0,-.3f,0,0},-1.f/120,0,true},
        {1440,7,2,{1,1,2,3,2,2},{0,1,2,6,0,0},{},-1.f/120,2,true},
    };
    for(const auto& p:patterns) {
        if(vertices!=p.vertices || texture_mask!=p.texture_mask)continue;
        const float phase=v[p.phase_row][3]/p.phase_scale[p.phase_row];
        if(!std::isfinite(phase) || phase < -1 || phase > 0)continue;
        bool match=true;
        for(unsigned r=0;r<(texture_mask==7?6u:4u);++r)for(unsigned c=0;c<4;++c) {
            const float expected=c==3?phase*p.phase_scale[r]+p.offset[r]:(c==r%2?p.basis[r]:0.f);
            if(!std::isfinite(v[r][c]) || std::abs(v[r][c]-expected)>1e-5f)match=false;
        }
        if(match)return &p;
    }
    return nullptr;
}
inline std::optional<std::array<float,2>> EponaSingleTexture(unsigned vertices,const Values& v,unsigned mask) {
    if(mask!=1 || (vertices!=90 && vertices!=144))return {};
    const std::array<float,2> basis=vertices==90?std::array{8.f,4.f}:std::array{1.f,5.f};
    for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c)
        if(!std::isfinite(v[r][c]) || (c<3 && std::abs(v[r][c]-(c==r?basis[r]:0.f))>1e-5f))return {};
    if(vertices==90 && std::abs(v[0][3]-2*v[1][3])>1e-5f)return {};
    if(vertices==144 && std::abs(v[0][3])>1e-5f)return {};
    return vertices==90?std::array{-4.f/75,-2.f/75}:std::array{0.f,-1.f/30};
}
struct WaterfallPattern {
    unsigned vertices,texture_mask;
    bool additive;
    std::array<float,4> step;
};
// Captured Woodfall sheets, foam, runoff and pool surfaces. Room variants
// have different mesh sizes but share scroll speeds. Single-texture sheets
// leave the second UV basis stale; only validate their active coordinates.
inline const WaterfallPattern* WoodfallWater(unsigned vertices,const Values& v,unsigned texture_mask=0) {
    static constexpr WaterfallPattern patterns[]{
        {234,3,true,{-1.f/480,1.f/48,1.f/480,1.f/80}},
        {144,1,true,{0,1.f/30,0,0}},
        {120,3,true,{0,7.f/480,-1.f/240,1.f/80}},
        {186,1,true,{0,1.f/60,0,0}},
        {138,3,false,{0,-1.f/120,-1.f/120,-1.f/120}},
        {372,1,true,{0,1.f/60,0,0}},
        {156,1,true,{0,1.f/30,0,0}},
        {174,3,true,{0,7.f/480,-1.f/240,1.f/80}},
        {150,1,true,{0,1.f/30,0,0}},
        {114,3,true,{0,7.f/480,-1.f/240,1.f/80}},
        {168,1,true,{0,-1.f/120,0,0}},
        {621,3,true,{1.f/200,1.f/200,-1.f/800,1.f/600}},
        {519,3,true,{1.f/600,1.f/400,-1.f/800,1.f/600}},
        {942,3,true,{1.f/150,0,-1.f/100,-7.f/1200}},
        {1191,3,true,{1.f/150,0,-1.f/100,-7.f/1200}},
        {480,1,false,{1.f/1200,0,0,0}},
    };
    for(const auto& pattern:patterns) {
        if(vertices!=pattern.vertices)continue;
        // The Great Fairy fountain also has a 480-vertex water layer, but
        // uses two textures. Require explicit material context for this one.
        if(vertices==480 && texture_mask!=1)return nullptr;
        std::array<std::array<float,3>,4> basis{{{1,0,0},{0,1,0},{1,0,0},{0,1,0}}};
        if(vertices==138) {
            basis[2]={1.9922411442f,-.1742986441f,0};basis[3]={.1742986441f,1.9922411442f,0};
        } else if(vertices==621) {
            basis[0][0]=3;basis[1][1]=2;
        } else if(vertices==942) {
            basis={{{-1.9998657703f,-3.4638698101f,0},{3.4638698101f,-1.9998657703f,0},
                    {0,-8,0},{7,0,0}}};
        } else if(vertices==1191) {
            basis={{{3.7585752010f,-1.3680083752f,0},{1.3680083752f,3.7585752010f,0},
                    {8,0,0},{0,7,0}}};
        }
        const unsigned rows=pattern.texture_mask==1?2:4;
        for(unsigned r=0;r<rows;++r)for(unsigned c=0;c<4;++c)
            if(!std::isfinite(v[r][c]) || (c<3 && std::abs(v[r][c]-basis[r][c])>1e-5f))return nullptr;
        return &pattern;
    }
    return nullptr;
}
// Boss entrance's green column: three UV bases, two scrolling layers. The
// second moves twice as fast as the third and both wrap together.
inline bool BossEntrance(unsigned vertices,const Values& v) {
    if(vertices!=240)return false;
    constexpr float basis[]{12,1,16,4,5,1};
    for(unsigned r=0;r<6;++r)for(unsigned c=0;c<4;++c) {
        if(!std::isfinite(v[r][c]))return false;
        if(c==3 && (r==3 || r==5))continue;
        if(std::abs(v[r][c]-(c==r%2?basis[r]:0.f))>1e-5f)return false;
    }
    return v[5][3]>=-1 && v[5][3]<=0 && std::abs(v[3][3]-2*v[5][3])<1e-5f;
}
// Captured perspective song layers: vertex count plus complete fixed UV basis.
inline bool Recognized(unsigned vertices,const Values& v,unsigned texture_mask=0) {
    if(MoonEffect(vertices,v,texture_mask))return true;
    if(EponaSingleTexture(vertices,v,texture_mask))return true;
    if(WoodfallWater(vertices,v,texture_mask))return true;
    if(BossEntrance(vertices,v))return true;
    struct Pattern { unsigned vertices; std::array<float,4> basis; };
    constexpr Pattern patterns[]{
        {90,{3,2,6,4}}, {90,{4,1,-8,2}}, // Time
        {90,{6,3,10,-4}}, {288,{2,4,3,6}}, // Healing
        {90,{4,3,-8,-4}}, {288,{1,2,-2,6}}, // Soaring
        {360,{2,4,-5,-8}}, {360,{1,3,3,4}}, // Storms
        {90,{8,1,6,-3}}, {480,{3,1,6,-2}}, // Temple song family
    };
    for(const auto& p:patterns) {
        if(vertices!=p.vertices)continue;
        bool match=true;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c) {
            const float expected=c==r%2?p.basis[r]:0;
            match &= std::isfinite(v[r][c]) && std::abs(v[r][c]-expected)<1e-5f;
        }
        if(match)return true;
    }
    // Captured waterfall sheets use the same scrolling-UV shader as song
    // effects, but are ordinary room draws rather than song skeleton draws.
    struct WaterPattern { unsigned vertices; std::array<std::array<float,3>,4> basis; };
    constexpr WaterPattern water_patterns[]{
        {276,{{{1,0,0},{0,1,0},{2.9883618355f,.2614479661f,0},{-.2614479661f,2.9883618355f,0}}}},
        {237,{{{1,0,0},{0,1,0},{2.9883618355f,.2614479661f,0},{-.2614479661f,2.9883618355f,0}}}},
        {282,{{{1,0,0},{0,1.5f,0},{.9961205721f,-.0871493220f,0},{.3485972881f,3.9844822884f,0}}}},
        {270,{{{1,0,0},{0,1.5f,0},{.9961205721f,-.0871493220f,0},{.3485972881f,3.9844822884f,0}}}},
        {423,{{{1.5f,0,0},{0,1,0},{2.8975834846f,-.7764056921f,0},{.5176038146f,1.9317222834f,0}}}},
        {549,{{{2,0,0},{0,2,0},{1,0,0},{0,1,0}}}},
        // Great Fairy fountain water layers, including the 924-vertex sheet.
        // All three use independently scrolling texture offsets.
        {924,{{{1,0,0},{0,2,0},{3,0,0},{0,2,0}}}},
        {360,{{{3,0,0},{0,3,0},{3.9844822884f,-.3485972881f,0},
               {.4357466102f,4.9806027412f,0}}}},
        {480,{{{1,0,0},{0,1,0},{1,0,0},{0,2,0}}}},
    };
    for(const auto& p:water_patterns) {
        if(vertices!=p.vertices)continue;
        bool match=true;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)
            match &= std::isfinite(v[r][c]) && std::abs(v[r][c]-p.basis[r][c])<1e-5f;
        if(match)return true;
    }
    // Epona and the shared background use a fixed rotated second UV basis.
    for(const auto first:std::array<std::array<float,3>,3>{{{90,1,1},{90,8,4},{144,1,5}}}) {
        if(vertices!=unsigned(first[0]))continue;
        const Values basis{{{first[1],0,0,0},{0,first[2],0,0},
            {4.6982188225f,1.7100105286f,0,0},{-1.7100105286f,4.6982188225f,0,0}}};
        bool match=true;
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)
            match &= std::isfinite(v[r][c]) && std::abs(v[r][c]-basis[r][c])<1e-5f;
        if(match)return true;
    }
    return false;
}
class History {
    struct Entry { std::uint64_t frame{}; Values value{}; std::optional<Values> half; };
    std::map<Key,Entry> entries;
public:
    void Reset() { entries.clear(); }
    std::optional<Values> Apply(std::uint64_t frame,const Key& key,const Values& value,bool allow,unsigned texture_mask=0,
                                std::optional<unsigned> coastal={}) {
        for(auto it=entries.begin();it!=entries.end();)
            if(it->second.frame>frame || frame-it->second.frame>2)it=entries.erase(it);else ++it;
        const auto epona=EponaSingleTexture(key[5],value,texture_mask);
        const auto* moon=MoonEffect(key[5],value,texture_mask);
        const auto bay=GreatBayWaterInterpolation::Identify(key[5],texture_mask,value);
        // The renderer supplies this only after the complete material matches.
        if(coastal && (*coastal>=std::size(CoastalWaterInterpolation::Patterns) ||
           CoastalWaterInterpolation::Patterns[*coastal].vertices!=key[5] ||
           CoastalWaterInterpolation::Patterns[*coastal].mask!=texture_mask ||
           !CoastalWaterInterpolation::MatchesUV(*coastal,value)))return {};
        for(unsigned r=0;r<(coastal?CoastalWaterInterpolation::Rows(*coastal):bay?GreatBayWaterInterpolation::Rows(*bay):epona?2u:6u);++r)
            for(float v:value[r])if(!std::isfinite(v))return {};
        auto it=entries.find(key);
        if(it==entries.end()) {
            // Adjacent Woodfall rooms can submit over sixteen water materials
            // together. Keep their independent histories bounded without
            // starving the last sheets drawn in the frame.
            if(entries.size()<32)entries.emplace(key,Entry{frame,value,{}});
            return {};
        }
        auto& e=it->second;
        if(bay || coastal) {
            const unsigned rows=coastal?CoastalWaterInterpolation::Rows(*coastal):GreatBayWaterInterpolation::Rows(*bay);
            if(e.frame==frame) {
                if(!allow || !e.half)return {};
                auto half=value;
                for(unsigned r=0;r<rows;++r) {
                    if(value[r]!=e.value[r])return {};
                    if(coastal && CoastalWaterInterpolation::Patterns[*coastal].rotating)
                        half[r]=(*e.half)[r];
                    else half[r][3]=(*e.half)[r][3];
                }
                return half;
            }
            const auto half=frame==e.frame+1?
                (coastal?CoastalWaterInterpolation::Midpoint(*coastal,e.value,value):
                 GreatBayWaterInterpolation::Midpoint(*bay,e.value,value)):std::nullopt;
            e={frame,value,half};
            return allow?e.half:std::nullopt;
        }
        if(moon) {
            const unsigned rows=texture_mask==7?6:4;
            auto half=value;
            if(e.frame==frame) {
                if(!allow || !e.half)return {};
                for(unsigned r=0;r<rows;++r)if(value[r]!=e.value[r])return {};
                for(unsigned r=0;r<rows;++r)half[r][3]=(*e.half)[r][3];
                return half;
            }
            const float phase=value[moon->phase_row][3]/moon->phase_scale[moon->phase_row];
            const float old=e.value[moon->phase_row][3]/moon->phase_scale[moon->phase_row];
            const float delta=phase-old,step=delta-std::round(delta);
            const bool valid=frame==e.frame+1 && MoonEffect(key[5],e.value,texture_mask)==moon &&
                (std::abs(step)<1e-5f || std::abs(step-moon->step)<1e-5f);
            float midpoint=phase-step*.5f;
            if(midpoint>0)midpoint-=1;
            if(midpoint<-1)midpoint+=1;
            for(unsigned r=0;r<rows;++r)half[r][3]=midpoint*moon->phase_scale[r]+moon->offset[r];
            e={frame,value,valid?std::optional<Values>{half}:std::nullopt};
            return allow?e.half:std::nullopt;
        }
        if(epona) {
            // These Epona layers enable only texture zero. The other UV
            // matrices contain leftovers from the instrument/background draw.
            // They neither identify the layer nor participate in its midpoint.
            auto half=value;
            if(e.frame==frame) {
                if(!allow || !e.half || value[0]!=e.value[0] || value[1]!=e.value[1])return {};
                for(unsigned r=0;r<2;++r)half[r][3]=(*e.half)[r][3];
                return half;
            }
            bool moving=true,stationary=true;
            for(unsigned r=0;r<2;++r) {
                const float delta=value[r][3]-e.value[r][3];
                moving &= std::abs(delta-(*epona)[r])<1e-5f;
                stationary &= std::abs(delta)<1e-6f;
                half[r][3]=(value[r][3]+e.value[r][3])*.5f;
            }
            const bool valid=frame==e.frame+1 && EponaSingleTexture(key[5],e.value,texture_mask) && (moving || stationary);
            e={frame,value,valid?std::optional<Values>{half}:std::nullopt};
            return allow?e.half:std::nullopt;
        }
        if(e.frame==frame)return allow && e.value==value ? e.half : std::nullopt;
        bool valid=frame==e.frame+1;
        auto half=value;
        if(const auto* water=WoodfallWater(key[5],value,texture_mask)) {
            valid &= WoodfallWater(key[5],e.value,texture_mask)!=nullptr;
            bool moving=true,stationary=true;
            const unsigned rows=water->texture_mask==1?2:4;
            for(unsigned r=0;r<rows;++r) {
                const float delta=value[r][3]-e.value[r][3];
                moving &= std::abs(delta-water->step[r])<1e-5f;
                stationary &= std::abs(delta)<1e-6f;
                half[r][3]=(value[r][3]+e.value[r][3])*.5f;
            }
            valid &= moving || stationary;
            e={frame,value,valid?std::optional<Values>{half}:std::nullopt};
            return allow?e.half:std::nullopt;
        }
        if(BossEntrance(key[5],value)) {
            const float delta=value[5][3]-e.value[5][3];
            const float step=delta-std::round(delta);
            valid &= BossEntrance(key[5],e.value) &&
                (std::abs(step)<1e-5f || std::abs(step+1.f/30)<1e-5f);
            float phase=value[5][3]-step*.5f;
            if(phase>0)phase-=1;
            if(phase<-1)phase+=1;
            half[3][3]=2*phase;half[5][3]=phase;
            e={frame,value,valid?std::optional<Values>{half}:std::nullopt};
            return allow?e.half:std::nullopt;
        }
        for(unsigned row=0;row<4;++row) {
            for(unsigned c=0;c<3;++c)valid &= std::abs(value[row][c]-e.value[row][c])<1e-5f;
            // Captured offsets are unwrapped; jumps/reset/wraps keep the native value.
            valid &= std::abs(value[row][3]-e.value[row][3])<.25f;
            half[row][3]=(value[row][3]+e.value[row][3])*.5f;
        }
        e={frame,value,valid?std::optional<Values>{half}:std::nullopt};
        return allow?e.half:std::nullopt;
    }
};
}
