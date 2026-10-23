// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mm3d/link_pose_capture.h"
#include <unordered_map>
#include <cstring>
#include <limits>
#include <span>
namespace MM3D::LinkInterpolation {
using namespace LinkPoseMath;
using Palette=std::array<Matrix,10>;
using Overrides=std::array<std::optional<Matrix>,10>;
inline constexpr std::array<unsigned,5> PipeBones{2,3,5,6,4};
// Local actor/archive correlation identifies the five-bone `chat` fairy model.
// Its two glow quads share the root position, but use a scaled view-facing basis.
inline bool FairyGlow(const Matrix& matrix,const LinkPoseCapture::LivePose& pose,
                      unsigned vertices) {
    if(vertices!=4 || pose.signature!=0x09e2df0cf6765f8eull ||
       pose.pose.bones.size()!=5 || !Near(matrix,matrix,0))return false;
    const double scale=matrix[0][0];
    if(scale<=0)return false;
    for(unsigned row=0;row<3;++row) {
        if(std::abs(matrix[row][3]-pose.pose.bones[0][row*4+3])>.03)return false;
        for(unsigned col=0;col<3;++col)
            if(std::abs(matrix[row][col]-(row==col?scale:0))>scale*.0002)return false;
    }
    return true;
}
// Human Link's bowstring collapses its Y scale at rest, stretches when drawn,
// and crosses zero during release. Reconstruct orientation from X/Z and blend
// its signed Y scale separately. Generic skeleton checks remain unchanged.
inline bool HumanSkeleton(const LinkPoseCapture::LivePose& pose) {
    return pose.pose.bones.size()==26 && pose.bind.size()==26 &&
        pose.words.size()==LinkPoseCapture::BoneWords.size() &&
        std::equal(pose.words.begin(),pose.words.end(),LinkPoseCapture::BoneWords.begin());
}
inline std::optional<Matrix> BowStringBasis(const Matrix& matrix) {
    if(!Near(matrix,matrix,0))return {};
    auto result=matrix;std::array<double,3> x{},z{};double xx=0,zz=0,dot=0;
    for(unsigned r=0;r<3;++r) {
        x[r]=matrix[r][0];z[r]=matrix[r][2];xx+=x[r]*x[r];zz+=z[r]*z[r];dot+=x[r]*z[r];
    }
    if(xx<1e-10 || zz<1e-10 || std::abs(dot)>.002*std::sqrt(xx*zz))return {};
    for(unsigned r=0;r<3;++r){x[r]/=std::sqrt(xx);z[r]/=std::sqrt(zz);}
    const std::array<double,3> y{z[1]*x[2]-z[2]*x[1],z[2]*x[0]-z[0]*x[2],z[0]*x[1]-z[1]*x[0]};
    double signed_y=0;
    for(unsigned r=0;r<3;++r)signed_y+=matrix[r][1]*y[r];
    if(std::abs(signed_y)>1.01*std::sqrt(xx))return {};
    for(unsigned r=0;r<3;++r)
        if(std::abs(matrix[r][1]-signed_y*y[r])>.002*std::sqrt(xx))return {};
    for(unsigned r=0;r<3;++r)result[r][1]=y[r]*std::sqrt(xx);
    return result;
}
inline std::optional<Matrix> BowStringHalf(const Matrix& a,const Matrix& b) {
    const auto aa=BowStringBasis(a),bb=BowStringBasis(b);
    if(!aa || !bb)return {};
    auto half=Half(*aa,*bb);if(!half)return {};
    const auto signed_length=[](const Matrix& value,const Matrix& basis) {
        double dot=0,length=0;
        for(unsigned r=0;r<3;++r){dot+=value[r][1]*basis[r][1];length+=basis[r][1]*basis[r][1];}
        return dot/std::sqrt(length);
    };
    double length=0;
    for(unsigned r=0;r<3;++r)length+=(*half)[r][1]*(*half)[r][1];
    const double scale=(signed_length(a,*aa)+signed_length(b,*bb))/(2*std::sqrt(length));
    for(unsigned r=0;r<3;++r)(*half)[r][1]*=scale;
    return half;
}
inline bool BowStringBone(const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    return vertices==12 && HumanSkeleton(pose) && BowStringBasis(palette[0]) &&
        PaletteNear(palette[0],Expand(pose.pose.bones[24]));
}
inline bool BunnyHoodBones(const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=330 || pose.signature!=0x4ce0cfdeaf2fb04eull || pose.pose.bones.size()!=3 ||
       pose.bind.size()!=3 || pose.words!=std::vector<std::uint32_t>{0xffff0000,0x1001,0x11002})return false;
    if(!PaletteNear(palette[0],Expand(pose.pose.bones[0])))return false;
    for(unsigned slot=1;slot<=2;++slot)
        if(!PaletteNear(palette[slot],Multiply(Expand(pose.pose.bones[slot]),Expand(pose.bind[slot]))))return false;
    return true;
}
inline Matrix BunnyHeadOffset() {
    auto offset=Identity();const double s=std::sqrt(.5);
    offset[0]={-s,s,0,0};offset[1]={-s,-s,0,110};return offset;
}
struct Match { unsigned bone{}; bool skin{}; bool operator==(const Match&)const=default; };
inline std::optional<Match> Identify(const Matrix& matrix,const LinkPoseCapture::LivePose& pose) {
    if(!Inverse(matrix))return {};
    std::optional<Match> match;
    for(unsigned bone=0;bone<pose.pose.bones.size();++bone) {
        const auto direct=Expand(pose.pose.bones[bone]);
        for(bool skin : {false,true}) {
            const auto candidate=skin?Multiply(direct,Expand(pose.bind[bone])):direct;
            if(!PaletteNear(matrix,candidate))continue;
            if(match) {
                // Identity binds don't create two different bone identities.
                if(match->bone==bone && Near(Expand(pose.bind[bone]),Identity(),1e-5))continue;
                return {};
            }
            match=Match{bone,skin};
        }
    }
    return match;
}
// Precompute current direct/skin candidates once per pose instead of multiplying
// every bone by its bind for every material and unrelated scene draw. Snapshots
// also detect same-frame pose/bind changes; this cache carries no animation state.
class BoneCandidates {
    std::vector<LinkPoseCapture::Matrix> bones,bind;
    std::vector<std::array<Matrix,2>> candidates;
    std::vector<bool> identity_bind;
    std::array<double,3> low{},high{};
public:
    void Prepare(const LinkPoseCapture::LivePose& pose) {
        const auto same=[](const auto& a,const auto& b) {
            return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(a[0]))==0);
        };
        if(same(bones,pose.pose.bones) && same(bind,pose.bind))return;
        bones=pose.pose.bones;bind=pose.bind;
        candidates.clear();identity_bind.clear();
        low.fill(std::numeric_limits<double>::infinity());
        high.fill(-std::numeric_limits<double>::infinity());
        if(bones.size()!=bind.size())return;
        candidates.reserve(bones.size());identity_bind.reserve(bones.size());
        for(unsigned i=0;i<bones.size();++i) {
            const auto direct=Expand(bones[i]),binding=Expand(bind[i]);
            candidates.push_back({direct,Multiply(direct,binding)});
            for(const auto& candidate:candidates.back())for(unsigned axis=0;axis<3;++axis) {
                const auto value=candidate[axis][3];
                if(std::isfinite(value)) {low[axis]=std::min(low[axis],value);high[axis]=std::max(high[axis],value);}
            }
            identity_bind.push_back(Near(binding,Identity(),1e-5));
        }
    }
    std::optional<Match> Identify(const Matrix& matrix) const {
        // Conservative bounds over every direct and skin candidate. Use the
        // original subtraction/tolerance so boundary rounding cannot reject a
        // match that the reference matcher accepts.
        for(unsigned axis=0;axis<3;++axis) {
            const auto value=matrix[axis][3];
            if(!std::isfinite(value) || low[axis]-value>.03 || value-high[axis]>.03)return {};
        }
        if(!Inverse(matrix))return {};
        std::optional<Match> match;
        for(unsigned bone=0;bone<candidates.size();++bone)for(bool skin:{false,true}) {
            const auto& candidate=candidates[bone][skin];
            // Most draws belong to other actors. Reject distant translations
            // before touching the basis, using the same positional tolerance.
            if(std::abs(matrix[0][3]-candidate[0][3])>.03 ||
               std::abs(matrix[1][3]-candidate[1][3])>.03 ||
               std::abs(matrix[2][3]-candidate[2][3])>.03 ||
               !PaletteNear(matrix,candidate))continue;
            if(match) {
                if(match->bone==bone && identity_bind[bone])continue;
                return {};
            }
            match=Match{bone,skin};
        }
        return match;
    }
};
// Once the actor is uniquely known, equivalent skin bones may name the same
// palette transform. Accept them only if their interpolated results also agree.
inline std::optional<Matrix> ResolvePalette(const Matrix& matrix,
        const LinkPoseCapture::LivePose& pose,const std::vector<Matrix>& half,const Matrix& view) {
    if(!Inverse(matrix) || half.size()!=pose.pose.bones.size())return {};
    std::optional<Matrix> result;
    for(unsigned bone=0;bone<pose.pose.bones.size();++bone)for(bool skin:{false,true}) {
        auto candidate=Expand(pose.pose.bones[bone]);
        if(skin)candidate=Multiply(candidate,Expand(pose.bind[bone]));
        if(!PaletteNear(matrix,candidate))continue;
        auto value=Multiply(view,half[bone]);
        if(skin)value=Multiply(value,Expand(pose.bind[bone]));
        if(!Near(value,value,0) || (result && !PaletteNear(*result,value)))return {};
        result=value;
    }
    return result;
}
// seaboss_base_01: the 528-vertex water surface uses direct bone 1.
// It coincides with the stationary arena root while resting, so a generic
// matrix-only lookup cannot establish ownership before the landing wobble.
inline bool GyorgWaterBone(const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    return vertices==528 && pose.signature==0x265c0b54e456fdffull &&
        pose.pose.bones.size()==2 && pose.bind.size()==2 &&
        pose.words==std::vector<std::uint32_t>{0xffff0000,1} &&
        PaletteNear(palette[0],Expand(pose.pose.bones[1]));
}
// Sliding doors use direct bone 0. These locally audited shutter models have
// tiny bind translations which make generic direct/skin lookup ambiguous.
// Preserve the direct transform and require the specific bind for each family.
inline bool DungeonShutterBone(const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    const auto signature=pose.signature;
    const bool standard=signature==0x57306c6ee1db06efull || signature==0x0425b0994521dcd5ull ||
        signature==0xe3b575dedeb5ae54ull || signature==0x3d9d08ddcad1f38eull ||
        signature==0x1ee17d0b154d34f6ull || signature==0xb86510278af23ef2ull;
    const bool fortress=signature==0xc876669185553f9bull;
    auto bind=Identity();bind[2][3]=fortress?.0012207040563225746:-.0006294240010902286;
    return ((standard && vertices==36) || (fortress && (vertices==84 || vertices==264))) &&
        pose.pose.bones.size()==1 && pose.bind.size()==1 &&
        pose.words==std::vector<std::uint32_t>{0xffff0000} &&
        Near(Expand(pose.bind[0]),bind,1e-7) &&
        PaletteNear(palette[0],Expand(pose.pose.bones[0]));
}
// Snowhead's broken snow-boulder halves can share their transform with a
// shadow actor. The 210-vertex snow half belongs to this verified root model.
inline bool SnowBoulderBone(const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    return vertices==210 && pose.signature==0xd67c27ec27487acbull &&
        pose.pose.bones.size()==1 && pose.bind.size()==1 &&
        pose.words==std::vector<std::uint32_t>{0xffff0000} &&
        Near(Expand(pose.bind[0]),Identity(),1e-5) &&
        PaletteNear(palette[0],Expand(pose.pose.bones[0]));
}
// The captured wing_warp model's long bone chains accumulate small differences
// between reconstructed binds and guest skin matrices. Identify the complete
// left/right wing palette, not an arbitrary bone with a wider global tolerance.
inline std::optional<std::array<unsigned,10>> SoaringWingBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=864 || pose.signature!=0xf4c419547d6f24a2ull ||
       pose.pose.bones.size()!=42 || pose.bind.size()!=42 || pose.words.size()!=42)return {};
    constexpr std::array<std::array<unsigned,10>,2> wings{{
        {27,28,29,30,31,26,24,25,32,33},
        {9,10,11,12,13,6,8,7,14,15}}};
    std::optional<std::array<unsigned,10>> found;
    for(const auto& wing:wings) {
        bool matches=true;
        for(unsigned slot=0;slot<wing.size() && matches;++slot) {
            const auto skin=Multiply(Expand(pose.pose.bones[wing[slot]]),Expand(pose.bind[wing[slot]]));
            if(!Inverse(skin) || !Inverse(palette[slot])) {matches=false;break;}
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
                matches &= std::isfinite(palette[slot][r][c]) && std::isfinite(skin[r][c]) &&
                    std::abs(palette[slot][r][c]-skin[r][c])<=(c==3?.1:.0001);
        }
        if(matches) {if(found)return {};found=wing;}
    }
    return found;
}
// Dodongo's captured body/arm palettes differ from reconstructed skin matrices
// by up to 1.54e-5 in their basis. Validate the whole known palette, preserving
// that small native bind correction rather than widening generic matching.
inline std::optional<std::vector<unsigned>> DodongoBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(pose.signature!=0x529d17c9d3dc4acbull || pose.pose.bones.size()!=18 ||
       pose.bind.size()!=18 || pose.words.size()!=18 || (vertices!=756 && vertices!=408))return {};
    const std::vector<unsigned> bones=vertices==756?std::vector<unsigned>{4,2,11,5,12,8,15,9}:
        std::vector<unsigned>{13,6,10};
    for(unsigned slot=0;slot<bones.size();++slot) {
        const auto bone=bones[slot];
        const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
        if(!Inverse(skin) || !Inverse(palette[slot]))return {};
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
            if(!std::isfinite(palette[slot][r][c]) || !std::isfinite(skin[r][c]) ||
               std::abs(palette[slot][r][c]-skin[r][c])>(c==3?.03:.00002))return {};
    }
    return bones;
}
// Goht's active skin palettes, verified against the local model and three
// captures. Reconstructed binds differ by up to .05735 units / 1.009e-5.
// Match the complete mesh palette and preserve its native bind correction.
inline std::optional<std::span<const unsigned>> GohtSkinBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(pose.signature!=0x496431680f850b7full || pose.pose.bones.size()!=29 ||
       pose.bind.size()!=29 || pose.words.size()!=29)return {};
    struct Mesh {unsigned vertices,count;std::array<unsigned,10> bones;};
    static constexpr Mesh meshes[]{
        {129,2,{1,3}}, {756,3,{1,3,12}}, {1878,7,{1,5,9,23,3,12,27}},
        {150,2,{3,12}}, {2100,7,{7,6,11,10,5,3,9}},
        {2496,10,{19,1,25,22,21,26,28,27,23,20}}, {219,3,{1,3,12}},
        {348,3,{12,14,3}}, {606,2,{12,14}}, {96,2,{12,14}}, {750,3,{1,15,3}}};
    for(const auto& mesh:meshes) {
        if(mesh.vertices!=vertices)continue;
        for(unsigned slot=0;slot<mesh.count;++slot) {
            const auto bone=mesh.bones[slot];
            const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
            // Bone 15 is deliberately collapsed in these captures. Validate
            // its zero basis, but do not revive that hidden geometry.
            bool collapsed=bone==15;
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                collapsed &= skin[r][c]==0 && palette[slot][r][c]==0;
            if(!collapsed && (!Inverse(skin) || !Inverse(palette[slot])))return {};
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
                if(!std::isfinite(palette[slot][r][c]) || !std::isfinite(skin[r][c]) ||
                   std::abs(palette[slot][r][c]-skin[r][c])>(c==3?.06:.000012))return {};
        }
        return std::span<const unsigned>(mesh.bones.data(),mesh.count);
    }
    return {};
}
// These two verified NPC meshes start with coincident skin bones, so the
// generic slot-zero owner check cannot choose a bone. The asset's entire active
// palette establishes identity without relaxing any matrix tolerance.
inline std::optional<std::span<const unsigned>> NpcSkinBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    static constexpr std::array<unsigned,2> tingle{1,3};
    static constexpr std::array<unsigned,5> aroma{15,16,24,10,11};
    std::span<const unsigned> bones;
    unsigned count=0;
    if(vertices==30 && pose.signature==0x2215c979d3103929ull) {bones=tingle;count=22;}
    else if(vertices==144 && pose.signature==0x7900d5513a1e4e5dull) {bones=aroma;count=26;}
    else return {};
    if(pose.pose.bones.size()!=count || pose.bind.size()!=count || pose.words.size()!=count)return {};
    for(unsigned slot=0;slot<bones.size();++slot) {
        const auto bone=bones[slot];
        const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
        if(!Inverse(skin) || !Inverse(palette[slot]) || !PaletteNear(palette[slot],skin))return {};
    }
    return bones;
}
inline constexpr std::array<std::uint32_t,32> TwinmoldTopology{
    0xffff0000,0x1001,0x11002,0x21003,0x31004,0x21005,0x51006,0x21007,
    0x71008,0x81009,0x9100a,0x2100b,0xb100c,0xc100d,0xd100e,0x2100f,
    0x21010,0x21011,0x111012,0x11013,0x131014,0x141015,0x151016,0x161017,
    0x171018,0x181019,0x19101a,0x1a101b,0x1b101c,0x1c101d,0x1d101e,0x1e101f};
