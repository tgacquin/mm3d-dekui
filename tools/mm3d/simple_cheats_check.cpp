// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/simple_cheats.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unordered_map>
using namespace MM3D::SimpleCheats;
struct Memory {
    std::unordered_map<U32,U32> words;
    std::optional<U32> Read(U32 a){auto i=words.find(a);return i==words.end()?std::nullopt:std::optional<U32>(i->second);}
    void Write(U32 a,U32 v){assert(words.contains(a));words[a]=v;}
};
Memory Fixture() {
    Memory m;for(auto [a,w]:Guards)m.words[a]=w;
    m.words[0x72f5c0]=0x9000000;m.words[0x900016c]=0x9100000;m.words[0x910013c]=0x0402;
    for(U32 a=0x775300;a<=0x7753ec;a+=4)m.words[a]=0xa5a5a5a5;
    m.words[0x234b18]=0x7753d4;m.words[0x234b1c]=0x7753de;
    m.words[0x7751fc]=0x12340006;
    Set(m,0x775312,80,2);Set(m,0x775314,16,2);
    return m;
}
int main(int argc,char** argv) {
    auto m=Fixture();Reset();const auto initial=m.words;
    Tick(m,true);assert(ready&&m.words==initial);
    for(int bad=0;bad<5;++bad) {
        m=Fixture();Reset();Queue(Rupees500);continuous=511;
        if(bad==1)m.words[Guards[0].first]^=1;
        if(bad==2)m.words[0x910013c]=0x0102;
        if(bad==3)m.words.erase(0x775384);
        if(bad==4)Set(m,0x775312,0,2);
        const auto before=m.words;Tick(m,bad!=0);
        assert(!ready&&result==-1&&pending==0&&m.words==before);
    }
    m=Fixture();Reset();Queue(Rupees500);Tick(m,true);
    assert(Get(m,0x775318,2)==500&&m.words[0x775318]>>16==0xa5a5);
    assert(m.words[0x7751fc]==0x12340000);
    Set(m,0x775318,123,2);Tick(m,true);assert(Get(m,0x775318,2)==123); // Once only.
    Queue(Rupees0);Tick(m,true);assert(Get(m,0x775318,2)==0);
    Queue(Heal);Tick(m,true);assert(Get(m,0x775314,2)==80);
    Queue(Hearts20);Tick(m,true);assert(Get(m,0x775312,2)==320&&Get(m,0x775314,2)==320);
    for(U32 unlocked:{0u,1u})for(U32 upgrade:{0u,1u}) {
        Set(m,0x77531e,unlocked,1);Set(m,0x77531f,upgrade,1);Set(m,0x775317,4,1);
        Queue(Magic);Tick(m,true);assert(Get(m,0x775317,1)==(unlocked?(upgrade?96u:48u):4u));
    }
    m=Fixture();Reset();Queue(Masks);Queue(Items);Tick(m,true);
    for(U32 i=0;i<24;++i)assert(Get(m,0x77536c+i,1)==0x32+i);
    for(U32 i=0;i<7;++i)assert(Get(m,0x775384+i,1)==0x12);
    assert(Get(m,0x775354,1)==0xa5&&Get(m,0x77538b,1)==0xa5);
    assert(Get(m,0x775355,1)==1&&Get(m,0x775364,1)==16);
    Queue(Gilded);Tick(m,true);assert(Get(m,0x775352,1)==0x23&&Get(m,0x775353,1)==0xa5);
    Queue(Razor);Queue(Defense);Tick(m,true);assert(Get(m,0x775352,1)==0x22&&Get(m,0x775320,1)==1);
    const U32 upgrades=m.words[0x7753cc],quest=m.words[0x7753d0];
    Queue(Bags);Queue(Songs);Tick(m,true);
    assert(m.words[0x7753cc]==((upgrades&~63u)|27u));
    assert(m.words[0x7753d0]==(quest|0x0101f7cfu));
    for(unsigned i=0;i<Ammunition.size();++i) {
        const auto& a=Ammunition[i];continuous=1u<<i;
        Set(m,a.slot,255,1);Set(m,a.count,0,1);Tick(m,true);assert(Get(m,a.count,1)==0);
        Set(m,a.slot,a.item,1);Tick(m,true);assert(Get(m,a.count,1)==a.amount);
        continuous=0;Set(m,a.count,2,1);Tick(m,true);assert(Get(m,a.count,1)==2);
    }
    continuous=384;Set(m,0x77531e,1,1);Set(m,0x77531f,1,1);Tick(m,true);
    assert(Get(m,0x775314,2)==80&&Get(m,0x775317,1)==96);
    m=Fixture();Reset();
    Queue(DungeonKeys);Tick(m,true);assert(result==1);
    for(U32 i=0;i<4;++i) {
        assert(Get(m,0x7753d4+i,1)==0xa5);
        assert(Get(m,0x7753de + i,1)==99);
    }
    // Test a previously absent boss key and preserve every neighboring byte.
    Set(m,0x7753d5,6,1);Queue(DungeonKeys);Tick(m,true);
    assert(Get(m,0x7753d5,1)==7);
    for(U32 a=0x7753d8;a<0x7753ee;++a)
        if(a<0x7753de||a>0x7753e1)assert(Get(m,a,1)==0xa5);
    for(unsigned bad=0;bad<2;++bad) {
        m=Fixture();Reset();Queue(DungeonKeys);Queue(Heal);
        if(bad==0)m.words.erase(0x7753e0);else m.words[0x234b18]++;
        const auto before=m.words;Tick(m,true);assert(result==-1&&before==m.words);
    }
    const auto temple=[](U32 scene,U32 index) {
        auto m=Fixture();for(auto [at,word]:DungeonGuards)m.words[at]=word;
        m.words[0x9100148]=scene;m.words[0x788928]=(index<<16)|0x5678;
        m.words[0x7753d4]=0xffffffff;
        return m;
    };
    for(auto [scene,index]:std::array<std::pair<U32,U32>,9>{{
        {0x1b,0},{0x1f,0},{0x21,1},{0x44,1},{0x49,2},{0x5f,2},
        {0x16,3},{0x18,3},{0x36,3}}})for(Action action:{RemoveDungeonMap,RemoveDungeonCompass}) {
        m=temple(scene,index);Reset();Tick(m,true);assert(ready&&dungeon_ready);
        auto expected=m.words;const U32 mask=action==RemoveDungeonMap?4u:2u;
        expected[0x7753d4]&=~(mask<<(index*8));
        Queue(action);Tick(m,true);assert(result==1&&m.words==expected);
        // Other temple bytes, keys, fairy counts and all unrelated state stay exact.
        Queue(action);Tick(m,true);assert(m.words==expected); // idempotent
        Set(m,0x7753d4+index,255,1);const auto restored=m.words;
        Tick(m,true);assert(m.words==restored); // one-shot, not continuous
        Queue(RemoveDungeonMap);Queue(RemoveDungeonCompass);Tick(m,true);
        assert(Get(m,0x7753d4+index,1)==249); // boss key and other bits preserved
    }
    for(unsigned bad=0;bad<6;++bad) {
        m=temple(0x49,2);Reset();
        if(bad==0)m.words[0x9100148]=0x37; // cached index outside the temple
        if(bad==1)m.words[0x788928]=4u<<16;
        if(bad==2)m.words[0x788928]=0xffff0000;
        if(bad==3)m.words.erase(0x7753d4);
        if(bad==4)m.words[0x234b04]^=4;
        if(bad==5)m.words.erase(0x788928);
        Queue(RemoveDungeonMap);Queue(RemoveDungeonCompass);
        const auto before=m.words;Tick(m,true);
        assert(ready&&!dungeon_ready&&result==-2&&m.words==before);
    }
    const auto movement=[] {
        auto m=Fixture();m.words[0x72a008]=0x201;m.words[0x9100aac]=0;
        m.words[0x91020dc]=0x9200000;m.words[0x9200000]=0x00020000;
        m.words[0x9211db0]=0;m.words[0x9200068]=std::bit_cast<U32>(-3.f);
        m.words[0x9200064]=std::bit_cast<U32>(5.f);m.words[0x9200074]=std::bit_cast<U32>(-1.f);
        return m;
    };
    m=movement();Reset();auto native=m.words;MoonJump(m);assert(native==m.words);
    moon_jump=true;MoonJump(m);assert(m.words[0x9200068]==std::bit_cast<U32>(12.f));
    native[0x9200068]=std::bit_cast<U32>(12.f);assert(native==m.words);
    for(unsigned bad=0;bad<10;++bad) {
        m=movement();moon_jump=true;
        if(bad==0)m.words[0x72a008]=0x200;
        if(bad==1)m.words[0x72a008]=1;
        if(bad==2)m.words[0x9100aac]=1;
        if(bad==3)m.words[0x9211db0]=0x80;
        if(bad==4)m.words[0x9211db0]=0x20000000;
        if(bad==5)m.words[0x9200000]=0x00050001;
        if(bad==6)m.words[0x91020dc]=0xfffffffc;
        if(bad==7)m.words[Guards[0].first]^=1;
        if(bad==8)m.words[0x9200068]=0x7fc00000;
        if(bad==9)Set(m,0x775314,0,2);
        const auto before=m.words;MoonJump(m);assert(before==m.words);
    }
    Reset();assert(!ready&&!dungeon_ready&&pending==0&&continuous==0&&result==0&&!moon_jump);
    if(argc>1){std::ifstream f(argv[1],std::ios::binary);assert(f);
        const auto check=[&](const auto& guards){for(auto [a,w]:guards){U32 v=0;f.seekg(a-0x100000);f.read(reinterpret_cast<char*>(&v),4);assert(f&&v==w);}};
        check(Guards);check(DungeonGuards);}
    std::cout<<"Simple cheats: title/code/play guards, atomic preflight, one-shot effects, ownership, capacities, adjacent fields and session reset passed.\n";
}
