// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <iostream>
#include "mm3d/ice_particle_interpolation.h"
#include "mm3d/fire_effect_interpolation.h"
#include "mm3d/garo_flame_interpolation.h"
#include "mm3d/splash_interpolation.h"
#include "mm3d/ripple_interpolation.h"
#include "mm3d/forest_particle_interpolation.h"
#include "mm3d/snow_interpolation.h"
#include "mm3d/fire_impact_interpolation.h"
#include "mm3d/torch_interpolation.h"
#include "mm3d/water_particle_interpolation.h"
#include "mm3d/burst_particle_interpolation.h"
#include "mm3d/spark_interpolation.h"
#include "mm3d/firework_interpolation.h"
#include "mm3d/chest_particle_interpolation.h"
#include "mm3d/sword_trail_interpolation.h"
#include "mm3d/shockwave_interpolation.h"
#include "mm3d/bubble_interpolation.h"
#include "mm3d/leaf_particle_interpolation.h"
#include "mm3d/moth_interpolation.h"
#include "mm3d/message_interpolation.h"
#include "mm3d/file_select_interpolation.h"
#include "mm3d/song_effect_interpolation.h"
#include "mm3d/glare_interpolation.h"
#include "mm3d/bloom_sampling.h"
#include <limits>
#include <unordered_map>
#include "mm3d/link_pose_capture.h"
#include "mm3d/link_interpolation.h"
#include <stdexcept>
#include "mm3d/camera_midpoint.h"
#include "mm3d/world_map_interpolation.h"
#include "mm3d/water_model_interpolation.h"
#include "mm3d/moving_effect_interpolation.h"
#include "mm3d/mirror_projection_interpolation.h"
#include "mm3d/banner_interpolation.h"
using namespace MM3D::CameraMidpoint;
void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
Matrix Camera(double degrees,double x=0) {
    Matrix m=Identity();const double angle=degrees*3.141592653589793/180.;
    m[0][0]=m[2][2]=std::cos(angle);m[0][2]=std::sin(angle);m[2][0]=-std::sin(angle);m[0][3]=x;
    return m;
}
int main() {
 try {
    {
        namespace M = MM3D::MirrorProjectionInterpolation;
        const auto projector = [](const Matrix& pose, float alpha) {
            auto view = InverseRigid(pose);
            for (unsigned r = 0; r < 2; ++r)
                for (unsigned c = 0; c < 4; ++c) view[r][c] *= .03;
            return M::Sample{view, alpha};
        };
        const auto a = projector(Camera(0, 100), .4f);
        const auto b = projector(Camera(20, 104), .8f);
        const auto expected = projector(Camera(10, 102), .6f);
        Check(M::Recognized(3, M::Material, a.projection), "mirror receiver material recognized");
        auto wrong = M::Material; wrong[1][2] ^= 1;
        Check(!M::Recognized(3, wrong, a.projection), "ordinary wall combiner rejected");
        Check(!M::Recognized(1, M::Material, a.projection), "inactive projector texture rejected");
        auto scaled = a; scaled.projection[0][0] *= 2;
        Check(!M::Recognized(3, M::Material, scaled.projection), "different projector scale rejected");
        auto colored = M::Material; colored[2][3] = 0x80ffffff;
        Check(M::Recognized(3, colored, a.projection), "mirror brightness may vary");
        colored[2][3] ^= 1;
        Check(!M::Recognized(3, colored, a.projection), "different reflected-light color rejected");
        const auto half = M::Half(a, b);
        Check(half && Near(half->projection, expected.projection, 1e-6) &&
              std::abs(half->alpha - .6f) < 1e-6, "projector rotates around halfway origin and fades");
        // A point on the halfway projector's central ray must remain centered.
        // Interpolating only UV offsets or the view translation breaks this.
        const auto pose = Camera(10, 102);
        std::array<double, 4> point{pose[0][3] + pose[0][2] * 50,
            pose[1][3] + pose[1][2] * 50, pose[2][3] + pose[2][2] * 50, 1};
        for (unsigned r = 0; r < 3; ++r) {
            double value = 0;
            for (unsigned c = 0; c < 4; ++c) value += half->projection[r][c] * point[c];
            Check(std::abs(value - (r == 2 ? 50 : 0)) < 1e-5,
                  "all three projector rows map the halfway ray correctly");
        }
        Check(!M::Half(a, projector(Camera(0, 200), .8f)), "projector teleport rejected");
        Check(!M::Half(a, projector(Camera(90, 100), .8f)), "projector direction cut rejected");
        auto invalid = b; invalid.projection[2][3] = std::numeric_limits<double>::quiet_NaN();
        Check(!M::Half(a, invalid), "invalid projector rejected");
        M::History history;
        const M::History::Key key{42, 1};
        Check(!history.Apply(1, key, a), "projector first frame seeds");
        Check(history.Apply(2, key, b).has_value(), "projector consecutive frame interpolates");
        const auto repeated = history.Apply(2, key, b);
        Check(repeated && Near(repeated->projection, expected.projection, 1e-6),
              "all receiver meshes reuse the same midpoint");
        Check(!history.Apply(2, {43, 1}, b), "different texture history isolated");
        Check(!history.Apply(2, {42, 2}, b), "different scene generation isolated");
        Check(!history.Apply(4, key, a), "missing projector frame resets history");
        Check(!history.Apply(4, key, b), "multiple projectors sharing a texture rejected");
        Check(!history.Apply(5, key, b), "ambiguous projector does not seed a midpoint");
        history.Reset();
        Check(!history.Apply(6, key, b), "explicit projector reset seeds afresh");
    }
    {
        namespace L=MM3D::LinkInterpolation;
        using namespace MM3D::LinkPoseMath;
        const auto jelly=[](unsigned instance,bool large,const Matrix& world,const Matrix& camera=Identity()) {
            MM3D::LinkPoseCapture::LivePose p;
            p.pose.instance=instance;p.model=large?instance:1;
            p.signature=large?0xe4e4b9a66b715b85ull:0xb7bb8c8a4e2368b2ull;
            p.words=large?std::vector<std::uint32_t>{0xffff0000}:std::vector<std::uint32_t>{0xffff0000,1,2};
            p.bind.assign(p.words.size(),Flatten(Identity()));
            p.pose.bones.assign(p.words.size(),Flatten(Multiply(InverseRigid(camera),world)));
            return p;
        };
        const auto palette=[](const auto& pose) {
            L::Palette p;p.fill(Camera(0,99999));p[0]=Expand(pose.pose.bones[0]);return p;
        };
        auto a=jelly(1,false,Camera(0,0)),b=jelly(2,false,Camera(0,100));
        Check(L::GekkoJellySkeleton(a)==L::GekkoJellyKind::Small,"three coincident Gekko jelly bones recognized");
        L::History h;
        Check(!h.Apply(1,0,Identity(),palette(a),{a,b},true,216)[0],"Gekko jelly first frame seeds");
        // Addresses swap while each physical jelly moves ten units. Transform
        // both the capture and palette into a different camera's view space.
        const auto camera=Camera(20,50);
        auto a2=jelly(2,false,Camera(10,10),camera),b2=jelly(1,false,Camera(-10,90),camera);
        const auto native=palette(a2);
        auto half=h.Apply(2,33,camera,native,{b2,a2},true,216);
        Check(half[0] && Near(Multiply(camera,*half[0]),Camera(5,5),.001),"Gekko jelly follows world motion across swapped sorted slots");
        auto other=h.Apply(2,33,camera,palette(b2),{b2,a2},true,216);
        Check(other[0] && Near(Multiply(camera,*other[0]),Camera(-5,95),.001),"second Gekko jelly keeps independent identity");
        Check(native==palette(a2) && !half[1],"only jelly midpoint slot zero changes, native palette untouched");
        Check(h.Apply(2,33,camera,native,{b2,a2},true,216)==half,"both small-jelly surfaces share midpoint");
        Check(!h.Apply(2,33,camera,native,{b2,a2},false,216)[0],"disabled jelly interpolation stays native");
        Check(!h.Apply(4,99,camera,native,{b2,a2},true,216)[0],"jelly frame gap reseeds");
        h.Reset();Check(!h.Apply(5,132,camera,native,{b2,a2},true,216)[0],"jelly history reset clears midpoint");
        auto wrong=a;wrong.words[2]=0x1002;
        Check(!L::GekkoJellySkeleton(wrong),"unexpected jelly topology excluded");
        wrong=a;wrong.bind[2][3]=1;
        Check(!L::GekkoJellySkeleton(wrong),"jelly bind transforms verified");
        wrong=a;wrong.pose.bones[2][3]=1;
        Check(!L::GekkoJellySkeleton(wrong),"diverging jelly bone transforms excluded");
        wrong=a;wrong.signature++;
        Check(!L::GekkoJellySkeleton(wrong),"unrelated shared-bone skeleton excluded");
        L::History crossing;
        crossing.Apply(1,0,Identity(),palette(a),{a,b},true,216);
        auto center=jelly(3,false,Camera(0,50));
        Check(!crossing.Apply(2,33,Identity(),palette(center),{center},true,216)[0],"equidistant jelly crossing rejected");
        L::History spawn;spawn.Apply(1,0,Identity(),palette(a),{a},true,216);
        auto near=jelly(3,false,Camera(0,10)),far=jelly(4,false,Camera(0,30));
        Check(!spawn.Apply(2,33,Identity(),palette(far),{far,near},true,216)[0],"reciprocal match prevents two bubbles claiming one predecessor");
        L::History cut;cut.Apply(1,0,Identity(),palette(a),{a},true,216);
        auto jump=jelly(1,false,Camera(0,1000));
        Check(!cut.Apply(2,33,Identity(),palette(jump),{jump},true,216)[0],"jelly teleport rejected");
        L::History epoch;epoch.Apply(1,0,Identity(),palette(a),{a},true,216);
        near.generation=1;
        Check(!epoch.Apply(2,33,Identity(),palette(near),{near},true,216)[0],"jelly generation change reseeds");
        // The combined jelly's front/back surfaces are one deforming object,
        // even though all model and instance addresses alternate each frame.
        auto scale=Identity();scale[0][0]=scale[2][2]=.15;scale[1][1]=.08;
        auto big0=jelly(10,true,scale),big0back=jelly(11,true,scale);
        L::History big;
        Check(!big.Apply(1,0,Identity(),palette(big0),{big0,big0back},true,960)[0],"combined jelly seeds coincident surfaces once");
        auto moved=scale;moved[0][3]=20;moved[0][1]=.001;
        auto big1=jelly(12,true,moved),big1back=jelly(13,true,moved);
        const auto large=big.Apply(2,33,Identity(),palette(big1),{big1,big1back},true,960);
        const auto expected=L::AffineDeformationHalf(scale,moved);
        Check(expected && large[0] && Near(*large[0],*expected,1e-5),"combined jelly preserves small shear through model swaps");
        Check(big.Apply(2,33,Identity(),palette(big1back),{big1,big1back},true,960)==large,"front and back combined-jelly surfaces agree");
        L::History unrelated;
        unrelated.Apply(1,0,Identity(),palette(a),{a},true,216);
        Check(!unrelated.Apply(2,33,Identity(),palette(big1),{big1},true,960)[0],"small and combined jelly histories cannot cross-match");
        L::GekkoJellyHistory bounded;std::vector<MM3D::LinkPoseCapture::LivePose> crowd;
        for(unsigned i=0;i<33;++i)crowd.push_back(jelly(i,false,Camera(0,i*20)));
        Check(!bounded.Apply(1,0,Identity(),crowd[0],crowd,true),"jelly pool capacity is bounded");
        Check(!bounded.Apply(2,33,Identity(),a,{a},true),"pool overflow resets history");
    }
    {
        namespace F=MM3D::FireEffectInterpolation;
        const auto values=[](unsigned kind,float phase) {
            F::Values uv{};const auto& p=F::Patterns[kind];
            for(unsigned r=0;r<F::ActiveRows(kind);++r) {
                uv[r][r%2]=p.basis[r];uv[r][3]=p.multipliers[r]*phase;
            }
            return uv;
        };
        for(unsigned kind=0;kind<std::size(F::Patterns);++kind) {
            const auto& p=F::Patterns[kind];auto a=values(kind,.25f),b=values(kind,.25f+p.step);
            Check(F::Identify(p.vertices,p.mask,6,p.destination,false,a,p.material)==kind,"fire material and UV recognized");
            auto bad=p.material;bad[0][0]^=1;
            Check(!F::Identify(p.vertices,p.mask,6,p.destination,false,a,bad),"unrelated fire material excluded");
            Check(!F::Identify(p.vertices+1,p.mask,6,p.destination,false,a,p.material),"fire mesh count verified");
            Check(!F::Identify(p.vertices,p.mask,6,p.destination,true,a,p.material),"depth writing draw excluded");
            if(kind==0){a[5][1]=1.2f;b[5][1]=1.3f;}
            F::History h;F::Key key{1,2,3,4,kind};
            Check(!h.Apply(1,key,a,true),"fire UV first frame seeds");
            const auto half=h.Apply(2,key,b,true);
            Check(half.has_value(),"fire scroll interpolates consecutive frames");
            for(unsigned r=0;r<F::ActiveRows(kind);++r)
                Check(std::abs((*half)[r][3]-(a[r][3]+b[r][3])*.5f)<1e-5,"all fire texture layers share midpoint phase");
            if(kind==0)Check(std::abs((*half)[5][1]-1.25f)<1e-6,"fire ring pulse scale interpolates too");
            Check(h.Apply(2,key,b,true)==half,"repeated fire mesh shares midpoint");
            Check(!h.Apply(2,key,b,false),"native preview suppresses fire overrides");
            if(F::ActiveRows(kind)==4) {
                auto stale=b;stale[5]={4,5,6,7};const auto out=h.Apply(2,key,stale,true);
                Check(out && (*out)[5]==stale[5],"unused fire UV rows retain current draw values");
            }
            Check(!h.Apply(4,key,b,true),"fire frame gap reseeds");
            auto next=values(kind,.25f+2*p.step);
            auto changed_key=key;changed_key[3]++;
            Check(!h.Apply(5,changed_key,next,true),"fire scene generation isolates history");
            h.Reset();Check(!h.Apply(6,key,b,true),"fire reset clears history");
            const auto wrap=F::Midpoint(kind,values(kind,1-p.step),values(kind,0));
            Check(wrap && std::abs(*F::Phase(kind,*wrap)-(1-p.step*.5f))<1e-5,"linked fire layers wrap together");
            Check(!F::Midpoint(kind,a,values(kind,.8f)),"fire unexpected phase jump stays native");
            auto malformed=b;malformed[0][2]=.01f;
            Check(!F::Phase(kind,malformed),"fire UV shear rejected");
            malformed=b;malformed[0][3]=std::numeric_limits<float>::quiet_NaN();
            Check(!F::Phase(kind,malformed),"fire nonfinite UV rejected");
            F::History conflict;conflict.Apply(1,key,a,true);conflict.Apply(2,key,b,true);
            Check(!conflict.Apply(2,key,next,true),"conflicting same-frame fire phase rejected");
            Check(!conflict.Apply(3,key,next,true),"ambiguous fire frame cannot seed a midpoint");
        }
    }
    {
        namespace G=MM3D::GaroFlameInterpolation;
        std::array<G::Vertex,20> inputs{};std::vector<std::uint32_t> order;
        for(unsigned i=0;i<20;++i) {
            inputs[i][0]={float(i/2),float(i%2),0,1};
            inputs[i][1].fill(std::numeric_limits<float>::quiet_NaN());
            for(unsigned a=2;a<5;++a)inputs[i][a]={float(i/2)/9,float(1-i%2),0,1};
        }
        for(unsigned i=0;i<9;++i)for(unsigned n:{2*i,2*i+1,2*i+2,2*i+3,2*i+3,2*i+2})order.push_back(n);
        const auto decoded=G::Decode(inputs,order);Check(decoded.has_value(),"Garo ribbon topology and unused stale color accepted");
        auto bad_order=order;bad_order[5]=1;
        Check(!G::Decode(inputs,bad_order),"broken ribbon connectors rejected");
        auto bad_inputs=inputs;bad_inputs[0][2][0]=.2f;
        Check(!G::Decode(bad_inputs,order),"unrelated ribbon UV rejected");
        bad_inputs=inputs;bad_inputs[0][0][0]=std::numeric_limits<float>::infinity();
        Check(!G::Decode(bad_inputs,order),"nonfinite ribbon position rejected");
        const auto shifted=[&](float x){auto r=*decoded;for(auto& p:r.points)p[0]+=x;return r;};
        const auto a=shifted(0),b=shifted(100),a2=shifted(10),b2=shifted(90);
        G::Key key{1,2,3,4};G::History h;
        Check(!h.Apply(1,key,a,Identity()) && !h.Apply(1,key,b,Identity()),"two sword ribbons seed independently");
        auto hb=h.Apply(2,key,b2,Identity()),ha=h.Apply(2,key,a2,Identity());
        Check(ha && hb && std::abs(ha->points[0][0]-5)<1e-6 && std::abs(hb->points[0][0]-95)<1e-6,"ribbon identity survives draw order reversal");
        Check(h.Apply(2,key,a2,Identity())->points==ha->points,"duplicate sword submission reuses midpoint");
        Check(!h.Apply(2,key,shifted(12),Identity()),"previous sword cannot be claimed twice");
        Check(!h.Apply(4,key,a2,Identity()),"ribbon frame gap reseeds");
        auto new_key=key;new_key[3]++;
        Check(!h.Apply(5,new_key,a2,Identity()),"ribbon generation change reseeds");
        h.Reset();Check(!h.Apply(6,key,a2,Identity()),"ribbon reset clears history");
        G::History crossing;crossing.Apply(1,key,a,Identity());crossing.Apply(1,key,b,Identity());
        Check(!crossing.Apply(2,key,shifted(50),Identity()),"equally close swords are ambiguous");
        G::History jump;jump.Apply(1,key,a,Identity());
        Check(!jump.Apply(2,key,shifted(1000),Identity()),"teleporting sword stays native");
        G::History camera;const auto c0=Camera(10,200),c1=Camera(15,210);
        auto local0=a,local1=a2;
        local0.points=G::Transform(a.points,MM3D::LinkPoseMath::InverseRigid(c0));
        local1.points=G::Transform(a2.points,MM3D::LinkPoseMath::InverseRigid(c1));
        camera.Apply(1,key,local0,c0);const auto half=camera.Apply(2,key,local1,c1);
        Check(half && G::Distance(G::Transform(half->points,c1),shifted(5).points)<1e-7,"ribbon midpoint excludes camera motion");
        auto scale=Identity();scale[0][0]=2;
        Check(!camera.Apply(3,key,local1,scale),"nonrigid ribbon space rejected");
    }
    {
        namespace E=MM3D::MovingEffectInterpolation;
        for(unsigned kind=0;kind<std::size(E::Patterns);++kind) {
            if(E::MirrorLight(kind))continue;
            const auto& pattern=E::Patterns[kind];E::Sample a{},b{};
            a.model=Identity();for(unsigned r=0;r<3;++r)a.model[r][r]=.01;
            if(E::Mirrored(kind))a.model[2][2]*=-1;
            for(unsigned r=0;r<E::Rows(kind);++r){a.uv[r][r%2]=pattern.basis[r];a.uv[r][3]=-.5f;}
            b=a;b.model[0][3]=2;
            for(unsigned r=0;r<E::Rows(kind);++r)b.uv[r][3]+=pattern.step[r];
            Check(E::Identify(pattern.vertices,pattern.mask,a.uv,pattern.material)==kind,"moving effect material recognized");
            auto wrong=pattern.material;wrong[0][2]^=1;
            Check(!E::Identify(pattern.vertices,pattern.mask,a.uv,wrong),"unrelated effect material rejected");
            E::History h;E::History::Key key{1,2,3,4,kind};
            Check(!h.Apply(1,key,a),"moving effect seeds its instance");
            auto half=h.Apply(2,key,b);
            Check(half&&std::abs(half->model[0][3]-1)<1e-6,"moving flame/wake origin interpolates");
            Check(half&&half->model[2][2]==a.model[2][2],"wake reflection is preserved");
            if(E::Mirrored(kind)){
                auto spin=b;spin.model=Camera(120,2);
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)spin.model[r][c]*=.0095;
                for(unsigned r=0;r<3;++r)spin.model[r][2]*=-1;
                Check(E::Midpoint(kind,a,spin).has_value(),"fast mirrored wake spin uses quaternion midpoint");
            }
            Check(h.Apply(2,key,b).has_value(),"repeated effect draw keeps immutable midpoint");
            Check(!h.Apply(4,key,b),"moving effect frame gap reseeds");
            auto jump=b;jump.model[0][3]=1000;
            Check(!E::Midpoint(kind,a,jump),"effect teleport rejected");
            jump=b;jump.uv[3][3]+=.25;
            Check(!E::Midpoint(kind,a,jump),"unknown UV speed rejected");
            if(kind<2){jump=b;jump.uv[3][3]+=2;if(kind==0)jump.uv[5][3]+=1;
                Check(E::Midpoint(kind,a,jump).has_value(),"flame layers wrap in their shared phase");}
            if(E::AnimatedColors(kind)) {
                a.colors[0]={.2f,.4f,.6f,.8f};b.colors[0]={.4f,.6f,.8f,1.f};
                const auto pulse=E::Midpoint(kind,a,b);
                Check(pulse&&std::abs(pulse->colors[0][0]-.3f)<1e-6&&
                      std::abs(pulse->colors[0][3]-.9f)<1e-6,"alien glow colors and opacity interpolate");
                auto invalid=b;invalid.colors[0][0]=std::numeric_limits<float>::quiet_NaN();
                Check(!E::Midpoint(kind,a,invalid),"invalid glow colors rejected");
                auto colored=pattern.material;colored[0][3]=123;
                Check(E::Identify(pattern.vertices,pattern.mask,a.uv,colored)==kind,"alien animated color retains material identity");
                if(kind==4) {
                    auto wrap=b;wrap.uv[0][3]+=1;wrap.uv[2][3]+=2;
                    Check(E::Midpoint(kind,a,wrap).has_value(),"alien scrolling layers interpolate across their wrap periods");
                }
            }
            h.Reset();h.Apply(1,key,a);auto other=a;other.model[0][3]=4;h.Apply(1,key,other);
            Check(!h.Apply(2,key,b),"equidistant effect instances remain native");
        }
    }
    {
        namespace C=MM3D::CoastalWaterInterpolation;
        namespace S=MM3D::SongEffectInterpolation;
        for(unsigned kind=0;kind<std::size(C::Patterns);++kind) {
            const auto& p=C::Patterns[kind];C::Values a{},b{};
            if(p.rotating) {
                const auto vortex=[](double angle,float phase) {
                    C::Values v{};
                    for(unsigned layer=0;layer<2;++layer) {
                        const float scale=layer?4:2,c=scale*std::cos(angle),s=scale*std::sin(angle);
                        v[layer*2]={c,-s,0,scale*.5f-.5f*(c-s)+(layer?0:phase)};
                        v[layer*2+1]={s,c,0,scale*.5f-.5f*(s+c)+(layer?2*phase:0)};
                    }
                    return v;
                };
                constexpr double pi=3.141592653589793;
                a=vortex(-pi+.01,1);b=vortex(-pi+.01-pi/150,1+1.f/300);
                Check(C::Identify(240,3,a)==kind,"rotating vortex material basis recognized");
                Check(C::MatchesMaterial(kind,6,7,false,p.material),"rotating vortex combiner recognized");
                S::History h;S::Key key{1,2,3,4,7|(3<<8),240,0};
                Check(!h.Apply(1,key,a,true,3,kind),"rotating vortex seeds history");
                const auto half=h.Apply(2,key,b,true,3,kind);
                const auto expected=vortex(-pi+.01-pi/300,1+1.f/600);
                Check(half && C::VortexUV(*half),"vortex angle wrapping preserves scale and pivot");
                if(half)for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)
                    Check(std::abs((*half)[r][c]-expected[r][c])<1e-5,"vortex rotation and linked scroll are halfway");
                b[5]={9,8,7,6};const auto repeat=h.Apply(2,key,b,true,3,kind);
                Check(repeat && half && (*repeat)[0]==(*half)[0] && (*repeat)[5]==b[5],
                    "repeated vortex draws retain rotated basis and inactive rows");
                auto wrong=b;wrong[2][0]+=.1f;
                Check(!C::Identify(240,3,wrong),"unrelated rotating UV basis rejected");
                Check(!C::VortexHalf(a,vortex(-pi+.01-pi/150,2)),"vortex phase reset rejected");
                Check(!h.Apply(4,key,b,true,3,kind),"vortex frame gaps reseed");
                continue;
            }
            for(unsigned r=0;r<C::Rows(kind);++r) {
                for(unsigned c=0;c<3;++c)a[r][c]=p.basis[r][c];
                a[r][3]=-3;
            }
            b=a;
            for(unsigned r=0;r<C::Rows(kind);++r)b[r][3]+=p.step[r];
            if(p.vertices==72 && p.mask==7) {
                auto large=a,advanced=a;
                constexpr std::array<float,6> phases{-10.5730343f,2.5f,91.8100586f,1.0058957f,-94.7539902f,-2.1956034f};
                for(unsigned r=0;r<C::Rows(kind);++r) {
                    large[r][3]=phases[r];advanced[r][3]=phases[r]+p.step[r];
                }
                // Actual native float accumulation can differ by one ULP.
                advanced[4][3]=large[4][3]-.05001831055f;
                Check(C::Midpoint(kind,large,advanced).has_value(),"large waterfall phases tolerate native float rounding");
                advanced[4][3]-=.001f;
                Check(!C::Midpoint(kind,large,advanced),"rounding allowance still rejects changed scroll rates");
            }
            Check(C::MatchesMaterial(kind,p.source,p.destination,false,p.material),"coastal water material recognized");
            Check(C::IdentifyMaterial(p.vertices,p.mask,a,p.source,p.destination,false,p.material)==kind,
                "same-size water meshes resolve by full material");
            auto wrong=p.material;wrong[0][2]^=1;
            Check(!C::MatchesMaterial(kind,p.source,p.destination,false,wrong),"different combiner rejected");
            Check(!C::IdentifyMaterial(p.vertices,p.mask,a,p.source,p.destination,false,wrong),
                "unknown water material does not select an equal-size mesh");
            Check(!C::MatchesMaterial(kind,p.source,p.destination,true,p.material),"opaque geometry excluded");
            Check(!C::MatchesMaterial(kind,p.source==6?2:6,p.destination,false,p.material),"different source blend rejected");
            S::History h;S::Key key{1,2,3,4,p.destination|(p.mask<<8),p.vertices,0};
            Check(!h.Apply(1,key,a,true,p.mask,kind),"coastal water seeds history");
            const auto mid=h.Apply(2,key,b,true,p.mask,kind);
            Check(mid.has_value(),"vortex and waterfall scroll at midpoint");
            for(unsigned r=0;r<C::Rows(kind);++r)
                Check(std::abs((*mid)[r][3]-(a[r][3]+b[r][3])*.5f)<1e-6,"every active layer is halfway");
            if(p.mask!=7) {
                b[5]={9,8,7,99};
                const auto repeat=h.Apply(2,key,b,true,p.mask,kind);
                Check(repeat && (*repeat)[5]==b[5],"inactive UV rows remain native");
            }
            Check(!h.Apply(2,key,b,false,p.mask,kind),"disabled interpolation remains native");
            Check(!h.Apply(4,key,b,true,p.mask,kind),"coastal frame gap reseeds");
            if(p.alternate_step!=std::array<float,6>{}) {
                auto alternate=a;for(unsigned r=0;r<C::Rows(kind);++r)alternate[r][3]+=p.alternate_step[r];
                Check(C::Midpoint(kind,a,alternate).has_value(),"shared water mesh accepts both measured speeds");
            }
            if(p.wrap!=std::array<float,6>{}) {
                auto wrapped=b;
                for(unsigned r=0;r<C::Rows(kind);++r)if(p.wrap[r])
                    wrapped[r][3]-=std::copysign(p.wrap[r],p.step[r]);
                const auto loop=C::Midpoint(kind,a,wrapped);
                Check(loop.has_value(),"captured water cycle wraps smoothly");
                if(loop)for(unsigned r=0;r<C::Rows(kind);++r)
                    Check(std::abs((*loop)[r][3]-(wrapped[r][3]-.5f*p.step[r]))<2e-6,
                        "water wrap follows the short scroll step");
                for(unsigned r=0;r<C::Rows(kind);++r)if(p.wrap[r]) {
                    wrapped[r][3]+=.5f*p.wrap[r];break;
                }
                Check(!C::Midpoint(kind,a,wrapped),"nonperiodic water phase jump rejected");
            }
            b[0][3]+=1;
            Check(!h.Apply(5,key,b,true,p.mask,kind),"coastal phase reset does not streak");
        }
    }
    {
        namespace F=MM3D::ForestParticleInterpolation;
        const auto star=[](float x,float y,float alpha) {
            F::Quad q{};
            for(unsigned i=0;i<4;++i) {
                q.vertices[i][0]={x+float(i/2)*2,y+float(i%2)*2,0,1};
                q.vertices[i][1]={55.f/255,55.f/255,0,alpha};
                q.vertices[i][2]={float(i/2)*2,float(1-i%2)*2,0,1};
            }
            return q;
        };
        const std::vector<F::Quad> old{star(0,0,1),star(1,4,1)};
        const std::vector<F::Quad> now{star(0,5,200.f/255),star(1,9,1)};
        Check(F::BossMatch(now[0],old,true)==0,"vertical neighbors preserve boss particle identity");
        F::History h;h.Apply(1,1,1,old);
        const auto half=h.Apply(2,1,1,now);
        Check(half && std::abs(F::Center((*half)[0])[1]-3.5f)<1e-6,"first large lifetime fade now interpolates");
        Check(!F::BossMatch(star(0,-5,1),old,true),"new boss star at base cannot borrow older star");
        Check(!F::BossMatch(star(0,5,1),{star(0,0,.5f)},true),"respawn brightness reset rejected");
        Check(!F::BossMatch(star(0,5,1),{old[0],old[0]},true),"ambiguous coincident stars stay native");
    }
    {
        namespace B=MM3D::GreatBayWaterInterpolation;
        namespace S=MM3D::SongEffectInterpolation;
        for(unsigned kind=0;kind<std::size(B::Patterns);++kind) {
            const auto& pattern=B::Patterns[kind];
            for(unsigned vertices:pattern.meshes) {
                if(!vertices)continue;
                auto a=B::Basis(kind),b=a;
                for(unsigned r=0;r<B::Rows(kind);++r){a[r][3]=3.25f;b[r][3]=a[r][3]+pattern.step[r];}
                Check(B::Identify(vertices,pattern.mask,a)==kind,"Great Bay water basis recognized");
                auto tev=B::Material(kind);
                Check(B::MatchesMaterial(kind,pattern.source,pattern.destination,pattern.depth_write,tev),"Great Bay material accepted");
                tev[0][2]^=1;
                Check(!B::MatchesMaterial(kind,pattern.source,pattern.destination,pattern.depth_write,tev),"different water combiner rejected");
                Check(!B::MatchesMaterial(kind,pattern.source,pattern.destination,!pattern.depth_write,B::Material(kind)),"opaque pipe and transparent water stay separate");
                S::History h;S::Key key{100,200,300,400,pattern.destination|(pattern.mask<<8),vertices,0};
                Check(!h.Apply(1,key,a,true,pattern.mask),"Great Bay water seeds history");
                const auto half=h.Apply(2,key,b,true,pattern.mask);
                Check(half.has_value(),"Great Bay water produces midpoint");
                for(unsigned r=0;r<B::Rows(kind);++r)
                    Check(std::abs((*half)[r][3]-(a[r][3]+b[r][3])*.5f)<1e-6,"every active water texture scrolls at midpoint");
                Check(!h.Apply(2,key,b,false,pattern.mask),"disabled water retains native values");
                if(pattern.mask==3) {
                    auto stale=b;stale[4]={7,8,9,10};stale[5]={11,12,13,14};
                    const auto repeat=h.Apply(2,key,stale,true,pattern.mask);
                    Check(repeat && (*repeat)[4]==stale[4] && (*repeat)[5]==stale[5],"inactive water textures preserved on repeated draw");
                }
                Check(!h.Apply(4,key,b,true,pattern.mask),"Great Bay water frame gap reseeds");
                auto jump=b;jump[0][3]+=2;
                Check(!B::Midpoint(kind,b,jump),"water phase reset stays native");
                auto invalid=b;invalid[0][0]+=.01f;
                Check(!B::Identify(vertices,pattern.mask,invalid),"different UV basis rejected");
                invalid=b;invalid[1][3]=std::numeric_limits<float>::infinity();
                Check(!B::Midpoint(kind,b,invalid),"nonfinite water offset rejected");
            }
        }
    }
    {
        using MM3D::WidescreenProbe::LocalLayoutPane;
        constexpr auto shader=0x9b343fea686d98e0ULL,swizzle=0xac22b79e29209c01ULL;
        const std::array<float,4> right{301.05f,0,0,20.9f},ry{48.35f,0,20.9f,0};
        const std::array<float,4> left{-6.6f,0,0,22.8f},ly{78.6f,0,22.8f,0};
        Check(LocalLayoutPane(shader,swizzle,6,0,{0,3,0,1},right,ry),
              "Schedule detail flash keeps portrait widescreen scaling");
        Check(LocalLayoutPane(shader,swizzle,6,0,{1,3,0,1},left,ly),
              "Schedule row flash keeps portrait widescreen scaling");
        Check(!LocalLayoutPane(shader,swizzle,6,0,{0,3,0,1},{0,0,0,400},{0,0,240,0}),
              "fullscreen fades retain saved widescreen bypass");
        Check(!LocalLayoutPane(shader,swizzle,6,0,{0,3,0,1},{-13.333f,0,0,426.667f},{60,0,27.6f,0}),
              "full-width solid banners retain their rule");
        Check(!LocalLayoutPane(shader,swizzle,6,1,{0,3,0,1},right,ry)&&
              !LocalLayoutPane(shader,swizzle,12,0,{0,3,0,1},right,ry)&&
              !LocalLayoutPane(shader+1,swizzle,6,0,{0,3,0,1},right,ry),
              "textured draws, batches and world shaders are excluded");
        using MM3D::WidescreenProbe::PhotoFrameBatch;
        std::array<std::array<float,4>,96> photo{};
        const auto row=[&](unsigned i){return photo.at(i);};
        for(unsigned panes:{1u,2u})for(unsigned color_base:{60u,91u,92u})
            for(float scale:{.45f,.5f,.8f,.9f,.95f,1.f,1.2f})for(float alpha:{0.f,.3f,.75f,1.f}) {
            photo={};const unsigned frame=10+4*(panes-1);
            photo[10]={0,3,0,-95};photo[11]={-13.333f,0,0,426.667f};photo[12]={0,0,240,0};
            photo[95]={alpha*.5f,0,0,0};
            photo[frame]={0,3,0,float(color_base)};
            photo[frame+1]={-20,0,0,207.5f*16/15*scale};photo[frame+2]={25,0,128*scale,0};
            photo[color_base]={alpha,1,1,1};
            photo[color_base+1]={alpha,214.f/255,215.f/255,211.f/255};
            photo[color_base+2]={alpha,142.f/255,147.f/255,134.f/255};
            photo[color_base+3]={alpha,100.f/255,106.f/255,87.f/255};
            if(panes==2 && color_base==92)continue; // The backdrop needs a separate color register.
            Check(PhotoFrameBatch(shader,swizzle,panes*6,0,row),
                "photo viewer/animation keeps correction across packing, opacity, scale and position");
            photo[color_base+1][2]=0;
            Check(!PhotoFrameBatch(shader,swizzle,panes*6,0,row),"unrelated gradient retains fullscreen rule");
        }
        photo={};photo[10]={0,3,0,-95};photo[11]={0,0,0,426.667f};photo[12]={0,0,240,0};
        photo[95]={.5f,0,0,0};
        Check(!PhotoFrameBatch(shader,swizzle,6,0,row),"fullscreen fade alone retains fullscreen rule");
        auto invalid=right;invalid[0]=std::numeric_limits<float>::quiet_NaN();
        Check(!LocalLayoutPane(shader,swizzle,6,0,{0,3,0,1},invalid,ry),
              "invalid pane bounds are rejected");
    }
    {
        namespace B=MM3D::BannerInterpolation;
        B::Values a{};B::Tev tev{};
        for(unsigned i=0;i<6;++i)tev[i]={i==0?0x0ee00ee0u:i==1?0x0e3f0e3fu:0x0eef0eefu,
            i==1?32u:0u,i<2?0x10001u:0u,i==0?0x40ffffffu:0xffffffffu,0};
        a[10]={1,3,94,-93};a[14]={1,3,94,-92};
        a[11]={70,0,0,12.8f};a[12]={71,0,14.4f,0};a[13]={0,1,0,0};
        a[15]={69.2f,0,0,12.8f};a[16]={70.2f,0,14.4f,0};a[17]={0,1,0,0};
        a[94]={0,.8125f,0,.0625f};a[95]={0,.140625f,.0703125f,0};
        a[93]={.9f,0,0,0};a[92]={1,244.f/255,244.f/255,244.f/255};
        auto b=a;for(unsigned r:{11u,12u,15u,16u})b[r][0]-=.4f;
        b[13][0]=b[17][0]=.000625f;
        B::History h;
        Check(B::Text(a,12,tev),"location text and paired shadows recognized");
        Check(!h.Apply(1,{1,2,3,4},a,12,tev),"location text seeds history");
        tev[0][3]=0x58ffffff;
        const auto half=h.Apply(2,{1,2,3,4},b,12,tev);
        Check(half&&std::abs(half->values[11][0]-69.8f)<.0001f&&
            std::abs(half->values[12][0]-70.8f)<.0001f,"location title moves diagonally at midpoint");
        Check(half&&half->values[13][0]==.0003125f&&std::abs(half->alpha-76.f/255)<.0001f,
              "location title interpolates tilt and combiner fade");
        Check(half&&half->values[94]==a[94]&&half->values[92]==a[92],"banner atlas and tint stay native");
        Check(!h.Apply(4,{1,2,3,4},b,12,tev),"banner frame gaps reseed");
        Check(!h.Apply(5,{1,2,3,0},b,12,tev),"banner requires submitted owner");
        auto wrong=b;wrong[95][2]=.0625f;
        Check(!B::Text(wrong,12,tev),"ordinary font geometry excluded");
        wrong=b;wrong[93][0]=1;
        Check(!B::Text(wrong,12,tev),"different shadow style excluded");
        wrong=b;wrong[13][0]=std::numeric_limits<float>::quiet_NaN();
        Check(!B::Text(wrong,12,tev),"nonfinite banner rejected");
        h.Reset();h.Apply(1,{1,2,3,4},a,12,tev);
        wrong=a;wrong[11][0]-=1;wrong[15][0]-=1;
        Check(!h.Apply(2,{1,2,3,4},wrong,12,tev),"unrelated horizontal text motion excluded");
        B::Publish(10,2,42);Check(B::Owner(10,2)==42&&B::Owner(11,2)==42,"banner ownership survives asynchronous submission");
        B::Publish(11,2,0);Check(!B::Owner(11,2)&&!B::Owner(12,2),"inactive banner stamp clears ownership");
        B::Publish(12,2,42);Check(!B::Owner(14,2)&&!B::Owner(12,3),"stale banner and wrong generation excluded");
    }
    {
        namespace W=MM3D::WaterModelInterpolation;
        W::Sample a{Identity(),{},1};
        a.model[0][0]=a.model[2][2]=.024;a.model[1][1]=0;
        a.model[1][3]=430;
        for(unsigned r=0;r<6;++r)a.uv[r][r%2]=W::Patterns[0].basis[r];
        W::Tev tev{};for(auto& stage:tev)stage={268374015,0,0,0xffffffff,0};
        tev[0]={4392259,139264,589832,0xff000000,0};tev[1]={250547743,0,65536,0xffffffff,0};
        Check(W::Identify(288,7,7,a.uv,tev)==0,"water splash material recognized");
        Check(!W::Identify(288,3,7,a.uv,tev),"wrong water texture count rejected");
        auto b=a;b.model[0][0]=b.model[2][2]=.025;b.model[1][1]=.0048;b.alpha=.94f;
        for(unsigned r=0;r<6;++r)b.uv[r][3]+=W::Patterns[0].step[r];
        W::History h;const W::History::Key key{1,2,3,4,0};
        Check(!h.Apply(1,key,a),"collapsed splash seeds history");
        const auto half=h.Apply(2,key,b);
        Check(half&&std::abs(half->model[1][1]-.0024)<1e-8,
              "water splash grows smoothly from zero height");
        Check(half&&std::abs(half->alpha-.97f)<1e-6&&std::abs(half->uv[1][3]+1.f/120)<1e-6,
              "water texture motion and fading interpolate together");
        Check(h.Apply(2,key,b)&&W::Same(*h.Apply(2,key,b),*half),"duplicate water layers reuse midpoint");
        auto elsewhere=b;elsewhere.model[0][3]=2;
        Check(!h.Apply(2,key,elsewhere),"new impact center cannot inherit existing splash");
        Check(!h.Apply(4,key,b),"water sample gap rejects old impact");
        auto changed_key=key;changed_key[3]++;
        Check(!h.Apply(5,changed_key,b),"water generation change seeds new history");
        auto bad=b;bad.model[0][1]=.1;
        Check(!h.Apply(6,changed_key,bad),"unrecognized water deformation rejected");
        W::History crossing;auto left=a,right=a;
        left.model[0][0]=left.model[2][2]=.02;
        right.model[0][0]=right.model[2][2]=.028;
        crossing.Apply(1,key,left);crossing.Apply(1,key,right);
        auto middle=a;middle.model[1][1]=.004;
        Check(!crossing.Apply(2,key,middle),"ambiguous concentric water layers stay native");
        W::History wrap;auto old=a,next=b;old.uv[1][3]=-.99f;
        next.uv[1][3]=old.uv[1][3]+W::Patterns[0].step[1]+1;
        wrap.Apply(1,key,old);auto wrapped=wrap.Apply(2,key,next);
        Check(wrapped&&std::abs(wrapped->uv[1][3]-(next.uv[1][3]+1.f/120))<1e-6,
              "water UV wrap follows short path");
        // Native model-view receives half of the model motion and exactly one
        // camera transform, including the singular flattened surface wave.
        auto camera=Camera(30,200);
        if(half)Check(Near(Multiply(camera,Multiply(InverseRigid(camera),half->model)),half->model,.00001),
                      "water model midpoint preserves world pose through camera conversion");
    }
    {
        using namespace MM3D::LinkInterpolation;
        MM3D::LinkPoseCapture::LivePose water;
        water.signature=0x265c0b54e456fdffull;water.pose.instance=123;water.model=456;
        water.words={0xffff0000,1};water.bind.assign(2,MM3D::LinkPoseMath::Flatten(Identity()));
        water.pose.bones=water.bind;
        Palette palette;palette.fill(Identity());MM3D::LinkInterpolation::History h;
        Check(GyorgWaterBone(palette,water,528),"Gyorg water surface owns its direct animated bone");
        Check(!GyorgWaterBone(palette,water,576),"arena scenery cannot use water bone");
        h.Apply(1,0,Identity(),palette,{water},true,528);
        water.pose.bones[1][7]=2;palette[0]=MM3D::LinkPoseMath::Expand(water.pose.bones[1]);
        const auto half=h.Apply(2,33,Identity(),palette,{water},true,528);
        Check(half[0]&&std::abs((*half[0])[1][3]-1)<1e-6,"water landing wobble leaves resting pose at 60fps");
        auto clone=water;clone.pose.instance++;
        Check(!h.Apply(3,66,Identity(),palette,{water,clone},true,528)[0],"duplicate arena owners rejected");
    }
    {
        namespace W=MM3D::WorldMapInterpolation;
        Matrix a=Identity(),b=Identity(),p{};
        a[1]={0,.7592544555664062,.6507938504219055,239.99998474121094};
        a[2]={0,-.6507938504219055,.7592544555664062,280};
        b[0][3]=-.04417778551578522;
        b[1]={0,.7559844851493835,.6545894742012024,238.65383911132812};
        b[2]={0,-.6545894742012024,.7559844851493835,276.12481689453125};
        p[0][1]=3.734949827194214;p[1][0]=-2.2409701347351074;
        p[2]={0,0,1.0003907680511475,5.001953601837158};p[3][2]=-1;
        auto q=p;q[0][1]=3.7526111602783203;q[1][0]=-2.2515666484832764;
        const auto saved=q;
        const auto view=[](const Matrix& camera) {
            auto model=Identity();for(unsigned i=0;i<3;++i)model[i][i]=.04;
            return Multiply(InverseRigid(camera),model);
        };
        W::History h;
        Check(W::Projection(p)&&W::Projection(q),"captured world map lenses recognized");
        Check(!h.Apply(10,0,123,1,a,p,view(a)),"world map first frame seeds history");
        auto gameplay=p;gameplay[2][2]=1.0000892877578735;gameplay[2][3]=5.000446796417236;
        Check(!h.Apply(10,1,123,1,Camera(40,900),gameplay,Identity()),"unrelated world draw stays native");
        Check(!h.Apply(11,32,123,1,Camera(40,900),gameplay,Identity()),"paused world before map does not consume map frame");
        const auto half=h.Apply(11,33,123,1,b,q,view(b));
        Check(half&&!Near(*half,q,.001),"map zoom gets an intermediate camera and lens");
        auto lens=p;
        for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c)lens[r][c]=(p[r][c]+q[r][c])*.5;
        auto model=Identity();for(unsigned i=0;i<3;++i)model[i][i]=.04;
        // Native vertices already contain the current view. The new projection
        // must produce the halfway view of the same unchanged model, once.
        const auto actual=Multiply(*half,Multiply(InverseRigid(b),model));
        const auto expected=Multiply(lens,Multiply(InverseRigid(Midpoint(a,b)),model));
        Check(Near(actual,expected,.0001),"map vertices receive one halfway camera transform");
        Check(Near(q,saved,0),"native map projection unchanged");
        Check(Near(*h.Apply(11,34,123,1,b,q,view(b)),*half,0),"map materials share a single frame history");
        Check(!h.Apply(11,34,123,1,b,q,view(b),true),"map comparison remains native");
        Check(!h.Apply(13,99,123,1,b,q,view(b)),"map frame gap seeds history");
        Check(!h.Apply(14,132,124,1,b,q,view(b)),"map owner change resets history");
        Check(!h.Apply(15,165,124,2,b,q,view(b)),"map generation change resets history");
        Check(!h.Apply(16,198,0,2,b,q,view(b)),"closed map cannot authorize interpolation");
        Check(!h.Apply(17,231,124,2,b,q,view(b)),"map reopening seeds fresh history");
        Check(h.Apply(18,264,124,2,a,p,view(a)).has_value(),"map zoom out interpolates too");
        auto wrong=q;wrong[2][3]=5.000446796417236;
        Check(!W::Projection(wrong)&&!h.Apply(19,297,124,2,b,wrong,view(b)),"gameplay clip planes excluded");
        wrong=q;wrong[1][0]*=1.1;
        Check(!W::Projection(wrong),"different aspect excluded from map camera");
        wrong=q;wrong[0][1]=std::numeric_limits<double>::quiet_NaN();
        Check(!W::Projection(wrong),"nonfinite map lens rejected");
        Check(!W::Projection(Identity()),"map UI orthographic draws excluded");
        // A second live map capture has a slightly different clip range. The
        // old single-capture fingerprint rejected every one of its map draws.
        auto recent=p;recent[2]={0,0,1.0003937482833862,5.001968860626221};
        auto zoom=recent;zoom[0][1]=3.9386167526245117;zoom[1][0]=-2.3631701469421387;
        Check(W::Projection(recent)&&W::Projection(zoom),"second captured map clip range recognized");
        W::History recent_history;
        Check(!recent_history.Apply(1,0,42,1,a,recent,view(a)),"second map capture seeds history");
        const auto recent_half=recent_history.Apply(2,33,42,1,b,zoom,view(b));
        Check(recent_half&&!Near(*recent_half,zoom,.001),"second captured map zoom interpolates");
        recent[2][3]+=.00001;
        Check(!W::Projection(recent),"map depth row must preserve the verified near plane");
        recent=zoom;recent[2][2]=1.00042;recent[2][3]=5*recent[2][2];
        Check(W::Projection(recent),"unseen far plane accepted with map ownership");
        W::History unseen;
        Check(!unseen.Apply(1,0,42,1,a,recent,Identity()),"map owner and lens alone cannot authorize world geometry");
        Check(!unseen.Apply(1,0,42,1,a,recent,view(a)),"unseen far plane seeds owned map history");
        Check(unseen.Apply(2,33,42,1,b,recent,view(b)).has_value(),"unseen far plane zoom interpolates");
        auto marker=view(b);marker[0][3]+=30;
        Check(unseen.Apply(2,34,42,1,b,recent,marker).has_value(),"map markers share the seeded frame camera");
        Check(!unseen.Apply(3,66,42,1,b,recent,marker),"markers cannot authorize the following frame alone");
        auto invalid=view(b);invalid[0][0]=std::numeric_limits<double>::quiet_NaN();
        Check(!unseen.Apply(3,66,42,1,b,recent,invalid),"invalid map model rejected");
        // Partially explored map: same owned map camera and near plane, but
        // a 20,000-unit far plane. Previously all draws failed recognition.
        auto sparse=p;sparse[2]={0,0,1.0002501010894775,5.001250267028809};
        auto sparse_zoom=sparse;sparse_zoom[0][1]=3.8065857887268066;
        sparse_zoom[1][0]=-2.2839512825012207;
        W::History sparse_history;
        Check(W::Projection(sparse)&&W::Projection(sparse_zoom),"partially explored map projection recognized");
        Check(!sparse_history.Apply(1,0,42,1,a,sparse,view(a)),"sparse map seeds history");
        auto sparse_half=sparse_history.Apply(2,33,42,1,b,sparse_zoom,view(b));
        Check(sparse_half&&!Near(*sparse_half,sparse_zoom,.001),"partially explored map zoom interpolates");
        Check(!sparse_history.Apply(3,66,0,1,b,sparse_zoom,view(b)),"sparse map still requires a live owner");
        auto not_map=sparse;not_map[2][2]=1.0003;not_map[2][3]=5*not_map[2][2];
        Check(W::Projection(not_map),"far planes between old map bands are supported");
        not_map=sparse;not_map[2][3]+=.001;
        Check(!W::Projection(not_map),"sparse map requires the same near plane");

    }
    {
        namespace C = MM3D::ChestParticleInterpolation;
        const auto make = [](float x, float size) {
            std::vector<C::Vertex> v(5);
            for (unsigned k = 0; k < 4; ++k) {
                v[k][0] = {x+size*(float(k/2)-.5f), size*(float(k%2)-.5f), 0, 1};
                v[k][1] = {0, 105.f/255, 1, 1};
                v[k][2] = {float(k/2), float(1-k%2), 0, 1};
            }
            return v;
        };
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        auto a = C::Decode(make(0, 2), order), b = C::Decode(make(2, 20), order);
        Check(a && b, "chest particle size range decodes");
        Check(C::Decode(make(0,24.25f),order).has_value(),"large Snowhead chest sparkle is recognized");
        Check(!C::Decode(make(0,30),order),"unrelated oversized sparkle rejected");
        Check(!C::Decode(make(0, 0), order), "collapsed chest particle stays native");
        auto wrong = make(0, 2); wrong[0][2][0] = .5f;
        Check(!C::Decode(wrong, order), "chest requires square UVs");
        wrong = make(0, 2); for (auto& v : wrong) v[1] = {1,1,1,1};
        Check(!C::Decode(wrong, order), "chest rejects unrelated white particles");
        std::array<std::array<std::uint32_t,5>,6> tev{{
            {249564899,0,0,0,2}, {250482430,0,1,38655,0}, {249564976,0,8,0,0},
            {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0}, {268374015,0,0,0xffffffff,0}}};
        Check(C::Material(tev), "chest exact material accepted");
        tev[1][3] = 0;
        Check(!C::Material(tev), "chest rejects different material constant");
        C::History h; C::Key key{1,2};
        Check(!h.Apply(1,key,*a), "chest history seeds without changing birth");
        const auto half = h.Apply(2,key,*b);
        Check(half && std::abs(C::Center(half->front())[0]-1)<.001f &&
            std::abs(half->front().vertices[1][0][1]-5.5f)<.001f,
            "chest motion and randomly changing size interpolate");
        Check(half && half->front().indices == b->front().indices &&
            half->front().vertices[0][2] == b->front().vertices[0][2] &&
            half->front().vertices[0][1] == b->front().vertices[0][1],
            "chest preserves source indices UVs and tint");
        Check(h.Apply(2,key,*b)->front().vertices == half->front().vertices,
              "repeated chest draw uses immutable previous history");
        Check(!h.Apply(4,key,*b), "chest rejects gaps in native frames");
        h.Reset();
        auto first = *a; first.push_back(C::Decode(make(30,5),order)->front());
        h.Apply(1,key,first);
        auto next = *C::Decode(make(32,10),order); next.push_back(b->front());
        next.push_back(C::Decode(make(60,3),order)->front());
        const auto reordered = h.Apply(2,key,next);
        Check(reordered && std::abs(C::Center((*reordered)[0])[0]-31)<.001f &&
            std::abs(C::Center((*reordered)[1])[0]-1)<.001f &&
            (*reordered)[2].vertices == next[2].vertices,
            "chest reorder and new particle do not reuse unrelated histories");
        const auto compacted = h.Apply(3,key,*C::Decode(make(34,2),order));
        Check(compacted && std::abs(C::Center(compacted->front())[0]-33)<.001f,
              "chest compaction retains spatial identity");
        Check(!h.Apply(4,{1,3},*b), "chest scene generation isolates history");
        h.Reset(); Check(!h.Apply(5,key,*b), "chest reset clears history");
        h.Reset(); auto nearby = *a; nearby.push_back(C::Decode(make(2,2),order)->front());
        h.Apply(1,key,nearby);
        Check(!h.Apply(2,key,*C::Decode(make(1,4),order)), "ambiguous chest match stays native");
    }
    {
        namespace F=MM3D::FireworkInterpolation;
        const auto make=[](float x,float alpha) {
            std::vector<F::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+20*float(k/2),20*float(k%2),0,1};
                v[k][1]={1,1,200.f/255,alpha};v[k][2]={2*float(k/2),2*float(1-k%2),0,1};
            }
            return v;
        };
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        auto a=F::Decode(make(0,1.56f),order),b=F::Decode(make(5,1.4f),order);
        Check(a&&b,"firework squares retain overbright lifetime alpha");
        Check(!MM3D::WaterParticleInterpolation::DecodeQuads(make(0,1.56f),order),"other particle decoders keep normal alpha bounds");
        auto wrong=make(0,1);wrong[0][2][0]=.2f;
        Check(!F::Decode(wrong,order),"unrelated firework UV rejected");
        wrong=make(0,1);for(auto& v:wrong)v[1]={1,1,1,1};
        Check(!F::Decode(wrong,order),"ordinary white spark not classified as firework");
        F::History h;F::Key key{1,2};Check(!h.Apply(1,key,*a),"firework seeds history");
        const auto half=h.Apply(2,key,*b);
        Check(half&&std::abs((*half)[0].vertices[0][0][0]-2.5f)<.001f&&
            std::abs((*half)[0].vertices[0][1][3]-1.48f)<.001f,"firework motion and fade interpolate");
        Check(h.Apply(2,key,*b)&&F::Same(h.Apply(2,key,*b)->front(),half->front()),"repeated firework draw stays stable");
        Check(!h.Apply(3,key,*a),"new brighter firework cannot inherit fading history");
        Check(!h.Apply(5,key,*b),"firework frame gap rejected");
        h.Reset();auto first=*a;auto distant=F::Decode(make(150,1.56f),order);first.push_back(distant->front());
        h.Apply(1,key,first);auto next=*F::Decode(make(155,1.4f),order);next.push_back(b->front());
        const auto reordered=h.Apply(2,key,next);
        Check(reordered&&std::abs(reordered->front().vertices[0][0][0]-152.5f)<.001f&&
            std::abs((*reordered)[1].vertices[0][0][0]-2.5f)<.001f,"firework matching survives particle reorder");
        h.Reset();auto crowded=*a;crowded.push_back(a->front());h.Apply(1,key,crowded);
        auto pair=*b;pair.push_back(b->front());
        Check(!h.Apply(2,key,pair),"ambiguous overlapping fireworks stay native");
    }
    {
        namespace F=MM3D::FileSelectInterpolation;
        const auto plate=[](float border) {
            F::Values u{};const float w=18.5f*border,h=9*border,cx=207.1f,cy=117.72f+.75f*border;
            const float xs[]{cx+w/2-border,cx-w/2+border,cx-w/2};
            const float ys[]{cy+h/2-border,cy-h/2+border,cy-h/2};
            for(unsigned q=0;q<9;++q) {
                const unsigned r=10+4*q,uv=94-2*q,col=q%3,row=q/3;
                u[r]={0,3,float(uv),1};u[r+1]={xs[col],0,0,col==1?w-2*border:border};
                u[r+2]={ys[row],0,row==1?h-2*border:border,0};u[r+3]={0,1,0,0};
                u[uv]={0,col==2?.578125f:.640625f,0,col==1?0.f:.0625f};
                u[uv+1]={0,row==2?.875f:.9375f,row==1?0.f:.0625f,0};
            }
            return u;
        };
        auto a=plate(6),b=plate(8);F::Key key{1,2,3,4};F::History h;
        Check(F::PopupHistory::Recognized(a,54),"confirmation plate recognized by full nine-slice grid");
        Check(!h.Apply(1,key,a,54,true,{},1.f/3),"confirmation plate seeds");
        auto half=h.Apply(2,key,b,54,true,{},2.f/3);
        Check(half&&half->rectangles&&std::abs((*half->rectangles)[0][3]-7)<.001f&&
            half->alpha&&std::abs(*half->alpha-.5f)<.001f,"confirmation plate growth and fade share midpoint");
        Check(std::abs((*half->rectangles)[1][0]-plate(7)[12][0])<.001f,"popup follows captured layout pivot");
        auto close=h.Apply(3,key,a,54,true,{},1.f/3);
        Check(close&&close->rectangles,"confirmation close interpolates");
        Check(!h.Apply(5,key,b,54,true,{},2.f/3),"popup capture gap stays native");
        auto bad=b;bad[94][1]+=.1f;
        Check(!F::PopupHistory::Recognized(bad,54),"unrelated nine-slice atlas rejected");
        bad=b;bad[15][0]+=2;
        Check(!F::PopupHistory::Recognized(bad,54),"broken nine-slice grid rejected");
        h.Reset();h.Apply(1,key,a,54,true,{},.5f);
        bad=b;for(unsigned q=0;q<9;++q)bad[11+4*q][0]+=20;
        const auto jump=h.Apply(2,key,bad,54,true,{},.6f);
        Check(!jump||!jump->rectangles,"different popup pivot cannot inherit scale");
        h.Reset();h.Apply(1,key,a,54,true,{},.5f);
        auto other=key;other[3]++;
        Check(!h.Apply(2,other,b,54,true,{},.6f),"different menu owner does not reuse popup");
    }
    for(unsigned layer=0;layer<5;++layer) {
        namespace S=MM3D::SongEffectInterpolation;
        constexpr unsigned vertices[]{720,288,336,1440,1440};
        const unsigned mask=layer==2?3:7;
        const float step=layer<3?-1.f/160:-1.f/120;
        const auto make=[&](float phase) {
            S::Values v{};
            constexpr std::array<std::array<float,6>,5> basis{{
                {5,1,10,6,-8,2},{6,1,16,4,-24,7},{1,2,-2,3,0,0},
                {1,1,4,3,2,2},{1,1,2,3,2,2}}};
            for(unsigned r=0;r<6;++r)v[r][r%2]=basis[layer][r];
            if(layer==0) {v[0][3]=phase;v[3][3]=3*phase;v[5][3]=2*phase;}
            if(layer==1) {v[3][3]=2*phase;v[5][3]=7*phase;}
            if(layer==2) {v[1][3]=phase;v[3][3]=3*phase;v[4]={9,8,7,6};v[5]={5,4,3,2};}
            if(layer==3) {v[1][3]=phase-.1f;v[2][3]=2*phase;v[3][3]=3*phase-.3f;}
            if(layer==4) {v[1][3]=phase;v[2][3]=2*phase;v[3][3]=6*phase;}
            return v;
        };
        const auto same=[](const S::Values& a,const S::Values& b) {
            for(unsigned r=0;r<6;++r)for(unsigned c=0;c<4;++c)
                if(std::abs(a[r][c]-b[r][c])>1e-5f)return false;
            return true;
        };
        S::History h;S::Key key{1,2,3,4,mask<<8,vertices[layer],0};
        const auto a=make(-.5f),b=make(-.5f+step);
        Check(S::Recognized(vertices[layer],a,mask),"moon effect layers recognized");
        Check(!S::MoonEffect(vertices[layer],a,1),"moon active texture mask checked");
        auto wrong=a;wrong[0][0]+=.1f;
        Check(!S::MoonEffect(vertices[layer],wrong,mask),"unrelated UV basis excluded");
        wrong=a;wrong[3][3]+=.01f;
        Check(!S::MoonEffect(vertices[layer],wrong,mask),"moon layer phase relation checked");
        Check(!h.Apply(1,key,a,true,mask),"moon effect seeds history");
        const auto half=h.Apply(2,key,b,true,mask);
        Check(half && same(*half,make(-.5f+step*.5f)),"all active moon layers interpolate including third texture");
        Check(same(*h.Apply(2,key,b,true,mask),*half),"repeated moon draw stable");
        Check(!h.Apply(2,key,b,false,mask),"disabled moon interpolation stays native");
        Check(!h.Apply(4,key,b,true,mask),"moon frame gap resets");
        auto stationary=h.Apply(5,key,b,true,mask);
        Check(stationary && same(*stationary,b),"paused moon texture remains stationary");
        Check(!h.Apply(6,key,make(-.1f),true,mask),"moon discontinuity rejected");
        h.Reset();h.Apply(1,key,make(-1-step),true,mask);
        const auto wrap=h.Apply(2,key,make(0),true,mask);
        Check(wrap && same(*wrap,make(-1-step*.5f)),"moon phase wrap uses short path without flashing");
        Check(!h.Apply(1,key,a,true,mask),"moon rewind resets");
        if(layer==2) {
            auto stale=make(-.5f+step);stale[4]={1,2,3,4};stale[5]={6,7,8,9};
            auto result=h.Apply(2,key,stale,true,mask);
            Check(result && (*result)[4]==stale[4] && (*result)[5]==stale[5],"inactive third texture unchanged");
        }
    }
    {
        namespace F=MM3D::FileSelectInterpolation;
        const auto quad=[](F::Values& v,unsigned q,float x,float y) {
            const auto row=10+4*q;
            v[row]={1,3,94,1};v[row+1]={x,0,0,40};
            v[row+2]={y,0,14,0};v[row+3]={10,1,0,0};
        };
        F::Values a{},b{};quad(a,0,-200,65);quad(a,1,200,90);
        // Reorder the quads, split the batch and change unused uniform rows.
        quad(b,0,160,90);quad(b,1,-160,65);b[80][0]=123;
        Check(F::Candidate(0x9b343fea686d98e0ULL,0xac22b79e29209c01ULL,1,false,true,6,7,0,12),"file select candidate recognized");
        Check(!F::Candidate(0x9b343fea686d98e0ULL,0xac22b79e29209c01ULL,3,false,true,6,7,0,12),"file select texture mask guarded");
        F::History h;F::Key key{1,2,3,4};
        Check(!h.Apply(1,key,a,12,true),"file select first frame seeds");
        auto half=h.Apply(2,key,b,12,true);
        Check(half&&half->count==2&&half->x[0]==180&&half->x[1]==-180,"file widgets match across order changes");
        Check(h.Apply(2,key,b,12,true)->x==half->x,"repeated file select draw stable");
        F::Values split{};quad(split,0,-120,65);
        auto single=h.Apply(3,key,split,6,true);
        Check(single&&single->x[0]==-140,"file widgets match across batch changes");
        Check(!h.Apply(5,key,split,6,true),"file select frame gap resets");
        auto moved=split;moved[11][0]+=40;
        Check(!h.Apply(6,key,moved,6,false),"file select disabled stays native");
        auto bad=moved;bad[11][3]+=1;
        Check(!h.Apply(7,key,bad,6,true),"file select scale change rejected");
        auto far=bad;far[11][0]+=513;
        Check(!h.Apply(8,key,far,6,true),"file select teleport rejected");
        auto other=key;other[3]++;
        Check(!h.Apply(9,other,bad,6,true),"new native panel does not inherit motion");
        other=key;other[2]++;
        Check(!h.Apply(10,other,moved,6,true),"generation change cannot inherit motion");
        for(unsigned row:{0u,8u,9u,13u,94u,95u}) {
            h.Reset();h.Apply(1,key,split,6,true);
            bad=moved;bad[row][0]+=1;
            Check(!h.Apply(2,key,bad,6,true),"projection tint UV and geometry changes rejected");
        }
        h.Reset();h.Apply(1,key,split,6,true);h.Apply(1,key,moved,6,true);
        Check(!h.Apply(2,key,b,12,true),"ambiguous widget histories stay native");
        F::Values repeat{};quad(repeat,0,-100,65);
        auto disambiguated=h.Apply(2,key,repeat,6,true,{20});
        Check(disambiguated&&disambiguated->x[0]==-110,"native panel motion disambiguates repeated glyphs");
        Check(!h.Apply(2,key,repeat,6,true,{20,-20}),"conflicting panel motion retains native glyph");
        h.Reset();other=key;other[3]=0;
        h.Apply(1,other,split,6,true);
        Check(!h.Apply(2,other,moved,6,true),"inactive file select has no history");
        h.Reset();F::Values banner=split;
        banner[10]={0,3,94,1};banner[94]={0,0,0,.78125f};banner[95]={0,0,.25f,0};
        auto down=banner;down[12][0]+=12;
        h.Apply(1,key,banner,6,true);
        auto vertical=h.Apply(2,key,down,6,true);
        Check(vertical&&vertical->y[0]==71&&vertical->x[0]==-120,"banner slides down at midpoint with x unchanged");
        vertical=h.Apply(3,key,banner,6,true);
        Check(vertical&&vertical->y[0]==71,"banner slides up at midpoint");
        h.Reset();h.Apply(1,key,banner,6,true);down[12][2]+=1;
        Check(!h.Apply(2,key,down,6,true),"vertical resize remains native");
        h.Reset();h.Apply(1,key,split,6,true);auto reflow=split;reflow[12][0]+=12;
        Check(!h.Apply(2,key,reflow,6,true),"reflowed menu text is not a sliding banner");
        F::Tev tev{};
        for(unsigned i=0;i<6;++i)tev[i]={i==0?0x0ee00ee0u:i==1?0x0e3f0e3fu:0x0eef0eefu,0,i<2?0x10001u:0u,0xffffffffu,0};
        tev[0][3]=0x40ffffff;
        const auto alpha=F::GlowAlpha(tev);
        Check(alpha&&std::abs(*alpha-64.f/255)<1e-6,"captured cursor opacity material recognized");
        auto wrong=tev;wrong[0][3]^=1;
        Check(!F::GlowAlpha(wrong),"cursor tint changes rejected");
        wrong=tev;wrong[1][2]=0;
        Check(!F::GlowAlpha(wrong),"different combiner rejected");
        h.Reset();h.Apply(1,key,banner,6,true,{},.25f);
        auto pulse=h.Apply(2,key,banner,6,true,{},.3125f);
        Check(pulse&&pulse->alpha&&*pulse->alpha==.28125f&&pulse->x[0]==-120&&pulse->y[0]==65,"stationary cursor pulse establishes midpoint without moving geometry");
        Check(!h.Apply(3,key,banner,6,true,{},1.f),"large opacity discontinuity stays native");
        Check(!h.Apply(5,key,banner,6,true,{},.9f),"cursor frame gaps seed history");
        Check(!h.Apply(6,key,banner,6,false,{},.85f),"native preview suppresses cursor pulse");
        h.Reset();h.Apply(1,key,banner,6,true,{},.5f);
        auto second=banner;second[11][0]+=100;
        h.Apply(1,key,second,6,true,{},.6f);
        auto merged=banner;
        for(unsigned r=0;r<4;++r)merged[14+r]=second[10+r];
        Check(!h.Apply(2,key,merged,12,true,{},.55f),"merged quads with different alpha histories cannot borrow one fade");
        h.Reset();h.Apply(1,key,merged,12,true,{},.5f);
        pulse=h.Apply(2,key,second,6,true,{},.6f);
        Check(pulse&&pulse->alpha&&std::abs(*pulse->alpha-.55f)<1e-6,"cursor opacity survives batch splitting");
        F::Publish({100,7,42});
        Check(F::Owner(100,7)==42&&F::Owner(101,7)==42,"file select stamped asynchronous ownership");
        Check(!F::Owner(99,7)&&!F::Owner(102,7)&&!F::Owner(100,8),"stale future and wrong-generation samples rejected");
        F::Publish({101,7,0});
        Check(!F::Owner(101,7)&&!F::Owner(102,7),"inactive state overrides preceding menu sample");
        F::Publish({102,7,42,{0,100,200},true,{300,350,400}});
        F::Publish({103,7,42,{-40,160,120},true,{200,275,350}});
        Check(F::Motion(103,7,42)==std::vector<float>({0,-40,60,-80,-100,-75,-50}),
              "published panel and entrance displacements follow native motion");
        Check(F::Motion(103,7,99)==std::vector<float>({0}),"other owner cannot borrow panel motion");
        F::Publish({1,8,43});
        Check(!F::Owner(100,7)&&F::Owner(1,8)==43,"new generation discards old file select state");
        F::Values credits_a{},credits_b{};
        const auto credits=[&](F::Values& v,float y) {
            for(unsigned q=0;q<13;++q) {
                const unsigned row=10+4*q;
                v[row]={1,3,94,-93};v[row+1]={80.f+(q%3)*9,0,0,16};
                v[row+2]={y+20*(q/3),0,18,0};v[row+3]={0,1,0,0};
            }
            v[94]={0,.75f,0,.0625f};v[95]={0,.140625f,.0703125f,0};
            v[93]={1,0,.745098f,1};
        };
        constexpr float scroll=.6171875f;
        credits(credits_a,228.25f);credits(credits_b,228.25f-scroll);
        Check(F::CreditsLayout(credits_a,78),"multiline credits glyph layout recognized");
        F::History credits_history;const F::Key credits_key{1,2,7,1};
        Check(!credits_history.ApplyCredits(1,credits_key,credits_a,78,true),"credits first frame seeds");
        auto credits_half=credits_history.ApplyCredits(2,credits_key,credits_b,78,true);
        for(unsigned q=0;q<13;++q)
            Check(credits_half&&std::abs(credits_half->y[q]-(credits_a[12+4*q][0]-scroll*.5f))<.0001f,
                  "repeated letters on separate credits lines follow their own midpoint");
        auto credits_split=credits_b;credits_split[10][3]=-90;credits_split[90]=credits_split[93];
        credits_history.Reset();credits_history.ApplyCredits(1,credits_key,credits_a,78,true);
        const auto tail=credits_history.ApplyCredits(2,credits_key,credits_split,6,true);
        Check(tail&&tail->y[0]==credits_half->y[0],"credits survive batch splits and color register relocation");
        Check(!credits_history.ApplyCredits(4,credits_key,credits_split,6,true),"credits capture gaps reseed");
        credits_history.Reset();credits_history.ApplyCredits(1,credits_key,credits_a,78,true);
        auto credits_reflow=credits_a;for(unsigned q=0;q<13;++q)credits_reflow[12+4*q][0]-=20;
        Check(!credits_history.ApplyCredits(2,credits_key,credits_reflow,78,true),"menu credits_reflow is not a credits scroll");
        credits_history.Reset();credits_history.ApplyCredits(1,credits_key,credits_a,78,true);
        Check(!credits_history.ApplyCredits(2,credits_key,credits_a,78,true),"stationary text stays native");
        credits_history.Reset();credits_history.ApplyCredits(1,credits_key,credits_a,78,true);
        auto changed_tint=credits_b;changed_tint[93][0]=.5f;
        Check(!credits_history.ApplyCredits(2,credits_key,changed_tint,78,true),"changed glyph color cannot borrow old text");
        credits_history.Reset();credits_history.ApplyCredits(1,credits_key,credits_a,78,true);
        Check(!credits_history.ApplyCredits(2,credits_key,credits_b,78,false),"disabled credits stay native");
        auto credits_bad=credits_a;credits_bad[10][3]=-10;
        Check(!F::CreditsLayout(credits_bad,78),"credits color cannot alias geometry rows");
        credits_bad=credits_a;credits_bad[12][2]=14;
        Check(!F::CreditsLayout(credits_bad,78),"other font geometry excluded");
        Check(!F::CreditsLayout(credits_a,84),"oversized credits batch rejected");
        F::History owned_menu;owned_menu.Apply(1,credits_key,credits_a,78,true);
        auto menu_font=credits_a;for(unsigned q=0;q<13;++q)menu_font[11+4*q][0]+=10;
        const auto menu_half=owned_menu.Apply(2,credits_key,menu_font,78,true,{10.f});
        Check(menu_half&&menu_half->x[0]==credits_a[11][0]+5,
              "owned file-select text retains horizontal motion even with credits-sized glyphs");

    }
    {
        namespace F=MM3D::FireImpactInterpolation;
        F::Tev tev{};tev[0]={0x0ee00ee3,0,0,0,2};tev[1]={0x0eee0efe,0,1,15129780,0};
        tev[2]={0x0ee00f30,0,8,0,0};
        for(unsigned i=3;i<6;++i)tev[i]={0x0fff0fff,0,0,0xffffffff,0};
        Check(F::Material(tev)==F::Kind::Cloud,"fire impact cloud material recognized");
        tev[1][3]=0x40ffffff;
        Check(F::Material(tev)==F::Kind::Spark,"fire impact spark material recognized");
        tev[2][2]=0;Check(F::Material(tev)==F::Kind::None,"unrelated additive effects excluded");
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto make=[](F::Kind kind,float x,unsigned tile,float alpha) {
            std::vector<F::Vertex> v(5);const bool cloud=kind==F::Kind::Cloud;
            const float half=cloud?25.f:1.f;
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+(k<2?-half:half),k%2?-half:half,0,1};
                v[k][1]=cloud?std::array<float,4>{-.166089892f,-.067666233f,-.070741951f,alpha}:
                    std::array<float,4>{0,0,-128.f/255,alpha};
                v[k][2]=cloud?std::array<float,4>{float(tile%4+k/2)*.25f,1-float(tile/4+k%2)*.25f,0,1}:
                    std::array<float,4>{float(k/2)*2,float(1-k%2)*2,0,1};
            }
            return v;
        };
        for(auto kind:{F::Kind::Cloud,F::Kind::Spark}) {
            auto a=F::Decode(make(kind,0,15,.4f),order,kind);
            auto b=F::Decode(make(kind,1,0,.38f),order,kind);
            Check(a&&b,"captured fire impact geometry decodes");
            F::History h;Check(!h.Apply(1,1,2,kind,*a),"fire impact seeds history");
            const auto half=h.Apply(2,1,2,kind,*b);
            Check(half&&std::abs(F::Center((*half)[0])[0]-.5f)<.00001f,"fire impact motion interpolates");
            Check(half&&std::abs((*half)[0].vertices[0][1][3]-.39f)<.00001f,"fire impact opacity interpolates");
            Check(half&&(*half)[0].vertices[0][2]==(*b)[0].vertices[0][2],"atlas animation never blends through neighboring cells");
            Check(!h.Apply(4,1,2,kind,*b),"fire impact gaps reseed");
            Check(!h.Apply(5,1,3,kind,*b),"fire impact generations are isolated");
            Check(!h.Apply(5,2,3,kind,*b),"fire impact textures are isolated");
            h.Reset();h.Apply(1,1,2,kind,*a);h.Apply(1,1,2,kind,*a);
            Check(!h.Apply(2,1,2,kind,*b),"repeated impact batches stay ambiguous");
            auto duplicate=*a;duplicate.push_back((*a)[0]);
            Check(!F::Match((*b)[0],duplicate,kind,true),"overlapping impact particles are not guessed");
            auto distant=F::Decode(make(kind,100,0,.38f),order,kind);
            Check(!F::Match((*distant)[0],*a,kind,true),"new distant impacts cannot borrow motion");
            auto bad=make(kind,0,0,.4f);bad[0][1][0]=1;
            Check(!F::Decode(bad,order,kind),"unverified impact tint excluded");
        }
        auto a=F::Decode(make(F::Kind::Cloud,0,2,.4f),order,F::Kind::Cloud);
        auto b=F::Decode(make(F::Kind::Cloud,1,5,.38f),order,F::Kind::Cloud);
        Check(!F::Compatible((*b)[0],(*a)[0],F::Kind::Cloud),"cloud atlas discontinuity rejects unrelated particle");
        auto bad=make(F::Kind::Cloud,0,0,.4f);bad[0][2][0]=.1f;
        Check(!F::Decode(bad,order,F::Kind::Cloud),"fractional cloud atlas cells rejected");
    }
    {
        namespace S=MM3D::SnowInterpolation;
        S::Tev tev{};tev[0]={0x0e300e30,0x20000,0x10008,0x00ffffff,0};
        for(unsigned i=1;i<6;++i)tev[i]={0x0fff0fff,0,0,0xffffffff,0};
        Check(S::Material(tev),"captured snow material recognized");
        tev[0][1]=0;Check(!S::Material(tev),"other particle materials excluded from snow");
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto make=[](float x,float y,float alpha=.3f) {
            std::vector<S::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+(k<2?-1.f:1.f),y+(k%2?-1.f:1.f),0,1};
                v[k][1]={0,0,0,alpha};v[k][2]={k<2?0.f:1.f,k%2?0.f:1.f,0,1};
            }
            return v;
        };
        auto a=S::Decode(make(0,10),order),b=S::Decode(make(1,8,.31f),order);
        Check(a&&b,"captured snow strips decode");
        S::History h;Check(!h.Apply(1,{1,2},*a),"snow first frame seeds");
        const auto half=h.Apply(2,{1,2},*b);
        Check(half&&std::abs(S::Center((*half)[0])[0]-.5f)<.00001f&&
              std::abs(S::Center((*half)[0])[1]-9)<.00001f,"snow falls through intermediate center");
        Check(half&&std::abs((*half)[0].vertices[0][1][3]-.305f)<.00001f,"snow fade interpolates");
        Check(half&&(*half)[0].vertices[0][2]==(*b)[0].vertices[0][2]&&
              std::abs(S::SizeSquared((*half)[0])-4)<.00001f,"snow preserves UVs and square size");
        auto turned=(*b)[0];const auto center=S::Center(turned);
        const float angle=.4f;
        for(auto& v:turned.vertices) {
            const float x=v[0][0]-center[0],z=v[0][2]-center[2];
            v[0][0]=center[0]+std::cos(angle)*x+std::sin(angle)*z;
            v[0][2]=center[2]-std::sin(angle)*x+std::cos(angle)*z;
        }
        S::History rotating;rotating.Apply(1,{1,2},*a);
        const auto rotated_half=rotating.Apply(2,{1,2},{turned});
        Check(rotated_half&&std::abs(S::SizeSquared((*rotated_half)[0])-4)<.00001f,
              "camera turns preserve snowflake size");
        auto reordered=*a;auto second=(*a)[0];
        for(auto& v:second.vertices)v[0][0]+=40;
        reordered.push_back(second);S::History sorting;sorting.Apply(1,{1,2},reordered);
        std::swap(reordered[0],reordered[1]);
        for(auto& q:reordered)for(auto& v:q.vertices)v[0][1]-=2;
        const auto sorted_half=sorting.Apply(2,{1,2},reordered);
        Check(sorted_half&&S::Center((*sorted_half)[0])[0]==40&&S::Center((*sorted_half)[1])[0]==0&&
              S::Center((*sorted_half)[0])[1]==9,"snow matching survives particle order changes");
        Check(!h.Apply(4,{1,2},*b),"snow gaps reseed");
        Check(!h.Apply(5,{1,3},*b),"snow generations do not share history");
        Check(!h.Apply(5,{2,3},*b),"snow atlases do not share history");
        h.Reset();h.Apply(1,{1,2},*a);h.Apply(1,{1,2},*a);
        Check(!h.Apply(2,{1,2},*b),"repeated same-frame snow batches stay ambiguous");
        const auto distant=S::Decode(make(100,8),order);
        Check(!S::Match((*distant)[0],*a),"new distant snow cannot borrow an old flake");
        auto duplicate=*a;duplicate.push_back((*a)[0]);
        Check(!S::Match((*b)[0],duplicate),"coincident snowflakes stay ambiguous");
        auto changed=(*b)[0];changed.vertices[1][0][1]-=1;
        Check(!S::Compatible((*a)[0],changed),"different flake sizes cannot match");
        auto bad=make(0,10);bad[0][1][0]=1;
        Check(!S::Decode(bad,order),"unverified snow tint rejected");
        bad=make(0,10);bad[0][2][0]=2;
        Check(!S::Decode(bad,order),"other sprite UVs rejected");
        bad=make(0,10);bad[3][0][0]+=1;
        Check(!S::Decode(bad,order),"deformed snow quad rejected");
        bad=make(0,10);bad[0][0][0]=std::numeric_limits<float>::quiet_NaN();
        Check(!S::Decode(bad,order),"nonfinite snow rejected");
        auto invalid_order=order;invalid_order[5]=0;
        Check(!S::Decode(make(0,10),invalid_order),"visible corner cannot be trailing snow connector");
    }
    {
        namespace B=MM3D::BubbleInterpolation;
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto make=[](float x,float size,unsigned tile) {
            std::vector<B::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+(float(k/2)-.5f)*size,(.5f-float(k%2))*size,0,1};
                v[k][1]={.33f,.28f,.21f,1};v[k][2]={(tile+k/2)*.5f,float(1-k%2),0,1};
            }
            return v;
        };
        B::Tev tev{{{249564899,0,0,0,2},{250482430,0,1,6577716,0},
            {249564976,0,8,0,0},{268374015,0,0,0xffffffff,0},
            {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0}}};
        Check(B::Material(tev),"swimming bubble material recognized");
        tev[1][3]=12345;Check(B::Material(tev),"bubble lighting may change");
        tev[2][1]=32;Check(!B::Material(tev),"different particle material excluded");
        B::Tev pop_tev{{{238030384,32,65544,6579455,0},
            {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
            {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
            {268374015,0,0,0xffffffff,0}}};
        Check(B::PopMaterial(pop_tev),"Wart pop material recognized");
        pop_tev[0][1]=0;Check(!B::PopMaterial(pop_tev),"other translucent material rejected");
        auto pop_a=make(0,15,0),pop_b=make(8.3f,15,1);
        for(unsigned k=0;k<4;++k) {
            pop_a[k][1]={0,155.f/255,155.f/255,.8f};
            pop_b[k][1]={0,155.f/255,155.f/255,.76f};
        }
        Check(!B::Decode(pop_a,order),"fading bubbles require verified pop material");
        const auto pa=B::Decode(pop_a,order,true),pb=B::Decode(pop_b,order,true);
        Check(pa && pb,"cyan fading pop bubbles decode");
        B::History pop;pop.Apply(1,{9,0},*pa,true);
        const auto pm=pop.Apply(2,{9,0},*pb,true);
        Check(pm && std::abs(B::Center(pm->front())[0]-4.15f)<1e-5 &&
            std::abs(pm->front().vertices[0][1][3]-.78f)<1e-5,
            "Wart pop motion and fade interpolate together");
        Check(pm && pm->front().vertices[0][2]==pb->front().vertices[0][2],
            "Wart pop atlas tile stays native");
        pop_a[0][1][0]=1;Check(!B::Decode(pop_a,order,true),"unrelated tinted sprites rejected");
        const auto swim_a=B::Decode(make(0,2.4f,0),order);
        const auto swim_b=B::Decode(make(12,2.4f,1),order);
        Check(B::SwimmingBatch(*swim_a),"small swimming bubbles recognized");
        B::History swimming;const B::Key swim_key{3,4};
        swimming.Apply(1,swim_key,*swim_a,true);
        const auto swim_half=swimming.Apply(2,swim_key,*swim_b,true);
        Check(swim_half && std::abs(B::Center(swim_half->front())[0]-6)<1e-5,
            "fast stable-size swimming bubble gets midpoint");
        Check(swim_half && swim_half->front().vertices[0][2]==swim_b->front().vertices[0][2],
            "swimming bubble keeps current atlas tile");
        Check(!B::Compatible(swim_a->front(),swim_b->front()),"ordinary bubble motion bound unchanged");
        const auto newborn=B::Decode(make(12,2.48f,1),order);
        Check(!B::Compatible(swim_a->front(),newborn->front(),true),"different random size cannot inherit swimming history");
        const auto teleported=B::Decode(make(20,2.4f,1),order);
        Check(!B::Compatible(swim_a->front(),teleported->front(),true),"distant swimming bubble remains native");
        auto ambiguous=*swim_a;auto neighbor=swim_a->front();
        for(auto& v:neighbor.vertices)v[0][0]+=1;
        ambiguous.push_back(neighbor);swimming.Reset();swimming.Apply(1,swim_key,ambiguous,true);
        Check(!swimming.Apply(2,swim_key,*swim_b,true),"ambiguous equal-size swimming bubbles remain native");
        swimming.Reset();swimming.Apply(1,swim_key,*swim_a,true);
        Check(!swimming.Apply(3,swim_key,*swim_b,true),"swimming frame gaps reseed");
        Check(!swimming.Apply(4,swim_key,*swim_b,false),"bubble matching mode changes reseed");
        const auto dense=[&](unsigned count,float x) {
            std::vector<B::Vertex> inputs;
            std::vector<std::uint32_t> strip;
            for(unsigned i=0;i<count;++i) {
                const auto quad=make(x+i*40,2.4f,i%2);
                inputs.insert(inputs.end(),quad.begin(),quad.begin()+4);
                for(unsigned index:{0u,1u,2u,3u,3u,4u})strip.push_back(i*4+index);
            }
            inputs.emplace_back();return B::Decode(inputs,strip);
        };
        for(unsigned count:{64u,79u,100u,128u}) {
            const auto before=dense(count,0),after=dense(count,12);
            Check(before && after && before->size()==count,"dense swimming strip decodes within capacity");
            if(!before || !after)continue;
            B::History dense_history;
            Check(!dense_history.Apply(1,swim_key,*before,true),"dense trail seeds history");
            const auto middle=dense_history.Apply(2,swim_key,*after,true);
            Check(middle && middle->size()==count,"dense trail retains every bubble");
            if(middle)for(unsigned i=0;i<count;++i)
                Check(std::abs(B::Center((*middle)[i])[0]-(i*40+6))<.001f,
                    "every dense swimming bubble moves halfway");
            const auto repeated=dense_history.Apply(2,swim_key,*after,true);
            Check(repeated && middle && B::Same(repeated->back(),middle->back()),
                "dense repeated submissions preserve the last bubble midpoint");
            Check(!dense_history.Apply(4,swim_key,*after,true),"dense trail gap reseeds");
        }
        Check(!dense(129,0),"oversized bubble strip remains rejected");
        std::vector<B::Quad> too_many(129,swim_a->front());
        Check(!swimming.Apply(5,swim_key,too_many,true),"bubble history capacity remains bounded");
        const auto a=B::Decode(make(0,5,0),order),b=B::Decode(make(2,5.2f,1),order);
        Check(a && b,"underwater bubble atlas recognized");
        B::History h;const B::Key key{1,2};
        Check(!h.Apply(1,key,*a),"bubble first frame seeds");
        auto half=h.Apply(2,key,*b);
        Check(half && std::abs(B::Center(half->front())[0]-1)<1e-5 &&
              std::abs(B::Size(half->front())-5.1f)<1e-5,"bubble motion and size interpolate");
        for(unsigned k=0;k<4;++k)Check(half->front().vertices[k][2]==b->front().vertices[k][2],"bubble sprite frames unchanged");
        Check(h.Apply(2,key,*b)->front().vertices==half->front().vertices,"repeated bubble draws stable");
        Check(!h.Apply(4,key,*b),"bubble gap resets");
        Check(!h.Apply(5,{1,3},*b),"bubble scene change seeds");
        Check(!h.Apply(1,key,*a),"bubble rewind seeds");
        auto far=B::Decode(make(20,5,0),order);
        Check(!B::Compatible(far->front(),a->front()),"new distant bubble excluded");
        auto larger=B::Decode(make(0,9,0),order);
        Check(!B::Compatible(larger->front(),a->front()),"new differently sized bubble excluded");
        auto neighbors=*a;auto other=a->front();for(auto&v:other.vertices)v[0][0]+=4;
        neighbors.push_back(other);h.Reset();h.Apply(1,key,neighbors);
        Check(!h.Apply(2,key,*b),"crowded ambiguous bubbles remain native");
        auto bad=make(0,5,0);bad[0][2][0]=.25f;
        Check(!B::Decode(bad,order),"other atlas layouts excluded");
        bad=make(0,5,0);for(unsigned k=0;k<4;++k)bad[k][1][3]=.5f;
        Check(!B::Decode(bad,order),"unrelated fading particles excluded");
        auto invalid=order;invalid[0]=999;Check(!B::Decode(make(0,5,0),invalid),"bubble indices validated");
        h.Reset();Check(!h.Apply(9,key,*a),"bubble reset clears history");
    }

    {
        namespace L=MM3D::LinkInterpolation;using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose pose;
        const unsigned count=20,first=9,second=10;
        const unsigned vertices=564;
        pose.signature=0xbb44d2bc24eb2cd6ull;
        pose.pose.instance=800;pose.model=801;
        pose.pose.bones.resize(count);pose.bind.assign(count,Flatten(Identity()));
        pose.words.assign(count,0xffff0000);
        for(unsigned i=0;i<count;++i)pose.pose.bones[i]=Flatten(Camera(0,100+i*10));
        const auto set=[&](double a,double b) {
            pose.pose.bones[first]=Flatten(Camera(a));pose.pose.bones[second]=Flatten(Camera(b));
        };
        L::Palette palette;palette.fill(Camera(0,9999));
        set(0,0);palette[0]=Camera(0);
        Check(!L::Identify(palette[0],pose),"skeleton shared head bones are ambiguous to generic matcher");
        L::History h;h.Apply(1,0,Identity(),palette,{pose},true,vertices);
        set(20,20);palette[0]=Camera(20);
        auto result=h.Apply(2,33,Identity(),palette,{pose},true,vertices);
        Check(result[0] && Near(*result[0],Camera(10),1e-6),"Keeta shared head bones interpolate by consensus");
        Check(!h.Apply(2,33,Identity(),palette,{pose},false,vertices)[0],"disabled skeleton interpolation stays native");
        Check(!h.Apply(2,33,Identity(),palette,{pose},true,vertices+1)[0],"unverified skeleton mesh stays native");
        auto other=pose;other.pose.instance++;
        Check(!h.Apply(2,33,Identity(),palette,{pose,other},true,vertices)[0],"coincident skeleton actors stay ambiguous");
        Check(!h.Apply(4,99,Identity(),palette,{pose},true,vertices)[0],"skeleton frame gap resets");
        L::History diverging;
        set(0,10);palette[0]=Camera(0);diverging.Apply(1,0,Identity(),palette,{pose},true,vertices);
        set(20,20);palette[0]=Camera(20);
        Check(!diverging.Apply(2,33,Identity(),palette,{pose},true,vertices)[0],"shared current bones with different histories stay native");
        pose.signature++;
        L::History unknown;unknown.Apply(1,0,Identity(),palette,{pose},true,vertices);
        Check(!unknown.Apply(2,33,Identity(),palette,{pose},true,vertices)[0],"unknown skeleton signature stays native");
    }

    {
        namespace L=MM3D::LinkInterpolation;using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose pose;
        pose.signature=0x89c1c57e8d857e3bull;pose.pose.instance=810;pose.model=811;
        pose.words={0xffff0000,0x1001};pose.bind.assign(2,Flatten(Identity()));
        const auto model=[](double angle,double x) {
            auto m=Camera(angle,x);
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[r][c]*=.01;
            return m;
        };
        L::Palette palette;palette.fill(Camera(0,9999));
        const auto set=[&](double angle,double x) {
            palette[0]=model(angle,x);pose.pose.bones.assign(2,Flatten(palette[0]));
        };
        set(0,0);Check(!L::Identify(palette[0],pose),"ocarina duplicate bones need consensus");
        L::History h;
        Check(!h.Apply(1,0,Identity(),palette,{pose},true,384)[0],"ocarina initial pose seeds");
        // World position moves 4 units while the camera moves 10 units.
        set(30,-6);
        const auto half=h.Apply(2,33,Camera(0,10),palette,{pose},true,384);
        Check(half[0] && Near(*half[0],model(15,-8),1e-6),"ocarina toss and spin interpolate with camera motion and native scale");
        Check(!half[1],"ocarina leaves unused palette slots untouched");
        Check(!h.Apply(2,33,Camera(0,10),palette,{pose},false,384)[0],"disabled ocarina stays native");
        Check(!h.Apply(2,33,Camera(0,10),palette,{pose},true,383)[0],"ocarina mesh guard");
        auto duplicate=pose;duplicate.pose.instance++;
        Check(!h.Apply(2,33,Camera(0,10),palette,{pose,duplicate},true,384)[0],"coincident ocarinas stay ambiguous");
        Check(!h.Apply(4,99,Camera(0,10),palette,{pose},true,384)[0],"ocarina frame gap resets");
        pose.signature++;L::History unknown;
        unknown.Apply(1,0,Identity(),palette,{pose},true,384);
        Check(!unknown.Apply(2,33,Identity(),palette,{pose},true,384)[0],"unverified two-bone model stays native");
        pose.signature--;pose.words[1]=0xffff0001;
        L::History diverging;set(0,0);pose.pose.bones[1]=Flatten(model(10,0));
        diverging.Apply(1,0,Identity(),palette,{pose},true,384);set(30,4);
        Check(!diverging.Apply(2,33,Identity(),palette,{pose},true,384)[0],"ocarina coincident bones with divergent history stay native");
    }

    for(unsigned vertices:{180u,42u,408u}) {
        namespace L=MM3D::LinkInterpolation;using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose pose;
        pose.signature=0xb8b55777b90f150eull;pose.pose.instance=810;pose.model=811;
        pose.words={0xffff0000,1,2};pose.bind.assign(3,Flatten(Identity()));
        const auto model=[](double angle,double x) {
            auto m=Camera(angle,x);
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[r][c]*=.01;
            return m;
        };
        L::Palette palette;palette.fill(Camera(0,9999));
        const auto set=[&](double angle,double x) {
            palette[0]=model(angle,x);pose.pose.bones.assign(3,Flatten(palette[0]));
        };
        set(0,0);Check(!L::Identify(palette[0],pose),"held heart duplicate bones need consensus");
        L::History h;
        Check(!h.Apply(1,0,Identity(),palette,{pose},true,vertices)[0],"held heart initial pose seeds");
        // World position moves 4 units while the camera moves 10 units.
        set(30,-6);
        const auto half=h.Apply(2,33,Camera(0,10),palette,{pose},true,vertices);
        Check(half[0] && Near(*half[0],model(15,-8),1e-6),"held heart toss and spin interpolate with camera motion and native scale");
        Check(!half[1],"held heart leaves unused palette slots untouched");
        Check(!h.Apply(2,33,Camera(0,10),palette,{pose},false,vertices)[0],"disabled held heart stays native");
        Check(!h.Apply(2,33,Camera(0,10),palette,{pose},true,vertices+1)[0],"held heart mesh guard");
        auto duplicate=pose;duplicate.pose.instance++;
        Check(!h.Apply(2,33,Camera(0,10),palette,{pose,duplicate},true,vertices)[0],"coincident held hearts stay ambiguous");
        Check(!h.Apply(4,99,Camera(0,10),palette,{pose},true,vertices)[0],"held heart frame gap resets");
        pose.signature++;L::History unknown;
        unknown.Apply(1,0,Identity(),palette,{pose},true,vertices);
        Check(!unknown.Apply(2,33,Identity(),palette,{pose},true,vertices)[0],"unverified three-bone model stays native");
        pose.signature--;pose.words[1]=0xffff0001;
        L::History diverging;set(0,0);pose.pose.bones[1]=Flatten(model(10,0));
        diverging.Apply(1,0,Identity(),palette,{pose},true,vertices);set(30,4);
        Check(!diverging.Apply(2,33,Identity(),palette,{pose},true,vertices)[0],"held heart coincident bones with divergent history stay native");
    }

    for(unsigned vertices:{756u,408u}) {
        namespace L=MM3D::LinkInterpolation;using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose pose;
        pose.signature=0x529d17c9d3dc4acbull;pose.pose.instance=700;pose.model=701;
        pose.pose.bones.resize(18);pose.bind.assign(18,Flatten(Identity()));pose.words.assign(18,0xffff0000);
        const std::vector<unsigned> bones=vertices==756?std::vector<unsigned>{4,2,11,5,12,8,15,9}:
            std::vector<unsigned>{13,6,10};
        const auto update=[&](double angle) {
            for(unsigned i=0;i<18;++i)pose.pose.bones[i]=Flatten(Camera(angle+i,3*i));
        };
        const auto make=[&] {
            L::Palette palette;palette.fill(Camera(0,9999));
            for(unsigned slot=0;slot<bones.size();++slot) {
                palette[slot]=Expand(pose.pose.bones[bones[slot]]);
                palette[slot][0][0]+=.000015;palette[slot][0][3]+=.02;
            }
            return palette;
        };
        update(0);auto palette=make();
        Check(L::DodongoBones(palette,pose,vertices)==bones,"complete Dodongo palette recognized");
        Check(!L::Identify(palette[0],pose),"Dodongo precision correction outside generic matcher");
        L::History h;Check(!h.Apply(1,0,Identity(),palette,{pose},true,vertices)[0],"Dodongo seeds history");
        auto half=h.Apply(2,33,Identity(),palette,{pose},true,vertices);
        for(unsigned slot=0;slot<bones.size();++slot)
            Check(half[slot] && Near(*half[slot],palette[slot],1e-8),"still Dodongo preserves native correction");
        update(20);palette=make();half=h.Apply(3,66,Identity(),palette,{pose},true,vertices);
        for(unsigned slot=0;slot<bones.size();++slot) {
            const auto current=Expand(pose.pose.bones[bones[slot]]);
            const auto expected=Multiply(Camera(10+bones[slot],3*bones[slot]),Multiply(*Inverse(current),palette[slot]));
            Check(half[slot] && Near(*half[slot],expected,1e-6),"Dodongo arms and body interpolate with correction");
        }
        auto bad=palette;bad[bones.size()-1][0][0]+=.001;
        Check(!L::DodongoBones(bad,pose,vertices),"every Dodongo palette slot checked");
        bad=palette;bad[0][0][3]+=.2;
        Check(!L::DodongoBones(bad,pose,vertices),"Dodongo position mismatch rejected");
        bad=palette;bad[0][0][0]=std::numeric_limits<double>::quiet_NaN();
        Check(!L::DodongoBones(bad,pose,vertices),"nonfinite Dodongo rejected");
        auto wrong=pose;wrong.signature++;
        Check(!L::DodongoBones(palette,wrong,vertices) && !L::DodongoBones(palette,pose,vertices+1),"Dodongo correction restricted to model and mesh");
        Check(!h.Apply(3,66,Identity(),palette,{pose},false,vertices)[0],"disabled Dodongo stays native");
        Check(!h.Apply(5,132,Identity(),palette,{pose},true,vertices)[0],"Dodongo frame gap stays native");
        auto duplicate=pose;duplicate.pose.instance++;
        Check(!h.Apply(6,165,Identity(),palette,{pose,duplicate},true,vertices)[0],"two coincident Dodongos stay ambiguous");
    }
    {
        namespace S=MM3D::ShockwaveInterpolation;
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto make=[](float size,float alpha,float x=0) {
            std::vector<S::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+(float(k/2)-.5f)*size,0,(.5f-float(k%2))*size,1};
                v[k][1]={1,1,200.f/255,alpha};v[k][2]={float(k/2)*2,float(1-k%2)*2,0,1};
            }
            return v;
        };
        auto a=S::Decode(make(200,.6f),order),b=S::Decode(make(240,.5f),order);
        Check(a && b,"explosion rings recognized");
        S::History h;const S::Key key{1,2};
        Check(!h.Apply(1,key,*a),"ring first frame seeds");
        auto half=h.Apply(2,key,*b);
        Check(half && std::abs(half->front().vertices[0][0][0]+110)<1e-5 &&
              std::abs(half->front().vertices[0][1][3]-.55f)<1e-5,"ring expands and fades at midpoint");
        for(unsigned i=0;i<4;++i)Check(half->front().vertices[i][2]==b->front().vertices[i][2],"ring UVs unchanged");
        Check(h.Apply(2,key,*b)->front().vertices==half->front().vertices,"ring repeated draw stable");
        Check(!h.Apply(4,key,*b),"ring frame gaps rejected");
        Check(!h.Apply(5,{1,3},*b),"ring scene change seeds");
        Check(!S::Compatible(a->front(),b->front()),"ring reset cannot use fading ring");
        auto distant=*b;for(auto&v:distant.front().vertices)v[0][0]+=10;
        Check(!S::Compatible(distant.front(),a->front()),"different explosion centers rejected");
        auto fast=S::Decode(make(500,.5f),order);
        Check(!S::Compatible(fast->front(),a->front()),"discontinuous ring growth rejected");
        h.Reset();auto collapsed=S::Decode(make(0,.6f),order),expanded=S::Decode(make(100,.5f),order);
        Check(collapsed && expanded,"collapsed ring birth recognized");
        h.Apply(1,key,*collapsed);Check(h.Apply(2,key,*expanded).has_value(),"ring first expansion interpolates");
        h.Reset();h.Apply(1,key,{a->front(),a->front()});
        // Exact duplicate submissions are deduplicated, but distinct concentric
        // rings with equally plausible histories remain ambiguous.
        auto candidates=*a;auto near=a->front();
        for(auto&v:near.vertices) { v[0][0]+=.01f; }
        candidates.push_back(near);
        h.Reset();h.Apply(1,key,candidates);Check(!h.Apply(2,key,*b),"ambiguous concentric rings rejected");
        Check(!S::Decode(make(2,.5f),order),"small stars excluded from explosion rings");
        auto bad=make(200,.5f);bad[0][2][0]=.5f;
        Check(!S::Decode(bad,order),"other ring UVs rejected");
        bad=make(200,.5f);bad[0][0][0]=std::numeric_limits<float>::infinity();
        Check(!S::Decode(bad,order),"nonfinite explosion rejected");
        h.Reset();Check(!h.Apply(9,key,*a),"ring reset clears history");
        const auto landing = [&](float size, float alpha) {
            auto vertices = make(size, alpha, 510);
            for (unsigned k = 0; k < 4; ++k) {
                vertices[k][0][1] = 105;
                vertices[k][0][2] += 2130;
                vertices[k][1] = {.135332525f, .118415952f, .118415952f, alpha};
            }
            return vertices;
        };
        a = S::Decode(landing(108.8f, 150.f/255), order);
        b = S::Decode(landing(120, 129.f/255), order);
        Check(a && b, "captured Skulltula landing rings recognized");
        h.Reset();
        Check(!h.Apply(1, key, *a), "landing ring first frame seeds");
        half = h.Apply(2, key, *b);
        Check(half && S::Near(half->front().vertices[0][0][0], 510-57.2f) &&
              S::Near(half->front().vertices[0][1][3], 139.5f/255),
              "landing ring expansion and fade interpolate");
        for (unsigned k = 0; k < 4; ++k) {
            Check(half->front().vertices[k][2] == b->front().vertices[k][2],
                  "landing ring UVs preserved");
            for (unsigned c = 0; c < 3; ++c)
                Check(half->front().vertices[k][1][c] == b->front().vertices[k][1][c],
                      "landing ring tint preserved");
        }
        a = S::Decode(landing(140.32f, 45.f/255), order);
        b = S::Decode(landing(140.32f, 24.f/255), order);
        h.Reset();h.Apply(10, key, *a);
        Check(h.Apply(11, key, *b).has_value(), "landing ring final stationary fade interpolates");
        Check(!h.Apply(13, key, *b), "landing ring capture gaps do not borrow history");
        Check(!h.Apply(14, {1,3}, *b), "landing ring scene changes clear history");
        Check(!S::Compatible(a->front(), b->front()), "landing ring opacity reset rejected");
        bad = landing(120, .5f);
        for (auto& vertex : bad) vertex[1][0] = .2f;
        Check(!S::Decode(bad, order), "unrelated brown particle tint rejected");
        bad = landing(120, .5f);
        for (auto& vertex : bad) std::swap(vertex[0][1], vertex[0][2]);
        Check(!S::Decode(bad, order), "upright brown sprites rejected");
        Check(!S::Decode(landing(300, .5f), order), "oversized brown ground sprites rejected");
    }

    {
        namespace S=MM3D::SwordTrailInterpolation;
        const auto make=[](unsigned sections,float x,float opacity) {
            std::vector<S::Vertex> inputs(sections*2);
            std::vector<std::uint32_t> order;
            for(unsigned i=0;i<sections;++i) {
                const float t=float(i)/(sections-1);
                for(unsigned side=0;side<2;++side) {
                    auto& v=inputs[2*i+side];
                    v[0]={x+20*t,float(side)*10,0,1};
                    v[1]={1,1,1,(opacity+.4f*t)*(side?.24f:1.f)};
                    v[2]={float(i),float(1-side),0,1};
                }
                if(i+1<sections) {
                    for(unsigned k:{2*i,2*i+1,2*i+2,2*i+3})order.push_back(k);
                    if(i+2<sections){order.push_back(2*i+3);order.push_back(2*i+2);}
                }
            }
            return std::pair{inputs,order};
        };
        auto [a,order]=make(5,0,.2f);
        auto first=S::Decode(a,order);Check(first.has_value(),"sword ribbon recognized");
        auto [b,next_order]=make(9,2,.1f);auto second=S::Decode(b,next_order);
        S::History h;const S::Key key{1,2};
        Check(!h.Apply(1,key,*first,Identity()),"trail first frame seeds");
        auto half=h.Apply(2,key,*second,Identity());
        Check(half && std::abs(half->vertices[0][0][0]-1)<1e-5 &&
              std::abs(half->vertices[0][1][3]-.15f)<1e-5,"growing trail position and fade interpolate");
        for(unsigned i=0;i<b.size();++i) {
            Check(half->vertices[i][2]==b[i][2],"trail UVs retained");
            Check(std::abs(half->vertices[i][0][0]-(b[i][0][0]-1))<1e-5,"trail resamples old topology");
        }
        Check(second->vertices==b,"native trail untouched");
        Check(h.Apply(2,key,*second,Identity())->vertices==half->vertices,"repeated trail stable");
        auto [c,last_order]=make(2,3,0);auto last=S::Decode(c,last_order);
        Check(h.Apply(3,key,*last,Identity()).has_value(),"shrinking trail interpolates");
        Check(!h.Apply(5,key,*last,Identity()),"trail frame gap rejected");
        Check(!h.Apply(6,{1,3},*last,Identity()),"trail scene change seeds");
        Check(!h.Apply(1,key,*first,Identity()),"trail rewind seeds");
        h.Reset();h.Apply(1,key,*second,Identity());
        Check(!h.Apply(2,key,*first,Identity()),"new swing alpha reset rejected");
        h.Reset();h.Apply(1,key,*first,Identity());
        auto far=Identity();far[0][3]=200;
        Check(!h.Apply(2,key,*second,far),"separate remote trails rejected");
        h.Reset();h.Apply(1,key,*first,Identity());
        auto moved=*first;for(auto&v:moved.vertices)v[0][0]-=5;
        auto world=Identity();world[0][3]=5;
        half=h.Apply(2,key,moved,world);
        Check(half && std::abs(half->vertices[0][0][0]+5)<1e-5,"trail uses world positions across origin changes");
        Check(!h.Apply(2,key,*second,Identity()),"conflicting same-frame trails rejected");
        Check(!h.Apply(3,key,*last,Identity()),"ambiguous trail cannot seed next frame");
        auto bad=a;bad[2][2][0]=5;Check(!S::Decode(bad,order),"non-ribbon UVs rejected");
        bad=a;bad[0][1][0]=.5f;Check(!S::Decode(bad,order),"other colored effects rejected");
        bad=a;bad[0][0][0]=std::numeric_limits<float>::infinity();
        Check(!S::Decode(bad,order),"nonfinite ribbon rejected");
        auto bad_order=order;bad_order[4]=0;Check(!S::Decode(a,bad_order),"invalid strip connectors rejected");
        bad_order[0]=999;Check(!S::Decode(a,bad_order),"ribbon indices bounded");
        h.Reset();Check(!h.Apply(7,key,*first,Identity()),"trail reset clears history");
    }

    for(const auto wing:std::array<std::array<unsigned,10>,2>{{
            {27,28,29,30,31,26,24,25,32,33},{9,10,11,12,13,6,8,7,14,15}}}) {
        namespace L=MM3D::LinkInterpolation;
        using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose pose;
        pose.signature=0xf4c419547d6f24a2ull;pose.pose.instance=100;pose.model=200;
        pose.pose.bones.resize(42);pose.bind.resize(42);pose.words.resize(42);
        const auto update=[&](double angle) {
            for(unsigned i=0;i<42;++i) {
                pose.pose.bones[i]=Flatten(i?Camera(angle+i,3*i):Identity());
                pose.bind[i]=Flatten(Identity());pose.words[i]=i?i:0xffff0000;
            }
        };
        const auto palette_for=[&] {
            L::Palette palette;
            for(unsigned slot=0;slot<10;++slot) {
                palette[slot]=Expand(pose.pose.bones[wing[slot]]);
                palette[slot][0][0]+=.00004;palette[slot][0][3]+=.04;
            }
            return palette;
        };
        update(0);auto palette=palette_for();
        Check(L::SoaringWingBones(palette,pose,864)==wing,"complete Soaring wing palette identified");
        Check(!L::Identify(palette[0],pose),"Soaring correction remains outside generic match tolerance");
        Check(!L::SoaringWingBones(palette,pose,108),"other wing inherited palettes excluded");
        auto wrong=pose;wrong.signature++;
        Check(!L::SoaringWingBones(palette,wrong,864),"other actors cannot use wing tolerance");
        auto bad=palette;bad[9][0][3]+=.2;
        Check(!L::SoaringWingBones(bad,pose,864),"every wing slot must agree");
        bad=palette;bad[9][0][0]+=.001;
        Check(!L::SoaringWingBones(bad,pose,864),"large wing basis mismatch excluded");
        bad=palette;bad[0][0][0]=std::numeric_limits<double>::quiet_NaN();
        Check(!L::SoaringWingBones(bad,pose,864),"nonfinite wing palette rejected");
        L::History h;
        Check(!h.Apply(1,0,Identity(),palette,{pose},true,864)[0],"wing first frame seeds history");
        const auto still=h.Apply(2,33,Identity(),palette,{pose},true,864);
        for(unsigned slot=0;slot<10;++slot)
            Check(still[slot] && Near(*still[slot],palette[slot],1e-9),"stationary wings preserve guest bind correction");
        update(20);palette=palette_for();const auto native=palette;
        const auto half=h.Apply(3,66,Identity(),palette,{pose},true,864);
        for(unsigned slot=0;slot<10;++slot) {
            const auto current=Expand(pose.pose.bones[wing[slot]]);
            const auto expected=Multiply(Camera(10+wing[slot],3*wing[slot]),Multiply(*Inverse(current),palette[slot]));
            Check(half[slot] && Near(*half[slot],expected,1e-6),"all wing bones interpolate with native bind correction");
        }
        Check(palette==native,"wing native palette remains untouched");
        Check(!h.Apply(3,66,Identity(),palette,{pose},false,864)[0],"wing fallback disables midpoint");
        const auto repeated=h.Apply(3,66,Identity(),palette,{pose},true,864);
        Check(repeated==half,"wing repeated draw is consistent");
        Check(!h.Apply(5,132,Identity(),palette,{pose},true,864)[0],"wing missing frame stays native");
        auto duplicate=pose;duplicate.pose.instance++;
        Check(!h.Apply(6,165,Identity(),palette,{pose,duplicate},true,864)[0],"coincident wing actors stay ambiguous");
        h.Reset();Check(!h.Apply(7,198,Identity(),palette,{pose},true,864)[0],"wing reset clears history");
    }

    {
        namespace S=MM3D::SparkInterpolation;
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto spark=[](float x,float angle,float size,float alpha) {
            std::vector<S::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                const float a=(float(k/2)-.5f)*size,b=(.5f-float(k%2))*size;
                v[k][0]={x+a*std::cos(angle)-b*std::sin(angle),a*std::sin(angle)+b*std::cos(angle),0,1};
                v[k][1]={1,1,.5f,alpha};v[k][2]={float(k/2)*2,float(1-k%2)*2,0,1};
            }
            return v;
        };
        const auto native=spark(1,.35f,2.2f,160.f/255);
        auto a=S::Decode(spark(0,0,2,178.f/255),order),b=S::Decode(native,order);
        Check(a && b,"fuse stars recognized");
        S::History h;const S::Key key{1,1};
        Check(!h.Apply(1,key,*a) && !h.Apply(1,key,*a),"repeated first spark draw seeds once");
        auto half=h.Apply(2,key,*b);
        Check(half && S::Near(S::Center(half->front())[0],.5f),"fuse spark movement interpolates");
        Check(S::Near(half->front().vertices[0][1][3],169.f/255),"fuse spark fade interpolates");
        float edge2=0;
        for(unsigned c=0;c<3;++c) {
            float d=half->front().vertices[1][0][c]-half->front().vertices[0][0][c];edge2+=d*d;
        }
        Check(std::abs(std::sqrt(edge2)-2.1f)<1e-5,"rotating fuse star retains mean size");
        auto reordered=*b;reordered[0].indices={7,6,5,4};
        const auto repeated=h.Apply(2,key,reordered);
        Check(repeated && repeated->front().vertices==half->front().vertices &&
              repeated->front().indices==reordered.front().indices,"overlapping draw reuses midpoint with current indices");
        auto c=S::Decode(spark(2,.7f,2,140.f/255),order);
        Check(h.Apply(3,key,*c).has_value(),"overlapping sparks do not create duplicate previous candidates");
        Check(!h.Apply(5,key,*c),"spark frame gaps stay native");
        Check(!h.Apply(6,{1,2},*c),"spark scene generation isolates history");
        Check(!h.Apply(1,key,*a),"spark rewind stays native");
        Check(!S::Compatible(a->front(),b->front()),"fuse alpha reset cannot borrow dying spark");
        auto gray=spark(0,0,3,.5f);
        for(unsigned k=0;k<4;++k)gray[k][1]={.3f,.3f,.3f,.5f};
        auto mixed=native;mixed.resize(9);
        for(unsigned k=0;k<4;++k)mixed[4+k]=gray[k];
        const auto filtered=S::Decode(mixed,std::vector<std::uint32_t>{0,1,2,3,3,4,4,5,6,7,7,8});
        Check(filtered && filtered->size()==1,"gray particles in fuse batch excluded");
        auto output=mixed;
        for(unsigned k=0;k<4;++k)output[half->front().indices[k]]=half->front().vertices[k];
        for(unsigned k=4;k<9;++k)Check(output[k]==mixed[k],"excluded particle input untouched");
        for(unsigned k=0;k<4;++k)Check(output[k][2]==native[k][2],"fuse UVs untouched");
        h.Reset();auto neighbors=*a;auto neighbor=a->front();
        for(auto&v:neighbor.vertices)v[0][0]+=2;
        neighbors.push_back(neighbor);h.Apply(1,key,neighbors);
        Check(!h.Apply(2,key,*b),"equidistant spark history remains native");
        h.Reset();auto extra=a->front();for(auto&v:extra.vertices)v[0][0]+=20;
        h.Apply(1,key,{a->front(),extra});h.Apply(1,key,*a);
        Check(h.Apply(2,key,*b).has_value(),"overlapping subset draws preserve independent spark history");
        Check(!S::Decode(native,std::vector<std::uint32_t>{0,1,2,3,3,99}),"invalid fuse input rejected");
    }
    for(unsigned vertices:{90u,144u}) {
        namespace S=MM3D::SongEffectInterpolation;
        S::Values a{{{vertices==90?8.f:1.f,0,0,0},{0,vertices==90?4.f:5.f,0,0},
                     {3,0,0,10},{0,2,0,20}}};
        Check(S::Recognized(vertices,a,1),"single texture Epona layer recognized with stale inactive UVs");
        Check(!S::Recognized(vertices,a,0) && !S::Recognized(vertices,a,3) &&
              !S::Recognized(4,a,1),"Epona requires active material and mesh");
        auto bad=a;bad[0][1]=.01f;
        Check(!S::EponaSingleTexture(vertices,bad,1),"Epona active basis validated");
        bad=a;bad[0][3]=.1f;
        Check(!S::EponaSingleTexture(vertices,bad,1),"Epona offset relationship validated");
        S::History h;const S::Key key{1,2,0,0,257,vertices,0};
        Check(!h.Apply(1,key,a,true,1),"Epona first frame seeds history");
        auto b=a;const auto step=*S::EponaSingleTexture(vertices,a,1);
        for(unsigned r=0;r<2;++r)b[r][3]+=step[r];
        b[2]={5,4,3,2};b[5][3]=100;
        const auto native=b;auto half=h.Apply(2,key,b,true,1);
        Check(half.has_value(),"Epona scroll interpolates despite stale inactive matrices");
        for(unsigned r=0;r<6;++r)for(unsigned c=0;c<4;++c)
            Check(std::abs((*half)[r][c]-(r<2 && c==3?step[r]*.5f:b[r][c]))<1e-6,
                  "Epona changes only active offsets");
        Check(b==native,"Epona native uniforms untouched");
        b[2][0]=std::numeric_limits<float>::quiet_NaN();
        Check(S::Recognized(vertices,b,1),"disabled nonfinite Epona uniform ignored");
        half=h.Apply(2,key,b,true,1);
        Check(half && std::isnan((*half)[2][0]),"repeated Epona draw preserves its inactive uniforms");
        Check(!h.Apply(2,key,b,false,1),"Epona fallback suppresses override");
        for(unsigned r=0;r<2;++r)b[r][3]+=step[r];
        Check(!h.Apply(3,key,b,false,1),"Epona fallback advances native history");
        for(unsigned r=0;r<2;++r)b[r][3]+=step[r];
        Check(h.Apply(4,key,b,true,1).has_value(),"Epona recovers from fallback");
        Check(h.Apply(5,key,b,true,1).has_value(),"stationary Epona frame supported");
        Check(!h.Apply(7,key,b,true,1),"Epona frame gap stays native");
        Check(!h.Apply(8,key,a,true,1),"Epona animation restart stays native");
        Check(!h.Apply(1,key,a,true,1),"Epona frame rewind stays native");
        h.Reset();Check(!h.Apply(2,key,a,true,1),"Epona reset clears history");
    }

    {
        namespace E=MM3D::MovingEffectInterpolation;
        const auto uv=[](unsigned kind,double theta,float scroll) {
            E::Values v{};
            const auto rotation=[&](unsigned row,double angle) {
                const float c=2*std::cos(angle),s=2*std::sin(angle);
                v[row]={c,-s,0,1.f-.5f*(c-s)};v[row+1]={s,c,0,1.f-.5f*(s+c)};
            };
            if(kind==6) {
                v[0]={3,0,0,-scroll*3};v[1]={0,1,0,0};v[2]={4,0,0,scroll*4};v[3]={0,1,0,0};rotation(4,theta);
            } else {rotation(0,theta);rotation(2,-theta);v[4]={2,0,0,0};v[5]={0,2,0,0};}
            return v;
        };
        for(unsigned kind:{6u,7u,8u}) {
            constexpr double step=3.141592653589793/240;
            const auto& pattern=E::Patterns[kind];E::Sample a{},b{};
            a.model=Identity();b.model=Camera(0,2);
            a.uv=uv(kind,3.14,0);b.uv=uv(kind,3.14+step,1.f/120);
            Check(E::Identify(pattern.vertices,pattern.mask,a.uv,pattern.material)==kind,"mirror material and UV animation recognized");
            auto mid=E::Midpoint(kind,a,b);
            Check(mid && E::MirrorUV(kind,mid->uv),"mirror midpoint retains texture scale and pivot");
            auto expected=uv(kind,3.14+step*.5,1.f/240);
            if(mid)for(unsigned r=0;r<6;++r)for(unsigned c=0;c<4;++c)
                Check(std::abs(mid->uv[r][c]-expected[r][c])<1e-5,"mirror rotation wraps through pi without jumping");
            auto bad=b;bad.uv=uv(kind,3.14+.3,1.f/120);
            Check(!E::Midpoint(kind,a,bad),"mirror animation reset rejected");
            bad=b;bad.uv[0][0]=std::numeric_limits<float>::quiet_NaN();
            Check(!E::Midpoint(kind,a,bad),"nonfinite mirror UV rejected");
        }
    }

    {
        namespace I=MM3D::IceParticleInterpolation;
        const auto particle=[](float x,float size,float alpha,int tile,I::Kind kind) {
            std::vector<I::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+(k/2-.5f)*size,(1-k%2-.5f)*size,0,1};
                v[k][1]=kind==I::Kind::Breath?std::array<float,4>{120.f/255,96.f/255,48.f/255,alpha}:
                    kind==I::Kind::FreezeMist?std::array<float,4>{-.1476354897f,-.0430603027f,-.0430603027f,alpha}:
                    kind==I::Kind::FreezeShard?std::array<float,4>{1,1,1,alpha}:
                    std::array<float,4>{105.f/255,105.f/255,105.f/255,alpha};
                v[k][2]=I::Atlas(kind)?std::array<float,4>{(tile%4+k/2)*.25f,1-(tile/4+k%2)*.25f,0,1}:
                    kind==I::Kind::FreezeShard?std::array<float,4>{float(k/2),float(1-k%2),0,1}:
                    std::array<float,4>{(tile+k/2)*.5f,float(1-k%2),0,1};
            }
            return v;
        };
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        for(auto kind:{I::Kind::Breath,I::Kind::Pop}) {
            const float size=kind==I::Kind::Breath?50:12;
            const int old_tile=kind==I::Kind::Breath?15:1,new_tile=kind==I::Kind::Breath?0:1;
            auto a=I::Decode(particle(0,size,.8f,old_tile,kind),order,kind);
            auto b=I::Decode(particle(2,size,.775f,new_tile,kind),order,kind);
            Check(a && b,"captured breath/pop geometry recognized");
            I::History h;
            Check(!h.Apply(1,1,0,kind,*a),"particle birth seeds history");
            auto mid=h.Apply(2,1,0,kind,*b);
            Check(mid && std::abs(I::Center(mid->front())[0]-1)<.001f,"particle translation interpolated");
            Check(mid && std::abs(mid->front().vertices[0][1][3]-.7875f)<.001f,"particle fade interpolated");
            Check(mid && mid->front().vertices[0][2]==b->front().vertices[0][2],"atlas wrap retains native tile");
            Check(!h.Apply(4,1,0,kind,*b),"particle capture gap reseeds");
            Check(!h.Apply(5,1,1,kind,*b),"particle scene generation reseeds");
            auto bad=particle(0,size,.8f,old_tile,kind);bad[1][1][0]+=.1f;
            Check(!I::Decode(bad,order,kind),"unrelated particle color rejected");
            auto spawned=*b;spawned[0].vertices[0][1][3]=.1f;
            Check(!I::Compatible(spawned[0],a->front(),kind),"particle lifetime restart rejected");
        }
        for(auto kind:{I::Kind::FreezeMist,I::Kind::FreezeShard}) {
            const float size=kind==I::Kind::FreezeMist?78.08f:5.2f;
            const float old_alpha=kind==I::Kind::FreezeMist?.2f:1.f;
            const float alpha=kind==I::Kind::FreezeMist?.22745f:1.f;
            const auto a=I::Decode(particle(0,size,old_alpha,15,kind),order,kind);
            const auto b=I::Decode(particle(2,size,alpha,0,kind),order,kind);
            Check(a && b,"freezing mist and square ice fragments recognized");
            I::History h;h.Apply(1,1,0,kind,*a);auto mid=h.Apply(2,1,0,kind,*b);
            Check(mid && std::abs(I::Center(mid->front())[0]-1)<.001f,"freezing particles get intermediate positions");
            Check(mid && std::abs(mid->front().vertices[0][1][3]-(alpha+old_alpha)*.5f)<.00001f,
                "freezing mist opacity interpolates");
            Check(mid && mid->front().vertices[0][1][0]==b->front().vertices[0][1][0],
                "signed ice mist color offsets stay native");
            auto wrong=particle(0,size,alpha,0,kind);
            for(unsigned k=2;k<4;++k)wrong[k][0][0]+=size/6;
            Check(!I::Decode(wrong,order,kind),"rectangular bomb fragments cannot become ice shards");
            Check(!h.Apply(4,1,0,kind,*b),"freeze particles do not bridge gaps");
        }
        const auto qa=I::Decode(particle(0,50,.8f,4,I::Kind::Breath),order,I::Kind::Breath);
        const auto qb=I::Decode(particle(100,60,.8f,4,I::Kind::Breath),order,I::Kind::Breath);
        const auto na=I::Decode(particle(2,50,.8f,5,I::Kind::Breath),order,I::Kind::Breath);
        const auto nb=I::Decode(particle(102,60,.8f,5,I::Kind::Breath),order,I::Kind::Breath);
        I::History multiple;
        multiple.Apply(1,1,0,I::Kind::Breath,*qa);multiple.Apply(1,1,0,I::Kind::Breath,*qb);
        auto mb=multiple.Apply(2,1,0,I::Kind::Breath,*nb);
        auto ma=multiple.Apply(2,1,0,I::Kind::Breath,*na);
        Check(ma && mb,"multiple enemies sharing a material retain both histories");
        Check(ma && std::abs(I::Center(ma->front())[0]-1)<.001f &&
            mb && std::abs(I::Center(mb->front())[0]-101)<.001f,"reordered batches keep particle ownership");
        std::vector<I::Quad> ambiguous{qa->front(),qa->front()};
        Check(!I::Match(na->front(),ambiguous,I::Kind::Breath,true),"ambiguous particles stay native");
        I::Tev unrelated{};Check(I::Material(unrelated)==I::Kind::None,"unrelated material rejected");
    }

    {
        namespace B = MM3D::BurstParticleInterpolation;
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto dust = [](float x, float size, int tile, float alpha) {
            std::vector<B::Vertex> v(5);
            for (unsigned k = 0; k < 4; ++k) {
                v[k][0] = {x+(k/2-.5f)*size, (1-k%2-.5f)*size, 0, 1};
                v[k][1] = {70.f/255,70.f/255,70.f/255,alpha};
                v[k][2] = {(tile%4+k/2)*.25f, 1-(tile/4+k%2)*.25f, 0, 1};
            }
            return v;
        };
        const auto flame = [](float x, float phase) {
            std::vector<B::Vertex> v(5);
            for (unsigned k = 0; k < 4; ++k) {
                const float u = float(k/2), t = float(1-k%2);
                v[k][0] = {x+(u-.5f)*32.4f,(t-.5f)*48,0,1};
                v[k][1] = {.75f,.55f,.85f,.8f};
                v[k][2] = {u,t,0,1};v[k][3] = {u,t+phase,0,1};
                v[k][4] = {-2*u,2*t+4*phase,0,1};
            }
            return v;
        };
        auto a = B::Decode(dust(0,20,7,1),order,3);
        auto b = B::Decode(dust(4,21.7f,8,.75f),order,3);
        Check(a && b,"dust atlas squares recognized across tile row wrap");
        const B::Key key{1,2,3,1,0};B::History h;
        Check(!h.Apply(1,key,*a),"dust first frame stays native");
        auto half=h.Apply(2,key,*b);
        Check(half && B::Near(B::Center(half->front())[0],2) &&
              B::Near(B::Edge(half->front()),20.85f) && B::Near(half->front().vertices[0][1][3],.875f),
              "dust movement size and fade interpolate");
        Check(half->front().vertices[0][2]==b->front().vertices[0][2],"dust UVs never blend across atlas tiles");
        Check(!h.Apply(2,key,*b) && !h.Apply(3,key,*b),"duplicate particle batch cannot seed history");
        Check(!h.Apply(5,key,*b),"missing particle frame resets history");
        auto other_key=key;other_key[3]++;
        Check(!h.Apply(6,other_key,*b),"scene changes reset particles");
        Check(!h.Apply(1,key,*a),"particle rewind resets history");
        auto bad=dust(0,20,7,1);bad[0][1][0]=1;
        Check(!B::Decode(bad,order,3),"other tint excluded from dust");
        bad=dust(0,20,7,1);bad[0][2][0]+=.125f;
        Check(!B::Decode(bad,order,3),"off-grid atlas rejected");
        auto invalid_order=order;invalid_order[0]=999;
        Check(!B::Decode(dust(0,20,7,1),invalid_order,3),"invalid particle index rejected");
        bad=dust(0,20,7,1);bad[0][0][0]=std::numeric_limits<float>::quiet_NaN();
        Check(!B::Decode(bad,order,3),"nonfinite particle rejected");
        Check(!B::Compatible(a->front(),b->front()),"dust age reset rejected");
        auto distant=b->front();for(auto&v:distant.vertices)v[0][0]+=100;
        Check(!B::Compatible(distant,a->front()),"distant spawn does not borrow dust history");
        h.Reset();auto duplicates=*a;duplicates.push_back(a->front());h.Apply(1,key,duplicates);
        Check(!h.Apply(2,key,*b),"ambiguous particle neighbors stay native");
        std::vector<B::Quad> old_dust, new_dust;
        for (int age : {4, 10}) for (unsigned i=0;i<30;++i) {
            auto before=B::Decode(dust(0,9.7f+1.7f*age,age,1),order,3)->front();
            auto after=B::Decode(dust(0,9.7f+1.7f*(age+1),age+1,1),order,3)->front();
            const float angle=float(i)*6.2831853f/30;
            const std::array<float,3> position{std::cos(angle)*20,0,std::sin(angle)*20};
            for(unsigned k=0;k<4;++k)for(unsigned c=0;c<3;++c) {
                before.vertices[k][0][c]+=position[c]+(c==0?age*100.f:0);
                after.vertices[k][0][c]+=position[c]*float(age+2)/float(age+1)+(c==0?age*100.f+1:0);
            }
            old_dust.push_back(before);new_dust.push_back(after);
        }
        std::reverse(new_dust.begin(),new_dust.end());
        auto dust_matches=B::DustMatches(new_dust,old_dust);
        for(unsigned i=0;i<60;++i)
            Check(dust_matches[i] && *dust_matches[i]==59-i,"overlapping dust bursts match by age and expansion after sorting");
        new_dust[0]=new_dust[1];dust_matches=B::DustMatches(new_dust,old_dust);
        Check(!dust_matches[0],"changed dust burst population rejects group correspondence");
        h.Reset();a=B::Decode(flame(0,-59.f/60),order,5);b=B::Decode(flame(2,0),order,5);
        Check(a && b,"purple flame linked texture layers recognized");
        h.Apply(1,key,*a);half=h.Apply(2,key,*b);
        Check(half && B::Near(B::Center(half->front())[0],1) &&
              B::Near(half->front().vertices[1][3][1],1.f/120) &&
              B::Near(half->front().vertices[1][4][1],1.f/30),
              "flame scroll wraps both layers together at midpoint");
        bad=flame(0,0);bad[0][4][1]+=.1f;
        Check(!B::Decode(bad,order,5),"unlinked flame texture layers excluded");
        bad=flame(0,0);bad[0][1][2]=.5f;
        Check(!B::Decode(bad,order,5),"other flame tint excluded");
        auto blood=dust(0,4,0,.5f);
        for(unsigned k=0;k<4;++k) {
            const float alpha=.5f;
            blood[k][1]={k==0?alpha*10/255:0,
                k==0?(32+168*alpha)/255:k==2?(32+96*alpha)/255:(64+64*alpha)/255,
                k==0?alpha*10/255:0,alpha};
            blood[k][2]={float(k/2),float(1-k%2),0,1};
        }
        a=B::Decode(blood,order,3);Check(a && a->front().kind==B::Kind::Blood,"green blood gradient recognized");
        for(unsigned k=0;k<4;++k)blood[k][0][0]+=3;
        b=B::Decode(blood,order,3);h.Reset();h.Apply(1,key,*a);half=h.Apply(2,key,*b);
        Check(half && B::Near(B::Center(half->front())[0],1.5f),"blood travels at midpoint");
        Check(half->front().vertices[0][1]==a->front().vertices[0][1] &&
              half->front().vertices[1][1]==a->front().vertices[1][1],"blood corner gradient retained");
        const auto blue = [&](float x, float alpha) {
            auto v=dust(x,4,0,alpha);
            for(unsigned k=0;k<4;++k) {
                v[k][1]={k==0?alpha*10/255:0,k==0?alpha*10/255:0,
                    k==0?(32+168*alpha)/255:k==2?(32+96*alpha)/255:(64+64*alpha)/255,alpha};
                v[k][2]={float(k/2),float(1-k%2),0,1};
            }
            return v;
        };
        const auto blue_before=B::Decode(blue(0,.5f),order,3,true);
        const auto blue_after=B::Decode(blue(3,.5f-1.f/24),order,3,true);
        Check(blue_before && blue_after && blue_after->front().kind==B::Kind::BlueBlood,
              "blue blood exact corner gradient recognized");
        Check(!B::Decode(blue(0,.5f),order,3),"blue blood requires captured sprite material");
        auto wrong_blue=blue(0,.5f);wrong_blue[2][1][2]+=.1f;
        Check(!B::Decode(wrong_blue,order,3,true),"unrelated blue gradient rejected");
        Check(!B::Compatible(blue_after->front(),a->front()),"blue and green blood cannot share motion");
        Check(!B::Compatible(blue_before->front(),blue_after->front()),"blue blood lifetime reset rejected");
        B::History blue_history;auto blue_key=key;blue_key[4]=unsigned(B::Kind::BlueBlood);
        blue_history.Apply(1,blue_key,*blue_before);
        const auto blue_half=blue_history.Apply(2,blue_key,*blue_after);
        Check(blue_half && B::Near(B::Center(blue_half->front())[0],1.5f) &&
              B::Near(blue_half->front().vertices[0][1][3],.5f-1.f/48),
              "blue droplet position and lifetime interpolate");
        Check(blue_half->front().vertices[0][2]==blue_after->front().vertices[0][2],
              "blue blood UVs stay native");
        Check(!blue_history.Apply(4,blue_key,*blue_after),"missing blue blood frame resets history");
        blood[2][1][1]=.75f;
        Check(!B::Decode(blood,order,3),"unrelated green corner pattern rejected");
        std::vector<B::Quad> cloud;
        for(unsigned i=0;i<27;++i) {
            auto q=a->front();
            for(auto&v:q.vertices) {
                v[0][0]+=float(i%3)*2;v[0][1]+=float(i/3%3)*3;v[0][2]+=float(i/9)*4;
            }
            cloud.push_back(q);
        }
        auto expanded=cloud;
        for(auto&q:expanded) {
            const auto center=B::Center(q);
            for(auto&v:q.vertices)for(unsigned c=0;c<3;++c)v[0][c]+=center[c]*.1f+1;
        }
        std::reverse(expanded.begin(),expanded.end());
        auto matches=B::BloodMatches(expanded,cloud);
        Check(matches && (*matches)[0]==26 && (*matches)[26]==0,
              "expanding blood cloud matches reordered drops without relying on input slots");
        auto blue_cloud=cloud,blue_expanded=expanded;
        for(auto* group:{&blue_cloud,&blue_expanded})for(auto& q:*group) {
            q.kind=B::Kind::BlueBlood;
            for(auto& v:q.vertices)std::swap(v[1][1],v[1][2]);
        }
        const auto blue_matches=B::BloodMatches(blue_expanded,blue_cloud);
        Check(blue_matches && *blue_matches==*matches,"blue burst matches all 27 reordered droplets");
        Check(!B::BloodMatches(blue_expanded,cloud),"different blood colors cannot share a burst");
        expanded[0]=expanded[1];
        Check(!B::BloodMatches(expanded,cloud),"changed blood population cannot use expansion matching");
        auto debris=dust(0,6,0,1);
        for(unsigned k=0;k<4;++k) {
            debris[k][0][0]*=7.f/6;debris[k][1]={1,1,1,1};
            debris[k][2]={float(k/2),float(1-k%2),0,1};
        }
        Check(!B::Decode(debris,order,3),"white sprites require verified bomb material");
        a=B::Decode(debris,order,3,true);
        Check(a&&a->front().kind==B::Kind::BombDebris,"bomb fragment aspect recognized");
        for(unsigned k=0;k<4;++k)debris[k][0][0]+=4;
        b=B::Decode(debris,order,3,true);h.Reset();h.Apply(1,key,*a);half=h.Apply(2,key,*b);
        Check(half&&B::Near(B::Center(half->front())[0],2),"bomb fragments get halfway motion");
        Check(half->front().vertices[0][2]==b->front().vertices[0][2],"bomb fragment texture stays native");
        auto resized=b->front();for(auto&v:resized.vertices)v[0][1]*=1.1f;
        Check(!B::Compatible(resized,a->front()),"different fragment sizes cannot borrow motion");
        debris[0][1][3]=.5f;
        Check(!B::Decode(debris,order,3,true),"unverified translucent white sprite rejected");
        std::array<std::array<std::uint32_t,5>,6> material{};
        for(unsigned i=0;i<6;++i)material[i]={i?268374015u:238030384u,0,i?0u:65537u,i?0xffffffffu:0u,0};
        Check(B::BombMaterial(material),"captured bomb material recognized");
        material[0][1]=32;
        Check(!B::BombMaterial(material),"other particle material rejected");
    }
    {
        namespace M = MM3D::MessageInterpolation;
        const auto plate = [](float scale, float opacity) {
            M::Values u{};
            u[8] = {opacity, 1, opacity * .90196085f, 1};
            u[10] = {0, 3, 94, -93};
            u[11] = {200 - 125.9168f * scale, 0, 0, 251.8336f * scale};
            u[12] = {111.9f - 31.92f * scale, 0, 63.84f * scale, 0};
            u[13] = {5 * scale, scale, 0, 0};
            u[94] = {0, 0, 0, 1};u[95] = {0, .25f, .25f, 0};
            return u;
        };
        M::History h;const M::Key key{123, 456, 1};
        auto a = plate(.7f, 0), b = plate(1.05f, .3f);
        Check(M::Recognized(a) && M::Recognized(b), "message opening plate recognized");
        Check(!h.Apply(1, key, a), "message first frame stays native");
        auto half = h.Apply(2, key, b);
        Check(half && M::Near((*half)[3][1], .875f) && M::Near((*half)[0][0], .15f),
              "message opening scale and fade have a midpoint");
        Check(M::Near((*half)[1][0] + (*half)[1][3] * .5f, 200),
              "message midpoint scales about the native center");
        a = plate(.7875f, .3f);a[65][1] = 12345;a[92][1] = .916015625f; // inactive earlier batch data
        half = h.Apply(3, key, a);
        Check(half && M::Near((*half)[3][1], .91875f), "message closing ignores stale layout uniforms");
        Check(!h.Apply(5, key, b), "hidden message gaps do not reuse old motion");
        Check(!h.Apply(6, M::Key{123, 456, 2}, b), "message scene generation seeds history");
        Check(bool(h.Apply(6, key, b)), "compatible message resumes after gap");
        Check(!h.Apply(6, key, b) && !h.Apply(7, key, a), "ambiguous plate cannot seed next midpoint");
        h.Reset();h.Apply(1, key, b);
        auto moved = b;moved[11][0] += 30;
        Check(!h.Apply(2, key, moved), "different window center is not blended");
        auto hud = b;hud[10][3] = 1;
        Check(!M::Recognized(hud), "ordinary HUD quad excluded");
        auto text = b;text[10][0] = 1;
        Check(!M::Recognized(text), "glyph quad excluded");
        auto invalid = b;invalid[11][0] = std::numeric_limits<float>::quiet_NaN();
        Check(!M::Recognized(invalid), "nonfinite message coordinates rejected");
        Check(!M::Candidate(0x9b343fea686d98e0ULL, 0xac22b79e29209c01ULL, 96, -93),
              "text batches do not incur CPU message replay capture");
        h.Reset();h.Apply(10, key, b);a = b;a[95][1] = .5f;
        Check(!h.Apply(11, key, a) && !h.Apply(12, key, b), "atlas changes break message history");
        h.Reset();h.Apply(10, key, b);a = b;a[0][0] = 2;
        Check(!h.Apply(11, key, a), "message projection changes stay native");
        h.Reset();h.Apply(10, key, b);
        Check(!h.Apply(1, key, b), "message rewind seeds history");
        h.Reset();h.Apply(1, key, b);a = plate(.2f, .3f);
        Check(!h.Apply(2, key, a), "large message pose discontinuity stays native");
        const auto dialogue = [&](float scale, const std::array<float, 4>& color) {
            auto u = plate(scale, 1);
            u[95] = {0, 0, .25f, 0};u[8] = color;
            u[12][0] += 75;
            return u;
        };
        h.Reset();
        a = dialogue(.35f, {.5882353f, .5882353f, 1, 1});
        b = dialogue(.7f, {.7117647f, .7117647f, .9705882f, 1});
        Check(M::Recognized(a) && M::Recognized(b), "regular dialogue atlas and animated tint recognized");
        h.Apply(1, key, a);half = h.Apply(2, key, b);
        Check(half && M::Near((*half)[3][1], .525f) && M::Near((*half)[0][1], .65f),
              "regular dialogue opening scale and tint interpolate");
        h.Reset();a = dialogue(.525f, {0, 0, 0, .6980392f});
        b = dialogue(.2625f, {0, 0, 0, .6980392f});
        h.Apply(1, key, a);half = h.Apply(2, key, b);
        Check(half && M::Near((*half)[3][1], .39375f) && M::Near((*half)[0][3], .6980392f),
              "translucent black dialogue closing interpolates without changing opacity");
        h.Reset();a = dialogue(1, {0, 0, 0, .5f});b = dialogue(1, {0, 0, 0, .7f});
        h.Apply(1, key, a);half = h.Apply(2, key, b);
        Check(half && M::Near((*half)[0][3], .6f), "regular dialogue opacity interpolates");
        b[8] = {1, 1, .90196085f, 1};
        Check(!h.Apply(3, key, b), "sudden dialogue color switch stays native");
        h.Reset();a = plate(1, 1);b = a;b[95][1] = 0;
        h.Apply(1, key, a);
        Check(!h.Apply(2, key, b), "sign to regular dialogue style switch seeds fresh history");
        b[95][1] = .5f;
        Check(!M::Recognized(b), "unverified atlas rows remain excluded");
        // Latest regular textbox uses the warm fade previously accepted only
        // on signs. Retain its first-row atlas and native center/size.
        const auto warm = [](float scale, float fade) {
            M::Values u{};
            u[8]={fade,1,fade*(230.f/255),1};
            u[10]={0,3,94,-93};u[13]={5*scale,scale,0,0};
            u[11]={200-127.175964f*scale,0,0,254.351929f*scale};
            u[12]={186.899998f-32.239201f*scale,0,64.478402f*scale,0};
            u[93]={.87843144f,1,1,1};u[94]={0,0,0,1};u[95]={0,0,.25f,0};
            return u;
        };
        h.Reset();a=warm(.35f,0);b=warm(.7f,0);
        Check(M::Recognized(a)&&M::Recognized(b),"warm dialogue opening recognized on first atlas row");
        h.Apply(1,key,a);half=h.Apply(2,key,b);
        Check(half&&M::Near((*half)[3][1],.525f),"warm dialogue pop-in gets halfway scale");
        h.Reset();a=warm(1,.94531262f);b=warm(1,1.02734387f);
        h.Apply(1,key,a);half=h.Apply(2,key,b);
        Check(half&&M::Near((*half)[0][0],.98632824f)&&M::Near((*half)[0][1],1),
              "warm dialogue fade overshoot interpolates while fixed channels stay fixed");
        h.Reset();a=warm(.525f,1);b=warm(.2625f,1);
        h.Apply(1,key,a);half=h.Apply(2,key,b);
        Check(half&&M::Near((*half)[3][1],.39375f),"warm dialogue closing gets halfway scale");
        b=warm(1,.5f);b[8][2]=.2f;
        Check(!M::Recognized(b),"unrelated first-row color is not a warm dialogue fade");
        b=warm(1,.5f);b[8][3]=.7f;
        Check(!M::Recognized(b),"warm fade requires its verified fixed alpha");
        const auto cream = [&](float scale) {
            auto u=warm(scale,1);u[8]={250.f/255,1,230.f/255,1};return u;
        };
        h.Reset();a=cream(.7f);b=cream(1.05f);
        Check(M::Recognized(a)&&M::Recognized(b),"fixed cream dialogue recognized");
        h.Apply(1,key,a);half=h.Apply(2,key,b);
        Check(half&&M::Near((*half)[3][1],.875f)&&(*half)[0]==a[8],
              "cream dialogue opening interpolates scale and preserves tint");
        h.Reset();a=cream(.525f);b=cream(.2625f);
        h.Apply(1,key,a);half=h.Apply(2,key,b);
        Check(half&&M::Near((*half)[3][1],.39375f),"cream dialogue closing interpolates");
        b=cream(1);b[8][0]=.95f;
        Check(!M::Recognized(b),"unverified cream tint rejected");
        b=cream(1);b[8][3]=.7f;
        Check(!M::Recognized(b),"cream dialogue requires captured alpha");
    }
    {
        using namespace MM3D::LinkPoseMath;
        namespace I=MM3D::LinkInterpolation;
        MM3D::LinkPoseCapture::LivePose nut;
        nut.signature=0x31d188026f978991ull;nut.pose.instance=123;nut.model=456;
        nut.words={0xffff0000,1};nut.bind.assign(2,Flatten(Identity()));
        auto a=Camera(0),b=Camera(40,10);
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c){a[r][c]*=.01;b[r][c]*=.01;}
        nut.pose.bones.assign(2,Flatten(a));
        I::Palette palette;palette.fill(a);I::History h;
        Check(!I::Identify(a,nut),"identical projectile bones are ambiguous to the generic matcher");
        Check(!h.Apply(1,0,Identity(),palette,{nut},true,198)[0],"nut seeds history");
        nut.pose.bones.assign(2,Flatten(b));palette.fill(b);
        const auto mid=h.Apply(2,33,Identity(),palette,{nut},true,198);
        const auto expected=Half(a,b);
        Check(mid[0] && expected && Near(*mid[0],*expected,1e-6),
              "Deku nut rotation and translation interpolate through equivalent bones");
        Check(!h.Apply(2,33,Identity(),palette,{nut},false,198)[0],"disabled nut interpolation stays native");
        Check(!h.Apply(3,66,Identity(),palette,{nut},true,48)[0],"other material counts excluded");
        Check(!h.Apply(5,132,Identity(),palette,{nut},true,198)[0],"missing nut frames seed again");
        auto duplicate=nut;duplicate.pose.instance++;
        Check(!h.Apply(6,165,Identity(),palette,{nut,duplicate},true,198)[0],
              "overlapping nut instances remain ambiguous");
        h.Reset();nut.pose.bones.assign(2,Flatten(a));nut.pose.bones[1][3]=2;palette.fill(a);
        h.Apply(1,0,Identity(),palette,{nut},true,198);
        nut.pose.bones.assign(2,Flatten(b));palette.fill(b);
        Check(!h.Apply(2,33,Identity(),palette,{nut},true,198)[0],
              "nut bones must agree on the midpoint as well as the current transform");
    }
    {
        using namespace MM3D::LinkPoseMath;
        namespace I=MM3D::LinkInterpolation;
        MM3D::LinkPoseCapture::LivePose scrub;
        scrub.signature=0x3d654b8a99a21237ull;scrub.pose.instance=123;scrub.model=456;
        scrub.words.assign(27,1);scrub.words[0]=0xffff0000;
        scrub.bind.assign(27,Flatten(Identity()));
        scrub.pose.bones.assign(27,Flatten(Identity()));
        auto a=Camera(0),b=Camera(20,4);
        I::Palette p;p.fill(a);I::History h;
        Check(!h.Apply(1,0,Identity(),p,{scrub},true,750)[0],"Scrub seeds shared head bones");
        scrub.pose.bones.assign(27,Flatten(b));p.fill(b);
        auto half=h.Apply(2,33,Identity(),p,{scrub},true,750);
        Check(half[0] && Near(*half[0],Camera(10,2),1e-6),"Scrub shared head rotates at midpoint");
        auto other=scrub;other.pose.instance++;
        Check(!h.Apply(3,66,Identity(),p,{scrub,other},true,750)[0],"overlapping Scrubs stay ambiguous");
        Check(!h.Apply(4,99,Identity(),p,{scrub},true,750)[0],"missing Scrub frame resets");
        Check(!h.Apply(5,132,Identity(),p,{scrub},false,750)[0],"disabled Scrub stays native");
        h.Reset();scrub.pose.bones.assign(27,Flatten(a));scrub.pose.bones[1][3]=6;p.fill(a);
        h.Apply(1,0,Identity(),p,{scrub},true,750);
        scrub.pose.bones.assign(27,Flatten(b));p.fill(b);
        Check(!h.Apply(2,33,Identity(),p,{scrub},true,750)[0],"divergent shared head histories rejected");
        h.Reset();scrub.signature++;scrub.pose.bones.assign(27,Flatten(a));p.fill(a);
        h.Apply(1,0,Identity(),p,{scrub},true,750);
        scrub.pose.bones.assign(27,Flatten(b));p.fill(b);
        Check(!h.Apply(2,33,Identity(),p,{scrub},true,750)[0],"unverified shared skeleton stays native");
    }
    {
        namespace M=MM3D::MothInterpolation;
        std::array<M::Vertex,6> mesh{};
        const std::array<std::array<float,3>,6> positions{{
            {0,-1.398896f,4.461480f},{5.605600f,.303818f,4.011920f},
            {3.443792f,.050420f,-3.935964f},{-3.443792f,.050420f,-3.935964f},
            {-5.605600f,.303818f,4.011920f},{0,-2.000072f,-4.461480f}}};
        for(unsigned k=0;k<6;++k) {
            mesh[k][0]={positions[k][0],positions[k][1],positions[k][2],1};
            mesh[k][1]={0,127,0,1};
            mesh[k][2]={k==0 || k==5?32767.f:0.f,k==0 || k==1 || k==4?32767.f:0.f,0,1};
        }
        const std::vector<std::uint32_t> order{0,1,2,3,4,0,2,5,0,0,5,3};
        Check(M::Recognized(mesh,order),"captured six-point moth mesh recognized");
        auto bad=mesh;bad[0][0][0]+=1;
        Check(!M::Recognized(bad,order),"unrelated wing geometry excluded");
        bad=mesh;bad[0][2][0]=0;
        Check(!M::Recognized(bad,order),"moth UV layout required");
        auto bad_order=order;bad_order[0]=99;
        Check(!M::Recognized(mesh,bad_order),"invalid moth indices rejected");
        bad_order=order;bad_order[8]=1;
        Check(!M::Recognized(mesh,bad_order),"different moth connectivity rejected");
        auto reordered_mesh=mesh;std::reverse(reordered_mesh.begin(),reordered_mesh.end());
        auto reordered_order=order;for(auto& i:reordered_order)i=5-i;
        Check(M::Recognized(reordered_mesh,reordered_order),"moth recognition follows submission order");
        M::History h;const M::Key key{1,2,3};
        auto a=Identity(),b=Identity();b[0][3]=4;b[1][1]=-1;
        Check(!h.Apply(10,key,a),"moth first frame seeds history");
        const auto half=h.Apply(11,key,b);
        Check(half && (*half)[0][3]==2 && (*half)[1][1]==0,
              "moth flight and signed wing scale interpolate together");
        Check(!h.Apply(11,key,b),"a prior moth cannot match two current draws");
        h.Reset();h.Apply(1,key,a);
        Check(!h.Apply(3,key,b),"missing moth frames do not interpolate");
        h.Reset();h.Apply(1,key,a);b[0][3]=100;
        Check(!h.Apply(2,key,b),"moth teleport stays native");
        h.Reset();h.Apply(1,key,a);
        Check(!h.Apply(2,M::Key{2,2,3},a),"moth textures isolate history");
        Check(!h.Apply(3,M::Key{2,2,4},a),"moth scene changes isolate history");
        h.Reset();b=Identity();b[0][3]=20;
        h.Apply(1,key,a);h.Apply(1,key,b);
        a[0][3]=2;b[0][3]=22;
        const auto right=h.Apply(2,key,b),left=h.Apply(2,key,a);
        Check(right && left && (*right)[0][3]==21 && (*left)[0][3]==1,
              "moths match by world position despite draw reordering");
        h.Reset();a=Identity();b=Identity();b[0][3]=2;
        h.Apply(1,key,a);h.Apply(1,key,b);a[0][3]=1;
        Check(!h.Apply(2,key,a),"ambiguous moth crossing stays native");
        h.Reset();a=Identity();a[1][1]=0;h.Apply(1,key,a);
        Check(h.Apply(2,key,a).has_value(),"collapsed moth wings remain valid");
        a[0][0]=std::numeric_limits<double>::quiet_NaN();
        Check(!h.Apply(3,key,a),"invalid moth transform rejected");
        // Camera motion must not change flight matching or be applied twice.
        h.Reset();a=Identity();b=Identity();b[0][3]=4;
        const auto camera0=Camera(20,500),camera1=Camera(35,520);
        const auto view0=Multiply(InverseRigid(camera0),a),view1=Multiply(InverseRigid(camera1),b);
        h.Apply(1,key,Multiply(camera0,view0));
        const auto world_half=h.Apply(2,key,Multiply(camera1,view1));
        Check(world_half && std::abs((*world_half)[0][3]-2)<1e-8,
              "camera motion excluded from moth flight distance");
        const auto view_half=Multiply(InverseRigid(camera1),*world_half);
        Check(Near(Multiply(camera1,view_half),*world_half,1e-8),
              "moth midpoint returns through the current view");
    }
    {
        namespace L=MM3D::LeafParticleInterpolation;
        const auto leaf=[](float x,float cell) {
            std::vector<L::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+(k<2?0.f:12.f),k%2?0.f:12.f,0,1};
                v[k][1]={0,0,12,1};v[k][2]={1,1,1,1};
                v[k][3]={cell+(k<2?0.f:.5f),k%2?.5f:1.f,0,1};
            }
            return v;
        };
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto a=L::Decode(leaf(0,0),order),b=L::Decode(leaf(2,.5f),order);
        Check(a && b,"captured leaf shape and both upper atlas cells recognized");
        L::History history;const L::Key key{1,1};
        Check(!history.Apply(1,key,*a),"leaves seed history");
        const auto half=history.Apply(2,key,*b);
        Check(half && (*half)[0].vertices[0][0][0]==1,"leaf movement interpolates across atlas changes");
        Check((*half)[0].vertices[0][3]==(*b)[0].vertices[0][3],"leaf atlas cell is not blended across tiles");
        auto small=leaf(0,0);
        for(auto& v:small)for(unsigned a=0;a<2;++a)for(unsigned c=0;c<3;++c)v[a][c]*=7.f/12;
        const auto nut_leaves=L::Decode(small,order);
        Check(nut_leaves.has_value(),"smaller nut-impact atlas leaves recognized");
        auto moved=small;for(auto& v:moved)v[0][0]+=3;
        const auto next_leaves=L::Decode(moved,order);
        L::History nut_history;nut_history.Apply(1,key,*nut_leaves);
        const auto impact_half=nut_history.Apply(2,key,*next_leaves);
        Check(impact_half && (*impact_half)[0].vertices[0][0][0]==1.5f,
              "nut impact leaves move at the midpoint");
        // Majora's detachment burst reaches 100 independent falling squares.
        const auto fragment_batch=[&](unsigned count,float y) {
            std::vector<L::Vertex> inputs;
            std::vector<std::uint32_t> indices;
            for(unsigned q=0;q<count;++q) {
                auto v=leaf(0,q%2?.5f:0.f);
                for(unsigned k=0;k<4;++k) {
                    for(unsigned a=0;a<2;++a)for(unsigned c=0;c<3;++c)v[k][a][c]*=5.9995f/12;
                    v[k][0][0]+=q*30;v[k][0][1]+=y;
                    inputs.push_back(v[k]);
                }
                const auto i=q*4;
                for(auto k:{i,i+1,i+2,i+3,i+3,i+4})indices.push_back(k);
            }
            inputs.push_back({});
            return L::Decode(inputs,indices);
        };
        for(unsigned count:{1u,100u,128u}) {
            const auto first=fragment_batch(count,0),next=fragment_batch(count,-12);
            Check(first && next && first->size()==count,"full Majora burst and rounded six-unit squares recognized");
            history.Reset();history.Apply(1,key,*first);
            const auto mid=history.Apply(2,key,*next);
            Check(mid && mid->size()==count,"large falling fragment batches interpolate");
            for(unsigned q=0;q<count;++q)
                Check(std::abs((*mid)[q].vertices[0][0][1]-((*first)[q].vertices[0][0][1]-6))<.001f,
                      "every falling fragment uses midpoint position");
            Check((*mid)[0].vertices[0][3]==(*next)[0].vertices[0][3],"falling fragments preserve discrete UV cells");
        }
        Check(!fragment_batch(129,0),"fragment capacity stays bounded");
        const auto fragment=fragment_batch(1,0),fallen=fragment_batch(1,-12),teleport=fragment_batch(1,-20);
        history.Reset();history.Apply(1,key,*fragment);
        Check(!history.Apply(2,key,*teleport),"far respawn cannot inherit a falling fragment");
        auto overlap=*fallen;overlap.push_back(overlap[0]);
        history.Reset();history.Apply(1,key,*fragment);
        Check(!history.Apply(2,key,overlap),"fast ambiguous fragments remain native");
        auto resized=*fallen;for(auto& v:resized[0].vertices)v[1][2]=7;
        Check(!std::isfinite(L::Distance(fragment->front(),resized.front())),"fast fragments require stable size");
        auto big=*a;for(auto& v:big[0].vertices)v[0][1]-=12;
        Check(!std::isfinite(L::Distance(a->front(),big.front())),"large leaves retain original movement bound");
        auto tiny=small;for(auto& v:tiny)for(unsigned a=0;a<2;++a)for(unsigned c=0;c<3;++c)v[a][c]*=.5f;
        Check(!L::Decode(tiny,order),"unrelated tiny atlas particles remain excluded");
        auto bad=leaf(0,0);bad[0][1][2]=1;
        Check(!L::Decode(bad,order),"leaf normals must match the captured scaled shape");
        bad=leaf(0,0);bad[0][3][1]=.75f;
        Check(!L::Decode(bad,order),"other atlas layouts rejected");
        bad=leaf(0,0);bad[0][2][0]=0;
        Check(!L::Decode(bad,order),"other particle colors rejected");
        Check(!L::Decode(leaf(0,0),std::vector<std::uint32_t>{0,1,2,99,99,4}),"bad leaf indices rejected");
        history.Reset();history.Apply(1,key,*a);
        auto duplicate=*b;duplicate.push_back((*b)[0]);
        Check(!history.Apply(2,key,duplicate),"ambiguous leaves cannot share an old particle");
        history.Reset();history.Apply(1,key,*a);
        const auto far=L::Decode(leaf(100,0),order);
        Check(!history.Apply(2,key,*far),"new faraway leaves stay native");
        history.Reset();history.Apply(1,key,*a);
        Check(!history.Apply(3,key,*b),"missing leaf frames reacquire history");
        Check(!history.Apply(4,L::Key{2,1},*b) && !history.Apply(5,L::Key{2,2},*b),
              "leaf texture and scene changes do not inherit history");
        auto both=*a;both.push_back((*far)[0]);history.Reset();history.Apply(1,key,both);
        auto swapped=both;std::reverse(swapped.begin(),swapped.end());
        for(auto& q:swapped)for(auto& v:q.vertices)v[0][0]+=2;
        const auto reordered=history.Apply(2,key,swapped);
        Check(reordered && (*reordered)[0].vertices[0][0][0]==101 &&
              (*reordered)[1].vertices[0][0][0]==1,"separate leaf bursts survive reordering");
        const auto grass=[](float x,float size) {
            std::vector<L::Vertex> v(5);
            for(unsigned k=0;k<4;++k) {
                v[k][0]={x+(k<2?0.f:size*1.4f),k%2?0.f:size,0,1};
                v[k][1]={0,0,size/20,1};v[k][2]={1,1,1,1};
                v[k][3]={k<2?0.f:1.f,k%2?0.f:1.f,0,1};
            }
            return v;
        };
        for(float size:{2.97f,4.f,6.f,8.4375f}) {
            const auto first=L::Decode(grass(0,size),order),second=L::Decode(grass(7,size),order);
            Check(first && second && first->front().grass,"cut grass rectangle variants recognized");
            history.Reset();history.Apply(1,key,*first);
            const auto mid=history.Apply(2,key,*second);
            Check(mid && (*mid)[0].vertices[0][0][0]==3.5f,"fast grass fragments move halfway");
            Check((*mid)[0].vertices[0][3]==(*second)[0].vertices[0][3],"grass UVs preserved");
            auto wrong=grass(0,size);for(auto& v:wrong)v[0][0]*=1.2f;
            Check(!L::Decode(wrong,order),"unrelated rectangular debris rejected");
            wrong=grass(0,size);for(auto& v:wrong)v[1][2]=size;
            Check(!L::Decode(wrong,order),"grass normal scale is required");
            history.Reset();history.Apply(1,key,*a);
            Check(!history.Apply(2,key,*second),"grass cannot inherit brown leaf history");
        }
    }
    {
        ColorClears clears;
        constexpr std::uint32_t color=0x18237fc0,depth=0x18537fc0,size=512*400*4;
        const std::array<float,4> black{0,0,0,1};
        const std::array<float,4> white{246.f/255,246.f/255,236.f/255,1};
        Check(clears.Background(2251,color,512,400,true)==black,"no native clear retains black default");
        clears.Observe(2251,color,color+size,0xf6f6ecff,4);
        clears.Observe(2251,depth,depth+size,0x00ffffff,4);
        Check(clears.Background(2251,color,512,400,true)==white,
              "captured Double Time pale background preserved independently of depth clear");
        Check(clears.Background(2251,color,512,400,true)==white,"clear usable by CPU and GPU replay");
        Check(clears.Background(2252,color,512,400,true)==black,"no clear color leaks to next frame");
        Check(clears.Background(2251,color,512,400,false)==black,"non RGBA8 target rejected");
        Check(clears.Background(2251,color+8,512,400,true)==black,"different target rejected");
        Check(clears.Background(2251,color,256,400,true)==black,"partial target rejected");
        clears.Observe(2251,color+8,color+16,0,2);
        Check(clears.Background(2251,color,512,400,true)==black,"partial overwrite invalidates full clear");
        clears.Observe(2251,color,color+size,0xf6f6ec00,4);
        Check(clears.Background(2251,color,512,400,true)==white,"scene coverage remains opaque");
        clears.Invalidate(color+4,color+12);
        Check(clears.Background(2251,color,512,400,true)==black,"external write invalidates clear");
        clears.Observe(2252,color,color+size,0x000000ff,4);
        Check(clears.Background(2252,color,512,400,true)==black,"native black clear restored after time skip");
        clears.Observe(2252,color,color+size,0xf6f6ecff,4);
        clears.Reset();
        Check(clears.Background(2252,color,512,400,true)==black,"scene reset clears background history");
    }
    {
        Matrix a{{{0,.8930734395980835,-.0000012349019016255625,0},
                  {-.5480941534042358,0,.000040768718463368714,0},
                  {0,0,1.0001975297927856,5.001250267028809},{0,0,-1,0}}};
        Matrix b{{{0,.8506758809089661,-.000007828404704923742,0},
                  {-.5323510766029358,0,-.0000001523004584669252,0},
                  {0,0,1.0002080202102661,5.001250267028809},{0,0,-1,0}}};
        Check(!SameLens(a,b) && !GradualLensScale(a,b) && !GradualWorldWobble(a,b),
              "captured Double Time distortion exceeds ordinary lens rules");
        Check(GradualTimeSkipDistortion(a,b),"captured Double Time lens recognized");
        History h;
        Check(!h.Observe(1,0,Identity(),a),"time skip seeds projection history");
        const auto current=Camera(10,4),camera_half=Camera(5,2);
        const auto delta=Multiply(InverseRigid(camera_half),current);
        auto expected=b;
        for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c)expected[r][c]=(a[r][c]+b[r][c])*.5;
        const auto mid=h.Observe(2,33,current,b);
        Check(mid && Near(*mid,Multiply(expected,delta),1e-9),
              "time skip interpolates both lens axes and off-center terms with camera motion");
        auto alternate=b;alternate[2]={0,0,1.0000892877578735,5.000446796417236};
        expected[2]=alternate[2];
        const auto alternate_mid=h.Observe(2,33,current,alternate);
        Check(alternate_mid && Near(*alternate_mid,Multiply(expected,delta),1e-9),
              "time skip retains per-draw clipping planes");
        bool fallback=false;
        Check(Near(h.Preview(2,33,current,b,true,true,fallback),b,0) && fallback,
              "time skip comparison remains native");
        auto strong=b;strong[1][0]=a[1][0]*.89;
        Check(GradualTimeSkipDistortion(a,strong),"captured stronger independent-axis distortion accepted");
        auto wrong=b;wrong[2][3]=5.002084255218506;
        Check(!GradualTimeSkipDistortion(a,wrong),"underwater clipping excluded from time skip exception");
        wrong=b;wrong[0][0]=.01;
        Check(!GradualTimeSkipDistortion(a,wrong),"arbitrary projection shear excluded");
        wrong=b;wrong[1][2]=.01;
        Check(!GradualTimeSkipDistortion(a,wrong),"large off-center projection jump rejected");
        wrong=b;wrong[1][0]=a[1][0]*.8;
        Check(!GradualTimeSkipDistortion(a,wrong),"abrupt axis resize rejected");
        wrong=b;wrong[0][3]=.01;
        Check(!GradualTimeSkipDistortion(a,wrong),"screen translation excluded");
        auto plain_a=a,plain_b=b;
        plain_a[0][2]=plain_a[1][2]=plain_b[0][2]=plain_b[1][2]=0;
        Check(!GradualTimeSkipDistortion(plain_a,plain_b),"ordinary aspect changes do not use distortion exception");
        const auto ending=h.Observe(3,66,current,plain_b);
        Check(ending.has_value(),"distortion can end smoothly");
        const auto ended=h.Observe(4,99,current,plain_b);
        Check(ended && Near(*ended,plain_b,1e-9),"time skip offset does not leak into ordinary rendering");
        History cut;cut.Observe(1,0,Identity(),a);
        Check(!cut.Observe(2,33,Camera(0,300),b),"time skip still rejects real camera cuts");
        History gap;gap.Observe(1,0,Identity(),a);
        Check(!gap.Observe(3,66,Identity(),b),"time skip still rejects missing frames");
    }
    {
        // Consecutive captured Song of Inverted Time projections, frames 17860/17861.
        Matrix a{{{0,.6559310555458069,-3.4090248846041504e-6,0},
                  {-.3702084422111511,0,3.3503332815598696e-5,0},
                  {0,0,1.0003340244293213,5.001953601837158},{0,0,-1,0}}};
        Matrix b{{{0,.6648061275482178,-8.59062492963858e-6,0},
                  {-.3858739137649536,0,3.311909040348837e-6,0},
                  {0,0,1.000368595123291,5.001954078674316},{0,0,-1,0}}};
        Check(!SameLens(a,b) && !GradualLensScale(a,b) && !GradualWorldWobble(a,b),
              "Inverted Time distortion exceeds ordinary lens rules");
        Check(GradualTimeSkipDistortion(a,b),"captured Inverted Time distortion recognized");
        History h;h.Observe(1,0,Identity(),a);
        const auto mid=h.Observe(2,33,Identity(),b);
        auto expected=b;
        for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c)expected[r][c]=(a[r][c]+b[r][c])*.5;
        Check(mid && Near(*mid,expected,1e-9),"Inverted Time produces a real midpoint projection");
        Check((*mid)[2]==b[2],"Inverted Time retains current depth clipping");
        auto wrong=b;wrong[2]={0,0,1.0002080202102661,5.001250267028809};
        Check(!GradualTimeSkipDistortion(a,wrong),"different time-song depth ranges cannot bridge cuts");
        wrong=b;wrong[2][2]=1.001;
        Check(!GradualTimeSkipDistortion(a,wrong),"other clipping ranges excluded");
        wrong=b;wrong[0][2]=.001;
        Check(!GradualTimeSkipDistortion(a,wrong),"large Inverted Time projection offset rejected");
        wrong=b;wrong[0][1]*=2;
        Check(!GradualTimeSkipDistortion(a,wrong),"abrupt Inverted Time lens change rejected");
        bool fallback=false;
        Check(Near(h.Preview(2,33,Identity(),b,false,false,fallback),b,0) && fallback,
              "Inverted Time still requires a matching CPU camera");
        History cut;cut.Observe(1,0,Identity(),a);
        Check(!cut.Observe(2,33,Camera(0,300),b),"Inverted Time camera cuts stay native");
        History gap;gap.Observe(1,0,Identity(),a);
        Check(!gap.Observe(3,66,Identity(),b),"Inverted Time missing frames stay native");
    }
    {
        // Double Time's white-background cutscene, captured consecutive frames.
        Matrix a{{{0,.9797046184539795,1.3080077678750968e-6,0},
                  {-.5434868931770325,0,-1.7155532987089828e-5,0},
                  {0,0,1.0004703998565674,5.002501487731934},{0,0,-1,0}}};
        Matrix b{{{0,.9560847878456116,6.291206773312297e-6,0},
                  {-.5738077759742737,0,-1.4719977116328664e-5,0},
                  {0,0,1.0004734992980957,5.002501487731934},{0,0,-1,0}}};
        Check(!SameLens(a,b) && !GradualLensScale(a,b) && !GradualWorldWobble(a,b),
              "white Double Time cutscene needs distortion interpolation");
        Check(GradualTimeSkipDistortion(a,b),"shorter-range Double Time distortion recognized");
        History h;h.Observe(1,0,Identity(),a);
        const auto mid=h.Observe(2,33,Identity(),b);
        auto expected=b;
        for(unsigned r=0;r<2;++r)for(unsigned c=0;c<4;++c)expected[r][c]=(a[r][c]+b[r][c])*.5;
        Check(mid && Near(*mid,expected,1e-9),"white Double Time cutscene produces midpoint lens");
        Check(mid && (*mid)[2]==b[2],"white Double Time cutscene preserves depth clipping");
        auto wrong=b;wrong[2]={0,0,1.0002080202102661,5.001250267028809};
        Check(!GradualTimeSkipDistortion(a,wrong),"Double Time clipping families stay isolated");
        wrong=b;wrong[2][2]=1.0006;
        Check(!GradualTimeSkipDistortion(a,wrong),"uncaptured cutscene depth range rejected");
        wrong=b;wrong[0][0]=.01;
        Check(!GradualTimeSkipDistortion(a,wrong),"white cutscene does not admit arbitrary shear");
        wrong=b;wrong[0][2]=.001;
        Check(!GradualTimeSkipDistortion(a,wrong),"white cutscene rejects large projection offsets");
        wrong=b;wrong[0][1]*=2;
        Check(!GradualTimeSkipDistortion(a,wrong),"white cutscene rejects abrupt lens changes");
        History cut;cut.Observe(1,0,Identity(),a);
        Check(!cut.Observe(2,33,Camera(0,300),b),"white cutscene retains camera cut guard");
        History gap;gap.Observe(1,0,Identity(),a);
        Check(!gap.Observe(3,66,Identity(),b),"white cutscene retains missing frame guard");
    }
    {
        using namespace MM3D::WaterParticleInterpolation;
        const auto order_for=[](unsigned quads) {
            std::vector<std::uint32_t> order;
            for(unsigned q=0;q<quads;++q)
                for(unsigned v:{0u,1u,2u,3u,3u,4u})order.push_back(q*4+v);
            return order;
        };
        const auto drop=[](float x,float y) {
            std::vector<Vertex> inputs(5);
            for(unsigned i=0;i<4;++i) {
                inputs[i][0]={x+float(i/2)*2,y-float(i%2)*20,0,1};
                inputs[i][1]={1,1,1,150.f/255};
                inputs[i][2]={float(i/2),float(1-i%2),0,1};
            }
            return inputs;
        };
        const auto a=DecodeDrops(drop(0,0),order_for(1));
        const auto b=DecodeDrops(drop(0,-40.f/3),order_for(1));
        Check(a && b,"falling water droplet layout recognized");
        MM3D::WaterParticleInterpolation::History h;
        const Key key{1,2,3,4};
        Check(!h.ApplyDrops(1,key,*a),"droplets seed history");
        const auto half=h.ApplyDrops(2,key,*b);
        Check(half && std::abs((*half)[0].vertices[0][0][1]+20.f/3)<1e-5,
              "fast ceiling droplet advances halfway");
        auto wrong=drop(0,0);wrong[0][1][0]=.9f;
        Check(!DecodeDrops(wrong,order_for(1)),"other particle colors rejected");
        wrong=drop(0,0);wrong[0][2][0]=.125f;
        Check(!DecodeDrops(wrong,order_for(1)),"atlas splash is not a falling droplet");
        wrong=drop(0,0);wrong[3][0][0]+=2;
        Check(!DecodeDrops(wrong,order_for(1)),"nonquad particle geometry rejected");
        wrong=drop(0,0);wrong[0][0][2]=std::numeric_limits<float>::quiet_NaN();
        Check(!DecodeDrops(wrong,order_for(1)),"nonfinite water particle rejected");
        Check(!DecodeDrops(drop(0,0),std::vector<std::uint32_t>{0,1,2,3,2,4}),
              "incorrect degenerate connector rejected");
        Check(!DecodeDrops(drop(0,0),std::vector<std::uint32_t>{0,1,2,3,3,99}),
              "out of range water particle index rejected");
        auto reordered=drop(0,0);std::swap(reordered[0],reordered[2]);
        const auto decoded=DecodeDrops(reordered,std::vector<std::uint32_t>{2,1,0,3,3,4});
        Check(decoded && (*decoded)[0].vertices==(*a)[0].vertices,
              "droplets follow submission order instead of cache order");
        h.Reset();h.ApplyDrops(1,key,*a);
        const auto distant=DecodeDrops(drop(100,0),order_for(1));
        Check(!h.ApplyDrops(2,key,*distant),"new distant drop does not inherit old motion");
        auto two=*a;two.push_back((*distant)[0]);
        h.Reset();h.ApplyDrops(1,key,two);
        auto swapped=two;std::swap(swapped[0],swapped[1]);
        for(auto& q:swapped)for(auto& v:q.vertices)v[0][1]-=10;
        const auto separate=h.ApplyDrops(2,key,swapped);
        Check(separate && (*separate)[0].vertices[0][0][0]==100 &&
              (*separate)[1].vertices[0][0][0]==0 && (*separate)[1].vertices[0][0][1]==-5,
              "compacted droplet array retains separate motion");
        h.Reset();h.ApplyDrops(1,key,*a);
        auto duplicate=*b;duplicate.push_back(duplicate[0]);
        Check(!h.ApplyDrops(2,key,duplicate),"two new drops cannot inherit one old drop");
        h.Reset();h.ApplyDrops(1,key,duplicate);
        Check(!h.ApplyDrops(2,key,*b),"ambiguous old drops stay native");
        h.Reset();h.ApplyDrops(1,key,*a);
        Check(!h.ApplyDrops(3,key,*b),"droplet gaps stay native");
        Check(!h.ApplyDrops(4,Key{1,2,3,5},*b),"droplet scene change stays native");
        Check(!h.ApplyDrops(4,Key{9,2,3,4},*b),"droplet texture change stays native");
        h.Reset();h.ApplyDrops(1,key,*a);h.ApplyDrops(1,key,*b);
        Check(!h.ApplyDrops(2,key,*b),"repeated droplet batches invalidate history");

        const auto ring=[](float radius,float alpha,unsigned count=1) {
            std::vector<Vertex> inputs(count*8+1);
            constexpr int x[]{-1,-1,1,1,1,1,-1,-1},z[]{-1,0,-1,0,1,0,1,0};
            for(unsigned n=0;n<count;++n)for(unsigned i=0;i<8;++i) {
                inputs[n*8+i][0]={n*100.f+x[i]*radius,0,z[i]*radius,1};
                inputs[n*8+i][1]={0,0,0,alpha};
                inputs[n*8+i][2]={i%4<2?1.f:0.f,float(i%2),0,1};
            }
            return inputs;
        };
        const auto r1=DecodeRipples(ring(2,.8f),order_for(2));
        const auto r2=DecodeRipples(ring(4,.6f),order_for(2));
        Check(r1 && r2 && r1->indices==std::vector<std::uint32_t>{0,2,1,3,4,6,5,7},
              "CPU ripple halves canonicalize with correct writeback indices");
        Check(!h.ApplyRipples(1,key,*r1),"CPU ripple seeds history");
        const auto mid=h.ApplyRipples(2,key,*r2);
        Check(mid && std::abs(mid->points[0][0]+3)<1e-5 &&
              std::abs(mid->colors[0][3]-.7f)<1e-5,"CPU ripple radius and fade advance halfway");
        auto invalid_ring=ring(2,.8f);invalid_ring[0][0][1]=1;
        Check(!DecodeRipples(invalid_ring,order_for(2)),"nonhorizontal ring rejected");
        invalid_ring=ring(2,.8f);invalid_ring[7][2][0]=1;
        Check(!DecodeRipples(invalid_ring,order_for(2)),"wrong ring UV mapping rejected");
        invalid_ring=ring(2,.8f);invalid_ring[0][1][0]=1;
        Check(!DecodeRipples(invalid_ring,order_for(2)),"unrelated colored rings excluded");
        const auto brighter=DecodeRipples(ring(5,1),order_for(2));
        Check(!h.ApplyRipples(3,key,*brighter),"new brighter ripple does not inherit fading ring");
        Check(!h.ApplyRipples(5,key,*r2),"ripple gaps stay native");
        Check(!h.ApplyRipples(6,Key{1,2,3,5},*r2),"ripple scene change stays native");
        h.Reset();h.ApplyRipples(1,key,*r1);h.ApplyRipples(1,key,*r2);
        Check(!h.ApplyRipples(2,key,*r2),"repeated ripple batches invalidate history");
        h.Reset();
        const auto many=DecodeRipples(ring(2,.8f,64),order_for(128));
        const auto many_next=DecodeRipples(ring(4,.6f,64),order_for(128));
        Check(many && many_next,"busy sewer ripple batches fit bounded decoder");
        h.ApplyRipples(1,key,*many);
        Check(h.ApplyRipples(2,key,*many_next).has_value(),"all 64 ripple histories remain supported");
        Check(!DecodeRipples(ring(2,.8f,65),order_for(130)),"oversized ripple batch rejected");
        const auto concentric=[&](std::initializer_list<std::array<float,2>> values) {
            std::vector<Vertex> inputs;
            for(const auto& value:values) {
                const auto one=ring(value[0],value[1]);
                inputs.insert(inputs.end(),one.begin(),one.begin()+8);
            }
            inputs.push_back({});
            return DecodeRipples(inputs,order_for(static_cast<unsigned>(values.size())*2));
        };
        const auto initial=concentric({{18,.5f},{20,.4f}});
        const auto following=concentric({{21.6f,.365f},{19.6f,.465f},{1,1}});
        Check(initial && following,"concentric impact rings decode");
        h.Reset();h.ApplyRipples(1,key,*initial);
        const auto concentric_mid=h.ApplyRipples(2,key,*following);
        Check(concentric_mid && std::abs(concentric_mid->points[0][0]+20.8f)<1e-5 &&
              std::abs(concentric_mid->points[8][0]+18.8f)<1e-5 &&
              std::abs(concentric_mid->colors[0][3]-.3825f)<1e-5 &&
              std::abs(concentric_mid->colors[8][3]-.4825f)<1e-5,
              "later concentric rings retain independent expansion and fade after reordering");
        Check(concentric_mid->points[16][0]==-1 && concentric_mid->colors[16][3]==1,
              "new concentric ring does not inherit an older ring");
        const auto identical=concentric({{18,.5f},{18,.5f}});
        const auto single=concentric({{19.6f,.465f}});
        h.Reset();h.ApplyRipples(1,key,*identical);
        Check(!h.ApplyRipples(2,key,*single),"identical old concentric rings remain ambiguous");
        const auto identical_next=concentric({{19.6f,.465f},{19.6f,.465f}});
        const auto single_old=concentric({{18,.5f}});
        h.Reset();h.ApplyRipples(1,key,*single_old);
        Check(!h.ApplyRipples(2,key,*identical_next),"two new rings cannot share one old ring");
    }
    {
        using namespace MM3D::TorchInterpolation;
        const std::array<Vertex, 4> inputs{{
            {{{-4000,0,0,1},{0,0,0,1},{0,0,1,0}}},
            {{{4000,16000,0,1},{1,1,0,1},{0,0,1,0}}},
            {{{-4000,16000,0,1},{0,1,0,1},{0,0,1,0}}},
            {{{4000,0,0,1},{1,0,0,1},{0,0,1,0}}},
        }};
        const std::array<std::uint32_t, 6> order{0,1,2,0,3,1};
        const Values uv{{{1,0,0,0},{0,1,0,0},{1,0,0,0},{0,1,0,-.4f},
                          {-2,0,0,0},{0,2,0,-1.6f}}};
        Check(Recognized(inputs,order,uv),"torch quad and scrolling layer recognized");
        auto reordered=inputs;std::swap(reordered[0],reordered[2]);
        Check(Recognized(reordered,std::array<std::uint32_t,6>{2,1,0,2,3,1},uv),
              "torch decoder follows resolved submission order");
        auto wrong=inputs;wrong[0][0][0]=-3999;
        Check(!Recognized(wrong,order,uv),"other six-vertex effects excluded");
        wrong=inputs;wrong[1][2][2]=0;
        Check(!Recognized(wrong,order,uv),"torch normal is required");
        Check(!Recognized(inputs,std::array<std::uint32_t,6>{0,1,2,0,3,9},uv),
              "invalid torch indices rejected");
        auto wrong_uv=uv;wrong_uv[0][0]=2;
        Check(!Recognized(inputs,order,wrong_uv),"other UV bases excluded");
        wrong_uv=uv;wrong_uv[4][0]=2;
        Check(!Recognized(inputs,order,wrong_uv),"third torch UV basis is required");
        wrong_uv=uv;wrong_uv[5][3]=-.4f;
        Check(!Recognized(inputs,order,wrong_uv),"independent third-layer animation excluded");
        wrong_uv=uv;wrong_uv[3][3]=std::numeric_limits<float>::quiet_NaN();
        Check(!Recognized(inputs,order,wrong_uv),"nonfinite torch offset rejected");
        MM3D::TorchInterpolation::History h;
        const Key key{1,2,3,4};
        const Point a{0,0,0},b{100,0,0};
        Check(!h.Apply(1,key,a,-.4f) && !h.Apply(1,key,b,-.2f),"torches seed separately");
        const auto mb=h.Apply(2,key,b,-.2f-1.f/60);
        const auto ma=h.Apply(2,key,Point{.001f,0,0},-.4f-1.f/60);
        Check(ma && mb && std::abs((*ma)[0]-(-.4f-1.f/120))<1e-6 &&
              std::abs((*mb)[0]-(-.2f-1.f/120))<1e-6,"reordered torches preserve independent phases");
        Check(std::abs((*ma)[1]-(-1.6f-1.f/30))<1e-6 &&
              std::abs((*mb)[1]-(-.8f-1.f/30))<1e-6,"fast flame layer also advances halfway");
        Check(h.Apply(2,key,b,-.2f-1.f/60)==mb,"repeated torch draw retains midpoint");
        Check(!h.Apply(4,key,a,-.5f),"missing torch frame seeds fresh history");
        Check(!h.Apply(5,key,a,-.7f),"torch animation discontinuity stays native");
        h.Reset();h.Apply(1,key,a,-59.f/60);
        const auto wrap=h.Apply(2,key,a,0);
        Check(wrap && std::abs((*wrap)[0]-(-119.f/120))<1e-6 &&
              std::abs((*wrap)[1]-(-119.f/30))<1e-6,"both torch layers follow short wrapped path");
        Check(!h.Apply(3,Key{2,2,3,4},a,-1.f/60),"new texture does not inherit torch phase");
        Check(!h.Apply(3,Key{1,2,3,5},a,-1.f/60),"new scene does not inherit torch phase");
        h.Reset();h.Apply(1,key,a,-.4f);
        Check(!h.Apply(1,key,a,-.2f) && !h.Apply(2,key,a,-.4f-1.f/60),
              "conflicting colocated torches do not share history");
        h.Reset();
        Check(!h.Apply(2,key,a,-.4f),"torch reset drops history");
    }
    {
        using namespace MM3D::ForestParticleInterpolation;
        const auto spin=[](float degrees, float x) {
            std::vector<Vertex> inputs(9);
            const float angle=degrees*3.141592653589793f/180;
            for(unsigned k=0;k<4;++k) {
                const float u=k%2?1.f:-1.f,v=k<2?-1.f:1.f;
                inputs[k][0]={x+u*std::cos(angle)-v*std::sin(angle),
                    u*std::sin(angle)+v*std::cos(angle),0,1};
                inputs[k][1]={0,-55.f/255,200.f/255,.5f};
                inputs[k][2]={k<2?0.f:2.f,k%2?0.f:2.f,0,1};
                inputs[k+4]=inputs[k];inputs[k+4][0][0]+=20;
                inputs[k+4][1]={1,1,230.f/255,.5f};
            }
            return inputs;
        };
        const std::vector<std::uint32_t> order{0,1,2,3,3,4,4,5,6,7,7,8};
        const auto inputs=spin(0,0),moved=spin(20,1);
        const auto a=Decode(inputs,order,true),b=Decode(moved,order,true);
        Check(a && b && a->size()==1 && b->size()==1,"spin particles selected from mixed atlas batches");
        Check((*b)[0].indices==std::array<std::uint32_t,4>{0,1,2,3},"spin preserves original input slots");
        MM3D::ForestParticleInterpolation::History h;h.Apply(1,1,1,*a);
        const auto half=h.Apply(2,1,1,*b);
        Check(half && Near(Center((*half)[0])[0],.5f),"spin movement interpolates");
        const auto& q=(*half)[0];
        const float dx=q.vertices[1][0][0]-q.vertices[0][0][0];
        const float dy=q.vertices[1][0][1]-q.vertices[0][0][1];
        Check(Near(std::atan2(dy,dx)*180/3.141592653589793f,10) && Near(dx*dx+dy*dy,4),
              "spin rotates halfway without shrinking its square");
        const auto flipped=Decode(spin(160,1),order,true);
        Check(flipped && !Compatible((*a)[0],(*flipped)[0]),"nearby respawn with unrelated rotation rejected");
        auto bad=inputs;bad[0][2][1]=1;
        Check(!Decode(bad,order,true),"spin requires doubled UVs");
        bad=inputs;bad[0][0][0]-=1;
        Check(!Decode(bad,order,true),"spin requires a square");
        bad=inputs;for(auto&v:bad)v[1]={1,1,230.f/255,.5f};
        Check(!Decode(bad,order,true),"other pale particles sharing the atlas remain native");
        h.Reset();h.Apply(1,1,1,*a);
        Check(!h.Apply(3,1,1,*b),"spin history does not bridge missing frames");
    }
    {
        using namespace MM3D::ForestParticleInterpolation;
        const auto stars=[](float y) {
            std::vector<Vertex> inputs(249);
            std::vector<std::uint32_t> order;
            for(unsigned q=0;q<62;++q) {
                for(unsigned k=0;k<4;++k) {
                    auto& v=inputs[q*4+k];
                    v[0]={float(q)*20+(k<2?0.f:2.f),y+(k%2?0.f:2.f),0,1};
                    v[1]={55.f/255,55.f/255,0,.5f};
                    v[2]={k<2?0.f:2.f,k%2?0.f:2.f,0,1};
                }
                for(auto k:{0u,1u,2u,3u,3u,4u})order.push_back(q*4+k);
            }
            return std::pair{inputs,order};
        };
        auto [inputs,order]=stars(0);
        const auto first=Decode(inputs,order);
        auto [moved,indices]=stars(5);
        auto next=Decode(moved,indices);
        Check(first && next && first->size()==62,"full boss sparkle batch recognized");
        std::reverse(next->begin(),next->end());
        MM3D::ForestParticleInterpolation::History history;
        history.Apply(1,1,1,*first);
        const auto half=history.Apply(2,1,1,*next);
        Check(half && (*half)[0].vertices[0][0][1]==4.5f && (*half)[61].vertices[0][0][1]==4.5f,
              "reordered rising stars interpolate beyond the old forest movement limit");
        Check((*half)[0].indices==(*next)[0].indices,"sparkles retain current input indices");
        auto bad=inputs;bad[0][2][1]=1;
        Check(!Decode(bad,order),"boss sparkles require their doubled UV square");
        bad=inputs;for(auto& v:bad)v[1]={5.f/255,75.f/255,155.f/255,.5f};
        Check(!Decode(bad,order),"larger batch limit does not broaden forest matching");
        history.Reset();history.Apply(1,1,1,*first);
        auto far=*next;for(auto& q:far)for(auto& v:q.vertices)v[0][1]+=100;
        Check(!history.Apply(2,1,1,far),"respawned distant stars remain native");
        history.Reset();history.Apply(1,1,1,*first);
        auto duplicate=*next;duplicate.insert(duplicate.end(),next->begin(),next->end());
        Check(!history.Apply(2,1,1,duplicate),"oversized sparkle batch rejected");
    }
    {
        namespace S=MM3D::SongEffectInterpolation;
        for(unsigned count:{234u,144u,120u,186u,138u,372u,156u,174u,150u,114u,168u,621u,519u,942u,1191u}) {
            S::Values value{{{1,0,0,0},{0,1,0,0},{1,0,0,0},{0,1,0,0}}};
            if(count==138) {
                value[2]={1.9922411442f,-.1742986441f,0,0};
                value[3]={.1742986441f,1.9922411442f,0,0};
            } else if(count==621) {
                value[0][0]=3;value[1][1]=2;
            } else if(count==942) {
                value[0]={-1.9998657703f,-3.4638698101f,0,0};
                value[1]={3.4638698101f,-1.9998657703f,0,0};
                value[2]={0,-8,0,0};value[3]={7,0,0,0};
            } else if(count==1191) {
                value[0]={3.7585752010f,-1.3680083752f,0,0};
                value[1]={1.3680083752f,3.7585752010f,0,0};
                value[2]={8,0,0,0};value[3]={0,7,0,0};
            }
            const auto* pattern=S::WoodfallWater(count,value);
            Check(pattern && S::Recognized(count,value),"Woodfall water layers recognized");
            S::History h;const S::Key key{1,2,3,4,1,count,0};
            Check(!h.Apply(1,key,value,true),"Woodfall water seeds history");
            auto next=value;
            for(unsigned r=0;r<4;++r)next[r][3]+=pattern->step[r];
            if(pattern->texture_mask==1) {
                // Unused coordinates belong to another material, not this sheet.
                next[2]={8,9,10,99};next[3]={2,3,4,-10};
            }
            const auto mid=h.Apply(2,key,next,true);
            Check(mid.has_value(),"Woodfall scrolling layers interpolate");
            for(unsigned r=0;r<4;++r) {
                const float expected=pattern->texture_mask==1 && r>=2?next[r][3]:pattern->step[r]*.5f;
                Check(std::abs((*mid)[r][3]-expected)<1e-6f,"Woodfall scroll is halfway with inactive offsets untouched");
                for(unsigned c=0;c<3;++c)Check((*mid)[r][c]==next[r][c],"Woodfall bases stay unchanged");
            }
            Check(h.Apply(2,key,next,true)==mid,"repeated water draw reuses its midpoint");
            Check(!h.Apply(2,key,next,false),"disabled water interpolation stays native");
            Check(h.Apply(3,key,next,true).has_value(),"stationary waterfall can retain a midpoint");
            next[1][3]+=1;
            Check(!h.Apply(4,key,next,true),"waterfall phase reset stays native");
            Check(!h.Apply(6,key,next,true),"waterfall missing frames stay native");
            h.Reset();h.Apply(1,key,value,true);next=value;next[1][3]+=.1f;
            Check(!h.Apply(2,key,next,true),"other scrolling speeds do not use Woodfall interpolation");
            auto bad=value;bad[0][0]=2;
            Check(!S::WoodfallWater(count,bad),"different water UV basis rejected");
            Check(!S::WoodfallWater(6,value),"nearby quads with inherited UVs excluded");
        }
    }
    {
        namespace S=MM3D::SongEffectInterpolation;
        S::Values v{{{1,0,0,0},{0,1,0,0},{1,0,0,0},{0,2,0,0}}};
        Check(!S::WoodfallWater(480,v,3) && S::Recognized(480,v,3),
              "Great Fairy two-texture layer keeps its existing interpolation path");
        Check(!S::WoodfallWater(480,v),"ambiguous 480-vertex water requires material context");
        Check(S::WoodfallWater(480,v,1),"Woodfall single-texture surface recognized independently of stale UVs");
        S::History h;S::Key key{1,2,3,4,7|(1<<8),480,0};
        h.Apply(1,key,v,true,1);v[0][3]=1.f/1200;v[3][3]=15;
        const auto half=h.Apply(2,key,v,true,1);
        Check(half && std::abs((*half)[0][3]-1.f/2400)<1e-7f && (*half)[3]==v[3],
              "slow Woodfall surface scroll interpolates without touching inactive coordinates");
        h.Reset();key[4]=7|(3<<8);v[0][3]=v[3][3]=0;h.Apply(1,key,v,true,3);
        v[0][3]=.01f;v[3][3]=.02f;
        const auto fairy=h.Apply(2,key,v,true,3);
        Check(fairy && std::abs((*fairy)[0][3]-.005f)<1e-7f && std::abs((*fairy)[3][3]-.01f)<1e-7f,
              "Great Fairy 480-vertex layer still interpolates both textures");
    }
    {
        namespace S=MM3D::SongEffectInterpolation;
        S::History h;
        S::Values value{{{1,0,0,0},{0,1,0,0},{1,0,0,0},{0,1,0,0}}};
        for(unsigned i=0;i<32;++i)
            Check(!h.Apply(1,S::Key{1,i,3,4,1,144,0},value,true),"water material histories seed independently");
        value[1][3]=1.f/30;
        for(unsigned i=0;i<32;++i)
            Check(h.Apply(2,S::Key{1,i,3,4,1,144,0},value,true).has_value(),
                  "later water materials are not starved by the old sixteen-entry cap");
        Check(!h.Apply(2,S::Key{1,100,3,4,1,144,0},value,true),"water history remains bounded");
        Check(!h.Apply(5,S::Key{1,100,3,4,1,144,0},value,true),"expired water materials free history slots");
        value[1][3]+=1.f/30;
        Check(h.Apply(6,S::Key{1,100,3,4,1,144,0},value,true).has_value(),"new materials acquire released history slots");
    }
    {
        using namespace MM3D::SongEffectInterpolation;
        Values uv{{{12,0,0,0},{0,1,0,0},{16,0,0,0},{0,4,0,-.8f},
                   {5,0,0,0},{0,1,0,-.4f}}};
        Check(BossEntrance(240,uv) && Recognized(240,uv),"green boss column UV pattern recognized");
        Check(!BossEntrance(312,uv),"other portal meshes not inferred from inherited uniforms");
        auto bad=uv;bad[5][3]=-.3f;
        Check(!BossEntrance(240,bad),"portal layers must share the captured phase ratio");
        bad=uv;bad[4][0]=1;
        Check(!BossEntrance(240,bad),"portal requires all three UV bases");
        MM3D::SongEffectInterpolation::History history;const Key key{1,2,3,4,1,240,0};
        Check(!history.Apply(1,key,uv,true),"portal seeds phase history");
        uv[5][3]-=1.f/30;uv[3][3]=2*uv[5][3];
        auto half=history.Apply(2,key,uv,true);
        Check(half && std::abs((*half)[5][3]-(-.4f-1.f/60))<1e-5f &&
              std::abs((*half)[3][3]-(-.8f-1.f/30))<1e-5f,"both portal layers move halfway");
        history.Reset();uv[5][3]=-29.f/30;uv[3][3]=2*uv[5][3];history.Apply(1,key,uv,true);
        uv[3][3]=uv[5][3]=0;half=history.Apply(2,key,uv,true);
        Check(half && std::abs((*half)[5][3]+59.f/60)<1e-5f &&
              std::abs((*half)[3][3]+59.f/30)<1e-5f,"portal wrap takes the short path for both layers");
        Check(!history.Apply(4,key,uv,true),"portal frame gaps stay native");
        uv[5][3]=-.3f;uv[3][3]=-.6f;
        Check(!history.Apply(5,key,uv,true),"portal phase discontinuities stay native");
    }
    {
        using namespace MM3D::ForestParticleInterpolation;
        const auto sprite=[](float x) {
            std::vector<Vertex> inputs(5);
            for(unsigned i=0;i<4;++i) {
                inputs[i][0]={x+float(i&1)*8,float(i>>1)*8,0,1};
                inputs[i][1]={5.f/255,75.f/255,155.f/255,.5f};
                inputs[i][2]={float(i&1),float(i>>1),0,1};
            }
            return inputs;
        };
        const std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto a=Decode(sprite(0),order),b=Decode(sprite(2),order);
        Check(a && b,"captured forest strip layout recognized");
        MM3D::ForestParticleInterpolation::History h;
        Check(!h.Apply(1,1,1,*a),"forest particles seed history");
        const auto mid=h.Apply(2,1,1,*b);
        Check(mid && std::abs((*mid)[0].vertices[0][0][0]-1)<1e-6,
              "forest particle moves halfway in world space");
        Check(!h.Apply(4,1,1,*b),"particle gaps reset history");
        Check(!h.Apply(5,2,1,*b) && !h.Apply(6,2,2,*b),"texture and generation changes stay native");
        auto invalid=sprite(0);invalid[0][1][0]=.8f;
        Check(!Decode(invalid,order),"other particle colors excluded");
        invalid=sprite(0);invalid[2][0][2]=2;
        Check(!Decode(invalid,order),"non-square meshes excluded");
        invalid=sprite(0);invalid[0][0][0]=std::numeric_limits<float>::quiet_NaN();
        Check(!Decode(invalid,order),"nonfinite geometry excluded");
        Check(!Decode(sprite(0),std::vector<std::uint32_t>{0,1,2,3,2,4}),"wrong strip connectors rejected");
        Check(!Decode(sprite(0),std::vector<std::uint32_t>{0,1,2,99,99,4}),"invalid cache index rejected");
        auto reordered=sprite(0);std::swap(reordered[0],reordered[2]);
        const auto decoded=Decode(reordered,std::vector<std::uint32_t>{2,1,0,3,3,4});
        Check(decoded && (*decoded)[0].vertices==(*a)[0].vertices,
              "particle decode follows submission order rather than cache order");
        h.Reset();h.Apply(1,1,1,*a);
        auto duplicate=*b;duplicate.push_back(duplicate[0]);
        Check(!h.Apply(2,1,1,duplicate),"two particles cannot inherit one previous particle");
        h.Reset();h.Apply(1,1,1,duplicate);
        Check(!h.Apply(2,1,1,*b),"ambiguous previous particles rejected");
        h.Reset();h.Apply(1,1,1,*a);
        const auto distant=Decode(sprite(100),order);
        Check(!h.Apply(2,1,1,*distant),"new distant particle does not inherit old motion");
        auto both=*a;both.push_back((*distant)[0]);
        h.Reset();h.Apply(1,1,1,both);
        auto swapped=both;std::swap(swapped[0],swapped[1]);
        for(auto& q:swapped)for(auto& v:q.vertices)v[0][0]+=2;
        const auto matched=h.Apply(2,1,1,swapped);
        Check(matched && (*matched)[0].vertices[0][0][0]==101 && (*matched)[1].vertices[0][0][0]==1,
              "particle reordering preserves separate identities");
    }
    {
        using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose pose;
        pose.pose.bones.resize(5);pose.bind.resize(5);
        MM3D::LinkInterpolation::BoneCandidates cached;
        for(unsigned pass=0;pass<6;++pass) {
            for(unsigned i=0;i<5;++i) {
                pose.pose.bones[i]=Flatten(Camera(i*12+pass,i*5+pass));
                pose.bind[i]=Flatten(pass%2?Camera(-int(i)*3,i):Identity());
            }
            if(pass==4)pose.pose.bones[1]=pose.pose.bones[0];
            if(pass==5)pose.bind[2][0]=std::numeric_limits<float>::quiet_NaN();
            cached.Prepare(pose);
            for(unsigned i=0;i<5;++i)for(bool skin:{false,true}) {
                auto value=Expand(pose.pose.bones[i]);
                if(skin)value=Multiply(value,Expand(pose.bind[i]));
                for(double offset:{-.031,-.03,0.,.03,.031}) {
                    auto query=value;query[0][3]+=offset;
                    Check(cached.Identify(query)==MM3D::LinkInterpolation::Identify(query,pose),
                          "position bounds preserve matching tolerance edges");
                }
                for(double perturbation:{0.,.000001,.1}) {
                    auto query=value;query[0][0]+=perturbation;
                    Check(cached.Identify(query)==MM3D::LinkInterpolation::Identify(query,pose),
                          "cached matches preserve direct/skin tolerance and ambiguity semantics");
                }
            }
            Check(cached.Identify(Matrix{})==MM3D::LinkInterpolation::Identify(Matrix{},pose),
                  "cached matching rejects singular matrix");
        }
        pose.pose.bones.clear();pose.bind.clear();cached.Prepare(pose);
        Check(!cached.Identify(Identity()),"empty pose invalidates cached candidates");
    }
    {
        using namespace MM3D::SplashInterpolation;
        const auto sprite=[](int cell) {
            Sprite s{};
            for(unsigned i=0;i<4;++i){s.points[i]={float(i%2)*40,float(i<2)*30,0};s.colors[i]={0,0,0,.8f};
                s.uv[i]={float(cell%8)*.125f+float(i%2)*.125f,1.f-float(cell/8)*.5f-float(i/2)*.5f};}
            return s;
        };
        std::vector<MM3D::WaterParticleInterpolation::Vertex> inputs(5);
        constexpr unsigned canonical[]{0,2,1,3};
        auto native=sprite(5);
        for(unsigned k=0;k<4;++k) {
            auto& v=inputs[canonical[k]];
            v[0]={native.points[k][0],native.points[k][1],native.points[k][2],1};
            v[1]=native.colors[k];v[2]={native.uv[k][0],native.uv[k][1],0,1};
        }
        std::vector<std::uint32_t> order{0,1,2,3,3,4};
        const auto decoded=Decode(inputs,order);
        Check(decoded&&decoded->sprite.uv==native.uv&&decoded->sprite.points==native.points,
              "CPU splash canonicalizes strip corners without moving native inputs");
        order[4]=0;Check(!Decode(inputs,order),"CPU splash rejects unrelated topology");
        MM3D::SplashInterpolation::History h;
        Check(!h.Apply(1,1,0,sprite(0)),"splash first frame native");
        auto m=h.Apply(2,1,0,sprite(1));
        Check(m && m->blend && std::abs(m->sprite.colors[0][3]-.4f)<1e-6,"splash frame blend halves additive weights");
        Check(m->old_uv==sprite(0).uv,"splash keeps previous cell coordinates");
        Check(!h.Apply(4,1,0,sprite(2)),"splash gap resets");
        auto moved=sprite(3);for(auto& p:moved.points)p[0]+=50;
        Check(!h.Apply(5,1,0,moved),"new landing does not inherit old splash");
        Check(!h.Apply(6,1,0,sprite(0)),"sprite restart stays native");
    }
    {
        using namespace MM3D::RippleInterpolation;
        const auto ring=[](float radius,float alpha,float x=0) {
            Mesh m;m.points.resize(9);m.colors.resize(9);
            constexpr int xs[]{-1,1,-1,1,1,-1,1,-1},zs[]{-1,-1,0,0,1,1,0,0};
            for(unsigned i=0;i<8;++i){m.points[i]={x+xs[i]*radius,.5f,zs[i]*radius};m.colors[i]={0,0,0,alpha};}
            return m;
        };
        MM3D::RippleInterpolation::History h;
        Check(!h.Apply(1,1,0,ring(10,1)),"ripple first frame native");
        auto mid=h.Apply(2,1,0,ring(12,.8f));
        Check(mid && std::abs(mid->points[0][0]+11)<1e-6 && std::abs(mid->colors[0][3]-.9f)<1e-6,"ripple expansion and fade midpoint");
        Check(mid->points[8]==Point{},"ripple sentinel unchanged");
        Check(!h.Apply(3,1,0,ring(14,.6f,100)),"new ripple does not inherit other center");
        Check(!h.Apply(5,1,0,ring(16,.4f,100)),"ripple gap reset");
        Check(!h.Apply(6,2,0,ring(18,.2f,100)),"ripple texture reset");
        auto malformed=ring(18,.2f);malformed.points[3][1]=2;
        Check(!h.Apply(7,2,0,malformed),"nonplanar splash excluded");
    }
    {
        using namespace MM3D::SongEffectInterpolation;
        MM3D::SongEffectInterpolation::History h;Values v{};Key a{1,2,3,4,5},b{1,6,7,4,5};
        Check(!h.Apply(1,a,v,true),"song first sample native");
        v[0][3]=.04f;auto m=h.Apply(2,a,v,true);
        Check(m && std::abs((*m)[0][3]-.02f)<1e-6,"song texture offset midpoint");
        Check(!h.Apply(2,b,v,true),"song layers have independent history");
        Check(!h.Apply(2,a,v,false),"song comparison stays native");
        Check(!h.Apply(4,a,v,true),"song gap resets");
        v[0][3]=1;Check(!h.Apply(5,a,v,true),"song offset jump remains native");
        v[0][0]=2;Check(!h.Apply(6,a,v,true),"song texture basis changes reset");
        h.Reset();Check(!h.Apply(7,a,v,true),"song generation reset");
        v={};v[0][0]=8;v[1][1]=1;v[2][0]=6;v[3][1]=-3;
        Check(Recognized(90,v),"captured temple song material recognized");
        Check(!Recognized(4,v),"screen quad with inherited song uniforms excluded");
        v[0][1]=.1f;Check(!Recognized(90,v),"unverified UV basis excluded");
        v={{{8,0,0,0},{0,4,0,0},{4.6982188225f,1.7100105286f,0,0},{-1.7100105286f,4.6982188225f,0,0}}};
        Check(Recognized(90,v),"Epona rotated texture basis recognized");
        Check(!Recognized(4,v),"Epona screen overlay excluded");
        v[2][0]+=.01f;Check(!Recognized(90,v),"different texture rotation excluded");
        v={{{1,0,0,0},{0,1,0,0},{2.9883618355f,.2614479661f,0,0},{-.2614479661f,2.9883618355f,0,0}}};
        Check(Recognized(276,v),"Zora Hall waterfall UV basis recognized");
        Check(Recognized(237,v),"second Zora Hall waterfall sheet recognized");
        Check(!Recognized(174,v),"waterfall uniforms inherited by particles excluded");
        h.Reset();Check(!h.Apply(1,a,v,true),"waterfall seeds history");
        v[0][3]=-.0016667f;v[1][3]=.0208333f;v[2][3]=.0075f;v[3][3]=.05f;
        m=h.Apply(2,a,v,true);
        Check(m && std::abs((*m)[3][3]-.025f)<1e-6 &&
              std::abs((*m)[1][3]-.01041665f)<1e-6 && (*m)[2][0]==v[2][0],
              "both waterfall texture offsets interpolate without changing rotation");
        Check(!h.Apply(4,a,v,true),"waterfall capture gap stays native");
        v[2][0]+=.01f;Check(!Recognized(276,v),"different waterfall UV basis excluded");
        v={{{1,0,0,0},{0,1.5f,0,0},{.9961205721f,-.0871493220f,0,0},{.3485972881f,3.9844822884f,0,0}}};
        Check(Recognized(282,v) && Recognized(270,v),"waterfall additive overlays recognized");
        v={{{1.5f,0,0,0},{0,1,0,0},{2.8975834846f,-.7764056921f,0,0},{.5176038146f,1.9317222834f,0,0}}};
        Check(Recognized(423,v),"waterfall lower overlay recognized");
        v={{{2,0,0,0},{0,2,0,0},{1,0,0,0},{0,1,0,0}}};
        Check(Recognized(549,v),"waterfall water surface layer recognized");
        v={{{3,0,0,0},{0,3,0,0},{3.9844822884f,-.3485972881f,0,0},
            {.4357466102f,4.9806027412f,0,0}}};
        Check(Recognized(360,v),"fairy fountain rotated water layer recognized");
        v={{{1,0,0,0},{0,1,0,0},{1,0,0,0},{0,2,0,0}}};
        Check(Recognized(480,v),"fairy fountain water overlay recognized");
        v={{{1,0,0,.42f},{0,2,0,16.8f},{3,0,0,-1.26f},{0,2,0,12.6f}}};
        Check(Recognized(924,v),"fairy fountain waterfall sheet recognized");
        Check(!Recognized(6,v),"particles inheriting waterfall uniforms excluded");
        h.Reset();Check(!h.Apply(2210,a,v,true),"fountain waterfall seeds history");
        const auto previous=v;
        v[0][3]=.4204166532f;v[1][3]=16.81666565f;
        v[2][3]=-1.261250019f;v[3][3]=12.61249924f;
        m=h.Apply(2211,a,v,true);
        Check(m.has_value(),"fountain waterfall consecutive frame interpolates");
        if(m)for(unsigned row=0;row<4;++row) {
            Check(std::abs((*m)[row][3]-(previous[row][3]+v[row][3])*.5f)<1e-6,
                  "fountain waterfall interpolates all four captured offsets");
            for(unsigned c=0;c<3;++c)
                Check((*m)[row][c]==v[row][c],"fountain waterfall preserves UV basis");
        }
        v[2][0]+=.01f;
        Check(!Recognized(924,v),"different fountain UV basis excluded");


    }
    for(const auto signature:{0x578e07cd3309c81aull,0x018a783e5ef2d2bdull}) {
        using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose marker;
        marker.pose.instance=100;marker.model=200;marker.signature=signature;
        marker.pose.bones={Flatten(Camera(0))};marker.bind={Flatten(Identity())};
        marker.words={0xffff0000};
        auto depth=marker;depth.pose.instance=101;depth.model=201;
        MM3D::LinkInterpolation::Palette palette;palette.fill(Camera(0));
        MM3D::LinkInterpolation::History history;
        Check(!history.Apply(1,0,Identity(),palette,{marker,depth},true)[0],"marker pair seeds histories");
        marker.pose.bones={Flatten(Camera(60,4))};depth.pose.bones=marker.pose.bones;
        palette.fill(Expand(marker.pose.bones[0]));
        auto mid=history.Apply(2,33,Identity(),palette,{marker,depth},true);
        Check(mid[0] && Near(*mid[0],Camera(30,2),1e-6),"paired target rotation and position interpolate");
        Check(!history.Apply(4,99,Identity(),palette,{marker,depth},true)[0],"marker disappearance resets history");
        auto duplicate=depth;duplicate.pose.instance=102;duplicate.model=202;
        Check(!history.Apply(5,132,Identity(),palette,{marker,depth,duplicate},true)[0],"extra target owner is ambiguous");
        MM3D::LinkInterpolation::History disagree;
        marker.pose.bones={Flatten(Camera(0))};depth.pose.bones={Flatten(Camera(-60))};
        palette.fill(Camera(0));disagree.Apply(1,0,Identity(),palette,{marker,depth},true);
        palette.fill(Expand(depth.pose.bones[0]));disagree.Apply(1,0,Identity(),palette,{marker,depth},true);
        marker.pose.bones={Flatten(Camera(60))};depth.pose.bones=marker.pose.bones;
        palette.fill(Expand(marker.pose.bones[0]));
        Check(!disagree.Apply(2,33,Identity(),palette,{marker,depth},true)[0],"disagreeing target histories stay native");
        depth.signature=signature==0x578e07cd3309c81aull ? 0x018a783e5ef2d2bdull : 0x578e07cd3309c81aull;
        Check(!history.Apply(5,132,Identity(),palette,{marker,depth},true)[0],"mixed arrow and lock-on pair rejected");
        depth.signature=signature+1;
        Check(!history.Apply(5,132,Identity(),palette,{marker,depth},true)[0],"other paired models are not targets");
    }
    for(const auto vertices:{72u,576u}) {
        using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose sphere;
        sphere.pose.instance=100;sphere.model=200;sphere.signature=0x486289ad769db138ull;
        sphere.pose.bones={Flatten(Camera(0))};sphere.bind={Flatten(Identity())};
        sphere.words={0xffff0000};
        auto knot=sphere;knot.pose.instance=101;knot.model=201;knot.signature=0x6ea7a4a7d451310cull;
        MM3D::LinkInterpolation::Palette palette;palette.fill(Camera(0));
        MM3D::LinkInterpolation::History history;
        Check(!history.Apply(1,0,Identity(),palette,{sphere,knot},true,vertices)[0],
              "balloon and knot seed separate histories");
        sphere.pose.bones={Flatten(Camera(60,4))};knot.pose.bones=sphere.pose.bones;
        palette.fill(Expand(sphere.pose.bones[0]));
        const auto mid=history.Apply(2,33,Identity(),palette,{knot,sphere},true,vertices);
        Check(mid[0] && Near(*mid[0],Camera(30,2),1e-6),"balloon pair interpolates rotation and translation");
        Check(!history.Apply(2,33,Identity(),palette,{sphere,knot},false,vertices)[0],"balloon comparison stays native");
        Check(!history.Apply(2,33,Identity(),palette,{sphere,knot},true,vertices+1)[0],"unverified balloon material excluded");
        auto duplicate=knot;duplicate.pose.instance=102;duplicate.model=202;
        Check(!history.Apply(2,33,Identity(),palette,{sphere,knot,duplicate},true,vertices)[0],"extra balloon owner rejected");
        auto unrelated=knot;unrelated.signature++;
        Check(!history.Apply(2,33,Identity(),palette,{sphere,unrelated},true,vertices)[0],"unrelated shared root excluded");
        auto rebound=knot;rebound.bind[0][3]=1;
        Check(!history.Apply(2,33,Identity(),palette,{sphere,rebound},true,vertices)[0],"changed balloon bind excluded");
        Check(!history.Apply(4,99,Identity(),palette,{sphere,knot},true,vertices)[0],"balloon capture gap reseeds");
        MM3D::LinkInterpolation::History disagree;
        sphere.pose.bones={Flatten(Camera(0))};knot.pose.bones={Flatten(Camera(-60))};
        palette.fill(Expand(sphere.pose.bones[0]));
        disagree.Apply(1,0,Identity(),palette,{sphere,knot},true,vertices);
        palette.fill(Expand(knot.pose.bones[0]));
        disagree.Apply(1,0,Identity(),palette,{sphere,knot},true,vertices);
        sphere.pose.bones={Flatten(Camera(60))};knot.pose.bones=sphere.pose.bones;
        palette.fill(Expand(sphere.pose.bones[0]));
        Check(!disagree.Apply(2,33,Identity(),palette,{sphere,knot},true,vertices)[0],
              "coincident balloon poses with different histories rejected");
    }
    for(const auto vertices:{144u,600u}) {
        using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose target;
        target.pose.instance=100;target.model=200;target.signature=0x036049aec268c4f9ull;
        target.pose.bones={Flatten(Camera(0))};target.bind={Flatten(Identity())};
        target.words={0xffff0000};
        auto chain=target;chain.pose.instance=101;chain.model=201;chain.signature=0x22fef6cc8a39263cull;
        MM3D::LinkInterpolation::Palette palette;palette.fill(Camera(0));
        MM3D::LinkInterpolation::History history;
        Check(!history.Apply(1,0,Identity(),palette,{target,chain},true,vertices)[0],
              "Koume target and chain seed separate histories");
        target.pose.bones={Flatten(Camera(60,4))};chain.pose.bones=target.pose.bones;
        palette.fill(Expand(target.pose.bones[0]));
        const auto mid=history.Apply(2,33,Identity(),palette,{chain,target},true,vertices);
        Check(mid[0] && Near(*mid[0],Camera(30,2),1e-6),"Koume target pair interpolates rotation and translation");
        Check(!history.Apply(2,33,Identity(),palette,{target,chain},false,vertices)[0],"Koume target comparison stays native");
        Check(!history.Apply(2,33,Identity(),palette,{target,chain},true,vertices+1)[0],"unverified Koume target material excluded");
        auto duplicate=chain;duplicate.pose.instance=102;duplicate.model=202;
        Check(!history.Apply(2,33,Identity(),palette,{target,chain,duplicate},true,vertices)[0],"extra Koume target owner rejected");
        auto unrelated=chain;unrelated.signature++;
        Check(!history.Apply(2,33,Identity(),palette,{target,unrelated},true,vertices)[0],"unrelated shared root excluded");
        auto rebound=chain;rebound.bind[0][3]=1;
        Check(!history.Apply(2,33,Identity(),palette,{target,rebound},true,vertices)[0],"changed Koume target bind excluded");
        Check(!history.Apply(4,99,Identity(),palette,{target,chain},true,vertices)[0],"Koume target capture gap reseeds");
        MM3D::LinkInterpolation::History disagree;
        target.pose.bones={Flatten(Camera(0))};chain.pose.bones={Flatten(Camera(-60))};
        palette.fill(Expand(target.pose.bones[0]));
        disagree.Apply(1,0,Identity(),palette,{target,chain},true,vertices);
        palette.fill(Expand(chain.pose.bones[0]));
        disagree.Apply(1,0,Identity(),palette,{target,chain},true,vertices);
        target.pose.bones={Flatten(Camera(60))};chain.pose.bones=target.pose.bones;
        palette.fill(Expand(target.pose.bones[0]));
        Check(!disagree.Apply(2,33,Identity(),palette,{target,chain},true,vertices)[0],
              "coincident Koume target poses with different histories rejected");
    }
    {
        using MM3D::BloomSampling::MidpointComposite;
        constexpr auto shader=0x89b1ab98da3d9241ULL,swizzle=0x6f0b398b1fc186a9ULL;
        Check(MidpointComposite(shader,swizzle,4,3,true),"captured bloom composite accepted");
        Check(!MidpointComposite(shader,swizzle,72,3,true),"scenery/glare not mistaken for bloom");
        Check(!MidpointComposite(shader,swizzle,4,1,true),"single texture effect not broadened");
        Check(!MidpointComposite(shader,swizzle,4,3,false),"perspective geometry excluded");
        Check(!MidpointComposite(shader+1,swizzle,4,3,true),"unverified shader excluded");
    }
    {
        SceneCopies copies;
        copies.Begin(10,100);copies.Transfer(10,100,200);
        Check(!copies.PreviousContains(10,200),"feedback cannot use current-frame-only provenance");
        copies.Begin(11,100);
        Check(copies.PreviousContains(11,200),"feedback recognizes previous scene transfer");
        Check(!copies.PreviousContains(12,200),"feedback requires the active frame");
        copies.Begin(13,100);
        Check(!copies.PreviousContains(13,200),"feedback provenance expires over gaps");
        Matrix p{};p[0]={0,-1./120,0,1};p[1]={-1./200,0,0,1};
        p[2]={0,0,.5,-.5};p[3]={0,0,0,1};
        auto m=Identity();m[0][0]=m[1][1]=m[2][2]=1.0008;
        m[0][3]=-.16;m[1][3]=-.096;
        Check(TransformationFeedback(p,m,true,4,1),"captured feedback quad recognized");
        Check(!TransformationFeedback(p,m,false,4,1),"arbitrary texture never matches feedback");
        Check(!TransformationFeedback(p,m,true,4,0),"untextured bars and fades excluded");
        Check(!TransformationFeedback(p,m,true,72,1),"sun glare excluded");
        m[0][3]=200;
        Check(!TransformationFeedback(p,m,true,4,1),"different quad placement excluded");
    }
    {
        using namespace MM3D::LinkPoseMath;
        using MM3D::LinkPoseCapture::LivePose;
        const auto pose=[](unsigned count,unsigned instance,std::uint64_t signature) {
            LivePose p;p.pose.instance=instance;p.model=instance;p.signature=signature;
            p.pose.bones.assign(count,Flatten(Identity()));p.bind.assign(count,Flatten(Identity()));
            p.words.resize(count);p.words[0]=0xffff0000;
            for(unsigned i=1;i<count;++i)p.words[i]=i;
            return p;
        };
        {
            namespace L=MM3D::LinkInterpolation;
            auto cow=pose(10,9998,0xbd01809703b206e7ull);
            cow.words={0xffff0000,0x1001,0x11002,0x21003,0x11004,0x1005,0x51006,0x61007,0x71008,0x81009};
            const std::array<unsigned,10> palette_bones{0,6,5,7,8,9,2,4,1,3};
            const auto set=[&](LivePose& actor,double x,double shear) {
                auto root=Camera(0,x);root[0][0]=root[2][2]=.01;root[1][1]=.0101;
                actor.pose.bones[0]=Flatten(root);
                for(unsigned i=1;i<10;++i) {
                    auto local=Camera(0,i>=5?20:10*i);
                    if(i>=5){local[0][1]=shear;local[1][3]=10;}
                    actor.pose.bones[i]=Flatten(Multiply(Expand(actor.pose.bones[actor.words[i]>>16]),local));
                }
                L::Palette p;
                for(unsigned i=0;i<10;++i)p[i]=Expand(actor.pose.bones[palette_bones[i]]);
                return p;
            };
            auto other=cow;other.pose.instance++;
            auto first=set(cow,0,.1),second=set(other,100,.2);
            L::History history;
            history.Apply(1,0,Identity(),first,{cow,other},true,2274);
            history.Apply(1,0,Identity(),second,{cow,other},true,2274);
            first=set(cow,8,.2);second=set(other,92,.1);
            const auto native=first;
            const auto other_mid=history.Apply(2,33,Identity(),second,{other,cow},true,2274);
            const auto mid=history.Apply(2,33,Identity(),first,{other,cow},true,2274);
            auto expected_cow=cow,expected_other=other;
            const auto expected=set(expected_cow,4,.15),expected_second=set(expected_other,96,.15);
            for(unsigned slot=1;slot<=5;++slot) {
                Check(mid[slot] && Near(*mid[slot],expected[slot],.0001),
                    "cow tail preserves local shear and connects all five halfway joints");
                Check(other_mid[slot] && Near(*other_mid[slot],expected_second[slot],.0001),
                    "nearby cows retain independent tail histories despite reversed draw order");
            }
            Check(first==native,"cow tail interpolation never changes the native palette");
            auto a=Identity(),b=Identity();a[0][1]=.1;b[0][1]=.2;
            Check(!Half(a,b),"cow regression exercises shear rejected by ordinary rotation interpolation");
            const auto forward=L::CowTailHalf(a,b,cow,5),reverse=L::CowTailHalf(b,a,cow,5);
            Check(forward && reverse && Near(*forward,*reverse,0) && std::abs((*forward)[0][1]-.15)<1e-6,
                "cow tail shear interpolates symmetrically");
            Check(!L::CowTailHalf(a,b,cow,4) && !L::CowTailHalf(a,b,cow,10),"cow head and out-of-range joints retain ordinary checks");
            auto invalid=cow;invalid.signature^=1;
            Check(!L::CowTailHalf(a,b,invalid,5),"other actors never use cow deformation");
            invalid=cow;invalid.words[9]=0x71009;
            Check(!L::CowTailHalf(a,b,invalid,9),"unverified cow hierarchy rejected");
            auto broken=b;broken[1][1]=0;
            Check(!L::CowTailHalf(a,broken,cow,5),"collapsed tail stays native");
            broken=b;broken[0][1]=2;
            Check(!L::CowTailHalf(a,broken,cow,5),"large tail deformation stays native");
            broken=b;broken[0][3]=101;
            Check(!L::CowTailHalf(a,broken,cow,5),"tail teleport stays native");
            broken=b;broken[0][0]=std::numeric_limits<double>::quiet_NaN();
            Check(!L::CowTailHalf(a,broken,cow,5),"nonfinite tail stays native");
            Check(!history.Apply(2,33,Identity(),first,{cow},false,2274)[0],"disabled cow interpolation stays native");
            Check(!history.Apply(4,99,Identity(),first,{cow},true,2274)[0],"cow frame gap reseeds");
            cow.generation++;
            Check(!history.Apply(5,132,Identity(),first,{cow},true,2274)[0],"cow generation change reseeds");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto balloon=pose(9,9999,0xae6056c772fdbd26ull);
            balloon.words={0xffff0000,0x1001,0x1002,0x21003,0x31004,0x1005,0x51006,0x61007,0x1008};
            const std::array<unsigned,8> bones{3,4,2,1,8,6,7,5};
            const auto set=[&](LivePose& actor,double x,double shear,double angle) {
                actor.pose.bones[0]=Flatten(Camera(0,x));
                for(unsigned i=1;i<9;++i) {
                    auto local=Camera(angle,3*i*i);
                    if(i==1){local=Camera(0,10);local[0][1]=shear;local[1][1]=.86;}
                    actor.pose.bones[i]=Flatten(Multiply(Expand(actor.pose.bones[actor.words[i]>>16]),local));
                }
                L::Palette p;p.fill(Camera(0,10000));
                for(unsigned i=0;i<bones.size();++i)p[i]=Multiply(Expand(actor.pose.bones[bones[i]]),Expand(actor.bind[bones[i]]));
                return p;
            };
            auto other=balloon;other.pose.instance++;
            auto first=set(balloon,0,.08,0),second=set(other,100,.16,12);
            L::History history;
            history.Apply(1,0,Identity(),first,{balloon,other},true,1350);
            history.Apply(1,0,Identity(),second,{balloon,other},true,1350);
            first=set(balloon,8,.16,12);second=set(other,92,.08,0);
            const auto native=first;
            const auto other_mid=history.Apply(2,33,Identity(),second,{other,balloon},true,1350);
            const auto mid=history.Apply(2,33,Identity(),first,{other,balloon},true,1350);
            auto expected_actor=balloon,expected_other=other;
            const auto expected=set(expected_actor,4,.12,6),expected_second=set(expected_other,96,.12,6);
            for(unsigned slot=0;slot<bones.size();++slot) {
                Check(mid[slot] && Near(*mid[slot],expected[slot],.0001),
                    "ghost balloon body and dangling limbs interpolate together");
                Check(other_mid[slot] && Near(*other_mid[slot],expected_second[slot],.0001),
                    "nearby ghost balloons retain separate histories");
            }
            auto crossing=balloon;
            crossing.bind[2]=Flatten(Camera(0,-12));
            L::History crossing_history;
            auto crossing_palette=set(crossing,0,.08,12);
            crossing_history.Apply(1,0,Identity(),crossing_palette,{crossing},true,1350);
            crossing_palette=set(crossing,0,.16,0);
            Check(!L::Identify(crossing_palette[2],crossing),"balloon arm skin coincides with root at crossing");
            Check(L::GhostBalloonBodyBones(crossing_palette,crossing,1350),"full balloon mesh resolves crossed arm");
            const auto crossing_mid=crossing_history.Apply(2,33,Identity(),crossing_palette,{crossing},true,1350);
            auto crossing_expected=crossing;
            const auto expected_crossing=set(crossing_expected,0,.12,6);
            Check(crossing_mid[2] && Near(*crossing_mid[2],expected_crossing[2],.0001),
                "crossing balloon arm follows its own midpoint rather than stationary root");
            Check(!L::GhostBalloonBodyBones(crossing_palette,crossing,72),"balloon mesh mapping excludes other meshes");
            crossing_palette[7][0][3]+=10;
            Check(!L::GhostBalloonBodyBones(crossing_palette,crossing,1350),"partial balloon palette match rejected");
            Check(first==native,"ghost balloon native palette remains unchanged");
            const auto repeat=history.Apply(2,33,Identity(),first,{balloon},true,1350);
            Check(repeat==mid,"repeated balloon draws reuse the same midpoint");
            auto small=first;small[0]=first[3];small[1]=first[4];
            const auto small_mid=history.Apply(2,33,Identity(),small,{balloon},true,72);
            Check(small_mid[0] && Near(*small_mid[0],expected[3],.0001),
                "separate balloon body meshes share the deforming joint");
            auto a=Identity(),b=Identity();a[0][1]=.08;b[0][1]=.16;
            Check(!Half(a,b),"ghost balloon reproduces rejected rigid interpolation");
            const auto ab=L::GhostBalloonHalf(a,b,balloon,1),ba=L::GhostBalloonHalf(b,a,balloon,1);
            Check(ab && ba && Near(*ab,*ba,0),"balloon deformation is symmetric");
            Check(!L::GhostBalloonHalf(a,b,balloon,2),"balloon arms retain rigid interpolation guards");
            auto invalid=balloon;invalid.signature^=1;
            Check(!L::GhostBalloonHalf(a,b,invalid,1),"other balloon actors excluded");
            invalid=balloon;invalid.words[8]=0x11008;
            Check(!L::GhostBalloonHalf(a,b,invalid,1),"different balloon hierarchy excluded");
            auto broken=b;broken[1][1]=0;
            Check(!L::GhostBalloonHalf(a,broken,balloon,1),"collapsed balloon stays native");
            broken=b;broken[0][1]=2;
            Check(!L::GhostBalloonHalf(a,broken,balloon,1),"large balloon deformation stays native");
            broken=b;broken[0][3]=101;
            Check(!L::GhostBalloonHalf(a,broken,balloon,1),"balloon teleport stays native");
            broken=b;broken[0][0]=std::numeric_limits<double>::quiet_NaN();
            Check(!L::GhostBalloonHalf(a,broken,balloon,1),"nonfinite balloon stays native");
            Check(!history.Apply(2,33,Identity(),first,{balloon},false,1350)[3],"disabled balloon interpolation stays native");
            Check(!history.Apply(4,99,Identity(),first,{balloon},true,1350)[3],"balloon frame gap reseeds");
            balloon.generation++;
            Check(!history.Apply(5,132,Identity(),first,{balloon},true,1350)[3],"balloon generation change reseeds");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto wart=pose(8,10000,0xb4670e8c455fcc2bull);
            wart.words={0xffff0000,0x1001,0x11002,0x21003,0x11004,0x41005,0x11006,0x61007};
            const auto set=[&](double x,double stretch) {
                const std::array<double,8> offsets{0,0,2,4,10,12+stretch,-10,-12-stretch};
                for(unsigned i=0;i<8;++i)wart.pose.bones[i]=Flatten(Camera(0,x+offsets[i]));
                L::Palette body;body.fill(Camera(0,10000));
                for(unsigned i=0;i<3;++i)body[i]=Expand(wart.pose.bones[L::WartBodyPalette[i]]);
                return body;
            };
            auto body=set(0,0);
            Check(!L::Identify(body[0],wart),"Wart reproduces ambiguous coincident root/body bones");
            Check(L::WartBodyBones(body,wart,1872),"complete Wart body palette disambiguates ownership");
            L::History history;
            Check(!history.Apply(1,0,Identity(),body,{wart},true,1872)[0],"Wart first frame seeds history");
            body=set(8,4);const auto native=body;
            L::Palette eye;eye.fill(Camera(0,10000));eye[0]=Expand(wart.pose.bones[3]);
            auto eye_mid=history.Apply(2,33,Identity(),eye,{wart},true,396);
            Check(eye_mid[0] && std::abs((*eye_mid[0])[0][3]-8)<1e-6,
                "Wart eye follows the same halfway root even when drawn before body");
            const auto mid=history.Apply(2,33,Identity(),body,{wart},true,1872);
            const std::array<double,3> expected{4,18,-10};
            for(unsigned i=0;i<3;++i)Check(mid[i] && std::abs((*mid[i])[0][3]-expected[i])<1e-6,
                "all Wart body branches interpolate through their native hierarchy");
            Check(body==native,"Wart native palette remains unchanged");
            const auto repeat=history.Apply(2,34,Identity(),body,{wart},true,1872);
            for(unsigned i=0;i<3;++i)Check(mid[i] && repeat[i] && Near(*mid[i],*repeat[i],0),
                "repeated Wart body draws reuse one midpoint");
            auto invalid=body;invalid[2][0][3]+=1;
            Check(!L::WartBodyBones(invalid,wart,1872),"all three Wart palette slots must match");
            Check(!L::WartBodyBones(body,wart,396),"Wart eye does not use the body's skin palette");
            auto wrong=wart;wrong.signature^=1;
            Check(!L::WartBodyBones(body,wrong,1872),"unrelated skeleton never uses Wart matching");
            wrong=wart;wrong.words[7]=0x51007;
            Check(!L::WartBodyBones(body,wrong,1872),"changed Wart topology rejected");
            auto duplicate=wart;duplicate.pose.instance++;
            Check(!history.Apply(2,33,Identity(),body,{wart,duplicate},true,1872)[0],
                "two possible Wart actors remain ambiguous");
            Check(!history.Apply(2,33,Identity(),body,{wart},false,1872)[0],"disabled Wart interpolation stays native");
            wart.generation++;
            Check(!history.Apply(3,66,Identity(),body,{wart},true,1872)[0],"Wart generation change reseeds");
            Check(!history.Apply(5,132,Identity(),body,{wart},true,1872)[0],"Wart capture gap reseeds");
            body=set(300,4);
            Check(!history.Apply(6,165,Identity(),body,{wart},true,1872)[0],"Wart teleport stays native");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto human=pose(26,10001,100);
            human.words.assign(MM3D::LinkPoseCapture::BoneWords.begin(),MM3D::LinkPoseCapture::BoneWords.end());
            for(unsigned i=0;i<26;++i)human.pose.bones[i]=Flatten(Camera(0,i*5));
            auto flat=Camera(0,4);for(unsigned r=0;r<3;++r)flat[r][1]=0;
            human.pose.bones[24]=Flatten(flat);
            L::Palette p;p.fill(Camera(0,10000));p[0]=flat;
            Check(!L::Identify(flat,human),"generic bone matching still rejects singular matrices");
            Check(L::BowStringBone(p,human,12),"verified flat bowstring recognized");
            Check(!L::BowStringBone(p,human,18),"unrelated flat mesh rejected");
            L::History h;h.Apply(1,0,Identity(),p,{human},true,12);
            flat=Camera(20,6);for(unsigned r=0;r<3;++r)flat[r][1]=0;
            human.pose.bones[24]=Flatten(flat);p[0]=flat;
            auto mid=h.Apply(2,33,Identity(),p,{human},true,12);
            Check(mid[0] && !Near(*mid[0],flat,1e-5),"flat bowstring gets an intermediate pose");
            Check(mid[0] && (*mid[0])[0][1]==0 && (*mid[0])[1][1]==0 && (*mid[0])[2][1]==0,
                "bowstring stays flat at midpoint");
            Check(!h.Apply(4,99,Identity(),p,{human},true,12)[0],"bowstring disappearance reseeds");
            auto invalid=flat;invalid[0][0]=std::numeric_limits<double>::quiet_NaN();
            Check(!L::BowStringHalf(invalid,flat),"nonfinite bowstring rejected");
            // Release rapidly contracts and reverses the signed string scale;
            // neither a singular endpoint nor a >2x change is a discontinuity.
            for(auto scales:std::array<std::array<double,2>,5>{{{0,1},{1,.07},{.07,.00001},{.00001,0},{.07,-.03}}}) {
                L::History firing;
                const auto string=[&](double scale) {
                    auto m=Identity();m[1][1]=scale;
                    human.pose.bones[24]=Flatten(m);p[0]=m;return m;
                };
                string(scales[0]);firing.Apply(1,0,Identity(),p,{human},true,12);
                const auto current=string(scales[1]);
                const auto out=firing.Apply(2,33,Identity(),p,{human},true,12);
                Check(out[0] && std::abs((*out[0])[1][1]-(scales[0]+scales[1])/2)<1e-6,
                    "firing bowstring interpolates signed scale through collapse");
                const auto repeat=firing.Apply(2,34,Identity(),p,{human},true,12);
                Check(repeat[0] && out[0] && Near(*repeat[0],*out[0],0),"repeated bow draw retains midpoint");
                Check(!firing.Apply(4,99,Identity(),p,{human},true,12)[0],"firing gap reseeds");
                Check(Near(p[0],current,0),"bow interpolation preserves native palette");
            }
            auto skew=Identity();skew[0][1]=.1;
            Check(!L::BowStringHalf(skew,Identity()),"sheared string rejected");
            skew=Identity();skew[1][1]=2;
            Check(!L::BowStringHalf(skew,Identity()),"unverified string scale rejected");
            auto bunny=pose(3,10002,0x4ce0cfdeaf2fb04eull);bunny.words={0xffff0000,0x1001,0x11002};
            const auto set=[&](double angle) {
                human.pose.bones[11]=Flatten(Camera(angle,10));
                const auto root=Multiply(Expand(human.pose.bones[11]),L::BunnyHeadOffset());
                bunny.pose.bones[0]=bunny.pose.bones[1]=Flatten(root);
                bunny.pose.bones[2]=Flatten(Multiply(root,Camera(angle,4)));
                L::Palette out;out.fill(Camera(0,10000));out[0]=out[1]=root;out[2]=Expand(bunny.pose.bones[2]);return out;
            };
            L::History ears;auto before=set(0);ears.Apply(1,0,Identity(),before,{human,bunny},true,330);
            auto after=set(20);mid=ears.Apply(2,33,Identity(),after,{human,bunny},true,330);
            Check(mid[0] && Near(*mid[0],Multiply(Camera(10,10),L::BunnyHeadOffset()),1e-5),
                "bunny root follows head hierarchy through fixed attachment offset");
            Check(mid[2] && !Near(*mid[2],after[2],1e-5),"bunny ear child animation interpolates");
            auto duplicate=bunny;duplicate.pose.instance++;
            Check(!ears.Apply(2,33,Identity(),after,{human,bunny,duplicate},true,330)[0],"duplicate bunny owners rejected");
            for(auto signature:{0xe3b575dedeb5ae54ull,0x3d9d08ddcad1f38eull,0x1ee17d0b154d34f6ull,
                                0xb86510278af23ef2ull,0xc876669185553f9bull}) {
                auto door=pose(1,10003,signature);auto bind=Identity();
                bind[2][3]=signature==0xc876669185553f9bull?.0012207040563225746:-.0006294240010902286;
                door.bind[0]=Flatten(bind);p.fill(Camera(0,10000));p[0]=Identity();
                const unsigned vertices=signature==0xc876669185553f9bull?84:36;
                L::History doors;doors.Apply(1,0,Identity(),p,{door},true,vertices);
                p[0][1][3]=6;door.pose.bones[0]=Flatten(p[0]);
                mid=doors.Apply(2,33,Identity(),p,{door},true,vertices);
                Check(mid[0] && std::abs((*mid[0])[1][3]-3)<1e-6,"sliding-door family gets halfway translation");
                Check(!L::DungeonShutterBone(p,door,42),"unverified shutter mesh rejected");
            }
            auto light=pose(4,10004,0x84bf0b3a9fcca9e4ull);light.pose.bones[1]=light.pose.bones[2];
            p.fill(Camera(0,10000));p[0]=Identity();L::History beam;
            beam.Apply(1,0,Identity(),p,{light},true,60);
            for(auto& b:light.pose.bones) {
                b[3]=2;
            }
            p[0][0][3]=2;
            mid=beam.Apply(2,33,Identity(),p,{light},true,60);
            Check(mid[0] && std::abs((*mid[0])[0][3]-1)<1e-6,"equivalent mirror beam bones interpolate");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            {
                auto lower=pose(1,10020,0x046391818a624e42ull),upper=pose(1,10021,0x046391818a624e42ull);
                upper.model=lower.model;
                auto bind=Identity();bind[1][3]=-1199.988037109375;
                lower.bind[0]=upper.bind[0]=Flatten(bind);
                const auto set=[&](double y,double offset=0) {
                    auto a=Identity();for(unsigned c=0;c<3;++c)a[c][c]=.1;
                    a[1][3]=y;lower.pose.bones[0]=Flatten(a);
                    auto b=a;b[1][3]-=bind[1][3]*.1;b[1][3]+=offset;upper.pose.bones[0]=Flatten(b);
                    L::Palette p;p.fill(Camera(0,10000));p[0]=a;return p;
                };
                L::History h;auto a=set(0);h.Apply(1,0,Identity(),a,{lower,upper},true,384);
                auto b=set(4);const auto mid=h.Apply(2,33,Identity(),b,{lower,upper},true,384);
                Check(mid[0] && std::abs((*mid[0])[1][3]-2)<.0001,
                    "adjacent Snowhead pillar direct/skin owners agree on rising midpoint");
                Check(!h.Apply(2,33,Identity(),b,{lower,upper},true,383)[0],"unknown pillar mesh rejected");
                auto duplicate=upper;duplicate.pose.instance++;
                Check(!h.Apply(2,33,Identity(),b,{lower,upper,duplicate},true,384)[0],"third pillar owner rejected");
                Check(!h.Apply(4,99,Identity(),b,{lower,upper},true,384)[0],"pillar gap reseeds");
                L::History diverging;a=set(0,-1);diverging.Apply(1,0,Identity(),a,{lower,upper},true,384);
                auto other=a;other[0][1][3]=-1;diverging.Apply(1,0,Identity(),other,{lower,upper},true,384);
                b=set(4);Check(!diverging.Apply(2,33,Identity(),b,{lower,upper},true,384)[0],
                    "separately moving rings do not borrow conflicting histories");
                upper.pose.bones=lower.pose.bones;
                Check(!h.Apply(2,33,Identity(),b,{lower,upper},true,384)[0],"coincident duplicate rings remain ambiguous");
                auto middle=pose(1,10022,0x0952810a097e354aeull);
                auto middle_bind=Identity();middle_bind[0][0]=middle_bind[2][2]=.12006315588951111;
                middle_bind[1][1]=.10000000149011612;middle.bind[0]=Flatten(middle_bind);
                L::History central;a=set(0);
                middle.pose.bones[0]=Flatten(Multiply(a[0],*Inverse(middle_bind)));
                central.Apply(1,0,Identity(),a,{lower,middle},true,384);
                b=set(4);middle.pose.bones[0]=Flatten(Multiply(b[0],*Inverse(middle_bind)));
                const auto central_mid=central.Apply(2,33,Identity(),b,{lower,middle},true,384);
                Check(central_mid[0] && std::abs((*central_mid[0])[1][3]-2)<.0001,
                    "middle stone scaled skin agrees with ice ring midpoint");
                middle.bind[0][0]+=.001f;
                middle.pose.bones[0]=Flatten(Multiply(b[0],*Inverse(Expand(middle.bind[0]))));
                Check(!central.Apply(2,33,Identity(),b,{lower,middle},true,384)[0],
                    "different middle stone bind cannot borrow ice history");
            }
            for(unsigned vertices:{48u,72u,144u,348u,720u}) {
                auto screw=pose(1,10012,0x85335493df397bb2ull),deck=pose(1,10013,0x85335493df397bb2ull);
                L::Palette p;p.fill(Camera(0,10000));p[0]=Identity();L::History h;
                Check(!h.Apply(1,0,Identity(),p,{screw,deck},true,vertices)[0],"paired rotor seeds both models");
                p[0]=Camera(20);screw.pose.bones[0]=deck.pose.bones[0]=Flatten(p[0]);
                const auto mid=h.Apply(2,33,Identity(),p,{screw,deck},true,vertices);
                Check(mid[0] && Near(*mid[0],Camera(10),1e-6),"rotor deck and screw share halfway rotation");
                Check(!h.Apply(2,33,Identity(),p,{screw,deck},true,vertices+1)[0],"unverified paired rotor mesh rejected");
                auto third=pose(1,10014,screw.signature);third.pose.bones=screw.pose.bones;
                Check(!h.Apply(2,33,Identity(),p,{screw,deck,third},true,vertices)[0],"third coincident rotor remains ambiguous");
                Check(!h.Apply(4,99,Identity(),p,{screw,deck},true,vertices)[0],"rotor frame gaps reseed");
                L::History split;p[0]=Identity();screw.pose.bones[0]=Flatten(p[0]);deck.pose.bones[0]=Flatten(Camera(10));
                split.Apply(1,0,Identity(),p,{screw,deck},true,vertices);
                p[0]=Camera(10);split.Apply(1,0,Identity(),p,{screw,deck},true,vertices);
                p[0]=Camera(20);screw.pose.bones[0]=deck.pose.bones[0]=Flatten(p[0]);
                Check(!split.Apply(2,33,Identity(),p,{screw,deck},true,vertices)[0],"paired rotor midpoint disagreement rejected");
            }
        }
        {
            namespace L=MM3D::LinkInterpolation;
            {
                auto ice=pose(4,10015,0x4fa89f19eb218899ull);
                ice.words={0xffff0000,1,2,3};
                const auto set=[&](float xz,float y) {
                    auto matrix=Identity();matrix[0][0]=matrix[2][2]=xz;matrix[1][1]=y;
                    for(auto& bone:ice.pose.bones)bone=Flatten(matrix);
                    L::Palette p;p.fill(Camera(0,10000));p[0]=matrix;return p;
                };
                auto a=set(.01f,.0056f);L::History h;
                Check(!L::Identify(a[0],ice),"Freezard body has equivalent bones");
                h.Apply(1,0,Identity(),a,{ice},true,630);
                auto b=set(.011f,.0052f);const auto mid=h.Apply(2,33,Identity(),b,{ice},true,630);
                Check(mid[0] && std::abs((*mid[0])[0][0]-.0105f)<1e-7 &&
                    std::abs((*mid[0])[1][1]-.0054f)<1e-7,
                    "Freezard death expands sideways and shrinks vertically at midpoint");
                Check(!h.Apply(2,33,Identity(),b,{ice},true,629)[0],"unknown Freezard mesh excluded");
                auto twin=ice;twin.pose.instance++;
                Check(!h.Apply(2,33,Identity(),b,{ice,twin},true,630)[0],"coincident Freezards rejected");
                Check(!h.Apply(4,99,Identity(),b,{ice},true,630)[0],"Freezard capture gap reseeds");
                L::History diverging;a=set(.01f,.0056f);ice.pose.bones[1][3]=2;
                diverging.Apply(1,0,Identity(),a,{ice},true,630);
                b=set(.011f,.0052f);
                Check(!diverging.Apply(2,33,Identity(),b,{ice},true,630)[0],"Freezard divergent bone histories rejected");
                L::History collapse;a=set(.022f,.0008f);collapse.Apply(1,0,Identity(),a,{ice},true,630);
                b=set(.023f,0);
                const auto hidden=collapse.Apply(2,33,Identity(),b,{ice},true,630);
                Check(!hidden[0] || (*hidden[0])[1][1]==0,"collapsed Freezard is never revived");
            }
            for(unsigned vertices:{414u,186u}) {
                auto deku=pose(4,10011,0x150560ccf65e9d98ull);
                deku.words={0xffff0000,0x1001,0x11002,0x11003};
                const auto set=[&](double x) {
                    deku.pose.bones[0]=Flatten(Camera(0,x));
                    for(unsigned i=1;i<4;++i) {
                        deku.pose.bones[i]=Flatten(Camera(90,x));
                        deku.bind[i]=Flatten(Camera(-90,0));
                    }
                    L::Palette p;p.fill(Camera(0,10000));
                    for(unsigned i=0;i<3;++i)p[i]=Expand(deku.pose.bones[i+1]);
                    return p;
                };
                L::History h;auto a=set(0);h.Apply(1,0,Identity(),a,{deku},true,vertices);
                auto b=set(4);auto mid=h.Apply(2,33,Identity(),b,{deku},true,vertices);
                for(unsigned slot=0;slot<3;++slot)
                    Check(mid[slot] && Near(*mid[slot],Camera(90,2),1e-5),"burrowed Deku shared direct bones interpolate");
                auto duplicate=deku;duplicate.pose.instance++;
                Check(!h.Apply(2,33,Identity(),b,{deku,duplicate},true,vertices)[0],"overlapping burrowed heads rejected");
                L::History divergent;a=set(0);deku.pose.bones[3]=Flatten(Camera(90,20));
                divergent.Apply(1,0,Identity(),a,{deku},true,vertices);
                b=set(4);
                Check(!divergent.Apply(2,33,Identity(),b,{deku},true,vertices)[0],"shared current Deku bones with divergent midpoints rejected");
                auto hidden=Identity();for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)hidden[r][c]=0;
                for(auto& bone:deku.pose.bones) {
                    bone=Flatten(hidden);
                }
                b.fill(hidden);
                Check(!h.Apply(3,66,Identity(),b,{deku},true,vertices)[0],"burrowed head hidden geometry stays hidden");
            }
        }
        {
            namespace L=MM3D::LinkInterpolation;
            for(auto spec:{std::array<std::uint64_t,3>{0x049480e14d9b324bull,23,870},
                           std::array<std::uint64_t,3>{0x9bbb839482a3896aull,3,162}}) {
                auto actor=pose(unsigned(spec[1]),10010,spec[0]);L::Palette p;
                p.fill(Camera(0,10000));p[0]=Identity();L::History h;
                Check(!h.Apply(1,0,Identity(),p,{actor},true,unsigned(spec[2]))[0],"shared actor bones seed");
                for(auto& bone:actor.pose.bones) {
                    bone[3]=4;
                }
                p[0][0][3]=4;
                auto mid=h.Apply(2,33,Identity(),p,{actor},true,unsigned(spec[2]));
                Check(mid[0] && std::abs((*mid[0])[0][3]-2)<1e-6,"tank fish/Bombchu equivalent bones get midpoint");
                auto duplicate=actor;duplicate.pose.instance++;
                Check(!h.Apply(2,33,Identity(),p,{actor,duplicate},true,unsigned(spec[2]))[0],"overlapping fish/Bombchu instances remain ambiguous");
            }
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto fish=pose(8,438,0x3f33b43a6cf98ad4ull);
            const auto set=[&](double t,unsigned vertices) {
                for(unsigned i=0;i<8;++i)fish.pose.bones[i]=Flatten(Camera(0,i*20+t*4));
                fish.pose.bones[4]=fish.pose.bones[1]=fish.pose.bones[2];
                const std::vector<unsigned> bones=vertices==732?std::vector<unsigned>{2,3,7}:
                    std::vector<unsigned>{6,2,7,5,3};
                L::Palette p;p.fill(Camera(0,99999));
                for(unsigned i=0;i<bones.size();++i)p[i]=Expand(fish.pose.bones[bones[i]]);
                return p;
            };
            for(unsigned vertices:{732u,108u}) {
                L::History h;const auto a=set(0,vertices);
                Check(!h.Apply(1,0,Identity(),a,{fish},true,vertices)[0],"Gyorg fish seeds history");
                const auto b=set(1,vertices);
                const auto half=h.Apply(2,33,Identity(),b,{fish},true,vertices);
                for(unsigned i=0;i<(vertices==732?3u:5u);++i) {
                    auto expected=b[i];expected[0][3]-=2;
                    Check(half[i] && Near(*half[i],expected,.00001),"Gyorg fish body and fins interpolate");
                }
                Check(!h.Apply(2,33,Identity(),b,{fish},false,vertices)[0],"disabled fish stays native");
                auto duplicate=fish;duplicate.pose.instance++;
                Check(!h.Apply(3,66,Identity(),b,{fish,duplicate},true,vertices)[0],"coincident fish cannot borrow histories");
                Check(!h.Apply(4,99,Identity(),b,{fish},true,vertices)[0],"fish capture gap reseeds");
            }
            L::History h;auto a=set(0,732);fish.pose.bones[4]=Flatten(Camera(0,39));
            h.Apply(1,0,Identity(),a,{fish},true,732);
            const auto b=set(1,732);
            Check(!h.Apply(2,33,Identity(),b,{fish},true,732)[0],"divergent equivalent fish bones rejected");
            h.Reset();a=set(0,732);h.Apply(1,0,Identity(),a,{fish},true,733);set(1,732);
            Check(!h.Apply(2,33,Identity(),b,{fish},true,733)[0],"unknown fish mesh excluded from equivalent-bone fallback");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto princess=pose(25,439,0x91e28867df89d7bcull);
            princess.words[9]=(8u<<16)|9;
            princess.words[12]=(9u<<16)|12;
            const auto set=[&](double t) {
                for(unsigned i=0;i<25;++i)princess.pose.bones[i]=Flatten(Camera(0,i*20+t*4));
                princess.pose.bones[12]=princess.pose.bones[9];
                L::Palette p;p.fill(Camera(0,99999));p[0]=Expand(princess.pose.bones[9]);
                return p;
            };
            L::History h;auto a=set(0);
            Check(!L::Identify(a[0],princess),"Princess eye bones are ambiguous to generic lookup");
            Check(!h.Apply(1,0,Identity(),a,{princess},true,24)[0],"Princess eyes seed history");
            auto b=set(1);const auto half=h.Apply(2,33,Identity(),b,{princess},true,24);
            Check(half[0]&&Near(*half[0],Camera(0,182),1e-5),"Princess eyes follow interpolated head motion");
            Check(!h.Apply(2,33,Identity(),b,{princess},false,24)[0],"Princess eyes respect interpolation toggle");
            auto duplicate=princess;duplicate.pose.instance++;
            Check(!h.Apply(3,66,Identity(),b,{princess,duplicate},true,24)[0],"coincident Princess instances stay native");
            Check(!h.Apply(4,99,Identity(),b,{princess},true,24)[0],"Princess eye history resets after a gap");
            h.Reset();a=set(0);princess.pose.bones[12]=Flatten(Camera(0,179));
            h.Apply(1,0,Identity(),a,{princess},true,24);b=set(1);
            Check(!h.Apply(2,33,Identity(),b,{princess},true,24)[0],"divergent Princess eye midpoint bones rejected");
            h.Reset();a=set(0);h.Apply(1,0,Identity(),a,{princess},true,25);b=set(1);
            Check(!h.Apply(2,33,Identity(),b,{princess},true,25)[0],"unverified Princess mesh stays native");
            h.Reset();princess.signature++;a=set(0);h.Apply(1,0,Identity(),a,{princess},true,24);b=set(1);
            Check(!h.Apply(2,33,Identity(),b,{princess},true,24)[0],"other skeletons do not inherit Princess eye fallback");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto goht=pose(29,440,0x496431680f850b7full);
            const auto set=[&](double t,const std::vector<unsigned>& bones) {
                for(unsigned i=0;i<29;++i)goht.pose.bones[i]=Flatten(Camera(0,i*20+t*4));
                L::Palette p;p.fill(Camera(0,99999));
                for(unsigned slot=0;slot<bones.size();++slot) {
                    p[slot]=Expand(goht.pose.bones[bones[slot]]);
                    p[slot][1][3]+=.05; // Captured bind reconstruction error exceeds generic matching.
                }
                return p;
            };
            for(const auto& [vertices,bones]:std::vector<std::pair<unsigned,std::vector<unsigned>>>{
                {348,{12,14,3}},{606,{12,14}},{96,{12,14}},{2100,{7,6,11,10,5,3,9}}}) {
                L::History h;const auto a=set(0,bones);
                Check(L::GohtSkinBones(a,goht,vertices).has_value(),"Goht complete head/body skin palette recognized");
                Check(!L::Identify(a[0],goht),"captured Goht bind error exceeds generic tolerance");
                Check(!h.Apply(1,0,Identity(),a,{goht},true,vertices)[0],"Goht skin first frame stays native");
                const auto b=set(1,bones);const auto half=h.Apply(2,33,Identity(),b,{goht},true,vertices);
                for(unsigned slot=0;slot<bones.size();++slot) {
                    auto expected=b[slot];expected[0][3]-=2;
                    Check(half[slot]&&Near(*half[slot],expected,1e-5),"Goht head/body midpoint preserves native bind correction");
                }
                auto wrong=b;wrong[1][0][3]+=.1;
                Check(!L::GohtSkinBones(wrong,goht,vertices),"Goht matching requires every active slot");
                wrong=b;wrong[0][0][0]+=.00002;
                Check(!L::GohtSkinBones(wrong,goht,vertices),"Goht rejects unrelated basis transforms");
                Check(!L::GohtSkinBones(b,goht,vertices+1),"Goht rejects unknown mesh sizes");
                auto other=goht;other.signature++;
                Check(!L::GohtSkinBones(b,other,vertices),"Goht bounds do not apply to another model");
                auto duplicate=goht;duplicate.pose.instance++;
                Check(!h.Apply(3,66,Identity(),b,{goht,duplicate},true,vertices)[0],"Goht skin actor ownership remains unique");
                Check(!h.Apply(4,99,Identity(),b,{goht},true,vertices)[0],"Goht skin frame gaps stay native");
            }
            L::History h;
            const auto collapsed=[&](double t) {
                auto p=set(t,{1,15,3});auto hidden=Expand(goht.pose.bones[15]);
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)hidden[r][c]=0;
                goht.pose.bones[15]=Flatten(hidden);p[1]=hidden;return p;
            };
            auto a=collapsed(0);h.Apply(1,0,Identity(),a,{goht},true,750);
            auto b=collapsed(1);const auto half=h.Apply(2,33,Identity(),b,{goht},true,750);
            Check(half[0]&&!half[1]&&half[2],"Goht hidden bone stays collapsed while visible slots interpolate");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto goht=pose(29,439,0x496431680f850b7full);
            const auto set=[&](double t) {
                for(unsigned i=0;i<29;++i)goht.pose.bones[i]=Flatten(Camera(0,i*20+t*4));
                for(unsigned i:{2u,18u,24u})goht.pose.bones[i]=goht.pose.bones[1];
                L::Palette p;p.fill(Camera(0,99999));
                p[0]=Expand(goht.pose.bones[1]);p[1]=Expand(goht.pose.bones[5]);return p;
            };
            for(unsigned vertices:{12u,129u,150u,219u,750u,756u,1878u}) {
                L::History h;const auto a=set(0);
                Check(!L::Identify(a[0],goht),"Goht shared body bones are ambiguous to generic identification");
                Check(!h.Apply(1,0,Identity(),a,{goht},true,vertices)[0],"Goht first frame seeds history");
                const auto b=set(1);const auto mid=h.Apply(2,33,Identity(),b,{goht},true,vertices);
                Check(mid[0]&&Near(*mid[0],Camera(0,22),1e-5),"Goht shared body bones interpolate by consensus");
                Check(mid[1]&&Near(*mid[1],Camera(0,102),1e-5),"Goht remaining palette bones share the midpoint");
                Check(!h.Apply(2,33,Identity(),b,{goht},false,vertices)[0],"disabled Goht interpolation remains native");
                auto duplicate=goht;duplicate.pose.instance++;
                Check(!h.Apply(3,66,Identity(),b,{goht,duplicate},true,vertices)[0],"coincident Goht actors cannot borrow histories");
                Check(!h.Apply(4,99,Identity(),b,{goht},true,vertices)[0],"Goht rejected frame forces reacquisition");
            }
            L::History h;auto a=set(0);goht.pose.bones[24]=Flatten(Camera(0,19));
            h.Apply(1,0,Identity(),a,{goht},true,1878);
            auto b=set(1);
            Check(!h.Apply(2,33,Identity(),b,{goht},true,1878)[0],"divergent Goht midpoint bones remain rejected");
            h.Reset();a=set(0);h.Apply(1,0,Identity(),a,{goht},true,1879);b=set(1);
            Check(!h.Apply(2,33,Identity(),b,{goht},true,1879)[0],"unverified Goht meshes excluded");
            h.Reset();goht.signature=123;a=set(0);h.Apply(1,0,Identity(),a,{goht},true,1878);b=set(1);
            Check(!h.Apply(2,33,Identity(),b,{goht},true,1878)[0],"other 29-bone actors excluded");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            for(const auto [signature,bones,vertices]:std::array{
                std::array<std::uint64_t,3>{0x71a6a9af29d2e93full,20,276},
                std::array<std::uint64_t,3>{0x2839c49538bf9e2aull,3,12}}) {
                auto actor=pose(unsigned(bones),438,signature);
                actor.pose.bones.assign(bones,Flatten(Camera(0,20)));
                L::Palette a;a.fill(Camera(0,99999));a[0]=Camera(0,20);
                L::History history;history.Apply(1,0,Identity(),a,{actor},true,unsigned(vertices));
                actor.pose.bones.assign(bones,Flatten(Camera(0,24)));a[0]=Camera(0,24);
                const auto mid=history.Apply(2,33,Identity(),a,{actor},true,unsigned(vertices));
                Check(mid[0]&&Near(*mid[0],Camera(0,22),1e-5),"flag/flame equivalent bones agree on midpoint");
                auto duplicate=actor;duplicate.pose.instance++;
                Check(!history.Apply(3,66,Identity(),a,{actor,duplicate},true,unsigned(vertices))[0],
                      "coincident flag/flame actors remain ambiguous");
            }
            {
                auto rock=pose(1,436,0xd67c27ec27487acbull),shadow=pose(1,437,0xc16cd06a278b5f00ull);
                L::Palette p;p.fill(Camera(0,99999));p[0]=Camera(0,20);
                rock.pose.bones[0]=shadow.pose.bones[0]=Flatten(p[0]);L::History history;
                history.Apply(1,0,Identity(),p,{rock,shadow},true,210);
                p[0]=Camera(0,26);rock.pose.bones[0]=shadow.pose.bones[0]=Flatten(p[0]);
                const auto mid=history.Apply(2,33,Identity(),p,{rock,shadow},true,210);
                Check(mid[0]&&Near(*mid[0],Camera(0,23),1e-5),"snow boulder half cannot be blocked by its shadow");
                auto duplicate=rock;duplicate.pose.instance+=10;
                Check(!history.Apply(3,66,Identity(),p,{rock,duplicate},true,210)[0],"coincident boulder halves rejected");
            }
            auto door=pose(1,439,0x57306c6ee1db06efull);
            auto binding=Identity();binding[2][3]=-.0006294240010902286;
            door.bind[0]=Flatten(binding);
            const auto set=[&](double x) {
                door.pose.bones[0]=Flatten(Camera(0,x));
                L::Palette p;p.fill(Camera(0,99999));p[0]=Expand(door.pose.bones[0]);return p;
            };
            L::History h;auto a=set(20);
            Check(!L::Identify(a[0],door),"shutter's tiny bind offset makes direct/skin ambiguous");
            Check(!h.Apply(1,0,Identity(),a,{door},true,36)[0],"shutter seeds history");
            auto b=set(28);const auto half=h.Apply(2,33,Identity(),b,{door},true,36);
            Check(half[0] && Near(*half[0],Camera(0,24),.00001),"sliding dungeon shutter gets midpoint");
            const auto still=h.Apply(3,66,Identity(),b,{door},true,36);
            Check(still[0] && Near(*still[0],b[0],.00001),"stationary shutter does not shift");
            auto duplicate=door;duplicate.pose.instance++;
            Check(!h.Apply(4,99,Identity(),b,{door,duplicate},true,36)[0],"overlapping shutters rejected");
            Check(!h.Apply(5,132,Identity(),b,{door},true,36)[0],"shutter gap reseeds");
            h.Reset();a=set(20);h.Apply(1,0,Identity(),a,{door},true,37);b=set(28);
            Check(!h.Apply(2,33,Identity(),b,{door},true,37)[0],"other shutter meshes excluded");
            door.signature=0x0425b0994521dcd5ull;
            h.Reset();a=set(20);h.Apply(1,0,Identity(),a,{door},true,36);b=set(28);
            const auto snowhead=h.Apply(2,33,Identity(),b,{door},true,36);
            Check(snowhead[0] && Near(*snowhead[0],Camera(0,24),.00001),"Snowhead shutter uses its direct bone");
            door.signature++;h.Reset();a=set(20);h.Apply(1,0,Identity(),a,{door},true,36);b=set(28);
            Check(!h.Apply(2,33,Identity(),b,{door},true,36)[0],"other one-bone models retain ambiguity guard");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            for(bool jet:{false,true}) {
                auto water=pose(jet?6:4,440,jet?0xbfecd47d742679fdull:0x6a038b2b40fd9fbfull);
                for(unsigned vertices:jet?std::array{216u,252u,108u}:std::array{138u,96u,72u}) {
                    L::History h;L::Palette p;p.fill(Camera(0,99999));p[0]=Camera(0,40);
                    water.pose.bones.assign(water.pose.bones.size(),Flatten(p[0]));
                    Check(!h.Apply(1,0,Identity(),p,{water},true,vertices)[0],"Great Bay water skeleton seeds");
                    p[0]=Camera(0,44);water.pose.bones.assign(water.pose.bones.size(),Flatten(p[0]));
                    const auto half=h.Apply(2,33,Identity(),p,{water},true,vertices);
                    Check(half[0] && Near(*half[0],Camera(0,42),.00001),"equivalent Great Bay water bones interpolate by consensus");
                    auto duplicate=water;duplicate.pose.instance++;
                    Check(!h.Apply(3,66,Identity(),p,{water,duplicate},true,vertices)[0],"coincident water instances rejected");
                }
            }
        }
        {
            auto rupee=pose(7,321,0xdf35e5b3de06d85eull);
            const auto transform=[](double angle,double x) {
                auto m=Camera(angle,x);
                for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)m[r][c]*=.015;
                return m;
            };
            MM3D::LinkInterpolation::Palette p;p.fill(transform(0,0));
            rupee.pose.bones.assign(7,Flatten(p[0]));
            MM3D::LinkInterpolation::History h;
            Check(!h.Apply(1,0,Identity(),p,{rupee},true)[0],"rupee needs history");
            p.fill(transform(60,4));rupee.pose.bones.assign(7,Flatten(p[0]));
            auto mid=h.Apply(2,33,Identity(),p,{rupee},true);
            Check(mid[0] && Near(*mid[0],transform(30,2),1e-6),"small scaled rupee rotates and moves at midpoint");
            Check(!h.Apply(2,33,Identity(),p,{rupee},false)[0],"disabled interpolation leaves rupee native");
            auto duplicate=rupee;duplicate.pose.instance++;
            Check(!h.Apply(3,66,Identity(),p,{rupee,duplicate},true)[0],"overlapping rupee instances rejected");
            Check(!h.Apply(4,99,Identity(),p,{rupee},true)[0],"rupee gap resets history");
            MM3D::LinkInterpolation::History divergent;
            rupee.pose.bones[1]=Flatten(transform(-60,4));
            divergent.Apply(1,0,Identity(),p,{rupee},true);
            rupee.pose.bones.assign(7,Flatten(p[0]));
            Check(!divergent.Apply(2,33,Identity(),p,{rupee},true)[0],"divergent rupee bones stay native");
            rupee.signature++;
            Check(!h.Apply(5,132,Identity(),p,{rupee},true)[0],"unverified equivalent-bone model rejected");
        }
        {
            MM3D::LinkInterpolation::History h;
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
            // Clock instances plus the reset items must fit, while the history
            // still rejects an unbounded stream and expires old instances.
            for(unsigned frame=1;frame<=2;++frame)for(unsigned i=0;i<257;++i) {
                auto item=pose(1,10000+i,0x3fb703d154538323ull);
                item.pose.bones[0]=Flatten(Camera(0,i*10+frame*2));p[0]=Expand(item.pose.bones[0]);
                const auto half=h.Apply(frame,(frame-1)*33,Identity(),p,{item},true,24);
                if(frame==1 || i==256)Check(!half[0],"actor histories seed once and stay bounded");
                else Check(half[0] && Near(*half[0],Camera(0,i*10+3),1e-6),"busy reset scene retains each actor history");
            }
            auto later=pose(1,11000,1);p[0]=Identity();
            Check(!h.Apply(5,132,Identity(),p,{later},true,24)[0],"expired actors free history capacity");
            p[0]=Camera(0,4);later.pose.bones[0]=Flatten(p[0]);
            const auto half=h.Apply(6,165,Identity(),p,{later},true,24);
            Check(half[0] && Near(*half[0],Camera(0,2),1e-6),"new actors interpolate after old histories expire");
        }
        {
            auto bush=pose(1,12000,0x5985261146a459c5ull);
            MM3D::LinkInterpolation::History h;
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
            const auto set=[&](const Matrix& camera,const Matrix& world) {
                p[0]=Multiply(InverseRigid(camera),world);bush.pose.bones[0]=Flatten(p[0]);
            };
            set(Identity(),Camera(0,10));
            Check(!h.Apply(1,0,Identity(),p,{bush},true,48)[0],"standing bush seeds history");
            const auto camera=Camera(60,40);
            set(camera,Camera(20,10));
            auto half=h.Apply(2,33,camera,p,{bush},true,48);
            Check(half[0] && Near(*half[0],Multiply(InverseRigid(camera),Camera(10,10)),1e-5),
                  "rooted bush sway interpolates through camera movement");
            set(camera,Camera(40,70));const auto native=p;
            Check(!h.Apply(3,66,camera,p,{bush},true,48)[0],"reused bush slot cannot blend from another patch");
            Check(p==native,"bush reassignment leaves current native placement intact");
            Check(!h.Apply(3,66,camera,p,{bush},true,48)[0],"repeated reassigned bush draw remains native");
            set(camera,Camera(60,70));
            half=h.Apply(4,99,camera,p,{bush},true,48);
            Check(half[0] && Near(*half[0],Multiply(InverseRigid(camera),Camera(50,70)),1e-5),
                  "bush interpolation resumes after slot settles");
            auto fragment=pose(1,12001,0x6d9919dfd9b0b94dull);
            MM3D::LinkInterpolation::History fragments;
            p[0]=Identity();fragments.Apply(1,0,Identity(),p,{fragment},true,12);
            p[0]=Camera(20,60);fragment.pose.bones[0]=Flatten(p[0]);
            half=fragments.Apply(2,33,Identity(),p,{fragment},true,12);
            Check(half[0] && Near(*half[0],Camera(10,30),1e-5),"moving bush fragments retain ordinary actor interpolation");
        }
        for(unsigned vertices:{96u,12u,306u,30u,126u,402u,180u}) {
            const unsigned count=vertices==96?7:(vertices==12 || vertices==306)?3:4;
            const auto signature=vertices==96?0x9917e505ef9503eaull:
                (vertices==12 || vertices==306)?0x110f9bdb5eb0dd7aull:
                vertices==180?0x3b9945c18d4a600full:0x93dff8d74c226412ull;
            auto item=pose(count,322,signature),other=pose(count,323,signature);
            const auto set=[](LivePose& p,double angle,double x) {
                p.pose.bones.assign(p.pose.bones.size(),Flatten(Camera(angle,x)));
            };
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
            MM3D::LinkInterpolation::History h;
            set(item,0,0);set(other,0,30);p[0]=Camera(0,0);
            Check(!h.Apply(1,0,Identity(),p,{item,other},true,vertices)[0],"reset item seeds own history");
            p[0]=Camera(0,30);h.Apply(1,0,Identity(),p,{item,other},true,vertices);
            set(item,60,4);set(other,-60,40);p[0]=Camera(60,4);
            const auto native=p;
            auto half=h.Apply(2,33,Identity(),p,{item,other},true,vertices);
            Check(half[0] && Near(*half[0],Camera(30,2),1e-6),"lost item movement and rotation interpolate");
            Check(p==native,"reset items preserve native palette");
            p[0]=Camera(-60,40);half=h.Apply(2,33,Identity(),p,{item,other},true,vertices);
            Check(half[0] && Near(*half[0],Camera(-30,35),1e-6),"separate reset items retain independent trajectories");
            Check(!h.Apply(2,33,Identity(),p,{other},true,vertices+1)[0],"unverified reset item mesh excluded");
            Check(!h.Apply(2,33,Identity(),p,{other},false,vertices)[0],"reset item fallback stays native");
            auto duplicate=other;duplicate.pose.instance++;
            Check(!h.Apply(3,66,Identity(),p,{other,duplicate},true,vertices)[0],"coincident reset items stay ambiguous");
            Check(!h.Apply(4,99,Identity(),p,{other},true,vertices)[0],"reset item gap stays native");
            MM3D::LinkInterpolation::History divergent;
            other.pose.bones[1]=Flatten(Camera(60,40));
            divergent.Apply(1,0,Identity(),p,{other},true,vertices);
            set(other,-60,40);
            Check(!divergent.Apply(2,33,Identity(),p,{other},true,vertices)[0],"divergent shared item bones stay native");
            other.signature++;
            Check(!h.Apply(5,132,Identity(),p,{other},true,vertices)[0],"other item signatures excluded");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto strand=pose(10,431,0x5a77df48ac4918b7ull);
            for(unsigned i=1;i<10;++i)strand.words[i]=((i-1)<<16)|0x1000|i;
            const auto chain=[](double t) {
                L::Palette p;
                auto parent=Identity();
                for(unsigned i=0;i<10;++i) {
                    auto joint=Multiply(parent,Camera((20+i)*t,i?2:4*t));
                    parent=joint;
                    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                        joint[r][c]*=c==2?.0175:.035;
                    p[i]=joint;
                }
                return p;
            };
            const auto a=chain(0),b=chain(1),expected=chain(.5);
            const auto set=[&](const L::Palette& p) {
                for(unsigned i=0;i<10;++i)strand.pose.bones[i]=Flatten(p[i]);
            };
            Check(L::MajoraStrand(strand,240),"Majora strand skeleton and mesh recognized");
            Check(!L::MajoraStrand(strand,241),"other strand mesh excluded");
            auto malformed=strand;malformed.words[2]=0x00001002;
            Check(!L::MajoraStrand(malformed,240),"strand topology must match compensated chain");
            auto invalid=a[0];invalid[0][0]=0;
            Check(!L::UnscaledJoint(invalid),"singular parent scale rejected");
            invalid=a[0];invalid[0][1]=.01;
            Check(!L::UnscaledJoint(invalid),"genuine parent shear remains rejected");
            invalid=a[0];invalid[0][3]=std::numeric_limits<double>::quiet_NaN();
            Check(!L::UnscaledJoint(invalid),"nonfinite strand rejected");
            L::History h;set(a);
            Check(!h.Apply(1,0,Identity(),a,{strand},true,240)[0],"strand first frame seeds");
            set(b);const auto result=h.Apply(2,33,Identity(),b,{strand},true,240);
            for(unsigned i=0;i<10;++i)
                Check(result[i] && Near(*result[i],expected[i],1e-5),"all compensated strand joints reach hierarchical midpoint");
            Check(!h.Apply(2,33,Identity(),b,{strand},false,240)[0],"disabled strand interpolation stays native");
            Check(!h.Apply(4,99,Identity(),b,{strand},true,240)[0],"strand frame gaps stay native");
            const auto stationary=h.Apply(5,132,Identity(),b,{strand},true,240);
            for(unsigned i=0;i<10;++i)
                Check(stationary[i] && Near(*stationary[i],b[i],1e-5),"stationary compensated strand reconstructs native pose");
            auto far=b;for(auto& m:far)m[0][3]+=101;set(far);
            Check(!h.Apply(6,165,Identity(),far,{strand},true,240)[0],"strand teleport guard retained");
            strand.signature++;h.Reset();set(a);h.Apply(1,0,Identity(),a,{strand},true,240);
            set(b);const auto other=h.Apply(2,33,Identity(),b,{strand},true,240);
            Check(other[1] && Near(*other[1],b[1],1e-5),"unverified skeleton retains native shear fallback");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto whip=pose(113,434,0x18ccb05513137730ull);
            whip.words[17]=0x000f1011;whip.words[66]=0x00401042;
            for(unsigned i=18;i<=109;++i)if(i<=60 || i>=67)whip.words[i]=((i-1)<<16)|0x1000|i;
            const auto chain=[&](double t) {
                std::vector<Matrix> bones(113);
                for(unsigned i=0;i<113;++i) {
                    auto local=Camera((1+i*.1)*t,i?double(i):t*4);
                    if(i==17 || i==66 || (i>=18 && i<=60) || (i>=67 && i<=109))
                        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)local[r][c]*=c==2?.0175:.035;
                    if(i==0)bones[i]=local;
                    else {
                        const auto parent=bones[whip.words[i]>>16];
                        const bool compensated=(i>=18 && i<=60) || (i>=67 && i<=109);
                        bones[i]=Multiply(compensated?*L::UnscaledJoint(parent):parent,local);
                    }
                }
                return bones;
            };
            const auto set=[&](const std::vector<Matrix>& bones) {
                for(unsigned i=0;i<113;++i)whip.pose.bones[i]=Flatten(bones[i]);
            };
            const auto a=chain(0),b=chain(1),expected=chain(.5);
            const std::array<unsigned,10> left{20,19,18,17,15,36,37,38,39,40};
            const std::array<unsigned,10> right{68,69,67,66,64,100,101,102,99,103};
            const auto palette=[&](const auto& bones,const auto& indices) {
                L::Palette p;p.fill(Camera(0,99999));
                for(unsigned i=0;i<indices.size();++i)p[i]=bones[indices[i]];
                // Native skin correction just beyond generic translation tolerance.
                p[0][1][3]+=.04;
                return p;
            };
            L::History h;set(a);
            auto native=palette(a,left);
            Check(L::MajoraWhipBones(native,whip,672).has_value(),"complete left whip palette recognized despite bind rounding");
            Check(!h.Apply(1,0,Identity(),native,{whip},true,672)[0],"whip first frame seeds history");
            set(b);
            // Seed the second pose via a body draw before submitting the whips.
            L::Palette body;body.fill(Camera(0,99999));body[0]=b[0];
            h.Apply(2,33,Identity(),body,{whip},true,3072);
            for(const auto& indices:{left,right}) {
                native=palette(b,indices);
                const auto half=h.Apply(2,33,Identity(),native,{whip},true,672);
                for(unsigned slot=0;slot<10;++slot) {
                    const auto correction=Multiply(*Inverse(b[indices[slot]]),native[slot]);
                    Check(half[slot] && Near(*half[slot],Multiply(expected[indices[slot]],correction),.001),
                          "both whip chains interpolate even when body draw seeds the pose");
                }
            }
            native=palette(b,left);
            const auto stationary=h.Apply(3,66,Identity(),native,{whip},true,672);
            for(unsigned i=0;i<10;++i)Check(stationary[i] && Near(*stationary[i],native[i],.001),"stationary whip preserves native bind correction");
            Check(!h.Apply(3,66,Identity(),native,{whip},false,672)[0],"disabled whip stays native");
            Check(!h.Apply(5,132,Identity(),native,{whip},true,672)[0],"whip gaps reseed");
            auto duplicate=whip;duplicate.pose.instance++;
            Check(!h.Apply(6,165,Identity(),native,{whip,duplicate},true,672)[0],"coincident whip owners remain ambiguous");
            Check(!L::MajoraWhipBones(native,whip,673),"unverified whip mesh rejected");
            auto bad=native;bad[9][0][3]+=1;
            Check(!L::MajoraWhipBones(bad,whip,672),"every active whip palette slot must match");
            bad=native;bad[0][0][0]+=.001;
            Check(!L::MajoraWhipBones(bad,whip,672),"whip basis retains tight tolerance");
            whip.words[19]=0x00111013;
            Check(!L::MajoraWhipBones(native,whip,672),"unexpected whip topology rejected");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto twin=pose(32,436,0x323a7bd77f6edea5ull);
            twin.words.assign(L::TwinmoldTopology.begin(),L::TwinmoldTopology.end());
            const auto chain=[&](double t) {
                std::vector<Matrix> bones(32);
                for(unsigned i=0;i<32;++i) {
                    auto local=Camera((1+i*.1)*t,i?20+i*3:t*4);
                    if(i==0 || i>=20)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                        local[r][c]*=(i>=20 && i<=22 && c>0)?.9:.6;
                    if(i==0)bones[i]=local;
                    else {
                        const auto parent=bones[twin.words[i]>>16];
                        bones[i]=Multiply(i>=20?*L::UnscaledJoint(parent):parent,local);
                    }
                }
                return bones;
            };
            const auto set=[&](const auto& bones) {for(unsigned i=0;i<32;++i)twin.pose.bones[i]=Flatten(bones[i]);};
            const auto a=chain(0),b=chain(1),expected=chain(.5);
            const auto palette=[&](const auto& bones,const std::vector<unsigned>& indices) {
                L::Palette p;p.fill(Camera(0,99999));
                for(unsigned i=0;i<indices.size();++i)p[i]=bones[indices[i]];
                p[0][0][0]+=.0003;p[0][1][3]+=.06;return p;
            };
            std::vector<std::pair<unsigned,std::vector<unsigned>>> meshes;
            for(unsigned i=19;i<31;++i)meshes.push_back({300,{i,i+1,i==19?1:i-1}});
            meshes.insert(meshes.end(),{{2529,{16,1,4,15,6,19}},{504,{11,13,14,1,7,9,10}},
                {510,{1,19}},{675,{30,31}},{360,{31,30}},{105,{30,31}},{912,{1,17}}});
            for(const auto& [vertices,indices]:meshes) {
                L::History h;set(a);auto native=palette(a,indices);
                Check(L::TwinmoldBones(native,twin,vertices).has_value(),"verified Twinmold skin palette recognized");
                Check(!h.Apply(1,0,Identity(),native,{twin},true,vertices)[0],"Twinmold first frame seeds");
                set(b);native=palette(b,indices);
                // A rigid body draw can seed the frame before a skin segment.
                L::Palette body;body.fill(Camera(0,99999));body[0]=b[0];
                h.Apply(2,33,Identity(),body,{twin},true,216);
                const auto result=h.Apply(2,33,Identity(),native,{twin},true,vertices);
                for(unsigned i=0;i<indices.size();++i) {
                    const auto correction=Multiply(*Inverse(b[indices[i]]),native[i]);
                    Check(result[i] && Near(*result[i],Multiply(expected[indices[i]],correction),.001),
                          "Twinmold segments and head use compensated hierarchical midpoint");
                }
                const auto still=h.Apply(3,66,Identity(),native,{twin},true,vertices);
                for(unsigned i=0;i<indices.size();++i)
                    Check(still[i] && Near(*still[i],native[i],.001),"stationary Twinmold retains native skin correction");
                Check(!h.Apply(3,66,Identity(),native,{twin},false,vertices)[0],"disabled Twinmold stays native");
                Check(!h.Apply(5,132,Identity(),native,{twin},true,vertices)[0],"Twinmold gaps reseed");
                auto duplicate=twin;duplicate.pose.instance++;
                Check(!h.Apply(6,165,Identity(),native,{twin,duplicate},true,vertices)[0],"coincident Twinmolds rejected");
                auto bad=native;bad[indices.size()-1][0][3]+=1;
                Check(!L::TwinmoldBones(bad,twin,vertices),"entire Twinmold palette required");
                bad=native;bad[0][0][0]+=.001;
                Check(!L::TwinmoldBones(bad,twin,vertices),"Twinmold larger basis mismatch rejected");
                bad=native;bad[0][0][0]=std::numeric_limits<double>::quiet_NaN();
                Check(!L::TwinmoldBones(bad,twin,vertices),"nonfinite Twinmold palette rejected");
                auto wrong=twin;wrong.signature++;
                Check(!L::TwinmoldBones(native,wrong,vertices),"unrelated actor cannot use Twinmold tolerance");
                wrong=twin;wrong.words[22]=22;
                Check(!L::TwinmoldBones(native,wrong,vertices),"malformed Twinmold hierarchy rejected");
                Check(!L::TwinmoldBones(native,twin,vertices+1),"unknown Twinmold mesh rejected");
            }
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto eye=pose(22,438,0x2162c5636174fb2cull);
            eye.words={0xffff0000,0x1001,0x11002,0x21003,0x21004,0x41005,0x21006,0x21007,
                0x21008,0x81009,0x8100a,0x2100b,0xb100c,0xb100d,0x100e,0xe100f,
                0xf1010,0x101011,0xe1012,0x121013,0x131014,0xe1015};
            const std::vector<std::pair<unsigned,std::vector<unsigned>>> meshes{
                {1050,{2,6,12,9,13,10}},{888,{2,1,14,21,7,18,15}},
                {594,{19,20,16,17,7,18,15}},{162,{21,14}},
                {192,{2,3}},{30,{2,4,5}},{357,{14,21,11,8}}};
            // Every joint coincides at rest: ordinary per-slot ownership is
            // ambiguous, but verified whole-mesh ordering resolves it.
            const auto chain=[&](double t) {
                std::vector<Matrix> bones(22);
                for(unsigned i=0;i<22;++i) {
                    const auto local=Camera(t*(i+1),i?t:4*t);
                    bones[i]=i?Multiply(bones[eye.words[i]>>16],local):local;
                }
                return bones;
            };
            const auto a=chain(0),b=chain(1),expected=chain(.5);
            const auto set=[&](const auto& bones,const Matrix& camera) {
                for(unsigned i=0;i<22;++i)eye.pose.bones[i]=Flatten(Multiply(InverseRigid(camera),bones[i]));
            };
            const auto palette=[&](const auto& indices) {
                L::Palette p;p.fill(Camera(0,99999));
                for(unsigned i=0;i<indices.size();++i)p[i]=Expand(eye.pose.bones[indices[i]]);
                p[0][0][3]+=.02;return p;
            };
            for(const auto& [vertices,indices]:meshes) {
                L::History h;set(a,Identity());auto native=palette(indices);
                Check(L::EyegoreBones(native,eye,vertices).has_value(),"Eyegore coincident joints match full mesh");
                Check(!h.Apply(1,0,Identity(),native,{eye},true,vertices)[0],"Eyegore first frame seeds");
                const auto camera=Camera(15,10);set(b,camera);native=palette(indices);
                const auto result=h.Apply(2,33,camera,native,{eye},true,vertices);
                for(unsigned i=0;i<indices.size();++i) {
                    const auto correction=Multiply(*Inverse(Expand(eye.pose.bones[indices[i]])),native[i]);
                    Check(result[i] && Near(*result[i],Multiply(Multiply(InverseRigid(camera),expected[indices[i]]),correction),.001),
                        "Eyegore full hierarchy interpolates through camera motion and bind correction");
                }
                auto duplicate=eye;duplicate.pose.instance++;
                Check(!h.Apply(3,66,camera,native,{eye,duplicate},true,vertices)[0],"coincident Eyegore instances remain ambiguous");
                Check(!h.Apply(4,99,camera,native,{eye},false,vertices)[0],"disabled Eyegore stays native");
                Check(!h.Apply(6,165,camera,native,{eye},true,vertices)[0],"Eyegore gap reseeds");
                auto bad=native;bad[indices.size()-1][0][3]+=1;
                Check(!L::EyegoreBones(bad,eye,vertices),"every Eyegore palette slot must match");
                bad=native;bad[0][0][0]=std::numeric_limits<double>::quiet_NaN();
                Check(!L::EyegoreBones(bad,eye,vertices),"nonfinite Eyegore slot rejected");
                auto wrong=eye;wrong.signature++;
                Check(!L::EyegoreBones(native,wrong,vertices),"unrelated skeleton excluded from Eyegore mapping");
                wrong=eye;wrong.words[3]=3;
                Check(!L::EyegoreBones(native,wrong,vertices),"Eyegore full topology verified");
                Check(!L::EyegoreBones(native,eye,vertices+1),"unknown Eyegore mesh excluded");
            }
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto twin=pose(32,439,0x323a7bd77f6edea5ull);
            twin.words.assign(L::TwinmoldTopology.begin(),L::TwinmoldTopology.end());
            const auto set=[&](double x,const Matrix& camera) {
                for(auto& bone:twin.pose.bones)bone=Flatten(Multiply(InverseRigid(camera),Camera(0,x)));
                L::Palette p;p.fill(Camera(0,99999));p[0]=p[1]=Expand(twin.pose.bones[1]);return p;
            };
            L::History h;auto p=set(0,Identity());h.Apply(1,0,Identity(),p,{twin},true,510);
            const auto camera=Camera(10,500);p=set(150,camera);
            auto half=h.Apply(2,33,camera,p,{twin},true,510);
            Check(half[0] && Near(Multiply(camera,*half[0]),Camera(0,75),.001),"Twinmold fast swing remains continuous in world space");
            p=set(400,camera);
            Check(!h.Apply(3,66,camera,p,{twin},true,510)[0],"Twinmold teleport still reseeds");
            auto a=Identity(),b=Camera(5);a[0][1]=.01;b[0][1]=.02;
            Check(!Half(a,b) && L::TwinmoldHeadHalf(a,b,twin,18),"Twinmold terminal head shear has bounded fallback");
            Check(!L::TwinmoldHeadHalf(a,b,twin,17),"Twinmold affine fallback excludes other joints");
            b[0][1]=2;Check(!L::TwinmoldHeadHalf(a,b,twin,18),"large Twinmold deformation rejected");
            twin.words[18]=18;Check(!L::TwinmoldHeadHalf(a,a,twin,18),"Twinmold head fallback verifies topology");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto giant=pose(26,437,0x8025d822f84e8240ull);
            giant.words.assign(MM3D::LinkPoseCapture::BoneWords.begin(),MM3D::LinkPoseCapture::BoneWords.end());
            const auto chain=[&](double t) {
                std::vector<Matrix> bones(26);
                for(unsigned i=0;i<26;++i) {
                    auto local=Camera((1+i*.2)*t,i?10+i*2:t*4);
                    std::array<double,3> scale{1,1,1};
                    if(i==3)scale={1,1.25,1.25};
                    if(i==10)scale={1,1.4,1.4};
                    if(i==17 || i==21)scale={1.7,1.5,1.9};
                    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)local[r][c]*=.023*scale[c];
                    bones[i]=i?Multiply(*L::UnscaledJoint(bones[giant.words[i]>>16]),local):local;
                }
                return bones;
            };
            const auto set=[&](const auto& bones) {for(unsigned i=0;i<26;++i)giant.pose.bones[i]=Flatten(bones[i]);};
            const auto a=chain(0),b=chain(1),expected=chain(.5);
            const auto palette=[&](const auto& bones,const std::vector<unsigned>& indices) {
                L::Palette p;p.fill(Camera(0,99999));
                for(unsigned i=0;i<indices.size();++i)p[i]=bones[indices[i]];
                p[0][0][0]+=.00002;p[0][1][3]+=.05;return p;
            };
            const std::vector<std::pair<unsigned,std::vector<unsigned>>> meshes{
                {234,{17,16}},{234,{21,20}},{318,{17,16}},{318,{21,20}},
                {423,{9,14,15,10,16,17}},{747,{7,8,19,6,20,21}}};
            for(const auto& [vertices,indices]:meshes) {
                L::History h;set(a);auto native=palette(a,indices);
                Check(L::GiantLinkSkeleton(giant),"Giant Link proportions recognized");
                Check(L::GiantLinkBones(native,giant,vertices).has_value(),"Giant Link hand palettes recognized");
                Check(!h.Apply(1,0,Identity(),native,{giant},true,vertices)[0],"Giant Link seeds history");
                set(b);native=palette(b,indices);
                const auto result=h.Apply(2,33,Identity(),native,{giant},true,vertices);
                for(unsigned i=0;i<indices.size();++i) {
                    const auto correction=Multiply(*Inverse(b[indices[i]]),native[i]);
                    Check(result[i] && Near(*result[i],Multiply(expected[indices[i]],correction),.001),
                          "Giant Link scaled limbs interpolate without inheriting parent distortion");
                }
                const auto still=h.Apply(3,66,Identity(),native,{giant},true,vertices);
                for(unsigned i=0;i<indices.size();++i)
                    Check(still[i] && Near(*still[i],native[i],.001),"stationary Giant Link retains native palette");
                Check(!h.Apply(3,66,Identity(),native,{giant},false,vertices)[0],"disabled Giant Link stays native");
                Check(!h.Apply(5,132,Identity(),native,{giant},true,vertices)[0],"Giant Link gaps reseed");
                auto duplicate=giant;duplicate.pose.instance++;
                Check(!h.Apply(6,165,Identity(),native,{giant,duplicate},true,vertices)[0],"coincident Giant Link instances rejected");
                auto wrong=giant;wrong.signature++;
                Check(!L::GiantLinkSkeleton(wrong),"unrelated scaled skeleton excluded");
                wrong=giant;wrong.words[5]=5;
                Check(!L::GiantLinkSkeleton(wrong),"Giant Link requires human topology");
                wrong=giant;wrong.pose.bones[17]=Flatten(Identity());
                Check(!L::GiantLinkSkeleton(wrong),"ordinary human proportions excluded");
                auto bad=native;bad.back()[0][0]=0;bad[indices.size()-1][0][3]+=1;
                Check(!L::GiantLinkBones(bad,giant,vertices),"every Giant Link palette slot checked");
                bad=native;bad[0][0][0]+=.0001;
                Check(!L::GiantLinkBones(bad,giant,vertices),"Giant Link larger mismatch rejected");
                Check(!L::GiantLinkBones(native,giant,vertices+1),"unknown Giant Link material excluded");
            }
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto giant=pose(15,435,0x369a86dcc57ca979ull);
            const auto set=[&](double t) {
                for(unsigned i=0;i<15;++i)giant.pose.bones[i]=Flatten(Multiply(Camera(20*t,t*4),Camera(0,i*20)));
                L::Palette p;p.fill(Camera(0,99999));
                for(unsigned i=0;i<L::GiantLegPalette.size();++i)p[i]=Expand(giant.pose.bones[L::GiantLegPalette[i]]);
                p[0][0][0]+=.000018;p[0][1][3]+=.04;
                return p;
            };
            L::History h;auto a=set(0);
            Check(L::GiantLegBones(a,giant,1014),"complete giant leg palette accepts captured rounding");
            Check(!h.Apply(1,0,Identity(),a,{giant},true,1014)[0],"giant legs seed history");
            const auto b=set(1),native=b;
            const auto half=h.Apply(2,33,Identity(),b,{giant},true,1014);
            for(unsigned slot=0;slot<L::GiantLegPalette.size();++slot) {
                const auto bone=L::GiantLegPalette[slot];
                const auto correction=Multiply(*Inverse(Expand(giant.pose.bones[bone])),b[slot]);
                Check(half[slot] && Near(*half[slot],Multiply(Multiply(Camera(10,2),Camera(0,bone*20)),correction),.001),
                      "giant leg motion interpolates with native correction retained");
            }
            Check(b==native,"giant native palette remains unchanged");
            const auto stationary=h.Apply(3,66,Identity(),b,{giant},true,1014);
            for(unsigned i=0;i<7;++i)Check(stationary[i] && Near(*stationary[i],b[i],.00001),"stationary giant legs do not shift");
            Check(!h.Apply(3,66,Identity(),b,{giant},false,1014)[0],"disabled giant legs stay native");
            auto other=giant;other.pose.instance++;
            Check(!h.Apply(4,99,Identity(),b,{giant,other},true,1014)[0],"coincident giant instances stay ambiguous");
            Check(!h.Apply(5,132,Identity(),b,{giant},true,1014)[0],"giant gaps stay native");
            Check(!L::GiantLegBones(b,giant,1015),"other giant draw counts excluded");
            auto bad=b;bad[6][2][3]+=1;
            Check(!L::GiantLegBones(bad,giant,1014),"all seven giant bones required");
            bad=b;bad[0][0][0]+=.0001;
            Check(!L::GiantLegBones(bad,giant,1014),"larger giant mismatch rejected");
            giant.signature++;
            Check(!L::GiantLegBones(b,giant,1014),"unrelated skeleton cannot use giant tolerance");
        }
        {
            namespace L=MM3D::LinkInterpolation;
            for(bool aroma:{false,true}) {
                const unsigned count=aroma?26:22,vertices=aroma?144:30;
                auto npc=pose(count,436,aroma?0x7900d5513a1e4e5dull:0x2215c979d3103929ull);
                const std::vector<unsigned> indices=aroma?std::vector<unsigned>{15,16,24,10,11}:
                    std::vector<unsigned>{1,3};
                const auto set=[&](double t) {
                    for(unsigned i=0;i<count;++i)npc.pose.bones[i]=Flatten(Camera(0,i*20+t*4));
                    if(aroma) {npc.pose.bones[16]=npc.pose.bones[15];npc.pose.bones[11]=npc.pose.bones[10];}
                    else {npc.pose.bones[2]=npc.pose.bones[1];npc.pose.bones[5]=npc.pose.bones[3];}
                    L::Palette p;p.fill(Camera(0,99999));
                    for(unsigned slot=0;slot<indices.size();++slot)p[slot]=Expand(npc.pose.bones[indices[slot]]);
                    return p;
                };
                L::History h;auto a=set(0);
                Check(!L::Identify(a[0],npc),"NPC slot zero is ambiguous between coincident bones");
                Check(L::NpcSkinBones(a,npc,vertices).has_value(),"complete verified NPC palette establishes identity");
                Check(!h.Apply(1,0,Identity(),a,{npc},true,vertices)[0],"NPC first frame seeds");
                const auto b=set(1),native=b;
                const auto half=h.Apply(2,33,Identity(),b,{npc},true,vertices);
                for(unsigned slot=0;slot<indices.size();++slot)
                    Check(half[slot]&&Near(*half[slot],Camera(0,a[slot][0][3]+2),.00001),"verified NPC mesh interpolates");
                Check(b==native,"NPC native draw unchanged");
                const auto stationary=h.Apply(3,66,Identity(),b,{npc},true,vertices);
                for(unsigned slot=0;slot<indices.size();++slot)
                    Check(stationary[slot]&&Near(*stationary[slot],b[slot],.00001),"stationary NPC mesh stays fixed");
                auto duplicate=npc;duplicate.pose.instance++;
                Check(!h.Apply(4,99,Identity(),b,{npc,duplicate},true,vertices)[0],"coincident NPC instances remain rejected");
                Check(!h.Apply(5,132,Identity(),b,{npc},true,vertices)[0],"NPC capture gaps reseed");
                auto bad=b;bad[indices.size()-1][0][3]+=1;
                Check(!L::NpcSkinBones(bad,npc,vertices),"all active NPC palette slots must match");
                Check(!L::NpcSkinBones(b,npc,vertices+1),"unknown NPC material excluded");
                npc.signature++;
                Check(!L::NpcSkinBones(b,npc,vertices),"other models cannot borrow NPC palette identity");
            }
        }
        {
            namespace L=MM3D::LinkInterpolation;
            auto smith=pose(20,437,0xc4bab09e9917b4aeull);
            const auto set=[&](double t) {
                for(unsigned i=0;i<20;++i)smith.pose.bones[i]=Flatten(Camera(0,i*20+t*4));
                smith.pose.bones[12]=smith.pose.bones[11];smith.pose.bones[13]=smith.pose.bones[11];
                L::Palette p;p.fill(Camera(0,99999));p[0]=Expand(smith.pose.bones[12]);return p;
            };
            for(unsigned vertices:{36u,72u,78u,360u}) {
                L::History h;const auto a=set(0);
                Check(!L::Identify(a[0],smith),"Gabora head bones coincide");
                Check(!h.Apply(1,0,Identity(),a,{smith},true,vertices)[0],"Gabora head seeds history");
                const auto b=set(1);
                const auto half=h.Apply(2,33,Identity(),b,{smith},true,vertices);
                Check(half[0]&&Near(*half[0],Camera(0,222),.00001),"Gabora shared head bones interpolate by consensus");
                auto duplicate=smith;duplicate.pose.instance++;
                Check(!h.Apply(3,66,Identity(),b,{smith,duplicate},true,vertices)[0],"coincident smith instances stay ambiguous");
                Check(!h.Apply(4,99,Identity(),b,{smith},true,vertices)[0],"smith capture gaps reseed");
            }
            L::History h;auto a=set(0);smith.pose.bones[13]=Flatten(Camera(0,219));
            h.Apply(1,0,Identity(),a,{smith},true,360);
            const auto b=set(1);
            Check(!h.Apply(2,33,Identity(),b,{smith},true,360)[0],"divergent smith head histories cannot borrow a midpoint");
            h.Reset();a=set(0);h.Apply(1,0,Identity(),a,{smith},true,361);set(1);
            Check(!h.Apply(2,33,Identity(),b,{smith},true,361)[0],"unknown smith materials remain excluded");
        }
        {
            auto like=pose(5,432,0xe5524c5eb3c8c915ull);
            auto a=Identity();a[0][1]=.15;
            auto b=a;b[0][1]=.25;b[1][3]=10;
            auto expected=a;expected[0][1]=.20;expected[1][3]=5;
            Check(!Half(a,b),"Like Like shear is not a rigid scaled rotation");
            const auto mid=MM3D::LinkInterpolation::LikeLikeHalf(a,b,like);
            Check(mid && Near(*mid,expected,1e-10),"Like Like shear and translation blend");
            auto jump=b;jump[0][0]=2;
            Check(!MM3D::LinkInterpolation::LikeLikeHalf(a,jump,like),"Like Like scale jump rejected");
            jump=b;jump[1][3]=250;
            Check(!MM3D::LinkInterpolation::LikeLikeHalf(a,jump,like),"Like Like joint jump rejected");
            jump=b;jump[2][2]=0;
            Check(!MM3D::LinkInterpolation::LikeLikeHalf(a,jump,like),"Like Like singular joint rejected");
            like.signature++;
            Check(!MM3D::LinkInterpolation::LikeLikeHalf(a,b,like),"other actors retain shear rejection");
        }
        {
            auto slime=pose(1,433,0xf0c54784cce31d97ull);
            auto a=Identity();a[0][0]=a[1][1]=a[2][2]=.01;a[0][1]=.003;
            auto b=a;b[0][1]=.0045;b[1][3]=4;
            auto expected=a;expected[0][1]=.00375;expected[1][3]=2;
            Check(!Half(a,b),"blob death shear cannot use rigid scale interpolation");
            for(unsigned vertices:{18u,444u,612u}) {
                const auto mid=MM3D::LinkInterpolation::SlimeHalf(a,b,slime,vertices);
                const auto reverse=MM3D::LinkInterpolation::SlimeHalf(b,a,slime,vertices);
                Check(mid && reverse && Near(*mid,expected,1e-10) && Near(*reverse,expected,1e-10),
                      "blob death deformation interpolates symmetrically");
                MM3D::LinkInterpolation::History h;
                MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));p[0]=a;
                slime.pose.bones[0]=Flatten(a);
                Check(!h.Apply(1,0,Identity(),p,{slime},true,vertices)[0],"blob seeds before interpolation");
                p[0]=b;slime.pose.bones[0]=Flatten(b);
                const auto result=h.Apply(2,33,Identity(),p,{slime},true,vertices);
                Check(result[0] && Near(*result[0],expected,1e-6),
                      "actor replay replaces death wobble with affine midpoint");
                Check(!h.Apply(2,33,Identity(),p,{slime},false,vertices)[0],
                      "disabled blob interpolation remains native");
                Check(!h.Apply(4,99,Identity(),p,{slime},true,vertices)[0],"blob gap resets death history");
            }
            Check(!MM3D::LinkInterpolation::SlimeHalf(a,b,slime,36),
                  "shared one-bone prop signature alone cannot enable death fallback");
            auto invalid=b;invalid[0][0]=0;
            Check(!MM3D::LinkInterpolation::SlimeHalf(a,invalid,slime,444),"collapsed blob rejected");
            invalid=b;invalid[0][0]=-.01;
            Check(!MM3D::LinkInterpolation::SlimeHalf(a,invalid,slime,444),"flipped blob rejected");
            invalid=b;invalid[0][1]=.02;
            Check(!MM3D::LinkInterpolation::SlimeHalf(a,invalid,slime,444),"large deformation rejected");
            invalid=b;invalid[0][3]=101;
            Check(!MM3D::LinkInterpolation::SlimeHalf(a,invalid,slime,444),"blob teleport rejected");
            invalid=b;invalid[0][0]=std::numeric_limits<double>::quiet_NaN();
            Check(!MM3D::LinkInterpolation::SlimeHalf(a,invalid,slime,444),"nonfinite blob rejected");
            slime.signature++;
            Check(!MM3D::LinkInterpolation::SlimeHalf(a,b,slime,444),"other skeletons retain shear guard");
        }
        {
            // Captured Wart bubble roots contain shear, unlike rigid actors.
            auto bubble=pose(1,434,0xb847a2c91407dd5dull);
            auto a=Identity();a[0][0]=a[1][1]=a[2][2]=.13;a[0][1]=.006;
            auto b=a;b[0][1]=.004;b[1][3]=6;
            auto expected=a;expected[0][1]=.005;expected[1][3]=3;
            Check(!Half(a,b),"Wart bubble wobble is not a rigid transform");
            MM3D::LinkInterpolation::History h;
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));p[0]=a;
            bubble.pose.bones[0]=Flatten(a);
            Check(!h.Apply(1,0,Identity(),p,{bubble},true,216)[0],"Wart bubble seeds history");
            p[0]=b;bubble.pose.bones[0]=Flatten(b);
            const auto result=h.Apply(2,33,Identity(),p,{bubble},true,216);
            Check(result[0] && Near(*result[0],expected,1e-6),"Wart movement and wobble interpolate together");
            Check(!h.Apply(2,33,Identity(),p,{bubble},false,216)[0],"Wart toggle preserves native pose");
            Check(!h.Apply(4,99,Identity(),p,{bubble},true,216)[0],"Wart missing frame resets history");
            using MM3D::LinkInterpolation::WartBubbleHalf;
            const auto reverse=WartBubbleHalf(b,a,bubble,216);
            Check(reverse && Near(*reverse,expected,1e-6),"Wart affine blend is symmetric");
            Check(!WartBubbleHalf(a,b,bubble,18),"unverified Wart mesh rejected");
            auto invalid=b;invalid[0][0]=0;
            Check(!WartBubbleHalf(a,invalid,bubble,216),"collapsed Wart bubble rejected");
            invalid=b;invalid[0][0]=-.13;
            Check(!WartBubbleHalf(a,invalid,bubble,216),"Wart scale flip rejected");
            invalid=b;invalid[0][3]=101;
            Check(!WartBubbleHalf(a,invalid,bubble,216),"Wart teleport rejected");
            invalid=b;invalid[0][1]=1;
            Check(!WartBubbleHalf(a,invalid,bubble,216),"large Wart deformation rejected");
            invalid=b;invalid[0][0]=std::numeric_limits<double>::quiet_NaN();
            Check(!WartBubbleHalf(a,invalid,bubble,216),"nonfinite Wart transform rejected");
            auto left=bubble,right=bubble;left.pose.instance=10;right.pose.instance=20;
            auto l=a,r=a;l[0][3]=0;r[0][3]=200;
            left.pose.bones[0]=Flatten(l);right.pose.bones[0]=Flatten(r);
            h.Reset();p[0]=l;h.Apply(1,0,Identity(),p,{left,right},true,216);
            // Model slots exchange positions after the native depth sort.
            l[0][3]=4;r[0][3]=204;
            left.pose.bones[0]=Flatten(r);right.pose.bones[0]=Flatten(l);p[0]=r;
            const auto sorted=h.Apply(2,33,Identity(),p,{left,right},true,216);
            Check(sorted[0] && std::abs((*sorted[0])[0][3]-202)<1e-6,
                "Wart model reorder follows spatial bubble identity");
            p[0]=l;const auto sorted_left=h.Apply(2,33,Identity(),p,{left,right},true,216);
            Check(sorted_left[0] && std::abs((*sorted_left[0])[0][3]-2)<1e-6,
                "both reordered bubbles retain independent histories");
            h.Reset();l[0][3]=0;r[0][3]=1;
            left.pose.bones[0]=Flatten(l);right.pose.bones[0]=Flatten(r);p[0]=l;
            h.Apply(1,0,Identity(),p,{left,right},true,216);
            l[0][3]=.5;left.pose.bones[0]=Flatten(l);p[0]=l;
            Check(!h.Apply(2,33,Identity(),p,{left},true,216)[0],
                "ambiguous Wart crossings remain native");
            bubble.signature++;
            Check(!WartBubbleHalf(a,b,bubble,216),"other actors retain rigid guard");
        }
        {
            auto small=Identity(),large=Identity();
            small[0][0]=small[1][1]=small[2][2]=.4;
            Check(!Half(small,large),"default scale guard rejects fin-sized jump");
            const auto extended=Half(small,large,2.51);
            const auto retracted=Half(large,small,2.51);
            auto expected=Identity();expected[0][0]=expected[1][1]=expected[2][2]=.7;
            Check(extended && Near(*extended,expected,1e-10),"fin extension has intermediate size");
            Check(retracted && Near(*retracted,expected,1e-10),"fin retraction has intermediate size");
            for(const auto signature:{0x42c4c515d83599dbull,0x42c4c515d83599dcull}) {
                auto zora=pose(28,765,signature);
                for(unsigned bone:{24u,25u}) {
                    auto from=small,to=large;from[0][3]=to[0][3]=24;
                    zora.pose.bones[bone]=Flatten(from);
                    MM3D::LinkInterpolation::Palette palette;palette.fill(from);
                    MM3D::LinkInterpolation::ActorHistory history;
                    history.Apply(1,0,Identity(),palette,zora,true);
                    zora.pose.bones[bone]=Flatten(to);palette.fill(to);
                    const auto output=history.Apply(2,33,Identity(),palette,zora,true);
                    const double size=signature==0x42c4c515d83599dbull?.7:1.;
                    Check(output[0] && std::abs((*output[0])[0][0]-size)<1e-6,
                          "fin exception applies only to verified Zora bones");
                    zora.pose.bones[bone]=Flatten(Identity());
                }
            }
            small[0][0]=.1;
            Check(!Half(small,large,2.51),"larger fin scale jump remains rejected");
            small[0][0]=0;
            Check(!Half(small,large,2.51),"collapsed fin stays rejected");
        }
        {
            auto zora=pose(28,876,0x42c4c515d83599dbull);
            auto arm=Camera(20,15);auto other=Camera(-20,-15);
            zora.pose.bones[16]=zora.pose.bones[24]=Flatten(arm);
            zora.pose.bones[20]=zora.pose.bones[25]=Flatten(other);
            std::vector<Matrix> half(28,Identity());
            half[16]=Camera(10,7);half[24]=Camera(9,6);
            half[20]=Camera(-10,-7);half[25]=Camera(-9,-6);
            for(const auto pair:{std::array<unsigned,2>{16,24},std::array<unsigned,2>{20,25}}) {
                const auto current=Expand(zora.pose.bones[pair[0]]);
                const auto mid=MM3D::LinkInterpolation::ExtendedZoraFin(current,zora,half,Identity(),120);
                Check(mid && Near(*mid,half[pair[0]],1e-10),"extended blade follows forearm hierarchy");
                Check(!MM3D::LinkInterpolation::ExtendedZoraFin(current,zora,half,Identity(),144),
                      "other Zora geometry is excluded");
            }
            zora.pose.bones[24][0]*=.4;
            Check(!MM3D::LinkInterpolation::ExtendedZoraFin(arm,zora,half,Identity(),120),
                  "retracted fin cannot use extended arm attachment");
            zora.signature++;
            Check(!MM3D::LinkInterpolation::ExtendedZoraFin(other,zora,half,Identity(),120),
                  "non-Zora actor cannot use arm attachment");
        }
        for(const auto signature:{0xce5abb274d304de4ull,0x69e723ba0b961e6bull,0x04e3fb346228fd20ull,0xd56b42acae52759full,0x7526de611fb49ce3ull,0x3ed3e32e1ee38993ull,0x2507dd32b95b7c14ull}) {
            const unsigned count=(signature==0xce5abb274d304de4ull ||
                                  signature==0x3ed3e32e1ee38993ull) ? 3 :
                signature==0xd56b42acae52759full ? 7 :
                signature==0x7526de611fb49ce3ull ? 8 :
                signature==0x2507dd32b95b7c14ull ? 24 : 4;
            auto bubble=pose(count,654,signature);
            const auto transform=[](double scale,double x) {
                auto m=Identity();m[0][0]=m[2][2]=scale;m[1][1]=scale*.95;
                m[0][3]=x;return m;
            };
            MM3D::LinkInterpolation::Palette p;p.fill(transform(.02,0));
            bubble.pose.bones.assign(count,Flatten(p[0]));
            MM3D::LinkInterpolation::History h;
            Check(!h.Apply(1,0,Identity(),p,{bubble},true)[0],"bubble seeds history");
            p.fill(transform(.03,12));bubble.pose.bones.assign(count,Flatten(p[0]));
            auto mid=h.Apply(2,33,Identity(),p,{bubble},true);
            Check(mid[0] && Near(*mid[0],transform(.025,6),1e-6),"bubble growth and flight interpolate together");
            Check(!h.Apply(2,33,Identity(),p,{bubble},false)[0],"disabled bubble remains native");
            auto duplicate=bubble;duplicate.pose.instance++;
            Check(!h.Apply(3,66,Identity(),p,{bubble,duplicate},true)[0],"ambiguous bubbles rejected");
            Check(!h.Apply(4,99,Identity(),p,{bubble},true)[0],"bubble disappearance resets");
            MM3D::LinkInterpolation::History divergent;
            bubble.pose.bones[1]=Flatten(transform(.02,0));
            divergent.Apply(1,0,Identity(),p,{bubble},true);
            bubble.pose.bones.assign(count,Flatten(p[0]));
            Check(!divergent.Apply(2,33,Identity(),p,{bubble},true)[0],"disagreeing bubble bones rejected");
        }
        {
            auto barrier=pose(3,985,0x6735aa3cacc26ca6ull);
            const auto child=Camera(90);
            barrier.bind[1]=barrier.bind[2]=Flatten(InverseRigid(child));
            const auto update=[&](double angle,double x) {
                barrier.pose.bones[0]=Flatten(Camera(angle,x));
                barrier.pose.bones[1]=barrier.pose.bones[2]=Flatten(Multiply(Camera(angle,x),child));
            };
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
            MM3D::LinkInterpolation::History h;
            update(0,0);p[0]=Expand(barrier.pose.bones[1]);
            Check(!MM3D::LinkInterpolation::Identify(p[0],barrier),
                  "electric barrier's identical sibling bones are individually ambiguous");
            Check(!h.Apply(1,0,Identity(),p,{barrier},true,216)[0],"electric barrier seeds history");
            update(40,4);p[0]=Expand(barrier.pose.bones[1]);
            for(unsigned vertices:{216u,108u}) {
                const auto mid=h.Apply(2,33,Identity(),p,{barrier},true,vertices);
                Check(mid[0] && Near(*mid[0],Multiply(Camera(20,2),child),1e-5),
                      "both electric barrier materials interpolate matching sibling bones");
            }
            Check(!h.Apply(2,33,Identity(),p,{barrier},false,216)[0],"disabled barrier remains native");
            auto duplicate=barrier;duplicate.pose.instance++;
            Check(!h.Apply(3,66,Identity(),p,{barrier,duplicate},true,216)[0],
                  "overlapping electric barriers remain ambiguous");
            Check(!h.Apply(4,99,Identity(),p,{barrier},true,216)[0],"barrier disappearance resets history");
            MM3D::LinkInterpolation::History divergent;
            barrier.pose.bones[2][3]+=6;
            divergent.Apply(1,0,Identity(),p,{barrier},true,216);
            barrier.pose.bones[2][3]-=6;
            Check(!divergent.Apply(2,33,Identity(),p,{barrier},true,216)[0],
                  "barrier siblings with different midpoint trajectories stay native");
            barrier.signature++;
            Check(!h.Apply(5,132,Identity(),p,{barrier},true,216)[0],
                  "unverified three-bone effect retains ambiguity guard");
        }
        for(const auto signature:{0xe26d2bb9ac2e87b6ull,0x76a7e789464b75d3ull,0xe40ebd58d8b46adaull}) {
            const unsigned count=signature==0xe26d2bb9ac2e87b6ull?3:2;
            auto tree=pose(count,1100,signature),other=pose(count,1101,signature);
            const auto update=[&](double angle,double x) {
                tree.pose.bones.assign(count,Flatten(Camera(angle,x)));
                other.pose.bones.assign(count,Flatten(Camera(angle,x+200)));
            };
            MM3D::LinkInterpolation::Palette p;
            MM3D::LinkInterpolation::History h;
            update(0,0);p.fill(Expand(tree.pose.bones[0]));
            Check(!h.Apply(1,0,Identity(),p,{tree,other},true)[0],"intro tree seeds history");
            update(20,4);p.fill(Expand(tree.pose.bones[0]));
            const auto mid=h.Apply(2,33,Identity(),p,{other,tree},true);
            Check(mid[0] && Near(*mid[0],Camera(10,2),1e-5),"intro tree keeps its own motion among identical skeletons");
            Check(!h.Apply(2,33,Identity(),p,{tree,other},false)[0],"disabled intro trees remain native");
            other.pose.bones=tree.pose.bones;
            Check(!h.Apply(3,66,Identity(),p,{tree,other},true)[0],"overlapping tree instances rejected");
            Check(!h.Apply(4,99,Identity(),p,{tree},true)[0],"tree capture gaps reset history");
            MM3D::LinkInterpolation::History divergent;
            tree.pose.bones[1][3]+=6;
            divergent.Apply(1,0,Identity(),p,{tree},true);
            tree.pose.bones[1][3]-=6;
            Check(!divergent.Apply(2,33,Identity(),p,{tree},true)[0],"tree bones with different past motion stay native");
            tree.signature++;
            Check(!h.Apply(5,132,Identity(),p,{tree},true)[0],"unverified scenery excluded");
        }
        {
            auto bomber=pose(17,987,0xc299a4754ffae267ull);
            // Two bones resolve to the same skin transform, with distinct binds.
            for(unsigned i=1;i<17;++i)bomber.pose.bones[i][3]=float(i*10);
            bomber.bind[1][3]=-10;
            MM3D::LinkInterpolation::Palette p;p.fill(Identity());
            MM3D::LinkInterpolation::History h;
            Check(!h.Apply(1,0,Identity(),p,{bomber},true)[0],"Bomber shared skin seeds history");
            for(auto& b:bomber.pose.bones)b[3]+=4;
            p.fill(Camera(0,4));
            auto mid=h.Apply(2,33,Identity(),p,{bomber},true);
            Check(mid[0] && Near(*mid[0],Camera(0,2),1e-6),"Bomber equivalent skin bones interpolate");
            auto duplicate=bomber;duplicate.pose.instance++;
            Check(!h.Apply(3,66,Identity(),p,{bomber,duplicate},true)[0],"overlapping Bombers remain ambiguous");
            Check(!h.Apply(4,99,Identity(),p,{bomber},true)[0],"Bomber disappearance resets");
            MM3D::LinkInterpolation::History disagree;
            bomber.pose.bones[1][3]+=6;
            disagree.Apply(1,0,Identity(),p,{bomber},true);
            bomber.pose.bones[1][3]-=6;
            Check(!disagree.Apply(2,33,Identity(),p,{bomber},true)[0],"divergent Bomber skin histories rejected");
            bomber.signature++;
            Check(!h.Apply(5,132,Identity(),p,{bomber},true)[0],"unverified 17-bone actor excluded");
        }
        {
            auto postman=pose(15,1001,0xbf2f38bb6502de36ull);
            auto cap=pose(1,1002,0x10aa2e239eaea424ull);
            auto bag=pose(1,1003,0x61a2e0e2eed80dccull);
            const auto update=[&](double angle,double x) {
                const auto root=Camera(angle,x);
                postman.pose.bones[0]=Flatten(root);
                for(unsigned i=1;i<15;++i)
                    postman.pose.bones[i]=Flatten(Multiply(root,Camera(i*3,i*5)));
                cap.pose.bones[0]=postman.pose.bones[14];
                bag.pose.bones[0]=postman.pose.bones[7];
            };
            const auto palette=[&](unsigned bone) {
                MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
                p[0]=Expand(postman.pose.bones[bone]);return p;
            };
            MM3D::LinkInterpolation::History h;
            update(0,0);
            for(unsigned vertices:{204u,414u,96u})
                Check(!h.Apply(1,0,Identity(),palette(vertices==414?7:14),
                               {postman,cap,bag},true,vertices)[0],"Postman attachments seed independently");
            update(60,4);
            for(unsigned vertices:{204u,414u,96u}) {
                const unsigned bone=vertices==414?7:14;
                const auto p=palette(bone);
                const auto mid=h.Apply(2,33,Identity(),p,{postman,cap,bag},true,vertices);
                const auto expected=Multiply(Camera(30,2),Camera(bone*3,bone*5));
                Check(mid[0] && Near(*mid[0],expected,1e-4),
                      "Postman cap and bag stay on the interpolated parent bone");
                const auto separate=Half(Camera(bone*3,bone*5),p[0]);
                Check(separate && !PaletteNear(*separate,expected),
                      "attachment root interpolation would differ from parent hierarchy");
                Check(!h.Apply(2,33,Identity(),p,{postman,cap,bag},false,vertices)[0],
                      "disabled Postman interpolation stays native");
            }
            auto duplicate=postman;duplicate.pose.instance++;
            Check(!h.Apply(2,33,Identity(),palette(14),{postman,cap,bag,duplicate},true,204)[0],
                  "overlapping Postman owners remain ambiguous");
            Check(!h.Apply(2,33,Identity(),palette(14),{postman,cap,bag},true,205)[0],
                  "unverified attachment draw stays native");
            auto unrelated=cap;unrelated.signature++;
            Check(!h.Apply(2,33,Identity(),palette(14),{postman,unrelated,bag},true,204)[0],
                  "unverified accessory cannot borrow Postman history");
            cap.model++;
            Check(!h.Apply(3,66,Identity(),palette(14),{postman,cap,bag},true,204)[0],
                  "reused accessory address seeds new attachment");
            Check(h.Apply(4,99,Identity(),palette(14),{postman,cap,bag},true,204)[0].has_value(),
                  "stable attachment resumes after replacement");
            Check(!h.Apply(6,165,Identity(),palette(14),{postman,cap,bag},true,204)[0],
                  "Postman disappearance resets attachment");
            ++postman.generation;++cap.generation;
            Check(!h.Apply(7,198,Identity(),palette(14),{postman,cap,bag},true,204)[0],
                  "generation change resets Postman attachment");
            h.Reset();
            Check(!h.Apply(8,231,Identity(),palette(14),{postman,cap,bag},true,204)[0],
                  "explicit reset clears attachment history");
        }
        {
            auto carpenter=pose(20,1101,0x228b499bc4c2bef7ull);
            auto wood=pose(1,1102,0xa81ec0d047c2eaadull);
            const auto update=[&](double angle,double x) {
                const auto root=Camera(angle,x);
                carpenter.pose.bones[0]=Flatten(root);
                for(unsigned i=1;i<20;++i)
                    carpenter.pose.bones[i]=Flatten(Multiply(root,Camera(i*2,i*4)));
                wood.pose.bones[0]=carpenter.pose.bones[8];
            };
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
            MM3D::LinkInterpolation::History h;
            update(0,0);p[0]=Expand(wood.pose.bones[0]);
            Check(!h.Apply(1,0,Identity(),p,{carpenter,wood},true,36)[0],
                  "carpenter wood seeds attachment history");
            update(60,4);p[0]=Expand(wood.pose.bones[0]);
            auto mid=h.Apply(2,33,Identity(),p,{wood,carpenter},true,36);
            const auto expected=Multiply(Camera(30,2),Camera(16,32));
            Check(mid[0] && Near(*mid[0],expected,1e-4),
                  "wood follows carpenter bone eight through hierarchy rotation");
            Check(!h.Apply(2,33,Identity(),p,{carpenter,wood},false,36)[0],
                  "disabled interpolation leaves wood native");
            Check(!h.Apply(2,33,Identity(),p,{carpenter,wood},true,204)[0],
                  "wood cannot use Postman attachment geometry");
            auto duplicate=carpenter;duplicate.pose.instance+=10;
            Check(!h.Apply(2,33,Identity(),p,{carpenter,wood,duplicate},true,36)[0],
                  "wood cannot choose between overlapping carpenters");
            auto unrelated=wood;unrelated.signature++;
            Check(!h.Apply(2,33,Identity(),p,{carpenter,unrelated},true,36)[0],
                  "unverified tool does not use wood attachment");
            wood.model++;
            Check(!h.Apply(3,66,Identity(),p,{carpenter,wood},true,36)[0],
                  "replaced wood model seeds history");
            Check(h.Apply(4,99,Identity(),p,{carpenter,wood},true,36)[0].has_value(),
                  "wood resumes after stable attachment");
            Check(!h.Apply(6,165,Identity(),p,{carpenter,wood},true,36)[0],
                  "carpenter disappearance resets wood attachment");
            h.Reset();
            Check(!h.Apply(7,198,Identity(),p,{carpenter,wood},true,36)[0],
                  "reset clears wood history");
        }
        for (unsigned vertices : {36u, 120u}) {
            auto carpenter=pose(20,1101,0x228b499bc4c2bef7ull);
            auto pickaxe=pose(1,1102,0x06d0beac6055ea2cull);
            const auto update=[&](double angle,double x) {
                const auto root=Camera(angle,x);
                carpenter.pose.bones[0]=Flatten(root);
                for(unsigned i=1;i<20;++i)
                    carpenter.pose.bones[i]=Flatten(Multiply(root,Camera(i*2,i*4)));
                pickaxe.pose.bones[0]=carpenter.pose.bones[19];
            };
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
            MM3D::LinkInterpolation::History h;
            update(0,0);p[0]=Expand(pickaxe.pose.bones[0]);
            Check(!h.Apply(1,0,Identity(),p,{carpenter,pickaxe},true,vertices)[0],
                  "carpenter pickaxe seeds attachment history");
            update(60,4);p[0]=Expand(pickaxe.pose.bones[0]);
            auto mid=h.Apply(2,33,Identity(),p,{pickaxe,carpenter},true,vertices);
            const auto expected=Multiply(Camera(30,2),Camera(38,76));
            Check(mid[0] && Near(*mid[0],expected,1e-4),
                  "pickaxe follows carpenter bone nineteen through hierarchy rotation");
            Check(!h.Apply(2,33,Identity(),p,{carpenter,pickaxe},false,vertices)[0],
                  "disabled interpolation leaves pickaxe native");
            Check(!h.Apply(2,33,Identity(),p,{carpenter,pickaxe},true,204)[0],
                  "pickaxe cannot use Postman attachment geometry");
            auto duplicate=carpenter;duplicate.pose.instance+=10;
            Check(!h.Apply(2,33,Identity(),p,{carpenter,pickaxe,duplicate},true,vertices)[0],
                  "pickaxe cannot choose between overlapping carpenters");
            auto unrelated=pickaxe;unrelated.signature++;
            Check(!h.Apply(2,33,Identity(),p,{carpenter,unrelated},true,vertices)[0],
                  "unverified tool does not use pickaxe attachment");
            pickaxe.model++;
            Check(!h.Apply(3,66,Identity(),p,{carpenter,pickaxe},true,vertices)[0],
                  "replaced pickaxe model seeds history");
            Check(h.Apply(4,99,Identity(),p,{carpenter,pickaxe},true,vertices)[0].has_value(),
                  "pickaxe resumes after stable attachment");
            Check(!h.Apply(6,165,Identity(),p,{carpenter,pickaxe},true,vertices)[0],
                  "carpenter disappearance resets pickaxe attachment");
            h.Reset();
            Check(!h.Apply(7,198,Identity(),p,{carpenter,pickaxe},true,vertices)[0],
                  "reset clears pickaxe history");
        }
        for (unsigned vertices : {120u, 180u, 540u}) {
            auto bombchu=pose(9,1101,0xe6a4db84a5b61d37ull);
            auto bomb=pose(1,1102,0x09b0267833ca8895ull);
            const auto update=[&](double angle,double x) {
                const auto root=Camera(angle,x);
                bombchu.pose.bones[0]=Flatten(root);
                for(unsigned i=1;i<9;++i)
                    bombchu.pose.bones[i]=Flatten(Multiply(root,Camera(i*2,i*4)));
                bomb.pose.bones[0]=bombchu.pose.bones[8];
            };
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,9999));
            MM3D::LinkInterpolation::History h;
            update(0,0);p[0]=Expand(bomb.pose.bones[0]);
            Check(!h.Apply(1,0,Identity(),p,{bombchu,bomb},true,vertices)[0],
                  "bombchu bomb seeds attachment history");
            update(60,4);p[0]=Expand(bomb.pose.bones[0]);
            auto mid=h.Apply(2,33,Identity(),p,{bomb,bombchu},true,vertices);
            const auto expected=Multiply(Camera(30,2),Camera(16,32));
            Check(mid[0] && Near(*mid[0],expected,1e-4),
                  "bomb follows bombchu bone eight through hierarchy rotation");
            Check(!h.Apply(2,33,Identity(),p,{bombchu,bomb},false,vertices)[0],
                  "disabled interpolation leaves bomb native");
            Check(!h.Apply(2,33,Identity(),p,{bombchu,bomb},true,204)[0],
                  "bomb cannot use Postman attachment geometry");
            auto duplicate=bombchu;duplicate.pose.instance+=10;
            Check(!h.Apply(2,33,Identity(),p,{bombchu,bomb,duplicate},true,vertices)[0],
                  "bomb cannot choose between overlapping bombchus");
            auto unrelated=bomb;unrelated.signature++;
            Check(!h.Apply(2,33,Identity(),p,{bombchu,unrelated},true,vertices)[0],
                  "unverified tool does not use bomb attachment");
            bomb.model++;
            Check(!h.Apply(3,66,Identity(),p,{bombchu,bomb},true,vertices)[0],
                  "replaced bomb model seeds history");
            Check(h.Apply(4,99,Identity(),p,{bombchu,bomb},true,vertices)[0].has_value(),
                  "bomb resumes after stable attachment");
            Check(!h.Apply(6,165,Identity(),p,{bombchu,bomb},true,vertices)[0],
                  "bombchu disappearance resets bomb attachment");
            h.Reset();
            Check(!h.Apply(7,198,Identity(),p,{bombchu,bomb},true,vertices)[0],
                  "reset clears bomb history");
        }
        {
            auto link=pose(24,800,0x17beb877e4d23506ull);
            auto pipes=pose(7,801,0x79937db4f6cb5c95ull);
            for(unsigned i=1;i<24;++i)link.pose.bones[i][3]=float(100+i*10);
            for(unsigned i=2;i<7;++i)pipes.pose.bones[i][3]=float(i*10);
            MM3D::LinkInterpolation::Palette p;p.fill(Camera(0,999));
            p[0]=Identity();
            for(unsigned i=0;i<5;++i)p[i+1]=Expand(pipes.pose.bones[MM3D::LinkInterpolation::PipeBones[i]]);
            MM3D::LinkInterpolation::History h;
            h.Apply(1,0,Identity(),p,{link,pipes},true,1284);
            for(auto& b:link.pose.bones)b[3]+=4;
            for(auto& b:pipes.pose.bones)b[3]+=4;
            pipes.pose.bones[2][7]=6;
            p[0]=Camera(0,4);
            for(unsigned i=0;i<5;++i)p[i+1]=Expand(pipes.pose.bones[MM3D::LinkInterpolation::PipeBones[i]]);
            auto mid=h.Apply(2,33,Identity(),p,{link,pipes},true,1284);
            Check(mid[0] && Near(*mid[0],Camera(0,2),1e-6),"pipes and Link share interpolated base");
            Check(mid[1] && std::abs((*mid[1])[1][3]-3)<1e-6,"pipe branch animates independently of Link");
            for(unsigned i=1;i<=5;++i)Check(bool(mid[i]),"all unique pipe branches interpolate");
            auto duplicate=pipes;duplicate.pose.instance++;
            Check(!h.Apply(3,66,Identity(),p,{link,pipes,duplicate},true,1284)[0],"duplicate pipes rejected");
            auto unrelated=link;unrelated.signature++;
            Check(!h.Apply(3,66,Identity(),p,{unrelated,pipes},true,1284)[0],"pipes cannot override unrelated owner");
            Check(!h.Apply(2,33,Identity(),p,{link,pipes},false,1284)[1],"disabled pipes remain native");
            MM3D::LinkInterpolation::History closing;
            pipes.pose.bones.assign(7,Flatten(Camera(0,4)));
            pipes.pose.bones[2][0]=pipes.pose.bones[2][5]=pipes.pose.bones[2][10]=1.2f;
            p.fill(Camera(0,4));p[1]=Expand(pipes.pose.bones[2]);
            closing.Apply(1,0,Identity(),p,{link,pipes},true,1284);
            pipes.pose.bones[2]=Flatten(Camera(0,4));p[1]=Camera(0,4);
            mid=closing.Apply(2,33,Identity(),p,{link,pipes},true,1284);
            Check(mid[1] && std::abs((*mid[1])[0][0]-1.1)<1e-6,
                  "closing pipe retains its identity when all openings coincide");
            Check(mid[2] && std::abs((*mid[2])[0][0]-1)<1e-6,
                  "other openings do not inherit the played note");

        }
        {
            auto a=pose(1,1001,0x63f1ea87bac6ed15ull),b=pose(1,1002,0x237eacb5846225e3ull),c=pose(1,1003,0xa5ab84c08cb4c10cull);
            MM3D::LinkInterpolation::Palette p;p.fill(Identity());
            MM3D::LinkInterpolation::History h;
            Check(!h.Apply(1,0,Identity(),p,{a,b,c},true)[0],"sign group seeds independent histories");
            a.pose.bones=b.pose.bones=c.pose.bones={Flatten(Camera(60,4))};p.fill(Camera(60,4));
            auto mid=h.Apply(2,33,Identity(),p,{a,b,c},true);
            Check(mid[0] && Near(*mid[0],Camera(30,2),1e-6),"verified sign fragments agree on fall midpoint");
            auto duplicate=a;duplicate.pose.instance++;duplicate.model++;
            Check(!h.Apply(3,66,Identity(),p,{a,b,c,duplicate},true)[0],"two signs sharing fragment signature rejected");
            c.signature++;
            Check(!h.Apply(3,66,Identity(),p,{a,b,c},true)[0],"unverified sign owner rejected");
            c.signature--;
            MM3D::LinkInterpolation::History divergent;
            c.pose.bones={Flatten(Camera(-60))};
            divergent.Apply(1,0,Identity(),p,{a,b,c},true);
            p.fill(Camera(-60));divergent.Apply(1,0,Identity(),p,{a,b,c},true);
            c.pose.bones=a.pose.bones;p.fill(Camera(60,4));
            Check(!divergent.Apply(2,33,Identity(),p,{a,b,c},true)[0],"sign histories must agree");
        }
        for(const unsigned vertices:{12u,30u,36u,39u,42u}) {
            constexpr std::array<std::uint64_t,8> signatures{
                0x25691ac68afb02e2ull,0x7bb6e2032873c32eull,0xa8858ef16d4f6e4bull,
                0x15032d07b3410790ull,0x1ccc2f131d7a89cfull,0x981fa132959572f9ull,
                0xa5a4cdf77e3ead22ull,0x10167bcfce7776c8ull};
            std::vector<LivePose> logs;
            for(unsigned i=0;i<signatures.size();++i)logs.push_back(pose(1,2000+i,signatures[i]));
            const auto position=[&](const Matrix& m) {
                for(auto& piece:logs) {
                    auto root=m,bind=Identity();
                    if(piece.signature==0xa5a4cdf77e3ead22ull) {
                        constexpr double scale=.38303905725479126;
                        for(unsigned r=0;r<3;++r) {
                            bind[r][r]=scale;
                            for(unsigned c=0;c<3;++c)root[r][c]/=scale;
                        }
                    }
                    piece.pose.bones={Flatten(root)};piece.bind={Flatten(bind)};
                }
            };
            MM3D::LinkInterpolation::Palette p;p.fill(Identity());
            MM3D::LinkInterpolation::History h;
            position(Identity());
            Check(!h.Apply(1,0,Identity(),p,logs,true,vertices)[0],"training log fragments seed independent histories");
            position(Camera(60,4));p.fill(Camera(60,4));
            std::reverse(logs.begin(),logs.end());
            const auto mid=h.Apply(2,33,Identity(),p,logs,true,vertices);
            Check(mid[0] && Near(*mid[0],Camera(30,2),1e-5),
                  "training log pieces rotate and fall together, including scaled-bind piece");
            Check(!h.Apply(2,33,Identity(),p,logs,false,vertices)[0],"log comparison stays native");
            Check(!h.Apply(2,33,Identity(),p,logs,true,vertices+1)[0],"unverified log draw excluded");
            auto wrong=logs;wrong[0].signature++;
            Check(!h.Apply(2,33,Identity(),p,wrong,true,vertices)[0],"unrelated shared-root owner excluded");
            wrong=logs;wrong[0].bind[0][0]=2;
            Check(!h.Apply(2,33,Identity(),p,wrong,true,vertices)[0],"unverified fragment bind rejected");
            wrong=logs;wrong[0].words[0]=0;
            Check(!h.Apply(2,33,Identity(),p,wrong,true,vertices)[0],"unverified log topology rejected");
            wrong={logs[0],logs[0]};wrong[1].pose.instance+=100;wrong[1].model+=100;
            Check(!h.Apply(2,33,Identity(),p,wrong,true,vertices)[0],"coincident separate logs with same signature rejected");
            Check(!h.Apply(4,99,Identity(),p,logs,true,vertices)[0],"log capture gap reseeds");
            auto a=pose(1,3001,signatures[0]),b=pose(1,3002,signatures[1]);
            MM3D::LinkInterpolation::History divergent;
            b.pose.bones={Flatten(Camera(-60))};
            p.fill(Identity());divergent.Apply(1,0,Identity(),p,{a,b},true,vertices);
            p.fill(Camera(-60));divergent.Apply(1,0,Identity(),p,{a,b},true,vertices);
            a.pose.bones=b.pose.bones={Flatten(Camera(60,4))};p.fill(Camera(60,4));
            Check(!divergent.Apply(2,33,Identity(),p,{a,b},true,vertices)[0],
                  "log fragments meeting along different trajectories stay native");
        }
        auto effect=pose(4,123,0xdfe0c7f5a2531807ull);
        MM3D::LinkInterpolation::Palette palette;palette.fill(Identity());
        MM3D::LinkInterpolation::History effect_history;
        Check(!effect_history.Apply(1,0,Identity(),palette,{effect},true)[0],"effect seeds history");
        for(auto& b:effect.pose.bones)b[3]=2;
        for(auto& b:palette)b[0][3]=2;
        auto result=effect_history.Apply(2,33,Identity(),palette,{effect},true);
        Check(result[0] && std::abs((*result[0])[0][3]-1)<1e-6,"equivalent transformation bones interpolate");
        MM3D::LinkInterpolation::History divergent;
        effect.pose.bones[1][3]=8;
        divergent.Apply(1,0,Identity(),palette,{effect},true);
        effect.pose.bones[1][3]=2;
        Check(!divergent.Apply(2,33,Identity(),palette,{effect},true)[0],"different effect histories remain native");
        for(const auto signature:{0x2090ecc88f377de1ull,0x15ad1b7410d5bd8dull}) {
            const unsigned count=signature==0x2090ecc88f377de1ull?7:3;
            auto fairy_mask=pose(count,654,signature);
            const auto old_root=Camera(0,0),new_root=Camera(30,10),head_mid=Camera(12,4);
            std::vector<Matrix> locals;
            for(unsigned i=0;i<count;++i) {
                locals.push_back(Camera(0,double(i*3)));
                fairy_mask.pose.bones[i]=Flatten(Multiply(old_root,locals[i]));
            }
            MM3D::LinkInterpolation::Palette p;p.fill(old_root);
            MM3D::LinkInterpolation::ActorHistory attached;
            attached.Apply(1,0,Identity(),p,fairy_mask,true);
            const auto old_hair=locals[1];locals[1]=Camera(20,3);
            for(unsigned i=0;i<count;++i)fairy_mask.pose.bones[i]=Flatten(Multiply(new_root,locals[i]));
            p[0]=new_root;p[1]=Expand(fairy_mask.pose.bones[1]);
            const auto mid=attached.Apply(2,33,Identity(),p,fairy_mask,true,false,false,0,head_mid);
            Check(mid[0] && Near(*mid[0],head_mid,1e-6),"animated mask root follows head midpoint");
            Check(mid[1] && Near(*mid[1],Multiply(head_mid,*Half(old_hair,locals[1])),1e-5),
                  "mask hair retains local animation beneath interpolated head");
        }
        auto human=pose(26,456,1234);
        human.words.assign(MM3D::LinkPoseCapture::BoneWords.begin(),MM3D::LinkPoseCapture::BoneWords.end());
        auto mask=pose(1,789,0x6787d559c31d313aull);
        // Give each human bone a unique current matrix; the mask matches head 11.
        for(unsigned i=0;i<26;++i)human.pose.bones[i][7]=float(i);
        mask.pose.bones[0][7]=11;
        palette.fill(Expand(mask.pose.bones[0]));
        MM3D::LinkInterpolation::History attachment;
        attachment.Apply(1,0,Identity(),palette,{human,mask},true);
        for(auto& b:human.pose.bones)b[3]=2;
        mask.pose.bones[0][3]=2;palette.fill(Expand(mask.pose.bones[0]));
        result=attachment.Apply(2,33,Identity(),palette,{human,mask},true);
        Check(result[0] && std::abs((*result[0])[0][3]-1)<1e-6,"mask and head agree on shared midpoint");
        for(const auto signature:{0x9c5589d73f0deea4ull,0x533d208b50f73607ull,0x12345678ull}) {
            mask.signature=signature;mask.pose.bones[0]=human.pose.bones[11];
            palette.fill(Expand(mask.pose.bones[0]));
            MM3D::LinkInterpolation::History worn;
            Check(!worn.Apply(10,330,Identity(),palette,{human,mask},true)[0],"new head attachment seeds");
            for(auto& bone:human.pose.bones)bone[3]+=2;
            mask.pose.bones[0]=human.pose.bones[11];palette.fill(Expand(mask.pose.bones[0]));
            auto mid=worn.Apply(11,363,Identity(),palette,{human,mask},true);
            Check(mid[0] && std::abs((*mid[0])[0][3]-(mask.pose.bones[0][3]-1))<1e-6,
                  "single-bone worn masks share human head interpolation");
            Check(!worn.Apply(13,429,Identity(),palette,{human,mask},true)[0],"head attachment gap rejected");
            mask.pose.instance++;
            Check(!worn.Apply(14,462,Identity(),palette,{human,mask},true)[0],"new mask instance seeds");
        }
        for(auto& bone:human.pose.bones)bone[3]=2;
        mask.signature=0x6787d559c31d313aull;
        mask.pose.bones[0]=human.pose.bones[11];palette.fill(Expand(mask.pose.bones[0]));
        MM3D::LinkInterpolation::History different_attachment;
        mask.pose.bones[0][3]=8;
        different_attachment.Apply(1,0,Identity(),palette,{human,mask},true);
        palette.fill(Expand(mask.pose.bones[0]));
        different_attachment.Apply(1,0,Identity(),palette,{human,mask},true);
        mask.pose.bones[0][3]=2;palette.fill(Expand(mask.pose.bones[0]));
        Check(!different_attachment.Apply(2,33,Identity(),palette,{human,mask},true)[0],"disagreeing mask history rejected");
    }
    {
        using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose fairy;
        fairy.signature=0x09e2df0cf6765f8eull;fairy.pose.instance=123;fairy.model=456;
        fairy.pose.bones.assign(5,Flatten(Identity()));
        fairy.bind.assign(5,Flatten(Identity()));fairy.words={0xffff0000,1,2,3,4};
        MM3D::LinkInterpolation::Palette palette;
        palette.fill(Identity());
        for(unsigned i=0;i<3;++i)palette[0][i][i]=14;
        MM3D::LinkInterpolation::History history;
        Check(!history.Apply(1,0,Identity(),palette,{fairy},true,4)[0],
              "fairy glow requires previous pose");
        const auto camera=Camera(20,4);
        auto world=Identity();world[0][3]=10;
        const auto current=Multiply(InverseRigid(camera),world);
        fairy.pose.bones.assign(5,Flatten(current));
        for(unsigned i=0;i<3;++i)palette[0][i][3]=current[i][3];
        const auto result=history.Apply(2,33,camera,palette,{fairy},true,4);
        auto midpoint=Identity();midpoint[0][3]=5;
        const auto expected=Multiply(InverseRigid(camera),midpoint);
        Check(result[0].has_value(),"fairy glow interpolates with moving camera");
        for(unsigned i=0;i<3;++i) {
            Check(std::abs((*result[0])[i][3]-expected[i][3])<.0001,
                  "fairy glow follows midpoint world position");
            for(unsigned j=0;j<3;++j)Check((*result[0])[i][j]==palette[0][i][j],
                  "fairy glow preserves native orientation and size");
        }
        for(unsigned i=1;i<10;++i)Check(!result[i],"glow never modifies unused bones");
        Check(!history.Apply(2,33,camera,palette,{fairy},false,4)[0],"comparison keeps native glow");
        auto frog=fairy;frog.signature=0xc0835328e6782411ull;
        Check(!MM3D::LinkInterpolation::FairyGlow(palette[0],frog,4),"frog cannot match fairy glow");
        Check(!MM3D::LinkInterpolation::FairyGlow(palette[0],fairy,24),"fairy wings excluded from glow path");
        auto other=fairy;other.pose.instance++;
        Check(!history.Apply(2,33,camera,palette,{fairy,other},true,4)[0],"ambiguous fairy instance rejected");
        auto bad=palette[0];bad[0][3]+=1;
        Check(!MM3D::LinkInterpolation::FairyGlow(bad,fairy,4),"nearby unrelated particle excluded");
        bad=palette[0];bad[0][1]=1;
        Check(!MM3D::LinkInterpolation::FairyGlow(bad,fairy,4),"nonbillboard geometry excluded");
        Check(!history.Apply(4,99,camera,palette,{fairy},true,4)[0],"glow frame gap resets history");
        fairy.generation++;
        Check(!history.Apply(5,132,camera,palette,{fairy},true,4)[0],"glow generation change resets history");
    }
    {
        using namespace MM3D::LinkPoseMath;
        MM3D::LinkPoseCapture::LivePose fairy;
        fairy.signature=0x09e2df0cf6765f8eull;fairy.pose.instance=123;fairy.model=456;
        fairy.bind.assign(5,Flatten(Identity()));fairy.words={0xffff0000,1,2,3,4};
        fairy.pose.bones.clear();
        for(unsigned i=0;i<5;++i)fairy.pose.bones.push_back(Flatten(Camera(0,i*3)));
        auto other=fairy;other.pose.instance++;
        MM3D::LinkInterpolation::Palette palette;palette.fill(Camera(0,3));
        MM3D::LinkInterpolation::History twins;
        Check(!twins.Apply(1,0,Identity(),palette,{fairy,other},true,24)[0],"duplicate fairy histories seed");
        for(unsigned i=0;i<5;++i)fairy.pose.bones[i]=Flatten(Camera(0,i*3+4));
        other.pose.bones=fairy.pose.bones;palette.fill(Camera(0,7));
        auto mid=twins.Apply(2,33,Identity(),palette,{fairy,other},true,24);
        Check(mid[0] && Near(*mid[0],Camera(0,5),1e-6),"identical fairy wings share agreed midpoint");
        Check(!twins.Apply(2,33,Identity(),palette,{fairy,other},false,24)[0],"disabled fairy twins stay native");
        other.pose.bones[4][3]+=1;
        Check(!twins.Apply(2,33,Identity(),palette,{fairy,other},true,24)[0],"different complete fairy poses rejected");
        MM3D::LinkInterpolation::History crossing;
        other.pose.bones=fairy.pose.bones;
        crossing.Apply(1,0,Identity(),palette,{fairy},true,24);
        for(auto& bone:other.pose.bones)bone[3]+=4;
        palette.fill(Camera(0,11));crossing.Apply(1,0,Identity(),palette,{other},true,24);
        other.pose.bones=fairy.pose.bones;palette.fill(Camera(0,7));
        Check(!crossing.Apply(2,33,Identity(),palette,{fairy,other},true,24)[0],
              "meeting fairies with different previous trajectories rejected");
    }
    {
        using G=MM3D::GlareInterpolation;
        G::ScreenHistory cpu;
        G::ScreenSample screen;screen.vertices.resize(72);screen.alpha=1;
        for(auto& v:screen.vertices){v.position={0,0,-.3f,1};v.color={1,1,1,1};}
        const auto native=screen;
        Check(!cpu.Apply(1,{1,2,3},screen),"CPU glare seeds from native shaded vertices");
        for(auto& v:screen.vertices){v.position[0]+=.1f;v.position[1]+=.1f;}
        screen.alpha=.5f;
        auto half=cpu.Apply(2,{1,2,3},screen);
        Check(half && half->vertices[0].position[0]==.05f && half->alpha==.75f,
              "CPU glare movement and intensity interpolate");
        Check(half && half->vertices[0].uv==native.vertices[0].uv &&
              half->vertices[0].position[2]==native.vertices[0].position[2],"glare UV and depth stay native");
        Check(cpu.Apply(2,{1,2,3},screen)==half,"duplicate glare draw keeps the same midpoint");
        Check(screen.vertices[0].position[0]==.1f && screen.alpha==.5f,"native glare snapshot is unchanged");
        screen.vertices[0].uv[0]=1;
        Check(!cpu.Apply(3,{1,2,3},screen),"glare UV changes reject cross-matching geometry");
        screen.vertices[0].position[0]+=1;
        Check(!cpu.Apply(4,{1,2,3},screen),"glare camera cut stays native");
        Check(!cpu.Apply(6,{1,2,3},screen),"glare missing frame does not blend stale vertices");
        Check(!cpu.Apply(7,{2,2,3},screen),"CPU glare texture replacement resets history");
        Check(!cpu.Apply(8,{2,4,3},screen),"CPU glare framebuffer replacement resets history");
        Check(!cpu.Apply(9,{2,4,5},screen),"CPU glare generation resets history");
        screen.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();
        Check(!cpu.Apply(10,{2,4,5},screen),"nonfinite CPU glare rejected");
        Matrix gp{};gp[0]={0,-1./120,0,1};gp[1]={-1./200,0,0,1};
        gp[2]={0,0,1./1800,1./3};gp[3][3]=1;
        auto gm=Identity();gm[2][3]=-1200;
        G::Tev gt{{{238030384,0,65537,0,0},{250547951,0,65537,0x65ffffff,0},
            {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
            {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0}}};
        Check(G::ScreenDraw(gp,gm,gt),"captured glare transform and animated material recognized");
        gt[1][3]=0x65ffff00;
        Check(!G::ScreenDraw(gp,gm,gt),"other additive colors excluded");
        gt[1][3]=0xffffffff;gm[2][3]=0;
        Check(!G::ScreenDraw(gp,gm,gt),"other screen overlay transforms excluded");
        MM3D::GlareInterpolation h;
        MM3D::GlareInterpolation::Points a{},b{};
        b[0]={10,20,0};
        Check(!h.Apply(1,123,0,a),"glare first frame stays native");
        auto mid=h.Apply(2,123,0,b);
        Check(mid && (*mid)[0][0]==5 && (*mid)[0][1]==10,"glare positions interpolate");
        Check((*mid)[48]==b[48],"extra glare vertex stays native");
        Check(!h.Apply(4,123,0,a),"glare disappearance resets history");
        Check(!h.Apply(5,456,0,b),"glare texture changes reset history");
        Check(!h.Apply(6,456,1,a),"glare generation reset");
        b[0][2]=1;
        Check(!h.Apply(7,456,1,b),"changed glare geometry rejected");
        b[0][0]=500;
        Check(!h.Apply(8,456,1,b),"large glare jump rejected");
    }

    {
        SceneCopies copies;
        copies.Begin(10,0x1000);
        copies.Transfer(10,0x1000,0x2000);
        Check(copies.Contains(10,0x2000),"scene transfer preserves provenance");
        Check(!copies.Contains(11,0x2000),"previous frame copy rejected");
        copies.Transfer(10,0x3000,0x2000);
        Check(!copies.Contains(10,0x2000),"overwritten scene copy rejected");
        copies.Begin(11,0x4000);
        Check(!copies.Contains(11,0x1000),"new frame clears prior scene");
        Check(NormalizedSceneCopy(true,true,true,4,1,true,false),"normalized opaque scene copy accepted");
        Check(!NormalizedSceneCopy(false,true,true,4,1,true,false),"native shrink remains native");
        Check(!NormalizedSceneCopy(true,false,true,4,1,true,false),"unrelated texture rejected");
        Check(!NormalizedSceneCopy(true,true,false,4,1,true,false),"perspective geometry rejected");
        Check(!NormalizedSceneCopy(true,true,true,4,3,true,false),"multitexture effects rejected");
        Check(!NormalizedSceneCopy(true,true,true,4,1,false,false),"blended effects rejected");
        Check(!NormalizedSceneCopy(true,true,true,4,1,true,true),"stencilled effects rejected");
        Check(NormalizedSceneClear(true,true,true,4,0,true,false),"normalized solid scene clear candidate");
        Check(!NormalizedSceneClear(false,true,true,4,0,true,false),"ordinary clears cannot be omitted");
        Check(!NormalizedSceneClear(true,false,true,4,0,true,false),"unrelated framebuffer clear rejected");
        Check(!NormalizedSceneClear(true,true,false,4,0,true,false),"solid perspective geometry is not a clear");
        Check(!NormalizedSceneClear(true,true,true,6,0,true,false),"unverified clear geometry rejected");
        Check(!NormalizedSceneClear(true,true,true,4,1,true,false),"textured geometry is not a clear");
        Check(!NormalizedSceneClear(true,true,true,4,0,false,false),"alpha fades are not omitted");
        Check(!NormalizedSceneClear(true,true,true,4,0,true,true),"stencil masks are not omitted");

        // Captured closing-day framebuffer/texture allocation. All decisions
        // depend on transfer geometry, not these particular guest addresses.
        constexpr std::uint32_t scene=0x18237fc0,texture=0x18000980;
        constexpr auto destination=texture+SceneCopies::PaddingBytes;
        SceneCopies padded;
        const auto copy=[&](bool succeeded=true,unsigned size=SceneCopies::SceneBytes,
                            unsigned input_gap=0,unsigned output_gap=0) {
            padded.TextureTransfer(20,scene,destination,size,16384,input_gap,16384,output_gap,succeeded);
        };
        const auto accepted=[&] {
            return padded.PaddedTextureContains(20,texture,512,512,true);
        };
        padded.Begin(20,scene,512,400,true);
        const auto visible_copy=[&](unsigned size=768000,unsigned in_width=15360,
                                    unsigned in_gap=1024,unsigned out_width=15360,
                                    unsigned out_gap=1024,bool succeeded=true) {
            padded.TextureTransfer(20,scene,destination,size,in_width,in_gap,out_width,out_gap,succeeded);
        };
        visible_copy();
        Check(accepted(),"captured 480-column scene with 512-pixel tiled stride accepted");
        visible_copy(768000-16);
        Check(!accepted(),"incomplete visible rectangle rejected");
        visible_copy(768000,15360,1008);
        Check(!accepted(),"wrong input tile stride rejected");
        visible_copy(768000,15360,1024,15360,1008);
        Check(!accepted(),"wrong output tile stride rejected");
        visible_copy(768000,15360,1024,15344,1040);
        Check(!accepted(),"matching total stride cannot hide wrong copied width");
        visible_copy(768000,1920,128,1920,128);
        Check(!accepted(),"linear row gaps cannot stand in for tiled bands");
        visible_copy(768000,15360,1024,15360,1024,false);
        Check(!accepted(),"failed visible-rectangle copy rejected");
        visible_copy();
        // The final tile band begins 49 strides later, beyond the packed
        // byte count. Writes there must still invalidate gapped provenance.
        padded.InvalidatePadded(destination+49*16384,destination+49*16384+16);
        Check(!accepted(),"write in last gapped band invalidates scene copy");
        copy();
        Check(accepted(),"complete scene transfer recognizes padded sampler base");
        Check(!padded.Contains(20,texture),"padded alias never enters exact scene provenance");
        Check(!padded.PaddedTextureContains(21,texture,512,512,true),"old padded copy rejected");
        Check(!padded.PaddedTextureContains(20,texture+16,512,512,true),"interior address is not a scene alias");
        Check(!padded.PaddedTextureContains(20,texture,512,1024,true),"larger containing texture rejected");
        Check(!padded.PaddedTextureContains(20,texture,256,512,true),"different texture stride rejected");
        Check(!padded.PaddedTextureContains(20,texture,512,512,false),"different texture format rejected");
        padded.InvalidatePadded(texture,destination);
        Check(accepted(),"writing only unused texture padding preserves scene copy");
        padded.InvalidatePadded(destination+128,destination+144);
        Check(!accepted(),"partial fill or CPU write invalidates copied scene");
        copy();padded.InvalidatePadded(destination-16,destination+16);
        Check(!accepted(),"overlapping render target invalidates copied scene");
        copy(false);Check(!accepted(),"failed transfer cannot preserve old padded provenance");
        copy(true,SceneCopies::SceneBytes-16);Check(!accepted(),"partial transfer rejected");
        copy(true,SceneCopies::SceneBytes,16);Check(!accepted(),"gapped input rejected");
        copy(true,SceneCopies::SceneBytes,0,16);Check(!accepted(),"gapped output rejected");
        copy();padded.Transfer(20,0,0x20000000);
        Check(!accepted(),"untracked transfer conservatively ends padded provenance");
        padded.Begin(20,scene,256,400,true);copy();
        Check(!accepted(),"copy size cannot hide incorrect source stride");
        padded.Begin(20,scene,512,400,false);copy();
        Check(!accepted(),"copy size cannot hide incorrect source format");
        padded.Begin(20,scene,512,400,true);copy();
        padded.Begin(22,scene,512,400,true);
        Check(!padded.PaddedTextureContains(22,texture,512,512,true),"frame gap clears padded provenance");
        Check(!padded.PreviousContains(22,texture),"padded alias cannot become transformation feedback");

        Matrix shrink_projection{};
        shrink_projection[0]={0,-1./120,0,1};shrink_projection[1]={-1./200,0,0,1};
        shrink_projection[2]={0,0,1./1800,1./3};shrink_projection[3]={0,0,0,1};
        auto shrink_model=Identity();shrink_model[2][3]=-1200;
        const std::array<float,4> shrink_uv{0,0,478.f/512,399.f/512};
        Check(NormalizedShrinkMesh(shrink_projection,shrink_model,shrink_uv),"verified normalized shrink mesh accepted");
        auto changed_uv=shrink_uv;changed_uv[3]=1;
        Check(!NormalizedShrinkMesh(shrink_projection,shrink_model,changed_uv),"sampling uncopied padding rejected");
        changed_uv=shrink_uv;changed_uv[0]=.1f;
        Check(!NormalizedShrinkMesh(shrink_projection,shrink_model,changed_uv),"cropped scene quad rejected");
        changed_uv=shrink_uv;changed_uv[0]=std::numeric_limits<float>::quiet_NaN();
        Check(!NormalizedShrinkMesh(shrink_projection,shrink_model,changed_uv),"nonfinite scene UV rejected");
        shrink_model[0][0]=.95;
        Check(!NormalizedShrinkMesh(shrink_projection,shrink_model,shrink_uv),"unnormalized scene quad rejected");

        SceneClearPair clear;
        padded.Begin(23,scene,512,400,true);
        padded.TextureTransfer(23,scene,destination,768000,15360,1024,15360,1024,true);
        clear.Observe(23,scene,true,false);
        Check(!clear.Observe(23,scene,false,NormalizedSceneCopy(true,
                  padded.PaddedTextureContains(23,texture,512,512,true),true,4,1,true,false)) &&
              !clear.Pending(23),"padded scene copy resolves closing-day clear without 30fps fallback");
        Check(!clear.Observe(11,0x4000,true,false) && clear.Pending(11),
              "clear alone blocks paired presentation until its copy arrives");
        copies.Transfer(11,0x4000,0x5000);
        Check(!clear.Observe(11,0x4000,false,NormalizedSceneCopy(
                  true,copies.Contains(11,0x5000),true,4,1,true,false)) && !clear.Pending(11),
              "verified clear and scene copy preserve private midpoint");
        clear.Observe(12,0x4000,true,false);
        Check(clear.Observe(12,0x4000,false,false),"intervening HUD forces fallback");
        clear.Observe(13,0x4000,true,false);
        Check(clear.Observe(13,0x6000,false,true),"copy to different target cannot validate clear");
        clear.Observe(14,0x4000,true,false);
        Check(clear.Observe(14,0x4000,true,false) && clear.Pending(14),
              "second clear does not hide first unmatched clear");
        Check(!clear.Pending(15) && !clear.Observe(15,0x4000,false,true),
              "unmatched clear does not poison following frame");
        clear.Observe(16,0x4000,true,false);
        clear.Reset();
        Check(!clear.Pending(16),"mode or session reset discards pending clear");
    }

    {
        namespace L=MM3D::LinkPoseCapture;
        struct Memory {
            std::unordered_map<L::U32,L::U32> words;
            std::optional<L::U32> Read(L::U32 at) {
                const auto it=words.find(at);return it==words.end()?std::nullopt:std::optional(it->second);
            }
        } memory; // Intentionally no Write method: this hook must remain read-only.
        constexpr L::U32 instance=0x08000000,wrapper=instance+0x1000,data=instance+0x2000,out=instance+0x3000;
        memory.words[instance+0xc]=wrapper;memory.words[instance+0x24]=out;memory.words[wrapper]=data;
        memory.words[data]=0x206c6b73;memory.words[data+4]=1160;memory.words[data+8]=26;
        for(unsigned i=0;i<26;++i) {
            memory.words[data+16+44*i]=L::BoneWords[i];memory.words[data+56+44*i]=L::Hashes[i];
            for(unsigned j=0;j<12;++j)memory.words[out+i*48+j*4]=std::bit_cast<L::U32>(float(i*12+j));
        }
        const auto before=memory.words;
        L::Start();L::begins=L::Now();
        L::Hook hook;hook.Enter(memory,instance);
        Check(hook.current.instance==instance,"identify full human Link skeleton signature");
        for(unsigned i=0;i<25;++i)hook.MatrixBuilt(memory,instance,i,7);
        Check(L::pose_count==0,"incomplete pose is not published");
        hook.MatrixBuilt(memory,instance,25,7);
        Check(L::pose_count==1 && L::poses.back().bones[25][11]==311.f,"publish all 26 matched bones");
        Check(memory.words==before,"pose capture leaves guest memory unchanged");
        ++memory.words[data+56];hook.Enter(memory,instance);
        Check(!hook.current.instance,"different bone signature rejected");
        memory.words=before;hook.Enter(memory,instance);
        hook.MatrixBuilt(memory,instance,0,8);hook.MatrixBuilt(memory,instance,1,9);
        Check(!hook.current.instance && L::pose_count==1,"cross-frame partial pose rejected");
        hook.Enter(memory,instance);memory.words[out]=0x7fc00000;
        hook.MatrixBuilt(memory,instance,0,10);
        Check(!hook.current.instance,"nonfinite bone matrix rejected");
        L::active=false;hook.Enter(memory,instance);
        Check(!hook.current.instance,"capture disabled has no guest observation");
        // A generic three-bone actor exercises runtime topology and bind parsing.
        memory.words=before;
        memory.words[data+4]=16+44*3;memory.words[data+8]=3;
        for(unsigned i=0;i<3;++i) {
            memory.words[data+16+44*i]=i==0?0xffff0000:((i-1)<<16)|i;
            memory.words[data+56+44*i]=100+i;
            for(unsigned j=0;j<9;++j)
                memory.words[data+20+44*i+j*4]=std::bit_cast<L::U32>(j<3?1.f:0.f);
        }
        L::interpolate_actors=true;++L::interpolation_generation;
        hook.Enter(memory,instance);
        Check(hook.current.instance==instance && !hook.human,"generic skeleton accepted separately from human Link");
        hook.MatrixBuilt(memory,instance,0,20);hook.MatrixBuilt(memory,instance,0,20);
        hook.MatrixBuilt(memory,instance,1,20);
        Check(L::ForFrame(21).empty(),"duplicate bones cannot complete a partial actor pose");
        hook.MatrixBuilt(memory,instance,2,20);
        const auto generic=L::ForFrame(21);
        Check(generic.size()==1 && generic[0].words.size()==3,"complete runtime skeleton published on renderer frame plus one");
        Check(L::ForFrame(20).empty(),"actor pose uses correct renderer frame");
        ++L::interpolation_generation;
        Check(L::ForFrame(21).empty(),"toggle discards stale generic poses");
        memory.words[data+16+44]=0x10001;hook.Enter(memory,instance);
        Check(!hook.current.instance,"cyclic skeleton parent rejected");
        memory.words[data+8]=129;hook.Enter(memory,instance);
        Check(!hook.current.instance,"oversized skeleton rejected");
        L::interpolate_actors=false;L::live.clear();

    }
    {
        namespace L=MM3D::LinkPoseCapture;
        namespace I=MM3D::LinkInterpolation;
        const auto pose=[](double world_x,double camera_x,unsigned frame) {
            L::LivePose p{};p.model=99;p.pose.instance=42;p.pose.frame=frame;
            for(unsigned i=0;i<26;++i) {
                auto bone=Identity();for(unsigned j=0;j<3;++j)bone[j][j]=.01;
                bone[0][3]=world_x+10*i-camera_x;
                p.pose.bones[i]=MM3D::LinkPoseMath::Flatten(bone);
                p.bind[i]=MM3D::LinkPoseMath::Flatten(Identity());
            }
            return p;
        };
        const auto palette=[](const L::LivePose& p) {
            I::Palette values;for(unsigned i=0;i<10;++i)values[i]=MM3D::LinkPoseMath::Expand(p.pose.bones[i]);
            return values;
        };
        auto first=pose(0,0,0),second=pose(2,10,1);
        I::History h;
        const auto initial=h.Apply(1,0,Identity(),palette(first),{first},true);
        Check(!initial[0],"Link first pose seeds without inventing history");
        auto camera=Identity();camera[0][3]=10;
        const auto result=h.Apply(2,33,camera,palette(second),{second},true);
        Check(result[0].has_value(),"matched human Link pose interpolates");
        Check(std::abs((*result[0])[0][3]+9)<1e-8,"world-space Link midpoint avoids double camera motion");
        Check(std::abs((*result[0])[0][0]-.01)<1e-8,"Link model scale retained");
        Check(result[9] && std::abs((*result[9])[0][3]-81)<1e-8,"hierarchical child stays attached");
        Check(!h.Apply(2,33,camera,palette(second),{second},false)[0],"native comparison never interpolates Link");
        {
            auto equivalent=second;
            equivalent.pose.bones.resize(2);equivalent.bind.resize(2);
            auto base=Identity(),child=Identity(),bind=Identity();
            base[0][3]=10;child[0][3]=20;bind[0][3]=-10;
            equivalent.pose.bones[0]=MM3D::LinkPoseMath::Flatten(base);
            equivalent.pose.bones[1]=MM3D::LinkPoseMath::Flatten(child);
            equivalent.bind[0]=MM3D::LinkPoseMath::Flatten(Identity());
            equivalent.bind[1]=MM3D::LinkPoseMath::Flatten(bind);
            std::vector<Matrix> halves{base,child};
            halves[0][0][3]=5;halves[1][0][3]=15;
            Check(!I::Identify(base,equivalent),"equivalent bones remain ambiguous for actor identification");
            auto resolved=I::ResolvePalette(base,equivalent,halves,Identity());
            Check(resolved && (*resolved)[0][3]==5,"equivalent interpolated skin transforms accepted");
            halves[1][0][3]=16;
            Check(!I::ResolvePalette(base,equivalent,halves,Identity()),
                  "coincident current bones with different histories rejected");
        }
        I::History later_slots;
        auto later0=palette(first),later1=palette(second);
        later0[0][0][3]=later1[0][0][3]=999;
        later_slots.Apply(1,0,Identity(),later0,{first},true);
        const auto later=later_slots.Apply(2,33,camera,later1,{second},true);
        Check(!later[0] && later[1] && std::abs((*later[1])[0][3]-1)<1e-8,
              "later-slot actor bones interpolate without replacing unmatched slot zero");
        auto duplicate_later=second;duplicate_later.pose.instance=123;
        Check(!later_slots.Apply(2,33,camera,later1,{second,duplicate_later},true)[1],
              "later-slot ambiguity stays native");
        auto repeated=later1;
        for(unsigned i=1;i<10;++i)repeated[i]=later1[1];
        Check(!later_slots.Apply(2,33,camera,repeated,{second},true)[1],
              "repeated copies of one bone cannot identify an actor");
        auto other=second;other.pose.instance=43;
        Check(!h.Apply(2,33,camera,palette(second),{second,other},true)[0],"ambiguous model instances rejected");
        auto npc=palette(second);npc[0][0][3]=999;
        Check(!h.Apply(2,33,camera,npc,{second},true)[0],"unmatched NPC draw untouched");
        Check(!h.Apply(4,99,camera,palette(second),{second},true)[0],"missing Link frame breaks history");
        auto teleported=pose(1000,10,4);
        Check(!h.Apply(5,132,camera,palette(teleported),{teleported},true)[0],"Link teleport uses native pose");

        I::History crowd;
        auto a0=pose(0,0,0),b0=pose(1000,0,0);b0.pose.instance=77;
        auto a1=pose(2,0,1),b1=pose(1006,0,1);b1.pose.instance=77;
        // Use a different bone count and topology for the second actor.
        for(auto* p:{&b0,&b1}) {
            p->pose.bones.resize(3);p->bind.resize(3);p->words={0xffff0000,1,0x10002};
        }
        const auto short_palette=[](const L::LivePose& p) {
            I::Palette values;for(auto& m:values)m=Identity();
            for(unsigned i=0;i<p.pose.bones.size();++i)values[i]=MM3D::LinkPoseMath::Expand(p.pose.bones[i]);
            return values;
        };
        crowd.Apply(1,0,Identity(),palette(a0),{a0,b0},true);
        crowd.Apply(1,0,Identity(),short_palette(b0),{a0,b0},true);
        const auto ar=crowd.Apply(2,33,Identity(),palette(a1),{a1,b1},true);
        const auto br=crowd.Apply(2,33,Identity(),short_palette(b1),{a1,b1},true);
        Check(ar[0] && std::abs((*ar[0])[0][3]-1)<1e-8,"first actor retains its own history");
        Check(br[0] && std::abs((*br[0])[0][3]-1003)<1e-8,"second actor uses its own history and runtime skeleton");
        b1.signature=1;
        Check(!crowd.Apply(3,66,Identity(),short_palette(b1),{b1},true)[0],"reused model address with changed skeleton resets history");
        auto duplicate=a1;duplicate.pose.instance=99;
        Check(!crowd.Apply(3,66,Identity(),palette(a1),{a1,duplicate},true)[0],"overlapping identical actors are not guessed between");
        auto rotated=Identity();rotated[0][0]=rotated[1][1]=0;rotated[0][1]=-1;rotated[1][0]=1;
        const auto half=MM3D::LinkPoseMath::Half(Identity(),rotated);
        Check(half && std::abs((*half)[0][0]-std::sqrt(.5))<1e-8,"bone rotation uses quaternion midpoint");
        auto collapsed=Identity();collapsed[0][0]=0;
        Check(!MM3D::LinkPoseMath::Half(Identity(),collapsed),"collapsed bone does not produce invalid rotation");
    }
    Check(IsCompletedCapture(41,42),"draws precede completed-frame counter increment");
    Check(!IsCompletedCapture(42,42),"in-progress draw is not the completed frame");
    Check(!IsCompletedCapture(40,42),"stale captured scene is rejected");
    Check(!IsCompletedCapture({},42),"missing captured frame is rejected");
    Check(!IsCompletedCapture(0,0),"no completed frame at session start");
    PresentationPacer pacer;
    constexpr auto period=PresentationPacer::Period;
    Check(pacer.Deadline(0,true)==0,"first pair starts without extra buffering");
    Check(pacer.Deadline(5000000,true)==period,"submission work counts toward presentation interval");
    Check(pacer.Deadline(period+5000000,true)==2*period,"no added per-frame delay");
    Check(pacer.Deadline(10*period,true)==10*period,"stall resumes immediately without buffering again");
    Check(pacer.Deadline(10*period,false)==10*period,"native frame clears pacing");
    Check(pacer.Deadline(10*period+1,true)==10*period+1,"reenabling starts immediately");
    PresentationPacer late;
    late.Deadline(0,true);
    const auto arrival=period+5000000;
    Check(late.Deadline(arrival,true)==arrival,"late arrival receives no additional wait");
    Check(late.Deadline(arrival+1000000,true)==arrival+period,"late recovery avoids a short catch-up interval");
    PresentationClock clock;
    Check(clock.Next(1,0,true)==Presentation::Native,"first frame stays native");
    Check(clock.Next(2,33,true)==Presentation::Native,"wait for stable cadence");
    Check(clock.Next(3,66,true)==Presentation::Pair,"30 Hz scene emits midpoint then native");
    Check(clock.Next(3,82,true)==Presentation::Skip,"duplicate refresh cannot emit another pair");
    Check(clock.Next(4,99,true)==Presentation::Pair,"next game frame emits one pair");
    Check(clock.Next(5,115,true)==Presentation::Native,"native 60 Hz scene is not doubled");
    Check(clock.Next(6,131,true)==Presentation::Native,"60 Hz stays native");
    Check(clock.Next(7,300,true)==Presentation::Native,"stall resets cadence");
    Check(clock.Next(8,333,false)==Presentation::Native,"incomplete HUD or scene falls back");
    Check(clock.Next(9,366,true)==Presentation::Pair,"native fallback frames also establish stable cadence");
    Check(clock.Next(10,399,true)==Presentation::Pair,"stable recovery resumes");
    Check(clock.Next(1,432,true)==Presentation::Native,"session rewind resets cadence");
    clock.Reset();
    Check(clock.Next(1,432,true)==Presentation::Native,"toggle reset restores native frame");
    PresentationClock menu_clock;
    Check(menu_clock.Next(1,0,false)==Presentation::Native,"menu capture starts without a midpoint");
    Check(menu_clock.Next(2,33,false)==Presentation::Native,"missing midpoint remains native at 30 Hz");
    Check(menu_clock.Next(2,49,false)==Presentation::Native,"duplicate native refresh does not establish cadence");
    Check(menu_clock.Next(3,66,true)==Presentation::Pair,"first valid menu midpoint uses the observed 30 Hz cadence");
    Check(menu_clock.Next(4,99,false)==Presentation::Native,"invalid menu frame is never paired despite stable cadence");
    Check(menu_clock.Next(4,115,false)==Presentation::Native,"invalid frame is not skipped on a repeated refresh");
    Check(menu_clock.Next(5,132,true)==Presentation::Pair,"valid short closing animation resumes immediately");
    Check(menu_clock.Next(5,148,true)==Presentation::Skip,"recovered pair is presented only once");
    Check(menu_clock.Next(6,148,false)==Presentation::Native,"native 60 Hz interval breaks menu cadence");
    Check(menu_clock.Next(7,164,true)==Presentation::Native,"eligibility cannot double a native 60 Hz menu");
    Check(menu_clock.Next(8,197,true)==Presentation::Native,"one 30 Hz interval is insufficient after 60 Hz");
    Check(menu_clock.Next(9,230,true)==Presentation::Pair,"two 30 Hz intervals establish cadence again");
    Check(menu_clock.Next(11,263,true)==Presentation::Native,"missing guest frame resets menu cadence");
    PresentationClock guest_clock;
    Check(guest_clock.Next(1,0,false,0)==Presentation::Native,"guest clock starts with native frame");
    Check(guest_clock.Next(2,33,false,33)==Presentation::Native,"guest cadence includes incomplete captures");
    Check(guest_clock.Next(3,200,true,66)==Presentation::Pair,"host pause capture stall cannot discard a valid menu midpoint");
    Check(guest_clock.Next(3,216,true,66)==Presentation::Skip,"stalled pair is still presented only once");
    Check(guest_clock.Next(4,217,true,99)==Presentation::Pair,"host catch-up interval does not masquerade as guest 60 Hz");
    Check(guest_clock.Next(5,250,false,132)==Presentation::Native,"guest cadence never bypasses missing HUD or scene");
    Check(guest_clock.Next(6,283,true,165)==Presentation::Pair,"next complete menu frame immediately resumes");
    Check(guest_clock.Next(7,316,true,182)==Presentation::Native,"a slow host cannot disguise genuine guest 60 Hz");
    Check(guest_clock.Next(8,349,true,198)==Presentation::Native,"guest 60 Hz continues natively");
    Check(guest_clock.Next(9,382,true,231)==Presentation::Native,"guest 30 Hz needs two intervals to recover");
    Check(guest_clock.Next(10,415,true,264)==Presentation::Pair,"guest 30 Hz recovers after two intervals");
    Check(guest_clock.Next(11,448,true,364)==Presentation::Native,"actual guest stall resets cadence");
    Check(guest_clock.Next(12,481,true,397)==Presentation::Native,"one interval after guest stall remains native");
    Check(guest_clock.Next(13,514,true,430)==Presentation::Pair,"guest stall recovery");
    Check(guest_clock.Next(15,547,true,463)==Presentation::Native,"guest timestamp cannot bypass a missing frame");
    Check(guest_clock.Next(16,580,true,0)==Presentation::Native,"guest time rewind resets cadence");
    Check(guest_clock.Next(17,613,true)==Presentation::Native,"losing guest timing resets instead of mixing clock sources");
    Check(guest_clock.Next(18,646,true)==Presentation::Native,"fallback clock establishes fresh cadence");
    Check(guest_clock.Next(19,679,true)==Presentation::Pair,"fallback clock recovers");
    Check(guest_clock.Next(20,712,true,712)==Presentation::Native,"acquiring guest timing resets even when numeric times align");
    const auto a=Camera(10,30),b=Camera(30,50);
    Check(Rigid(a)&&Near(Multiply(a,InverseRigid(a)),Identity(),1e-10),"rigid inverse");
    Check(Near(Midpoint(a,b),Camera(20,40),1e-10),"midpoint position and rotation");
    Check(Near(Midpoint(Camera(170),Camera(-170)),Camera(180),1e-10),"short path across rotation wrap");
    auto p=Identity();p[0]={0,1.4281474,0,0};p[1]={-.8568886,0,0,0};p[2]={0,0,1.00025,5.00125};p[3]={0,0,-1,0};
    History h;
    Check(!h.Observe(1,0,a,p),"first frame seeds history");
    const auto adjusted=h.Observe(2,33,b,p);
    Check(adjusted.has_value(),"consecutive frames produce midpoint projection");
    // Independent expected path: transform world point with the known halfway
    // camera, versus current view plus corrected projection. All clip components agree.
    const std::array<double,4> world{7,11,-200,1};
    const auto expected=Multiply(p,InverseRigid(Camera(20,40)));
    const auto actual=Multiply(*adjusted,InverseRigid(b));
    for(unsigned i=0;i<4;++i) {
        double x=0,y=0;for(unsigned j=0;j<4;++j){x+=expected[i][j]*world[j];y+=actual[i][j]*world[j];}
        Check(std::abs(x-y)<1e-9,"intermediate projection equals direct midpoint rendering");
    }
    Check(h.Observe(2,34,b,p).has_value(),"same frame reuses camera without advancing history");
    Check(!h.Observe(2,34,Camera(31,50),p),"another camera in same frame is excluded");
    Check(!h.Observe(4,66,b,p),"frame gap resets interpolation");
    Check(!h.Observe(5,300,b,p),"long stall resets interpolation");
    Check(!h.Observe(6,333,Camera(120,50),p),"large camera cut excluded");
    Check(!h.Observe(7,366,Camera(120,1000),p),"teleport excluded");
    auto different=p;different[0][1]*=2;
    Check(!h.Observe(8,399,Camera(120,1000),different),"projection change resets interpolation");
    Check(!h.Observe(0,420,b,p),"session rewind resets interpolation");
    auto bad=a;bad[0][0]=std::numeric_limits<double>::quiet_NaN();
    Check(!h.Observe(1,430,bad,p),"nonfinite camera rejected");
    bad=a;bad[0][0]*=2;Check(!Rigid(bad),"scaled transform is not a camera pose");
    bad=Identity();bad[0][0]=-1;Check(!Rigid(bad),"reflection rejected");
    Check(Near(Midpoint(a,a),a,1e-10),"stationary camera remains stationary");
    auto fade=MM3D::WidescreenProbe::TransitionOverlay;
    Check(TransitionFade(fade,Identity()),"verified orthographic fade accepted");
    Check(TransitionFade(fade,p),"exact perspective Z-target bars are a late overlay");
    fade.textures=1;
    Check(!TransitionFade(fade,p),"textured perspective particle is not a fade");
    fade.textures=0;
    ++fade.vertices;
    Check(!TransitionFade(fade,Identity()),"different geometry is not a fade");
    fade=MM3D::WidescreenProbe::TransitionOverlay;fade.textures=1;
    Check(!TransitionFade(fade,Identity()),"textured HUD is not a fade");
    fade=MM3D::WidescreenProbe::TransitionOverlay;fade.height=320;
    Check(!TransitionFade(fade,Identity()),"bottom-screen cover excluded");
    Check(WorldProjection(p),"world perspective accepted");
    Check(!WorldProjection(Identity()),"orthographic HUD excluded");
    auto sky=p;sky[2]={0,0,1.0000893,5.0004468};
    {
        auto dome=Identity();for(unsigned i=0;i<3;++i)dome[i][i]=320;
        Check(OpaqueSkyDome(288,sky,dome,0),"opaque sky dome is background geometry");
        Check(!OpaqueSkyDome(336,sky,dome,0),"unverified dome geometry excluded");
        auto foreground=dome;foreground[0][3]=12.77704;foreground[1][3]=.012;
        Check(OpaqueSkyDome(288,sky,foreground,0),"first-person offset sky remains behind winter background");
        Check(!OpaqueSkyDome(288,sky,foreground,1),"textured foreground cannot borrow sky classification");
        auto ordinary=sky;ordinary[2]={0,0,1.0003878,5.001939};
        Check(!OpaqueSkyDome(288,ordinary,foreground,0),"world clipping range cannot borrow sky classification");
        auto scaled=foreground;scaled[0][0]*=1.02;
        Check(!OpaqueSkyDome(288,sky,scaled,0),"sky size guard remains strict");
        foreground=dome;foreground[0][0]=.15;
        Check(!OpaqueSkyDome(288,sky,foreground,0),"small foreground geometry is not sky");
        Check(!OpaqueSkyDome(288,Identity(),dome,0),"orthographic geometry is not sky");
    }
    Check(SameLens(p,sky),"sky clipping range shares world lens");
    History mixed;
    mixed.Observe(1,0,a,sky);
    const auto sky_mid=mixed.Observe(2,33,b,sky);
    const auto world_mid=mixed.Observe(2,33,b,p);
    Check(sky_mid && world_mid,"sky and world can share camera history");
    const auto delta=Multiply(InverseRigid(Midpoint(a,b)),b);
    Check(Near(*sky_mid,Multiply(sky,delta),1e-10),"sky retains own clipping planes");
    Check(Near(*world_mid,Multiply(p,delta),1e-10),"world retains own clipping planes");
    auto underwater=p;underwater[0][1]*=1.0151;underwater[1][0]*=1.04193;
    const auto underwater_mid=mixed.Observe(2,33,b,underwater);
    Check(underwater_mid && Near(*underwater_mid,Multiply(underwater,delta),1e-10),
          "underwater anisotropic lens retains world camera interpolation");
    auto bad_water=underwater;bad_water[0][1]*=1.2;
    Check(!mixed.Observe(2,33,b,bad_water),"large alternate lens rejected");
    bad_water=underwater;bad_water[0][2]=.01;
    Check(!mixed.Observe(2,33,b,bad_water),"skewed alternate lens rejected");
    bad_water=underwater;bad_water[2]=sky[2];
    Check(!mixed.Observe(2,33,b,bad_water),"same clip range cannot bypass lens guard");
    Check(!mixed.Observe(2,33,a,underwater),"alternate lens requires identical camera");
    Check(!mixed.Observe(2,33,b,Identity()),"alternate lens rejects orthographic UI");
    Check(mixed.Observe(3,66,a,p).has_value(),"different first draw range preserves history");
    {
        History water;
        auto before=p;before[0][1]=1.44587;before[1][0]=-.834802;
        auto after=before;after[0][1]=1.44838;after[1][0]=-.83559;
        Check(!GradualLensScale(before,after),"captured underwater wobble is not uniform zoom");
        Check(!water.Observe(1,0,a,before),"underwater history seeds natively");
        const auto result=water.Observe(2,33,b,after);
        auto half=after;half[0][1]=(before[0][1]+after[0][1])*.5;
        half[1][0]=(before[1][0]+after[1][0])*.5;
        Check(result && Near(*result,Multiply(half,delta),1e-9),
              "underwater camera and independent lens axes interpolate together");
        const auto repeat=water.Observe(2,33,b,after);
        Check(repeat && Near(*repeat,*result,1e-10),"same-frame underwater draws reuse midpoint");
        auto clipped=after;clipped[2]=sky[2];
        const auto alternate=water.Observe(2,33,b,clipped);
        half[2]=sky[2];
        Check(alternate && Near(*alternate,Multiply(half,delta),1e-9),
              "underwater clipping remains specific to each draw");
        Check(!water.Observe(4,99,b,after),"underwater gaps still require reacquisition");
        auto invalid=after;invalid[0][1]*=1.06;
        Check(!GradualWorldWobble(before,invalid),"abrupt aspect change excluded from wobble");
        invalid=after;invalid[1][0]=before[1][0];
        Check(!GradualWorldWobble(before,invalid),"single-axis resize excluded from wobble");
        invalid=after;invalid[0][2]=.01;
        Check(!GradualWorldWobble(before,invalid),"projection skew excluded from wobble");
        Check(!GradualWorldWobble(before,clipped),"cross-frame clipping changes excluded from wobble");
        invalid=after;invalid[1][0]=-invalid[1][0];
        Check(!GradualWorldWobble(before,invalid),"flipped projection excluded from wobble");
        invalid=after;invalid[0][1]=std::numeric_limits<double>::quiet_NaN();
        Check(!GradualWorldWobble(before,invalid),"nonfinite wobble excluded");
        Check(!GradualWorldWobble(Identity(),Identity()),"orthographic UI excluded from wobble");
        History cut;cut.Observe(1,0,a,before);
        Check(!cut.Observe(2,33,Camera(0,1000),after),"underwater camera cut still rejected");
        auto swimming=before;
        swimming[2]={0,0,1.0004167556762695,5.002084255218506};
        swimming[0][1]=1.360515;swimming[1][0]=-.819312;
        auto next=swimming;next[0][1]=1.426104;next[1][0]=-.820839;
        History strong;strong.Observe(1,0,a,swimming);
        const auto strong_mid=strong.Observe(2,33,b,next);
        auto strong_half=next;
        strong_half[0][1]=(swimming[0][1]+next[0][1])*.5;
        strong_half[1][0]=(swimming[1][0]+next[1][0])*.5;
        Check(strong_mid && Near(*strong_mid,Multiply(strong_half,delta),1e-9),
              "captured strong swimming distortion keeps camera and lens interpolation");
        auto other=swimming,other_next=next;
        other[2]=other_next[2]={0,0,1.0003907680511475,5.001953601837158};
        Check(GradualWorldWobble(other,other_next).has_value(),"swimming distortion does not depend on scene draw distance");
        for(const auto depth:{std::array<double,2>{1.000735878944397,5.003678798675537},
                              std::array<double,2>{1.0005003213882446,5.002501964569092}}) {
            other=swimming;other_next=next;
            other[2]=other_next[2]={0,0,depth[0],depth[1]};
            other[0][1]=1.607111;other[1][0]=-.977137;
            other_next[0][1]=1.545353;other_next[1][0]=-.992935;
            History entry;entry.Observe(1,0,a,other);
            auto expected=other_next;
            expected[0][1]=(other[0][1]+other_next[0][1])*.5;
            expected[1][0]=(other[1][0]+other_next[1][0])*.5;
            const auto actual=entry.Observe(2,33,b,other_next);
            Check(actual && Near(*actual,Multiply(expected,delta),1e-9),"water-entry wobble keeps midpoint at both captured draw distances");
            other[2][3]=other_next[2][3]=10;
            Check(!GradualWorldWobble(other,other_next),"unrelated near-plane family does not receive stronger wobble allowance");
        }
        other_next=next;other_next[0][1]=swimming[0][1]*1.06;
        Check(!GradualWorldWobble(swimming,other_next),"strong underwater distortion remains bounded");
        other_next=next;other_next[1][0]=swimming[1][0];
        Check(!GradualWorldWobble(swimming,other_next),"strong underwater exception excludes single-axis resize");
    }
    auto ortho=Identity();
    Check(!SameLens(p,ortho),"orthographic lens is unrelated to world");
    History zoom;
    zoom.Observe(1,0,a,p);
    auto zoomed=p;zoomed[0][1]*=1.01;zoomed[1][0]*=1.01;
    const auto zoom_mid=zoom.Observe(2,33,b,zoomed);
    auto halfway_lens=p;halfway_lens[0][1]*=1.005;halfway_lens[1][0]*=1.005;
    Check(zoom_mid && Near(*zoom_mid,Multiply(halfway_lens,delta),1e-9),
          "gradual FOV zoom interpolates lens and camera together");
    auto zoom_sky=zoomed;zoom_sky[2]=sky[2];
    auto half_sky=halfway_lens;half_sky[2]=sky[2];
    const auto sky_zoom_mid=zoom.Observe(2,33,b,zoom_sky);
    Check(sky_zoom_mid && Near(*sky_zoom_mid,Multiply(half_sky,delta),1e-9),
          "zoom preserves each draw's near and far clipping planes");
    for(unsigned i=3;i<100;++i) {
        zoomed[0][1]*=.999;zoomed[1][0]*=.999;
        Check(zoom.Observe(i,i*33,b,zoomed).has_value(),"entry zoom does not restart history every frame");
    }
    auto sudden=zoomed;sudden[0][1]*=1.5;sudden[1][0]*=1.5;
    Check(!zoom.Observe(100,3300,b,sudden),"abrupt FOV cut still falls back");
    auto aspect=p;aspect[0][1]*=1.01;
    Check(!GradualLensScale(p,aspect),"aspect change is not mistaken for zoom");
    Check(!GradualLensScale(p,Identity()),"orthographic switch is not zoom");
    auto narrow=p;
    narrow[0][1]=3.;narrow[1][0]=-1.8;
    auto closer=narrow;closer[0][1]*=1.056;closer[1][0]*=1.056;
    Check(GradualLensScale(narrow,closer).has_value(),
          "steady narrow transformation zoom exceeding five percent is accepted");
    Check(GradualLensScale(closer,narrow).has_value(),"reverse narrow zoom is accepted");
    auto jump=narrow;jump[0][1]*=1.11;jump[1][0]*=1.11;
    Check(!GradualLensScale(narrow,jump),"relative cap still rejects narrow lens jumps");
    auto wide=p;wide[0][1]=1.;wide[1][0]=-.6;
    jump=wide;jump[0][1]*=1.056;jump[1][0]*=1.056;
    Check(!GradualLensScale(wide,jump),"larger angular lens change remains a cut");
    closer[1][0]*=1.01;
    Check(!GradualLensScale(narrow,closer),"narrow zoom cannot change aspect");
    // Room transitions can change projection or temporarily invalidate the CPU
    // snapshot for many frames. Keep displaying native geometry, without blending
    // cameras from different rooms, and resume midpoint rendering when stable.
    History transition;
    bool fallback=false;
    Check(Near(transition.Preview(1,0,a,p,true,false,fallback),p,0)&&fallback,
          "first visible scene renders immediately");
    Check(Near(transition.Preview(2,33,b,p,true,true,fallback),p,0)&&fallback,
          "comparison always uses native projection");
    Check(!Near(transition.Preview(3,66,a,p,true,false,fallback),p,1e-10)&&!fallback,
          "validated continuous camera interpolates");
    for(unsigned i=4;i<154;++i) {
        auto animated=p;animated[0][1]+=i*.001;
        Check(Near(transition.Preview(i,i*33,b,animated,true,false,fallback),animated,0)&&fallback,
              "changing transition projection never hides scene");
    }
    Check(Near(transition.Preview(154,5082,b,p,false,false,fallback),p,0)&&fallback,
          "unmatched CPU camera uses native projection");
    Check(Near(transition.Preview(155,5115,a,p,true,false,fallback),p,0)&&fallback,
          "reacquisition seeds fresh history");
    transition.Preview(156,5148,b,p,true,false,fallback);
    Check(!fallback,"midpoint resumes on next compatible frame");
    transition.Reset();
    Check(Near(transition.Preview(157,5181,a,p,true,false,fallback),p,0)&&fallback,
          "reset never reuses previous room camera");
    auto logo_camera=Identity();logo_camera[2][3]=30;
    Matrix logo_lens{};logo_lens[0][1]=2.390578508377075;logo_lens[1][0]=-1.43434739112854;
    logo_lens[2][2]=1.0003907680511475;logo_lens[2][3]=5.001953601837158;logo_lens[3][2]=-1;
    Check(TitleLogoCamera(logo_camera,logo_lens),"captured title camera recognized");
    Check(!TitleLogoCamera(a,logo_lens),"world camera excluded from title overlay");
    Check(!TitleLogoCamera(logo_camera,Identity()),"orthographic draw excluded from title camera");
    auto wrong_logo=logo_camera;wrong_logo[0][3]=1;
    Check(!TitleLogoCamera(wrong_logo,logo_lens),"translated world view excluded");
    History mixed_cameras;
    mixed_cameras.Preview(1,0,a,p,true,false,fallback);
    auto auxiliary=a;auxiliary[0][3]+=1000;
    Check(Near(mixed_cameras.Preview(1,1,auxiliary,p,false,false,fallback),p,0)&&fallback,
          "auxiliary camera remains native");
    mixed_cameras.Preview(2,33,b,p,true,false,fallback);
    Check(!fallback,"auxiliary draw does not erase previous validated world camera");
    mixed_cameras.Preview(2,34,auxiliary,p,false,false,fallback);
    Check(fallback,"rejected draw cannot inherit world interpolation");
    mixed_cameras.Preview(2,35,b,p,true,false,fallback);
    Check(!fallback,"world draw after auxiliary draw retains same-frame midpoint");
    mixed_cameras.Preview(3,66,auxiliary,p,false,false,fallback);
    mixed_cameras.Preview(4,99,a,p,true,false,fallback);
    Check(fallback,"whole rejected frame forces fresh acquisition");
    mixed_cameras.Preview(5,132,b,p,true,false,fallback);
    Check(!fallback,"interpolation resumes after two consecutive validated frames");
    mixed_cameras.Preview(5,133,auxiliary,p,false,false,fallback);
    mixed_cameras.Preview(6,165,auxiliary,p,true,false,fallback);
    Check(fallback,"validated camera cut still falls back after auxiliary draw");
    std::cout<<"PASS camera midpoint math, wrapped rotation, history and cut rejection\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
