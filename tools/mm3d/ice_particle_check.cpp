// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/ice_particle_interpolation.h"
#include <cassert>
#include <iostream>
namespace I=MM3D::IceParticleInterpolation;
I::Quad Puff(float x,int tile,float alpha=.4f) {
    I::Quad q{};
    for(unsigned v=0;v<4;++v) {
        q.indices[v]=v;
        q.vertices[v][0]={x+40*(v/2),40.f*(v%2),0,1};
        q.vertices[v][1]={120.f/255,96.f/255,48.f/255,alpha};
        q.vertices[v][2]={(tile%4)*.25f+(v/2)*.25f,1-(tile/4)*.25f-(v%2)*.25f,0,1};
    }
    return q;
}
int main() {
    for(bool blend:{false,true}) {
        I::History h;std::vector<I::Quad> previous;
        const auto old=Puff(0,0),now=Puff(8,1,.42f),birth=Puff(1000,0);
        auto* output=blend?&previous:nullptr;
        assert(!h.Apply(1,1,0,I::Kind::Breath,{old},output)&&previous.empty());
        const auto result=h.Apply(2,1,0,I::Kind::Breath,{now,birth},output);assert(result);
        assert(I::Near((*result)[0].vertices[0][0][0],4));
        assert(I::Near((*result)[0].vertices[0][1][3],blend?.205f:.41f));
        assert((*result)[0].vertices[0][2]==now.vertices[0][2]);
        assert(MM3D::BubbleInterpolation::Same((*result)[1],birth));
        if(blend) {
            assert(previous.size()==2);
            for(unsigned v=0;v<4;++v) {
                assert(previous[0].vertices[v][0]==(*result)[0].vertices[v][0]);
                assert(previous[0].vertices[v][2]==old.vertices[v][2]);
                assert(previous[1].vertices[v][1][3]==0);
                // Verified SourceAlpha/One composition: equal old/current
                // contributions, with unchanged brightness, even on overlap.
                for(float a:{0.f,.3f,1.f})for(float b:{0.f,.7f,1.f}) {
                    const float actual=a*previous[0].vertices[v][1][3]+b*(*result)[0].vertices[v][1][3];
                    assert(I::Near(actual,.41f*(a+b)*.5f));
                }
            }
            // Repeated native submissions do not advance history or split
            // already weighted values a second time.
            const auto again=h.Apply(2,1,0,I::Kind::Breath,{now,birth},&previous);assert(again);
            assert(MM3D::BubbleInterpolation::Same((*again)[0],(*result)[0]));
        }
        const auto same=h.Apply(3,1,0,I::Kind::Breath,{Puff(16,1,.42f)},output);assert(same);
        assert(previous.empty()&&I::Near((*same)[0].vertices[0][1][3],.42f));
        assert(!h.Apply(5,1,0,I::Kind::Breath,{Puff(24,2)},output)&&previous.empty());
        assert(!h.Apply(6,1,1,I::Kind::Breath,{Puff(32,3)},output)&&previous.empty());
    }
    // A jump across unrelated atlas frames must not blend.
    I::History h;std::vector<I::Quad> previous;
    h.Apply(1,1,0,I::Kind::Breath,{Puff(0,0)},&previous);
    assert(!h.Apply(2,1,0,I::Kind::Breath,{Puff(8,4)},&previous)&&previous.empty());
    // The same atlas transition in nonadditive freeze mist retains one sample.
    I::History mist;mist.Apply(1,1,0,I::Kind::FreezeMist,{Puff(0,0)},&previous);
    const auto unchanged=mist.Apply(2,1,0,I::Kind::FreezeMist,{Puff(8,1)},&previous);
    assert(unchanged&&previous.empty()&&I::Near((*unchanged)[0].vertices[0][1][3],.4f));
    std::cout<<"Ice breath: matched atlas blending, conserved additive weight, births, repeats, gaps and material isolation passed\n";
}