inline bool TwinmoldSkeleton(const LinkPoseCapture::LivePose& pose) {
    if(pose.signature!=0x323a7bd77f6edea5ull || pose.pose.bones.size()!=32 ||
       pose.bind.size()!=32 || pose.words.size()!=32 || pose.words[0]!=0xffff0000 ||
       pose.words[19]!=0x00011013)return false;
    return std::equal(pose.words.begin(),pose.words.end(),TwinmoldTopology.begin());
}
// Eyegore's rigid meshes share coincident skin transforms at rest. Use the
// complete native palettes, verified in zelda2_eg, to preserve their ownership.
inline std::optional<std::span<const unsigned>> EyegoreBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    static constexpr std::array<std::uint32_t,22> topology{
        0xffff0000,0x1001,0x11002,0x21003,0x21004,0x41005,0x21006,0x21007,
        0x21008,0x81009,0x8100a,0x2100b,0xb100c,0xb100d,0x100e,0xe100f,
        0xf1010,0x101011,0xe1012,0x121013,0x131014,0xe1015};
    if(pose.signature!=0x2162c5636174fb2cull || pose.pose.bones.size()!=22 ||
       pose.bind.size()!=22 || pose.words.size()!=22 ||
       !std::equal(pose.words.begin(),pose.words.end(),topology.begin()))return {};
    struct Mesh {unsigned vertices,count;std::array<unsigned,7> bones;};
    static constexpr Mesh meshes[]{
        {1050,6,{2,6,12,9,13,10}}, {888,7,{2,1,14,21,7,18,15}},
        {594,7,{19,20,16,17,7,18,15}}, {162,2,{21,14}},
        {192,2,{2,3}}, {30,3,{2,4,5}}, {357,4,{14,21,11,8}}};
    for(const auto& mesh:meshes) {
        if(mesh.vertices!=vertices)continue;
        for(unsigned slot=0;slot<mesh.count;++slot) {
            const auto bone=mesh.bones[slot];
            const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
            if(!Inverse(skin) || !Inverse(palette[slot]) || !PaletteNear(skin,palette[slot]))return {};
        }
        return std::span<const unsigned>(mesh.bones.data(),mesh.count);
    }
    return {};
}
// Giant Link keeps the human skeleton but gives his limbs independent scales.
// Verify the captured proportions so ordinary human Link retains its path.
inline bool GiantLinkSkeleton(const LinkPoseCapture::LivePose& pose) {
    if(pose.signature!=0x8025d822f84e8240ull || pose.pose.bones.size()!=26 ||
       pose.bind.size()!=26 || pose.words.size()!=26 ||
       !std::equal(pose.words.begin(),pose.words.end(),LinkPoseCapture::BoneWords.begin()))return false;
    const auto length=[&](unsigned bone,unsigned column) {
        double value=0;
        for(unsigned row=0;row<3;++row)value+=std::pow(pose.pose.bones[bone][row*4+column],2);
        return std::sqrt(value);
    };
    const double root=length(0,0);
    if(!std::isfinite(root) || root<1e-5)return false;
    constexpr std::array<unsigned,4> bones{3,10,17,21};
    constexpr std::array<std::array<double,3>,4> scales{{{1,1.25,1.25},{1,1.4,1.4},{1.7,1.5,1.9},{1.7,1.5,1.9}}};
    for(unsigned i=0;i<bones.size();++i)for(unsigned c=0;c<3;++c) {
        const double ratio=length(bones[i],c)/root;
        if(!std::isfinite(ratio) || std::abs(ratio-scales[i][c])>.015)return false;
    }
    return true;
}
// Giant Link magnifies the native hand bind rounding beyond the ordinary
// matcher. Validate both hands and their connected arm palettes as a whole.
inline std::optional<std::span<const unsigned>> GiantLinkBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if((vertices!=234 && vertices!=318 && vertices!=423 && vertices!=747) || !GiantLinkSkeleton(pose))return {};
    struct Mesh {unsigned vertices,count;std::array<unsigned,6> bones;};
    static constexpr std::array<Mesh,6> meshes{{
        {234,2,{17,16}},{234,2,{21,20}},
        // Open/gripping hands used while carrying Twinmold's boulders.
        {318,2,{17,16}},{318,2,{21,20}},
        {423,6,{9,14,15,10,16,17}},{747,6,{7,8,19,6,20,21}},
    }};
    std::optional<std::span<const unsigned>> found;
    for(const auto& mesh:meshes) {
        if(mesh.vertices!=vertices)continue;
        bool matches=true;
        for(unsigned slot=0;slot<mesh.count && matches;++slot) {
            const auto bone=mesh.bones[slot];
            const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
            if(!Inverse(skin) || !Inverse(palette[slot])) {matches=false;break;}
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
                matches &= std::isfinite(palette[slot][r][c]) && std::isfinite(skin[r][c]) &&
                    std::abs(palette[slot][r][c]-skin[r][c])<=(c==3?.065:.000025);
        }
        if(matches) {
            if(found)return {};
            found=std::span<const unsigned>(mesh.bones.data(),mesh.count);
        }
    }
    return found;
}
// Both verified Twinmold models share this skeleton and body palette ordering.
// Their skin matrices differ from reconstructed binds by up to .000294 in the
// basis and .063 units in translation. Require a complete known mesh palette;
// preserve its native correction instead of loosening generic bone matching.
inline std::optional<std::span<const unsigned>> TwinmoldBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=300 && vertices!=2529 && vertices!=504 && vertices!=510 &&
       vertices!=675 && vertices!=360 && vertices!=105 && vertices!=912)return {};
    if(!TwinmoldSkeleton(pose))return {};
    struct Mesh {unsigned vertices,count;std::array<unsigned,7> bones;};
    static constexpr std::array<Mesh,19> meshes{{
        {300,3,{19,20,1}},{300,3,{20,21,19}},{300,3,{21,22,20}},
        {300,3,{22,23,21}},{300,3,{23,24,22}},{300,3,{24,25,23}},
        {300,3,{25,26,24}},{300,3,{26,27,25}},{300,3,{27,28,26}},
        {300,3,{28,29,27}},{300,3,{29,30,28}},{300,3,{30,31,29}},
        {2529,6,{16,1,4,15,6,19}},{504,7,{11,13,14,1,7,9,10}},
        {510,2,{1,19}},{675,2,{30,31}},{360,2,{31,30}},
        {105,2,{30,31}},{912,2,{1,17}},
    }};
    std::optional<std::span<const unsigned>> found;
    for(const auto& mesh:meshes) {
        if(mesh.vertices!=vertices)continue;
        bool matches=true;
        for(unsigned slot=0;slot<mesh.count && matches;++slot) {
            const auto bone=mesh.bones[slot];
            const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
            if(!Inverse(skin) || !Inverse(palette[slot])) {matches=false;break;}
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
                matches &= std::isfinite(palette[slot][r][c]) && std::isfinite(skin[r][c]) &&
                    std::abs(palette[slot][r][c]-skin[r][c])<=(c==3?.075:.00035);
        }
        if(matches) {
            if(found)return {};
            found=std::span<const unsigned>(mesh.bones.data(),mesh.count);
        }
    }
    return found;
}
inline constexpr std::array<unsigned,7> GiantLegPalette{13,14,12,10,9,11,5};
inline bool GiantLegBones(const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=1014 || pose.signature!=0x369a86dcc57ca979ull ||
       pose.pose.bones.size()!=15 || pose.bind.size()!=15 || pose.words.size()!=15)return false;
    // The giant's leg material differs by up to 1.91e-5 / .040 units from
    // reconstructed binds. Match all seven slots, never a single nearby bone.
    for(unsigned slot=0;slot<GiantLegPalette.size();++slot) {
        const auto bone=GiantLegPalette[slot];
        const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
        if(!Inverse(skin) || !Inverse(palette[slot]))return false;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
            if(!std::isfinite(palette[slot][r][c]) || !std::isfinite(skin[r][c]) ||
               std::abs(palette[slot][r][c]-skin[r][c])>(c==3?.05:.00002))return false;
    }
    return true;
}
inline bool MajoraWhipSkeleton(const LinkPoseCapture::LivePose& pose) {
    if(pose.signature!=0x18ccb05513137730ull || pose.pose.bones.size()!=113 ||
       pose.bind.size()!=113 || pose.words.size()!=113 || pose.words[0]!=0xffff0000 ||
       pose.words[17]!=0x000f1011 || pose.words[66]!=0x00401042)return false;
    for(unsigned i=18;i<=109;++i) {
        if(i>60 && i<67)continue;
        if(pose.words[i]!=(((i-1)<<16)|0x1000|i))return false;
    }
    return true;
}
// Verified majora_demon whip palettes. Reconstructed binds differ from the
// guest's skin matrices by about .041 units in this capture. Validate the complete
// active palette, then preserve that small correction at the midpoint.
inline std::optional<std::span<const unsigned>> MajoraWhipBones(
        const Palette& palette,const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=576 && vertices!=600 && vertices!=624 && vertices!=672 &&
       vertices!=768 && vertices!=816)return {};
    if(!MajoraWhipSkeleton(pose))return {};
    struct Mesh {unsigned vertices,count;std::array<unsigned,10> bones;};
    static constexpr std::array<Mesh,12> meshes{{
        {768,9,{92,91,93,94,95,96,97,98,99}},
        {672,10,{68,69,67,66,64,100,101,102,99,103}},
        {768,10,{76,69,77,75,74,73,72,71,70,103}},
        {600,10,{104,105,106,107,108,109,72,71,70,103}},
        {672,10,{20,19,18,17,15,36,37,38,39,40}},
        {768,10,{20,21,22,24,23,28,27,26,25,40}},
        {816,10,{32,31,30,29,33,28,34,35,36,40}},
        {768,10,{46,47,48,49,50,51,52,53,54,40}},
        {576,10,{46,41,42,43,44,45,52,53,54,40}},
        {624,10,{80,81,79,78,77,82,83,53,54,40}},
        {600,10,{56,57,55,58,59,60,83,53,54,40}},
        {768,10,{87,86,85,84,88,89,83,90,91,40}},
    }};
    std::optional<std::span<const unsigned>> found;
    for(const auto& mesh:meshes) {
        if(mesh.vertices!=vertices)continue;
        bool matches=true;
        for(unsigned slot=0;slot<mesh.count && matches;++slot) {
            const auto bone=mesh.bones[slot];
            const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
            if(!Inverse(skin) || !Inverse(palette[slot])) {matches=false;break;}
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
                matches &= std::isfinite(palette[slot][r][c]) && std::isfinite(skin[r][c]) &&
                    std::abs(palette[slot][r][c]-skin[r][c])<=(c==3?.05:.00001);
        }
        if(matches) {
            if(found)return {};
            found=std::span<const unsigned>(mesh.bones.data(),mesh.count);
        }
    }
    return found;
}
// Like Like's scale-compensated joints produce local shear. For small changes,
// affine averaging preserves that shear without treating it as a rigid rotation.
// Restrict this fallback to the captured skeleton and bounded relative motion.
inline std::optional<Matrix> LikeLikeHalf(const Matrix& a,const Matrix& b,
                                        const LinkPoseCapture::LivePose& pose) {
    if(pose.signature!=0xe5524c5eb3c8c915ull || pose.pose.bones.size()!=5 ||
       pose.words.size()!=5 || pose.bind.size()!=5)return {};
    const auto inverse=Inverse(a);
    if(!inverse || !Inverse(b))return {};
    const auto relative=Multiply(*inverse,b);
    for(unsigned i=0;i<4;++i)
        if(a[3][i]!=(i==3?1.:0.) || b[3][i]!=(i==3?1.:0.))return {};
    for(unsigned i=0;i<3;++i) {
        if(std::abs(relative[i][3])>200)return {};
        for(unsigned j=0;j<3;++j)
            if(std::abs(relative[i][j]-(i==j?1.:0.))>.2)return {};
    }
    auto result=Identity();
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j)result[i][j]=(a[i][j]+b[i][j])*.5;
    return Inverse(result)?std::optional<Matrix>(result):std::nullopt;
}
// Bounded affine steps preserve small squash/shear deformations. Callers must
// verify the affected skeleton/joints or mesh before replacing rigid interpolation.
inline std::optional<Matrix> AffineDeformationHalf(const Matrix& a,const Matrix& b) {
    const auto ai=Inverse(a),bi=Inverse(b);
    if(!ai || !bi)return {};
    for(unsigned i=0;i<4;++i)
        if(a[3][i]!=(i==3?1.:0.) || b[3][i]!=(i==3?1.:0.))return {};
    double movement=0;
    for(unsigned i=0;i<3;++i)movement+=std::pow(a[i][3]-b[i][3],2);
    if(movement>=10000)return {};
    // Check both directions so expansion and contraction have identical guards.
    // A basis change this close to identity cannot flip or collapse through zero.
    for(const auto& relative:{Multiply(*ai,b),Multiply(*bi,a)}) {
        double change=0;
        for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
            change+=std::pow(relative[i][j]-(i==j?1.:0.),2);
        if(!std::isfinite(change) || change>.25)return {};
    }
    auto result=Identity();
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<4;++j)result[i][j]=(a[i][j]+b[i][j])*.5;
    return Inverse(result)?std::optional<Matrix>(result):std::nullopt;
}
// The terminal head joint inherits a small shear during the captured swing.
// Keep this fallback confined to that joint on both verified Twinmold models.
inline std::optional<Matrix> TwinmoldHeadHalf(const Matrix& a,const Matrix& b,
        const LinkPoseCapture::LivePose& pose,unsigned bone) {
    if(bone!=18 || !TwinmoldSkeleton(pose))return {};
    return AffineDeformationHalf(a,b);
}
// ChuChu death wobble skews its single model matrix; column-normalized rotation
// interpolation cannot represent it. Average small affine steps only for the
// captured slime materials (this root skeleton signature is shared by props).
inline std::optional<Matrix> SlimeHalf(const Matrix& a,const Matrix& b,
                                     const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(pose.signature!=0xf0c54784cce31d97ull || pose.pose.bones.size()!=1 ||
       pose.bind.size()!=1 || pose.words.size()!=1 || pose.words[0]!=0xffff0000 ||
       !Near(Expand(pose.bind[0]),Identity(),1e-5) ||
       (vertices!=18 && vertices!=444 && vertices!=612))return {};
    return AffineDeformationHalf(a,b);
}
// Both cow variants animate the five-joint tail with local shear. Rigid
// decomposition rejects every tail joint even though the head interpolates.
// Keep the deformation in its parent frame and retain bounded motion checks.
inline std::optional<Matrix> CowTailHalf(const Matrix& a,const Matrix& b,
        const LinkPoseCapture::LivePose& pose,unsigned bone) {
    static constexpr std::array<std::uint32_t,10> topology{
        0xffff0000,0x1001,0x11002,0x21003,0x11004,0x1005,0x51006,0x61007,0x71008,0x81009};
    if(bone<5 || bone>=10 || pose.signature!=0xbd01809703b206e7ull ||
       pose.pose.bones.size()!=10 || pose.bind.size()!=10 || pose.words.size()!=10 ||
       !std::equal(pose.words.begin(),pose.words.end(),topology.begin()))return {};
    return AffineDeformationHalf(a,b);
}
// The ghost balloon's body joint contains local shear; its dangling arms and
// hanging pieces retain ordinary rotation interpolation.
inline bool GhostBalloonSkeleton(const LinkPoseCapture::LivePose& pose) {
    static constexpr std::array<std::uint32_t,9> topology{
        0xffff0000,0x1001,0x1002,0x21003,0x31004,0x1005,0x51006,0x61007,0x1008};
    return pose.signature==0xae6056c772fdbd26ull &&
        pose.pose.bones.size()==9 && pose.bind.size()==9 && pose.words.size()==9 &&
        std::equal(pose.words.begin(),pose.words.end(),topology.begin());
}
inline std::optional<Matrix> GhostBalloonHalf(const Matrix& a,const Matrix& b,
        const LinkPoseCapture::LivePose& pose,unsigned bone) {
    if(bone!=1 || !GhostBalloonSkeleton(pose))return {};
    return AffineDeformationHalf(a,b);
}
// An arm's skin transform can coincide with the root for one frame. The
// verified full mesh palette resolves that ambiguity without relaxing matching.
inline constexpr std::array<unsigned,8> GhostBalloonBodyPalette{3,4,2,1,8,6,7,5};
inline bool GhostBalloonBodyBones(const Palette& palette,
        const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=1350 || !GhostBalloonSkeleton(pose))return false;
    for(unsigned slot=0;slot<GhostBalloonBodyPalette.size();++slot) {
        const auto bone=GhostBalloonBodyPalette[slot];
        const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
        if(!Inverse(skin) || !Inverse(palette[slot]) || !PaletteNear(palette[slot],skin))return false;
    }
    return true;
}
// Wart's body starts with coincident root/body skin transforms, so generic
// slot-zero matching cannot identify it. The local wort model specifies the
// complete 1872-vertex palette {1,5,7}; its separate eye uses direct bone 3.
inline constexpr std::array<unsigned,3> WartBodyPalette{1,5,7};
inline bool WartBodyBones(const Palette& palette,const LinkPoseCapture::LivePose& pose,
                          unsigned vertices) {
    static constexpr std::array<std::uint32_t,8> topology{
        0xffff0000,0x1001,0x11002,0x21003,0x11004,0x41005,0x11006,0x61007};
    if(vertices!=1872 || pose.signature!=0xb4670e8c455fcc2bull ||
       pose.pose.bones.size()!=8 || pose.bind.size()!=8 || pose.words.size()!=8 ||
       !std::equal(pose.words.begin(),pose.words.end(),topology.begin()))return false;
    for(unsigned slot=0;slot<WartBodyPalette.size();++slot) {
        const auto bone=WartBodyPalette[slot];
        const auto skin=Multiply(Expand(pose.pose.bones[bone]),Expand(pose.bind[bone]));
        if(!Inverse(skin) || !Inverse(palette[slot]) || !PaletteNear(palette[slot],skin))return false;
    }
    return true;
}
// Wart's 216-vertex wort_ball mesh squashes with a sheared root matrix.
// Ordinary rigid/scale decomposition rejects that wobble, including translation.
inline bool WartBubbleSkeleton(const LinkPoseCapture::LivePose& pose) {
    return pose.signature==0xb847a2c91407dd5dull && pose.pose.bones.size()==1 &&
        pose.bind.size()==1 && pose.words.size()==1 && pose.words[0]==0xffff0000 &&
        Near(Expand(pose.bind[0]),Identity(),1e-5);
}
inline std::optional<Matrix> WartBubbleHalf(const Matrix& a,const Matrix& b,
        const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=216 || !WartBubbleSkeleton(pose))return {};
    return AffineDeformationHalf(a,b);
}
// Gekko's small jellies have three coincident direct bones. The combined jelly
// alternates front/back model instances, all with the same one-bone skeleton.
// Neither a bone index nor a model-slot address is a stable motion identity.
enum class GekkoJellyKind { Small, Large };
inline std::optional<GekkoJellyKind> GekkoJellySkeleton(const LinkPoseCapture::LivePose& pose) {
    const bool small=pose.signature==0xb7bb8c8a4e2368b2ull;
    if(!small && pose.signature!=0xe4e4b9a66b715b85ull)return {};
    const unsigned count=small?3:1;
    if(pose.pose.bones.size()!=count || pose.bind.size()!=count || pose.words.size()!=count)return {};
    for(unsigned i=0;i<count;++i)
        if(pose.words[i]!=(i?i:0xffff0000u) || !Near(Expand(pose.bind[i]),Identity(),1e-5) ||
           !PaletteNear(Expand(pose.pose.bones[0]),Expand(pose.pose.bones[i])))return {};
    return small?GekkoJellyKind::Small:GekkoJellyKind::Large;
}
class GekkoJellyHistory {
    struct Sample {Matrix world;GekkoJellyKind kind;unsigned generation;std::uint32_t model;};
    std::vector<Sample> current,previous;
    std::vector<std::optional<Matrix>> half;
    std::uint64_t frame{};
    std::int64_t time{};
    Matrix view_camera{};
    bool seen{};
    static std::optional<Matrix> Blend(const Matrix& a,const Matrix& b) {
        if(auto rigid=Half(a,b))return rigid;
        return AffineDeformationHalf(a,b);
    }
    static std::optional<unsigned> Match(const Sample& sample,const std::vector<Sample>& pool) {
        double best=1e30,second=best;std::optional<unsigned> found;
        for(unsigned i=0;i<pool.size();++i) {
            const auto& other=pool[i];
            if(sample.kind!=other.kind || sample.generation!=other.generation ||
               (sample.kind==GekkoJellyKind::Small && sample.model!=other.model))continue;
            double distance=0;
            for(unsigned r=0;r<3;++r)distance+=std::pow(sample.world[r][3]-other.world[r][3],2);
            if(distance>64*64 || !Blend(sample.world,other.world))continue;
            if(distance<best){second=best;best=distance;found=i;}
            else second=std::min(second,distance);
        }
        return second>best*1.5+.01?found:std::nullopt;
    }
public:
    void Reset(){current.clear();previous.clear();half.clear();seen=false;}
    std::optional<Matrix> Apply(std::uint64_t next,std::int64_t now,const Matrix& camera,
            const LinkPoseCapture::LivePose& owner,const std::vector<LinkPoseCapture::LivePose>& poses,bool allow) {
        const auto kind=GekkoJellySkeleton(owner);
        if(!kind || !Rigid(camera))return {};
        if(!seen || next!=frame) {
            const bool continuous=seen && next==frame+1 && now>=time && now-time<=200;
            previous=continuous?std::move(current):std::vector<Sample>{};current.clear();half.clear();
            for(const auto& p:poses)if(const auto k=GekkoJellySkeleton(p)) {
                const auto world=Multiply(camera,Expand(p.pose.bones[0]));
                // Only the verified combined jelly's identical front/back
                // submissions are one object. Small coincident blobs remain
                // separate, so reciprocal matching rejects ambiguous crossings.
                if(*k==GekkoJellyKind::Large && std::any_of(current.begin(),current.end(),[&](const auto& item) {
                    return item.kind==*k && item.generation==p.generation && Near(item.world,world,1e-5);
                }))continue;
                if(current.size()==32){Reset();return {};}
                current.push_back({world,*k,p.generation,p.model});
            }
            half.resize(current.size());
            for(unsigned i=0;i<current.size();++i) {
                const auto match=Match(current[i],previous);
                if(match && Match(previous[*match],current)==i)
                    half[i]=Blend(previous[*match].world,current[i].world);
            }
            frame=next;time=now;view_camera=camera;seen=true;
        }
        if(!allow || !Near(camera,view_camera,.001))return {};
        const auto world=Multiply(camera,Expand(owner.pose.bones[0]));
        std::optional<unsigned> found;
        for(unsigned i=0;i<current.size();++i)
            if(current[i].kind==*kind && current[i].generation==owner.generation &&
               (*kind==GekkoJellyKind::Large || current[i].model==owner.model) && Near(current[i].world,world,1e-5)) {
                if(found)return {};
                found=i;
            }
        if(!found || !half[*found])return {};
        return Multiply(InverseRigid(camera),*half[*found]);
    }
};
// Wart sorts bubble model slots between frames. A model instance therefore is
// not a stable particle identity. Match the complete world-space bubble pool
// reciprocally; ambiguous crossings and new bubbles keep their native pose.
class WartBubbleHistory {
    struct Sample { Matrix world;std::uint32_t instance,model;unsigned generation; };
    std::vector<Sample> previous,current;
    std::vector<std::optional<Matrix>> half;
    std::uint64_t frame{};
    std::int64_t time{};
    Matrix view_camera{};
    bool seen{};
    static std::optional<unsigned> Match(const Sample& sample,const std::vector<Sample>& pool) {
        double best=1e30,second=best;std::optional<unsigned> found;
        for(unsigned i=0;i<pool.size();++i) {
            const auto& other=pool[i];
            if(sample.model!=other.model || sample.generation!=other.generation)continue;
            double distance=0;
            for(unsigned r=0;r<3;++r)distance+=std::pow(sample.world[r][3]-other.world[r][3],2);
            if(distance>32*32 || !AffineDeformationHalf(sample.world,other.world))continue;
            if(distance<best){second=best;best=distance;found=i;}
            else second=std::min(second,distance);
        }
        return second>best*1.5+.01?found:std::nullopt;
    }
public:
    void Reset(){previous.clear();current.clear();half.clear();seen=false;}
    std::optional<Matrix> Apply(std::uint64_t next,std::int64_t now,const Matrix& camera,
            const LinkPoseCapture::LivePose& owner,const std::vector<LinkPoseCapture::LivePose>& poses,bool allow) {
        if(!Rigid(camera))return {};
        if(!seen || next!=frame) {
            const bool continuous=seen && next==frame+1 && now>=time && now-time<=200;
            previous=continuous?std::move(current):std::vector<Sample>{};current.clear();half.clear();
            for(const auto& p:poses)if(WartBubbleSkeleton(p)) {
                if(current.size()==128){Reset();return {};}
                current.push_back({Multiply(camera,Expand(p.pose.bones[0])),p.pose.instance,p.model,p.generation});
            }
            half.resize(current.size());
            for(unsigned i=0;i<current.size();++i) {
                const auto match=Match(current[i],previous);
                if(match && Match(previous[*match],current)==i)
                    half[i]=AffineDeformationHalf(previous[*match].world,current[i].world);
            }
            frame=next;time=now;view_camera=camera;seen=true;
        }
        if(!allow || !Near(camera,view_camera,.001))return {};
        for(unsigned i=0;i<current.size();++i)
            if(current[i].instance==owner.pose.instance && current[i].model==owner.model &&
               current[i].generation==owner.generation && half[i])
                return Multiply(InverseRigid(camera),*half[i]);
        return {};
    }
};
// Extended fins are attached to the forearms after skeleton evaluation. Their
// stored parent remains bone 23, so interpolating that hierarchy disagrees with
// the arm's hierarchy even when both current matrices are identical.
inline std::optional<Matrix> ExtendedZoraFin(const Matrix& matrix,
        const LinkPoseCapture::LivePose& pose,const std::vector<Matrix>& half,
        const Matrix& view,unsigned vertices) {
    if(vertices!=120 || pose.signature!=0x42c4c515d83599dbull ||
       pose.pose.bones.size()!=28 || half.size()!=28)return {};
    std::optional<Matrix> result;
    for(const auto pair:{std::array<unsigned,2>{16,24},std::array<unsigned,2>{20,25}}) {
        if(!PaletteNear(matrix,Expand(pose.pose.bones[pair[0]])) ||
           !PaletteNear(matrix,Expand(pose.pose.bones[pair[1]])))continue;
        const auto arm=Multiply(view,half[pair[0]]);
        if(!Near(arm,arm,0) || (result && !PaletteNear(*result,arm)))return {};
        result=arm;
    }
    return result;
}
// Majora's ten-joint strands cancel inherited scale at each joint. Using the
// scaled parent to recover local rotations introduces shear, so ordinary Half
// rejects every child. Retain the hierarchy in unscaled parent frames instead.
inline bool MajoraStrand(const LinkPoseCapture::LivePose& pose,unsigned vertices) {
    if(vertices!=240 || pose.signature!=0x5a77df48ac4918b7ull ||
       pose.pose.bones.size()!=10 || pose.bind.size()!=10 || pose.words.size()!=10 ||
       pose.words[0]!=0xffff0000)return false;
    for(unsigned i=1;i<10;++i)
        if(pose.words[i]!=((i-1)<<16 | 0x1000 | i))return false;
    return true;
}
inline std::optional<Matrix> UnscaledJoint(const Matrix& matrix) {
    auto result=matrix;
    for(unsigned c=0;c<3;++c) {
        double length=0;
        for(unsigned r=0;r<3;++r)length+=matrix[r][c]*matrix[r][c];
        length=std::sqrt(length);
        if(!std::isfinite(length) || length<1e-5)return {};
        for(unsigned r=0;r<3;++r)result[r][c]/=length;
    }
    return Rigid(result)?std::optional{result}:std::nullopt;
}
class ActorHistory {
    std::uint64_t frame{};
    std::int64_t time{};
    unsigned generation{};
    std::uint32_t instance{},model{};
    bool seen{},ready{};
    std::vector<Matrix> current,half;
    std::uint64_t signature{};
    Matrix view_camera{};
public:
    void Reset() { seen=ready=false; }
    Overrides Apply(std::uint64_t next,std::int64_t now,const Matrix& camera,
                    const Palette& palette,const LinkPoseCapture::LivePose& selected,
                    bool allow,bool fairy_glow=false,bool deku_pipes=false,unsigned vertices=0,std::optional<Matrix> attached_root={}) {
        Overrides result{};
        const auto* pose=&selected;
        if(!pose || !Rigid(camera))return result;
        if(!seen || next!=frame) {
            const auto old=current;
            current.resize(pose->pose.bones.size());half.resize(current.size());
            bool continuous=seen && next==frame+1 && now>=time && now-time<=200 &&
                instance==pose->pose.instance && model==pose->model && generation==pose->generation && signature==pose->signature && old.size()==current.size();
            for(unsigned i=0;i<current.size();++i)current[i]=Multiply(camera,Expand(pose->pose.bones[i]));
            if(continuous) {
                double movement=0;for(unsigned i=0;i<3;++i)movement+=std::pow(old[0][i][3]-current[0][i][3],2);
                // Standing grass05 bushes are compacted between model slots
                // after cutting. A slot can then describe a different bush
                // tens of units away, still within the ordinary actor limit.
                // Compare world-space roots so camera movement remains smooth.
                const bool standing_bush=pose->signature==0x5985261146a459c5ull && current.size()==1;
                // Giant Link's swing can move Twinmold beyond the ordinary
                // 100-unit actor cutoff. Keep a bounded 200-unit budget only
                // for its verified skeleton; camera motion is removed above.
                continuous=movement<(standing_bush?1.:TwinmoldSkeleton(*pose)?40000.:10000.);
            }
            ready=continuous;
            const bool strand=MajoraStrand(*pose,vertices);
            // This is a property of the skeleton, not the first mesh drawn:
            // the body normally seeds history before either whip is submitted.
            const bool whips=MajoraWhipSkeleton(*pose);
            const bool twinmold=TwinmoldSkeleton(*pose);
            const bool giant_link=GiantLinkSkeleton(*pose);
            if(ready)for(unsigned i=0;i<current.size();++i) {
                if(i==0 && attached_root) {half[i]=*attached_root;continue;}
                const auto parent=pose->words[i]>>16;
                auto a=old[i],b=current[i];
                Matrix parent_half=Identity();
                if(parent!=65535) {
                    const bool compensated=strand || (whips && ((i>=18 && i<=60) || (i>=67 && i<=109))) ||
                        (twinmold && i>=20) || giant_link;
                    const auto pa=compensated?UnscaledJoint(old[parent]):std::optional{old[parent]};
                    const auto pb=compensated?UnscaledJoint(current[parent]):std::optional{current[parent]};
                    const auto ph=compensated?UnscaledJoint(half[parent]):std::optional{half[parent]};
                    if(!pa || !pb || !ph){half[i]=current[i];continue;}
                    const auto ai=Inverse(*pa),bi=Inverse(*pb);
                    if(!ai||!bi){half[i]=current[i];continue;}
                    a=Multiply(*ai,a);b=Multiply(*bi,b);
                    parent_half=*ph;
                }
                // Zora combat fins switch between .4 and 1 local scale.
                // Permit that captured transition only on their two bones.
                const bool zora_fin=pose->signature==0x42c4c515d83599dbull &&
                    current.size()==28 && (i==24 || i==25);
                auto local=Half(a,b,zora_fin?2.51:2.);
                if(!local && i==24 && HumanSkeleton(*pose))local=BowStringHalf(a,b);
                if(!local)local=LikeLikeHalf(a,b,*pose);
                if(!local)local=SlimeHalf(a,b,*pose,vertices);
                if(!local)local=CowTailHalf(a,b,*pose,i);
                if(!local)local=GhostBalloonHalf(a,b,*pose,i);
                if(!local)local=TwinmoldHeadHalf(a,b,*pose,i);
                half[i]=local?Multiply(parent_half,*local):current[i];
            }
            frame=next;time=now;instance=pose->pose.instance;model=pose->model;
            generation=pose->generation;signature=pose->signature;view_camera=camera;seen=true;
        }
        if(!allow || !ready || instance!=pose->pose.instance || model!=pose->model ||
           generation!=pose->generation || signature!=pose->signature ||
           !Near(view_camera,camera,.001))return result;
        const auto view=InverseRigid(camera);
        if(fairy_glow) {
            // Keep the native billboard orientation, size and pulsing. Only its
            // center follows the same interpolated root used by the wings.
            auto glow=palette[0];
            const auto root=Multiply(view,half[0]);
            for(unsigned i=0;i<3;++i)glow[i][3]=root[i][3];
            result[0]=glow;return result;
        }
        for(unsigned slot=0;slot<10;++slot)
            result[slot]=ResolvePalette(palette[slot],*pose,half,view);
        if(GhostBalloonBodyBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<GhostBalloonBodyPalette.size();++slot) {
                const auto bone=GhostBalloonBodyPalette[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(WartBodyBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<WartBodyPalette.size();++slot) {
                const auto bone=WartBodyPalette[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(BowStringBone(palette,*pose,vertices))result[0]=Multiply(view,half[24]);
        if(BunnyHoodBones(palette,*pose,vertices)) {
            result[0]=Multiply(view,half[0]);
            for(unsigned slot=1;slot<=2;++slot)
                result[slot]=Multiply(Multiply(view,half[slot]),Expand(pose->bind[slot]));
        }
        if(GyorgWaterBone(palette,*pose,vertices))result[0]=Multiply(view,half[1]);
        if(DungeonShutterBone(palette,*pose,vertices))result[0]=Multiply(view,half[0]);
        if(const auto wing=SoaringWingBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<wing->size();++slot) {
                const auto bone=(*wing)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                // Carry the guest's small bind difference into the midpoint.
                // A stationary pose therefore reproduces the native palette.
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(const auto fin=ExtendedZoraFin(palette[0],*pose,half,view,vertices))result[0]=fin;
        if(const auto bones=DodongoBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<bones->size();++slot) {
                const auto bone=(*bones)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(const auto bones=MajoraWhipBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<bones->size();++slot) {
                const auto bone=(*bones)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(const auto bones=GohtSkinBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<bones->size();++slot) {
                const auto bone=(*bones)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto inverse=Inverse(Multiply(Expand(pose->pose.bones[bone]),bind));
                if(!inverse) {result[slot].reset();continue;}
                const auto correction=Multiply(*inverse,palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(const auto bones=NpcSkinBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<bones->size();++slot) {
                const auto bone=(*bones)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(const auto bones=EyegoreBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<bones->size();++slot) {
                const auto bone=(*bones)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(const auto bones=GiantLinkBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<bones->size();++slot) {
                const auto bone=(*bones)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(const auto bones=TwinmoldBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<bones->size();++slot) {
                const auto bone=(*bones)[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(GiantLegBones(palette,*pose,vertices)) {
            for(unsigned slot=0;slot<GiantLegPalette.size();++slot) {
                const auto bone=GiantLegPalette[slot];
                const auto bind=Expand(pose->bind[bone]);
                const auto skin=Multiply(Expand(pose->pose.bones[bone]),bind);
                const auto correction=Multiply(*Inverse(skin),palette[slot]);
                result[slot]=Multiply(Multiply(Multiply(view,half[bone]),bind),correction);
            }
        }
        if(deku_pipes)for(unsigned slot=1;slot<=PipeBones.size();++slot) {
            const auto bone=PipeBones[slot-1];
            const auto bind=Expand(pose->bind[bone]);
            if(PaletteNear(palette[slot],Multiply(Expand(pose->pose.bones[bone]),bind)))
                result[slot]=Multiply(Multiply(view,half[bone]),bind);
        }
        return result;
    }
};
// One bounded history per skeleton instance, never shared between actors.
class History {
    WartBubbleHistory wart_bubbles;
    GekkoJellyHistory gekko_jellies;
    // Song of Time draws 128 clock instances plus Link and the lost items.
    // Keep room for that scene without dropping histories by submission order.
    static constexpr std::size_t MaxActors=256;
    struct Entry { ActorHistory history; std::uint64_t frame{}; };
    std::unordered_map<std::uint32_t,Entry> actors;
    struct CandidateEntry { BoneCandidates candidates;std::uint64_t frame{},query{};const LinkPoseCapture::LivePose* source{}; };
    std::uint64_t query_serial{};
    std::unordered_map<std::uint32_t,CandidateEntry> candidate_cache;
    std::optional<std::uint64_t> cleanup_frame;
    std::optional<Match> IdentifyCached(std::uint64_t frame,const Matrix& matrix,
                                      const LinkPoseCapture::LivePose& pose) {
        if(!candidate_cache.contains(pose.pose.instance) && candidate_cache.size()>=MaxActors)
            return Identify(matrix,pose);
        auto& entry=candidate_cache[pose.pose.instance];entry.frame=frame;
        if(entry.query!=query_serial || entry.source!=&pose) {
            entry.candidates.Prepare(pose);entry.query=query_serial;entry.source=&pose;
        }
        return entry.candidates.Identify(matrix);
    }
    std::array<std::uint32_t,6> head_attachment{};
    std::uint64_t head_attachment_frame{};
    bool head_attachment_seen{},head_attachment_ready{};
    struct Attachment {
        std::array<std::uint32_t,6> key{};
        std::uint64_t frame{};
        bool seen{},ready{};
    };
    std::unordered_map<std::uint32_t,Attachment> actor_attachments;
    Overrides ApplyKnown(std::uint64_t frame,std::int64_t now,const Matrix& camera,
                         const Palette& palette,const LinkPoseCapture::LivePose& pose,
                         bool allow,bool glow=false,bool pipes=false,unsigned vertices=0,std::optional<Matrix> attached_root={}) {
        if(!actors.contains(pose.pose.instance) && actors.size()>=MaxActors)return {};
        auto& entry=actors[pose.pose.instance];entry.frame=frame;
        return entry.history.Apply(frame,now,camera,palette,pose,allow,glow,pipes,vertices,attached_root);
    }
public:
    void Reset() { wart_bubbles.Reset();gekko_jellies.Reset();actors.clear();candidate_cache.clear();actor_attachments.clear();cleanup_frame.reset();head_attachment_seen=head_attachment_ready=false; }
    Overrides Apply(std::uint64_t frame,std::int64_t now,const Matrix& camera,
                    const Palette& palette,const std::vector<LinkPoseCapture::LivePose>& poses,
                    bool allow,unsigned vertices=0) {
        ++query_serial;
        if(cleanup_frame!=frame) {
            const auto expired=[&](const auto& pair){return pair.second.frame>frame || frame-pair.second.frame>2;};
            std::erase_if(actors,expired);std::erase_if(candidate_cache,expired);
            std::erase_if(actor_attachments,expired);cleanup_frame=frame;
        }
        if(vertices==216 || vertices==960) {
            const auto kind=vertices==216?GekkoJellyKind::Small:GekkoJellyKind::Large;
            const LinkPoseCapture::LivePose* owner=nullptr;
            for(const auto& pose:poses)if(GekkoJellySkeleton(pose)==kind &&
                PaletteNear(palette[0],Expand(pose.pose.bones[0]))) {
                if(owner && (kind==GekkoJellyKind::Small || pose.generation!=owner->generation ||
                    !Near(Expand(pose.pose.bones[0]),Expand(owner->pose.bones[0]),1e-5)))return {};
                owner=&pose;
            }
            if(owner) {
                Overrides result{};result[0]=gekko_jellies.Apply(frame,now,camera,*owner,poses,allow);
                return result;
            }
        }
        if(vertices==1872) {
            const LinkPoseCapture::LivePose* owner=nullptr;
            for(const auto& pose:poses)if(WartBodyBones(palette,pose,vertices)) {
                if(owner)return {};
                owner=&pose;
            }
            if(owner)return ApplyKnown(frame,now,camera,palette,*owner,allow,false,false,vertices);
        }
        if(vertices==216) {
            const LinkPoseCapture::LivePose* owner=nullptr;
            for(const auto& pose:poses)if(WartBubbleSkeleton(pose) &&
                PaletteNear(palette[0],Expand(pose.pose.bones[0]))) {
                if(owner)return {};
                owner=&pose;
            }
            if(owner) {
                Overrides result{};result[0]=wart_bubbles.Apply(frame,now,camera,*owner,poses,allow);
                return result;
            }
        }
        const LinkPoseCapture::LivePose* bow_owner=nullptr;
        if(vertices==12)for(const auto& pose:poses) {
            if(!BowStringBone(palette,pose,vertices))continue;
            if(bow_owner)return {};
            bow_owner=&pose;
        }
        if(bow_owner)return ApplyKnown(frame,now,camera,palette,*bow_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* bunny_owner=nullptr;
        if(vertices==330)for(const auto& pose:poses) {
            if(!BunnyHoodBones(palette,pose,vertices))continue;
            if(bunny_owner)return {};
            bunny_owner=&pose;
        }
        if(bunny_owner) {
            const LinkPoseCapture::LivePose* human=nullptr;
            const auto offset=BunnyHeadOffset();
            for(const auto& pose:poses) {
                if(!HumanSkeleton(pose) || !PaletteNear(palette[0],
                    Multiply(Expand(pose.pose.bones[11]),offset)))continue;
                if(human)return {};
                human=&pose;
            }
            std::optional<Matrix> root;
            if(human) {
                Palette head{};head[0]=Expand(human->pose.bones[11]);
                const auto mid=ApplyKnown(frame,now,camera,head,*human,allow);
                if(mid[0])root=Multiply(Multiply(camera,*mid[0]),offset);
            }
            return ApplyKnown(frame,now,camera,palette,*bunny_owner,allow && (!human || root.has_value()),
                false,false,vertices,root);
        }
        const LinkPoseCapture::LivePose* direct_owner=nullptr;
        if(vertices==528 || vertices==36 || vertices==84 || vertices==264 || vertices==210)for(const auto& pose:poses) {
            if(!GyorgWaterBone(palette,pose,vertices) && !DungeonShutterBone(palette,pose,vertices) && !SnowBoulderBone(palette,pose,vertices))continue;
            if(direct_owner)return {};
            direct_owner=&pose;
        }
        if(direct_owner)return ApplyKnown(frame,now,camera,palette,*direct_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* wing_owner=nullptr;
        if(vertices==864)for(const auto& pose:poses) {
            if(!SoaringWingBones(palette,pose,vertices))continue;
            if(wing_owner)return {}; // Coincident instances remain ambiguous.
            wing_owner=&pose;
        }
        if(wing_owner)return ApplyKnown(frame,now,camera,palette,*wing_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* dodongo_owner=nullptr;
        for(const auto& pose:poses) {
            if(!DodongoBones(palette,pose,vertices))continue;
            if(dodongo_owner)return {};
            dodongo_owner=&pose;
        }
        if(dodongo_owner)return ApplyKnown(frame,now,camera,palette,*dodongo_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* whip_owner=nullptr;
        for(const auto& pose:poses) {
            if(!MajoraWhipBones(palette,pose,vertices))continue;
            if(whip_owner)return {};
            whip_owner=&pose;
        }
        if(whip_owner)return ApplyKnown(frame,now,camera,palette,*whip_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* goht_owner=nullptr;
        for(const auto& pose:poses) {
            if(!GohtSkinBones(palette,pose,vertices))continue;
            if(goht_owner)return {};
            goht_owner=&pose;
        }
        if(goht_owner)return ApplyKnown(frame,now,camera,palette,*goht_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* npc_owner=nullptr;
        if(vertices==30 || vertices==144)for(const auto& pose:poses) {
            if(!NpcSkinBones(palette,pose,vertices))continue;
            if(npc_owner)return {};
            npc_owner=&pose;
        }
        if(npc_owner)return ApplyKnown(frame,now,camera,palette,*npc_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* eyegore_owner=nullptr;
        for(const auto& pose:poses) {
            if(!EyegoreBones(palette,pose,vertices))continue;
            if(eyegore_owner)return {};
            eyegore_owner=&pose;
        }
        if(eyegore_owner)return ApplyKnown(frame,now,camera,palette,*eyegore_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* giant_link_owner=nullptr;
        for(const auto& pose:poses) {
            if(!GiantLinkBones(palette,pose,vertices))continue;
            if(giant_link_owner)return {};
            giant_link_owner=&pose;
        }
        if(giant_link_owner)return ApplyKnown(frame,now,camera,palette,*giant_link_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* twinmold_owner=nullptr;
        for(const auto& pose:poses) {
            if(!TwinmoldBones(palette,pose,vertices))continue;
            if(twinmold_owner)return {};
            twinmold_owner=&pose;
        }
        if(twinmold_owner)return ApplyKnown(frame,now,camera,palette,*twinmold_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* giant_owner=nullptr;
        for(const auto& pose:poses) {
            if(!GiantLegBones(palette,pose,vertices))continue;
            if(giant_owner)return {};
            giant_owner=&pose;
        }
        if(giant_owner)return ApplyKnown(frame,now,camera,palette,*giant_owner,allow,false,false,vertices);
        const LinkPoseCapture::LivePose* found=nullptr;
        bool fairy_glow=false;
        std::vector<const LinkPoseCapture::LivePose*> owners;
        for(const auto& pose:poses) {
            if(pose.bind.size()!=5 || pose.words.size()!=5 ||
               !FairyGlow(palette[0],pose,vertices))continue;
            owners.push_back(&pose);fairy_glow=true;
        }
        if(!fairy_glow)
        for(const auto& pose:poses) {
            if(pose.pose.bones.empty() || pose.pose.bones.size()!=pose.bind.size() ||
               pose.words.size()!=pose.bind.size())continue;
            if(IdentifyCached(frame,palette[0],pose))owners.push_back(&pose);
        }
        // The opening cutscene submits two identical Tatl skeleton instances.
        // Only accept their shared draw if both complete current poses AND their
        // independently tracked midpoint palettes agree. Never pick an arbitrary
        // fairy when two different trajectories meet.
        if(owners.size()>1 && std::all_of(owners.begin(),owners.end(),[&](const auto* p) {
            if(p->signature!=0x09e2df0cf6765f8eull || p->pose.bones.size()!=5 ||
               p->bind.size()!=5 || p->words!=owners[0]->words)return false;
            for(unsigned i=0;i<5;++i)
                if(!Near(Expand(p->pose.bones[i]),Expand(owners[0]->pose.bones[i]),1e-5) ||
                   !Near(Expand(p->bind[i]),Expand(owners[0]->bind[i]),1e-5))return false;
            return true;
        })) {
            Overrides consensus{};bool first=true,agree=true;
            for(const auto* pose:owners) {
                const auto candidate=ApplyKnown(frame,now,camera,palette,*pose,allow,fairy_glow,false,vertices);
                if(first) {consensus=candidate;first=false;continue;}
                for(unsigned i=0;i<10;++i)
                    if(bool(candidate[i])!=bool(consensus[i]) ||
                       (candidate[i] && !PaletteNear(*candidate[i],*consensus[i])))agree=false;
            }
            return agree?consensus:Overrides{};
        }
        if(fairy_glow) {
            if(owners.size()!=1)return {};
            found=owners[0];owners.clear();
        }
        // Deku pipes share their base with Link, but animate their own branches.
        // Validate the captured draw and its ordered skin palette before selecting it.
        const LinkPoseCapture::LivePose* pipes=nullptr;
        if(!fairy_glow && vertices==1284)for(const auto& pose:poses) {
            if(pose.signature!=0x79937db4f6cb5c95ull || pose.pose.bones.size()!=7 ||
               pose.bind.size()!=7 || pose.words.size()!=7)continue;
            std::vector<Matrix> current;
            for(const auto& bone:pose.pose.bones)current.push_back(Expand(bone));
            if(!ResolvePalette(palette[0],pose,current,Identity()))continue;
            bool matches=true;
            for(unsigned slot=1;slot<=PipeBones.size();++slot) {
                const auto bone=PipeBones[slot-1];
                matches &= PaletteNear(palette[slot],Multiply(current[bone],Expand(pose.bind[bone])));
            }
            if(!matches)continue;
            if(pipes)return {};
            pipes=&pose;
        }
        if(pipes) {
            if(owners.size()>1)return {};
            if(!owners.empty() && (owners[0]->signature!=0x17beb877e4d23506ull ||
                                   owners[0]->pose.bones.size()!=24))return {};
            auto result=ApplyKnown(frame,now,camera,palette,*pipes,allow,false,true);
            if(!owners.empty()) {
                const auto& link=*owners[0];
                const auto shared=ApplyKnown(frame,now,camera,palette,link,allow);
                // Only the base is shared. A resting opening may happen to
                // equal Link's transform but retains its explicit bone identity.
                if(result[0] && (!shared[0] || !PaletteNear(*result[0],*shared[0])))
                    result[0].reset();
            }
            return result;
        }
        if(owners.size()>1 && owners.size()<=10) {
            constexpr std::array<std::uint64_t,10> sign_fragments{
                0x63f1ea87bac6ed15ull,0x237eacb5846225e3ull,0xa5ab84c08cb4c10cull,
                0x45e68157044301aeull,0x3cc2d5b83770f56aull,0x957491d08c5433edull,
                0x15e80cb6d1b86f59ull,0xe0695fdbeb7af70bull,0x8c3894096e7beb07ull,
                0xe6f89274e4c3a586ull};
            bool verified=true;
            for(unsigned i=0;i<owners.size();++i) {
                const auto& p=*owners[i];
                verified &= p.pose.bones.size()==1 &&
                    std::find(sign_fragments.begin(),sign_fragments.end(),p.signature)!=sign_fragments.end();
                for(unsigned j=0;j<i;++j)verified &= p.signature!=owners[j]->signature &&
                    p.model!=owners[j]->model && p.pose.instance!=owners[j]->pose.instance;
            }
            if(verified) {
                auto result=ApplyKnown(frame,now,camera,palette,*owners[0],allow);
                for(unsigned i=1;i<owners.size();++i) {
                    const auto other=ApplyKnown(frame,now,camera,palette,*owners[i],allow);
                    for(unsigned slot=0;slot<result.size();++slot)
                        if(!result[slot] || !other[slot] || !PaletteNear(*result[slot],*other[slot]))result[slot].reset();
                }
                return result;
            }
        }
        // Swordsman school logs draw multiple cut-piece models at a shared
        // transform. These eight one-bone models are verified in zelda2_maruta.
        // Preserve their separate histories and require agreement, as for signs.
        if(owners.size()>1 && owners.size()<=8 &&
           (vertices==12 || vertices==30 || vertices==36 || vertices==39 || vertices==42)) {
            constexpr std::array<std::uint64_t,8> log_fragments{
                0x25691ac68afb02e2ull,0x7bb6e2032873c32eull,0xa8858ef16d4f6e4bull,
                0x15032d07b3410790ull,0x1ccc2f131d7a89cfull,0x981fa132959572f9ull,
                0xa5a4cdf77e3ead22ull,0x10167bcfce7776c8ull};
            bool verified=true;
            for(unsigned i=0;i<owners.size();++i) {
                const auto& p=*owners[i];
                auto bind=Identity();
                if(p.signature==0xa5a4cdf77e3ead22ull)
                    for(unsigned c=0;c<3;++c)bind[c][c]=.38303905725479126;
                verified &= p.pose.bones.size()==1 && p.bind.size()==1 && p.words.size()==1 &&
                    p.words[0]==0xffff0000 && Near(Expand(p.bind[0]),bind,1e-5) &&
                    std::find(log_fragments.begin(),log_fragments.end(),p.signature)!=log_fragments.end();
                for(unsigned j=0;j<i;++j)verified &= p.signature!=owners[j]->signature &&
                    p.model!=owners[j]->model && p.pose.instance!=owners[j]->pose.instance;
            }
            if(verified) {
                auto result=ApplyKnown(frame,now,camera,palette,*owners[0],allow);
                for(unsigned i=1;i<owners.size();++i) {
                    const auto other=ApplyKnown(frame,now,camera,palette,*owners[i],allow);
                    for(unsigned slot=0;slot<result.size();++slot)
                        if(!result[slot] || !other[slot] || !PaletteNear(*result[slot],*other[slot]))result[slot].reset();
                }
                return result;
            }
        }
        if(owners.size()>1) {
            // Verified attachments/paired marker passes: never pick an arbitrary owner.
            // A shared slot is usable only when both independent histories agree.
            if(owners.size()!=2)return {};
            // Captured rigid accessories inherit their parent's bone verbatim.
            // Use that bone's hierarchy midpoint: independently interpolating
            // the attached model's root would detach it during joint rotation.
            struct AttachedModel {
                std::uint64_t body,item;
                unsigned bones,parent;
                std::array<unsigned,3> draws;
            };
            static constexpr std::array<AttachedModel,5> attached_models{{
                {0xbf2f38bb6502de36ull,0x10aa2e239eaea424ull,15,14,{204,96,96}}, // Postman cap
                {0xbf2f38bb6502de36ull,0x61a2e0e2eed80dccull,15,7,{414,414,414}}, // Postman bag
                {0x228b499bc4c2bef7ull,0xa81ec0d047c2eaadull,20,8,{36,36,36}}, // Carpenter wood
                {0x228b499bc4c2bef7ull,0x06d0beac6055ea2cull,20,19,{120,36,36}}, // Milk Road pickaxe
                {0xe6a4db84a5b61d37ull,0x09b0267833ca8895ull,9,8,{120,180,540}}, // Real Bombchu tail bomb
            }};
            for(unsigned index=0;index<2;++index) {
                const auto& body=*owners[index];const auto& item=*owners[1-index];
                const auto model=std::find_if(attached_models.begin(),attached_models.end(),[&](const auto& m) {
                    return body.signature==m.body && item.signature==m.item &&
                        std::find(m.draws.begin(),m.draws.end(),vertices)!=m.draws.end();
                });
                if(model==attached_models.end() || body.pose.bones.size()!=model->bones ||
                   body.bind.size()!=model->bones || body.words.size()!=model->bones ||
                   item.pose.bones.size()!=1 || item.bind.size()!=1 || item.words.size()!=1 ||
                   item.words[0]!=0xffff0000 || !Near(Expand(item.bind[0]),Identity(),1e-5) ||
                   body.pose.instance==item.pose.instance || body.model==item.model)continue;
                const auto parent=Expand(body.pose.bones[model->parent]);
                if(!PaletteNear(palette[0],parent) ||
                   !PaletteNear(Expand(item.pose.bones[0]),parent))continue;
                if(!actor_attachments.contains(item.pose.instance) &&
                   actor_attachments.size()>=128)return {};
                auto& attachment=actor_attachments[item.pose.instance];
                const std::array<std::uint32_t,6> key{body.pose.instance,body.model,body.generation,
                    item.pose.instance,item.model,item.generation};
                if(!attachment.seen || attachment.frame!=frame || attachment.key!=key) {
                    attachment.ready=attachment.seen && attachment.key==key &&
                        frame==attachment.frame+1;
                    attachment.key=key;attachment.frame=frame;attachment.seen=true;
                }
                return ApplyKnown(frame,now,camera,palette,body,allow && attachment.ready);
            }
            const auto mask=[](const auto& p) {
                return p.pose.bones.size()==1 &&
                    (p.signature==0x6787d559c31d313aull || p.signature==0xbee0d7a2ff0e9481ull);
            };
            const auto human=[](const auto& p) {
                return p.words.size()==LinkPoseCapture::BoneWords.size() &&
                    std::equal(p.words.begin(),p.words.end(),LinkPoseCapture::BoneWords.begin());
            };
            // Worn masks inherit the human head transform. Blend
            // through the head's actual hierarchy, not a separate root rotation.
            // Require the same attachment on consecutive frames; newly equipped
            // masks and actor swaps must seed rather than borrow old history.
            for(unsigned index=0;index<2;++index) {
                const auto& link=*owners[index];const auto& item=*owners[1-index];
                const bool animated_mask=(item.signature==0x2090ecc88f377de1ull && item.pose.bones.size()==7) ||
                    (item.signature==0x15ad1b7410d5bd8dull && item.pose.bones.size()==3);
                const auto count=item.pose.bones.size();
                if(!human(link) || link.pose.bones.size()!=26 || (count!=1 && !animated_mask) ||
                   item.bind.size()!=count || item.words.size()!=count ||
                   link.pose.instance==item.pose.instance || link.model==item.model ||
                   !PaletteNear(palette[0],Expand(link.pose.bones[11])) ||
                   !PaletteNear(Multiply(Expand(item.pose.bones[0]),Expand(item.bind[0])),
                                Expand(link.pose.bones[11])))continue;
                const std::array<std::uint32_t,6> key{link.pose.instance,link.model,link.generation,
                    item.pose.instance,item.model,item.generation};
                if(!head_attachment_seen || head_attachment_frame!=frame || head_attachment!=key) {
                    head_attachment_ready=head_attachment_seen && head_attachment==key &&
                        frame==head_attachment_frame+1;
                    head_attachment=key;head_attachment_frame=frame;head_attachment_seen=true;
                }
                auto result=ApplyKnown(frame,now,camera,palette,link,allow && head_attachment_ready);
                if(!animated_mask)return result;
                // The mask root uses a fixed bind offset from the head. Seed
                // its hierarchy even on the first attachment frame, then use
                // the head midpoint as the parent of its animated children.
                std::optional<Matrix> root;
                const auto offset=Inverse(Expand(item.bind[0]));
                if(result[0] && offset)root=Multiply(Multiply(camera,*result[0]),*offset);
                return ApplyKnown(frame,now,camera,palette,item,
                    allow && head_attachment_ready && root.has_value(),false,false,vertices,root);
            }
            const auto marker=[](const auto& p) {
                // Paired target/mark models and their depth models, locally verified.
                return p.pose.bones.size()==1 &&
                    (p.signature==0x578e07cd3309c81aull || p.signature==0x018a783e5ef2d2bdull);
            };
            const bool marker_pair=marker(*owners[0]) && marker(*owners[1]) &&
                owners[0]->signature==owners[1]->signature &&
                owners[0]->pose.instance!=owners[1]->pose.instance && owners[0]->model!=owners[1]->model;
            // Tingle's sphere and knot are separate one-bone models sharing
            // the same root. Keep both histories and require midpoint agreement.
            const auto balloon=[](const auto& p) {
                return p.pose.bones.size()==1 && p.bind.size()==1 && p.words.size()==1 &&
                    p.words[0]==0xffff0000 && Near(Expand(p.bind[0]),Identity(),1e-5) &&
                    (p.signature==0x486289ad769db138ull || p.signature==0x6ea7a4a7d451310cull);
            };
            const bool balloon_pair=(vertices==72 || vertices==576) &&
                balloon(*owners[0]) && balloon(*owners[1]) &&
                owners[0]->signature!=owners[1]->signature &&
                owners[0]->pose.instance!=owners[1]->pose.instance && owners[0]->model!=owners[1]->model;
            // Koume's hanging target and chain are separate models with the
            // same moving root. Both histories must agree before sharing it.
            const auto koume_target=[](const auto& p) {
                return p.pose.bones.size()==1 && p.bind.size()==1 &&
                    p.words==std::vector<std::uint32_t>{0xffff0000} &&
                    Near(Expand(p.bind[0]),Identity(),1e-5) &&
                    (p.signature==0x036049aec268c4f9ull || p.signature==0x22fef6cc8a39263cull);
            };
            const bool koume_target_pair=(vertices==144 || vertices==600) &&
                koume_target(*owners[0]) && koume_target(*owners[1]) &&
                owners[0]->signature!=owners[1]->signature &&
                owners[0]->pose.instance!=owners[1]->pose.instance && owners[0]->model!=owners[1]->model;
            // Great Bay's rotating screw and its foreground deck are separate
            // models with the same root skeleton and transform. Both histories
            // must agree, just as for the paired balloon meshes above.
            const auto rotor=[](const auto& p) {
                return p.signature==0x85335493df397bb2ull && p.pose.bones.size()==1 &&
                    p.bind.size()==1 && p.words==std::vector<std::uint32_t>{0xffff0000} &&
                    Near(Expand(p.bind[0]),Identity(),1e-5);
            };
            const bool rotor_pair=(vertices==48 || vertices==72 || vertices==144 ||
                vertices==348 || vertices==720) && rotor(*owners[0]) && rotor(*owners[1]) &&
                owners[0]->pose.instance!=owners[1]->pose.instance && owners[0]->model!=owners[1]->model;
            // Adjacent Snowhead ice rings alias a direct matrix with the next
            // ring's skin matrix (its bind shifts by one ring height). Keep
            // both histories: once punched apart they must agree independently
            // before either can own a shared draw.
            const auto pillar_part=[](const auto& p) {
                auto bind=Identity();
                if(p.signature==0x046391818a624e42ull)bind[1][3]=-1199.988037109375;
                else if(p.signature==0x0952810a097e354aeull) {
                    // The middle stone section's scaled skin also coincides
                    // with one ice ring's direct transform.
                    bind[0][0]=bind[2][2]=.12006315588951111;bind[1][1]=.10000000149011612;
                } else return false;
                return p.pose.bones.size()==1 && p.bind.size()==1 &&
                    p.words==std::vector<std::uint32_t>{0xffff0000} && Near(Expand(p.bind[0]),bind,1e-5);
            };
            bool pillar_pair=false;
            if(vertices==384 && pillar_part(*owners[0]) && pillar_part(*owners[1]) &&
               (owners[0]->signature==0x046391818a624e42ull || owners[1]->signature==0x046391818a624e42ull) &&
               owners[0]->pose.instance!=owners[1]->pose.instance &&
               ((owners[0]->signature==owners[1]->signature)==(owners[0]->model==owners[1]->model))) {
                const auto a=Expand(owners[0]->pose.bones[0]),b=Expand(owners[1]->pose.bones[0]);
                const auto ab=Multiply(a,Expand(owners[0]->bind[0])),bb=Multiply(b,Expand(owners[1]->bind[0]));
                pillar_pair=!PaletteNear(a,b) &&
                    ((PaletteNear(palette[0],a) && PaletteNear(palette[0],bb)) ||
                     (PaletteNear(palette[0],b) && PaletteNear(palette[0],ab)));
            }
            if(!(marker_pair || balloon_pair || koume_target_pair || rotor_pair || pillar_pair || (mask(*owners[0])&&human(*owners[1])) ||
                 (mask(*owners[1])&&human(*owners[0]))))return {};
            auto a=ApplyKnown(frame,now,camera,palette,*owners[0],allow);
            const auto b=ApplyKnown(frame,now,camera,palette,*owners[1],allow);
            for(unsigned i=0;i<a.size();++i)
                if(!a[i] || !b[i] || !PaletteNear(*a[i],*b[i]))a[i].reset();
            return a;
        }
        if(owners.size()==1)found=owners[0];
        if(!found)for(const auto& pose:poses) {
            // Verified effects, pickups, Bomber and intro trees have equivalent bones.
            // ResolvePalette still rejects them if their midpoint poses diverge.
            const unsigned count=(pose.signature==0x91e28867df89d7bcull && vertices==24) ? 25 : // Deku Princess eyes: coincident head bones 9/12
                (pose.signature==0x4fa89f19eb218899ull && vertices==630) ? 4 : // Freezard shared body bones, including death shrink
                (pose.signature==0x150560ccf65e9d98ull &&
                (vertices==414 || vertices==186)) ? 4 : // Burrowed Deku head, shared direct bones
                (pose.signature==0x9bbb839482a3896aull &&
                (vertices==162 || vertices==72 || vertices==276 || vertices==54)) ? 3 : // Bombchu shared roots
                (pose.signature==0x049480e14d9b324bull && vertices==870) ? 23 : // Marine lab fish, shared skin bones
                (pose.signature==0x84bf0b3a9fcca9e4ull &&
                (vertices==60 || vertices==90 || vertices==30)) ? 4 : // Mirror-shield light: equivalent beam bones
                (pose.signature==0x496431680f850b7full &&
                (vertices==12 || vertices==129 || vertices==150 || vertices==219 ||
                 vertices==750 || vertices==756 || vertices==1878)) ? 29 : // Goht shared body/limb bones
                (pose.signature==0x71a6a9af29d2e93full &&
                (vertices==24 || vertices==276)) ? 20 : // Snowhead flag (ht_hata)
                (pose.signature==0x2839c49538bf9e2aull &&
                (vertices==12 || vertices==24)) ? 3 : // Blue Bubble flame (torch4_soul)
                (pose.signature==0x3f33b43a6cf98ad4ull &&
                (vertices==732 || vertices==108)) ? 8 : // Gyorg small fish (minibus_b)
                (pose.signature==0x6a038b2b40fd9fbfull &&
                 (vertices==138 || vertices==96 || vertices==72)) ? 4 : // Great Bay wheel water
                (pose.signature==0xbfecd47d742679fdull &&
                 (vertices==216 || vertices==252 || vertices==108)) ? 6 : // Great Bay water jet
                pose.signature==0xdfe0c7f5a2531807ull ? 4 :
                pose.signature==0xdf35e5b3de06d85eull ? 7 :
                (pose.signature==0x93dff8d74c226412ull && (vertices==30 || vertices==126 || vertices==402)) ? 4 : // Removed Deku mask
                (pose.signature==0x3b9945c18d4a600full && vertices==180) ? 4 : // Hinged door
                (pose.signature==0xbb44d2bc24eb2cd6ull &&
                 (vertices==12 || vertices==42 || vertices==81 || vertices==564 || vertices==768)) ? 20 : // Captain Keeta head/root
                (pose.signature==0xc4bab09e9917b4aeull &&
                 (vertices==36 || vertices==72 || vertices==78 || vertices==360)) ? 20 : // Gabora head/shared bones
                (pose.signature==0x89c1c57e8d857e3bull && vertices==384) ? 2 : // Skull Kid's tossed ocarina
                (pose.signature==0xb8b55777b90f150eull &&
                 (vertices==180 || vertices==42 || vertices==408)) ? 3 : // Held Piece of Heart
                (pose.signature==0x9917e505ef9503eaull && vertices==96) ? 7 : // Reset rupees
                (pose.signature==0x110f9bdb5eb0dd7aull && (vertices==12 || vertices==306)) ? 3 : // Reset Deku stick
                pose.signature==0xce5abb274d304de4ull ? 3 :
                pose.signature==0x69e723ba0b961e6bull ? 4 :
                pose.signature==0xc299a4754ffae267ull ? 17 :
                pose.signature==0x3d654b8a99a21237ull ? 27 : // Business Scrub head/shared bones
                pose.signature==0x04e3fb346228fd20ull ? 4 :
                pose.signature==0xd56b42acae52759full ? 7 :
                pose.signature==0x7526de611fb49ce3ull ? 8 :
                pose.signature==0x3ed3e32e1ee38993ull ? 3 :
                pose.signature==0x6735aa3cacc26ca6ull ? 3 : // Zora electric barrier
                pose.signature==0xe26d2bb9ac2e87b6ull ? 3 : // Intro forest trees
                (pose.signature==0x31d188026f978991ull && vertices==198) ? 2 : // Deku nut projectile
                pose.signature==0x76a7e789464b75d3ull ? 2 :
                pose.signature==0xe40ebd58d8b46adaull ? 2 :
                pose.signature==0x2507dd32b95b7c14ull ? 24 : 0;
            if(!count || pose.pose.bones.size()!=count ||
               pose.bind.size()!=count || pose.words.size()!=count)continue;
            std::vector<Matrix> current;
            for(const auto& bone:pose.pose.bones)current.push_back(Expand(bone));
            if(ResolvePalette(palette[0],pose,current,Identity())) {
                if(found)return {};
                found=&pose;
            }
        }
        if(!found) {
            // Some actor materials leave slot zero unmatched, while their
            // active bones occupy later slots. Require three distinct bones
            // and a unique instance; stale or ambiguous palettes stay native.
            for(const auto& pose:poses) {
                if(pose.pose.bones.empty() || pose.pose.bones.size()!=pose.bind.size() ||
                   pose.words.size()!=pose.bind.size())continue;
                std::vector<unsigned> bones;
                for(unsigned slot=1;slot<palette.size();++slot)
                    if(const auto match=IdentifyCached(frame,palette[slot],pose))
                        if(std::find(bones.begin(),bones.end(),match->bone)==bones.end())
                            bones.push_back(match->bone);
                if(bones.size()>=3){if(found)return {};found=&pose;}
            }
        }
        if(!found)return {};
        return ApplyKnown(frame,now,camera,palette,*found,allow,fairy_glow,false,vertices);
    }
};

}
