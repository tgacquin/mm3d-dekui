// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <iostream>
#include "mm3d/dungeon_map_interpolation.h"
namespace D=MM3D::DungeonMapInterpolation;
D::Vertices Quad(float x,float y,float u=0) {
    D::Vertices v(6);
    constexpr std::array<std::array<float,2>,6> corners{{{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}}};
    for(unsigned i=0;i<6;++i) {
        v[i].position={x+corners[i][0]*.1f,y+corners[i][1]*.1f,-1.f/3,1};
        v[i].uv={u+corners[i][0]*.1f,corners[i][1]*.1f};v[i].color={1,1,1,1};
    }
    return v;
}
int main() {
    using MM3D::CameraMidpoint::Matrix;
    // Matrices and material captured while panning three different floors.
    Matrix p{};p[0]={0,-.008333333767950535,0,1};p[1]={-.004999999888241291,0,0,1};
    p[2]={0,0,.0005555555690079927,.3333333432674408};p[3][3]=1;
    auto m=MM3D::CameraMidpoint::Identity();m[2][3]=-1200;
    MM3D::GlareInterpolation::Tev tev{{{238030384,0,65537,0,0},
        {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},
        {268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0},{268374015,0,0,0xffffffff,0}}};
    for(unsigned count:{12u,18u,42u,48u,108u})assert(D::ScreenDraw(0x89b1ab98da3d9241,0x6f0b398b1fc186a9,count,p,m,tev));
    auto bad=p;bad[3][2]=-1;assert(!D::ScreenDraw(0x89b1ab98da3d9241,0x6f0b398b1fc186a9,48,bad,m,tev));
    auto glare=tev;glare[1]={250547951,0,65537,0xffffffff,0};
    assert(!D::ScreenDraw(0x89b1ab98da3d9241,0x6f0b398b1fc186a9,72,p,m,glare));
    assert(!D::ScreenDraw(0x9b343fea686d98e0,0xac22b79e29209c01,6,p,m,tev));
    D::Identity id{0x1000000,0x2000000,1,0x100,1};
    D::Publish(10,2,id);assert(D::Current(10,2)==id&&D::Current(11,2)==id);
    assert(!D::Current(9,2)&&!D::Current(12,2)&&!D::Current(10,3));
    const D::Ownership ownership{1,2,3,4,5,6,7,8};
    D::Publish(11,2,{},ownership);assert(!D::Current(11,2));
    assert(D::CurrentStamp(11,2)->ownership==ownership);
    assert(!D::CurrentStamp(13,2));
    D::Publish(2,3,id);assert(!D::Current(10,2)&&D::Current(2,3)==id);
    D::History history;
    D::Key floor{id,10,20,2},marker{id,11,20,2};
    auto a=Quad(.2f,.3f),b=Quad(.8f,.9f,.5f);
    assert(!history.Apply(1,floor,a)&&!history.Apply(1,marker,b));
    auto moved=a,icons=b;
    for(auto* vertices:{&moved,&icons})for(auto& v:*vertices){v.position[0]+=.04f;v.position[1]-=.02f;}
    const auto native=moved,native_icons=icons;
    const auto half=history.Apply(2,floor,moved),icon_half=history.Apply(2,marker,icons);
    assert(half&&icon_half&&moved==native&&icons==native_icons);
    assert(history.result==D::Result::Accepted);
    for(unsigned i=0;i<6;++i) {
        assert(std::abs((*half)[i].position[0]-(a[i].position[0]+.02f))<.00001f);
        assert(std::abs((*icon_half)[i].position[1]-(b[i].position[1]-.01f))<.00001f);
        assert((*half)[i].uv==a[i].uv&&(*icon_half)[i].color==b[i].color);
    }
    assert(history.Apply(2,floor,moved)==half); // duplicate native submission
    for(unsigned field:{0u,1u,2u,3u,4u}) {
        D::History h;assert(!h.Apply(1,floor,a));auto next=floor;next.identity[field]++;
        assert(!h.Apply(2,next,moved)); // owner/resource/room/floor/zoom changes
    }
    for(unsigned variant=0;variant<6;++variant) {
        D::History h;assert(!h.Apply(1,floor,a));auto next=moved;auto key=floor;
        if(variant==0)next[0].uv[0]+=.1f;
        if(variant==1)next[0].position[0]+=.01f; // shape change, not a pan
        if(variant==2)for(auto& v:next)v.position[0]+=1; // large jump
        if(variant==3)next[0].position[0]=NAN;
        if(variant==4)next.insert(next.end(),b.begin(),b.end());
        if(variant==5)key.generation++;
        assert(!h.Apply(2,key,next));
        constexpr std::array reasons{D::Result::UV,D::Result::NonuniformPan,D::Result::Teleport,
            D::Result::Nonfinite,D::Result::VertexCount,D::Result::NoPreviousKey};
        assert(h.result==reasons[variant]);
    }
    // Native strip expansion includes invisible connectors and a stationary
    // end vertex. Separately rounded icons can move by six and seven pixels
    // in the same batch; every visible triangle must still reach its midpoint.
    const auto strip=[](const D::Vertices& left,const D::Vertices& right) {
        auto v=left;
        for(auto x:{left[5],left[5],right[0],left[5],right[0],right[0]})v.push_back(x);
        v.insert(v.end(),right.begin(),right.end());
        D::Vertex sentinel=right[5];sentinel.position={1,1,-1.f/3,1};
        for(auto x:{right[5],right[5],sentinel,right[5],right[5],sentinel})v.push_back(x);
        return v;
    };
    auto rounded_left=a,rounded_right=b;
    for(auto& v:rounded_left)v.position[0]-=6.f/120;
    for(auto& v:rounded_right)v.position[0]-=7.f/120;
    auto strip_before=strip(a,b),strip_after=strip(rounded_left,rounded_right);
    D::History strips;assert(!strips.Apply(1,marker,strip_before));
    const auto native_strip=strip_after;
    const auto strip_half=strips.Apply(2,marker,strip_after);
    assert(strip_half&&strip_after==native_strip);
    for(unsigned i=0;i<strip_after.size();++i) {
        const bool visible=i<6||(i>=12&&i<18);
        if(visible) {
            assert(std::abs((*strip_half)[i].position[0]-(strip_before[i].position[0]+strip_after[i].position[0])*.5f)<.00001f);
            assert((*strip_half)[i].uv==strip_after[i].uv);
        } else assert((*strip_half)[i]==strip_after[i]);
    }
    auto revealed=strip_after;revealed[6].position[1]+=.1f;
    D::History topology;assert(!topology.Apply(1,marker,strip_before));
    assert(!topology.Apply(2,marker,revealed)&&topology.result==D::Result::NonuniformPan);
    // A later independently matched triangle must obey the teleport bound too.
    auto jumped=rounded_right;for(auto& v:jumped)v.position[0]+=1;
    D::History distant;assert(!distant.Apply(1,marker,strip_before));
    assert(!distant.Apply(2,marker,strip(rounded_left,jumped))&&distant.result==D::Result::Teleport);
    // A native sort swaps two sprites with different atlas cells and depths.
    // Current triangle order and depth stay native; each follows its own old XY.
    auto high=b;for(auto& v:high)v.position[2]+=.00037f;
    auto pan_low=a,pan_high=high;
    for(auto* icon:{&pan_low,&pan_high})for(auto& v:*icon){v.position[0]+=.04f;v.position[1]-=.02f;}
    D::History reorder;auto old_order=strip(a,high),new_order=strip(pan_high,pan_low);
    assert(!reorder.Apply(1,marker,old_order));const auto reorder_half=reorder.Apply(2,marker,new_order);
    assert(reorder_half&&reorder.result==D::Result::Accepted);
    for(unsigned i=0;i<new_order.size();++i) {
        if(i<6||(i>=12&&i<18)) {
            const auto& old_vertex=i<6?high[i]:a[i-12];
            assert(std::abs((*reorder_half)[i].position[0]-(old_vertex.position[0]+new_order[i].position[0])*.5f)<.00001f);
            assert((*reorder_half)[i].position[2]==new_order[i].position[2]);
            assert((*reorder_half)[i].uv==new_order[i].uv);
        } else assert((*reorder_half)[i]==new_order[i]);
    }
    // Identical nearby sprites must not borrow each other's history. The
    // fallback refuses a genuinely ambiguous assignment instead of guessing.
    auto duplicate=a;for(auto& v:duplicate)v.position[0]+=.05f;
    assert(!D::ReorderedPan(strip(a,duplicate),strip(duplicate,a)));
    // Two initial candidates can be disambiguated by another forced match.
    auto far=a;for(auto& v:far)v.position[0]+=.3f;
    auto one=a,two=far;
    for(auto& v:one)v.position[0]+=.06f;
    for(auto& v:two)v.position[0]+=.06f;
    const auto forced=D::ReorderedPan(strip(a,far),strip(two,one));assert(forced);
    assert(std::abs((*forced)[0].position[0]-(far[0].position[0]+two[0].position[0])*.5f)<.00001f);
    auto wrong_order=new_order;wrong_order[0].uv[0]+=.01f;
    assert(!D::ReorderedPan(old_order,wrong_order));
    wrong_order=new_order;wrong_order[0].position[0]+=.01f;
    assert(!D::ReorderedPan(old_order,wrong_order));
    wrong_order=new_order;for(auto& v:wrong_order)v.position[0]+=2;
    assert(!D::ReorderedPan(old_order,wrong_order));
    D::History gaps;assert(!gaps.Apply(1,floor,a));assert(!gaps.Apply(3,floor,moved));
    assert(!gaps.Apply(2,floor,a));gaps.Reset();assert(!gaps.Apply(3,floor,moved));
    D::History ambiguous;assert(!ambiguous.Apply(1,floor,a));assert(!ambiguous.Apply(1,floor,b));
    assert(!ambiguous.Apply(2,floor,moved));assert(ambiguous.result==D::Result::Ambiguous);
    D::History homogeneous;auto bad_w=a;bad_w[0].position[3]=2;
    assert(!homogeneous.Apply(1,floor,bad_w)&&homogeneous.result==D::Result::HomogeneousW);
    std::cout<<"Dungeon map panning, synchronized markers, ownership and discontinuity checks passed\n";
}
