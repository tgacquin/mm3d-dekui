// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

namespace MM3D::SimpleCheats {
using U32=std::uint32_t;
enum Action : unsigned { Rupees500, Rupees0, Heal, Hearts20, Magic, Masks, Items,
    Defense, Gilded, Razor, Songs, Bags, DungeonKeys, RemoveDungeonMap, RemoveDungeonCompass, Count };
inline constexpr std::array<const char*,Count> Labels{
    "500 rupees", "0 rupees", "Refill hearts", "20 hearts + refill", "Refill magic",
    "All masks", "All items", "Enhanced defense",
    "Mirror Shield + Gilded Sword", "Mirror Shield + Razor Sword",
    "All boss remains and songs", "Largest quiver and bomb bag",
    "All dungeon small keys + boss keys", "Remove current dungeon map", "Remove current dungeon compass"};
inline constexpr std::array<const char*,Count> Help{
    "Sets carried rupees to 500.", "Clears carried rupees and pending rupee changes.",
    "Refills health to your current maximum.", "Sets maximum health to 20 hearts and refills it.",
    "Refills the magic meter if you have already unlocked magic.",
    "Adds all 24 masks.", "Adds the usable items and replaces all seven bottle slots with empty bottles.",
    "Enables enhanced defense.", "Sets sword and shield equipment; reopen the equipment screen to refresh it.",
    "Sets sword and shield equipment; reopen the equipment screen to refresh it.",
    "Grants the four remains and song flags; preserves heart pieces and notebook progress.",
    "Upgrades quiver and bomb bag capacity; preserves wallet, stick and nut upgrades.",
    "Gives 99 small keys and the boss key for each of the four temples. Preserves maps, compasses and stray fairies.",
    "Removes only this temple's map. Reopen the map screen to refresh it.",
    "Removes only this temple's compass. Reopen the map screen to refresh it."};
struct Ammo {const char* label;U32 count;U32 slot;U32 item;U32 amount;};
// USA Restoration uses the 0x775xxx save layout, not the supplied v1.1
// 0x776xxx layout. Inventory IDs and equipment masks match game/common_data.h.
inline constexpr std::array<Ammo,7> Ammunition{{
    {"Infinite arrows",0x775391,0x775355,1,99},
    {"Infinite bombs",0x775396,0x77535a,6,99},
    {"Infinite Bombchus",0x775397,0x77535b,7,99},
    {"Infinite Deku nuts",0x775399,0x77535d,9,50},
    {"Infinite Deku sticks",0x775398,0x77535c,8,99},
    {"Infinite magic beans",0x77539a,0x77535e,10,99},
    {"Infinite Powder Kegs",0x77539c,0x775360,12,99}
}};
inline std::atomic<U32> pending{0}, continuous{0};
inline std::atomic<bool> ready{false};
inline std::atomic<bool> dungeon_ready{false};
inline std::atomic<bool> moon_jump{false};
inline std::atomic<int> result{0}; // 0 idle, 1 applied, -1 unsupported save, -2 no current temple.
inline void Reset(){pending=0;continuous=0;ready=false;dungeon_ready=false;moon_jump=false;result=0;}
inline void Queue(Action a){if(a<Count)pending.fetch_or(1u<<a);}
inline constexpr std::array<std::pair<U32,U32>,7> Guards{{
    {0x4bfab4,0xe92d4ff0},{0x4bfabc,0xe1a04000},{0x4bfdbc,0x7752d8},
    {0x4bfdc0,0x626c3c},{0x58d920,0x7752d8},
    {0x7d0910,0xe2804c01},{0x7d08f8,0xebfffa1b}
}};
template<class M> bool CodeMatches(M& m){for(auto [a,w]:Guards)if(m.Read(a)!=w)return false;return true;}
template<class M> std::optional<U32> Get(M& m,U32 at,unsigned bytes) {
    auto w=m.Read(at&~3u);if(!w)return {};
    return (*w>>((at&3)*8))&(bytes==1?255u:65535u);
}
template<class M> void Set(M& m,U32 at,U32 value,unsigned bytes) {
    const U32 shift=(at&3)*8,mask=(bytes==1?255u:65535u)<<shift;
    if(auto word=m.Read(at&~3u))m.Write(at&~3u,(*word&~mask)|((value<<shift)&mask));
}
template<class M> bool InGame(M& m) {
    auto context=m.Read(0x72f5c0);
    if(!context||*context<0x100000||*context>0x3ffff000||(*context&3))return false;
    auto game=m.Read(*context+0x16c);
    if(!game||*game<0x100000||*game>0x3ffff000||(*game&3))return false;
    auto state=m.Read(*game+0x13c);
    auto health=Get(m,0x775312,2);
    return state&&(*state&65535)==0x0402&&health&&*health>=16&&*health<=320;
}
inline constexpr std::array<std::pair<U32,U32>,6> DungeonGuards{{
    {0x233e08,0xe59f2cf4},{0x233f30,0xe1d225b2},
    {0x234b04,0x7888d8},{0x234b18,0x7753d4},
    {0x626c40,2},{0x626c44,4} // native compass/map grant masks
}};
template<class M> std::optional<U32> CurrentDungeon(M& m) {
    if(!InGame(m))return {};
    for(auto [at,word]:DungeonGuards)if(m.Read(at)!=word)return {};
    const auto context=m.Read(0x72f5c0),game=m.Read(*context+0x16c);
    const auto scene=Get(m,*game+0x148,2);
    if(!scene)return {};
    // The native dungeon index can remain cached outside a temple. Accept
    // only temple scenes (both Stone Tower orientations) and their boss rooms.
    switch(*scene) {
    case 0x1b:case 0x1f:case 0x21:case 0x44:case 0x49:case 0x5f:
    case 0x16:case 0x18:case 0x36:break;
    default:return {};
    }
    // Same halfword used by the native map/compass/key grant code, not the
    // currently highlighted floor or a guessed inventory index from the scene.
    const auto index=Get(m,0x7888d8+0x52,2);
    if(!index||*index>=4||!Get(m,0x7753d4+*index,1))return {};
    return index;
}
template<class M> void FillMagic(M& m) {
    auto unlocked=Get(m,0x77531e,1),upgrade=Get(m,0x77531f,1);
    if(unlocked&&*unlocked&&upgrade)Set(m,0x775317,*upgrade?96:48,1);
}
// Called from Restoration's guarded pad-update hook, once per game update.
// The slow general cheat timer is unsuitable for a steady ascent. Only change
// vertical velocity: releasing the chord leaves native gravity/collision intact.
template<class M> void MoonJump(M& m) {
    if(!moon_jump || !CodeMatches(m) || !InGame(m))return;
    const auto buttons=m.Read(0x72a008);
    if(!buttons || (*buttons&0x201)!=0x201)return; // L + A
    const auto context=m.Read(0x72f5c0),game=m.Read(*context+0x16c);
    const auto pause=Get(m,*game+0xaac,2),player=m.Read(*game+0x20dc);
    if(!pause || (*pause&3) || !player || *player<0x100000 || *player>0x3ffe0000 || (*player&3))return;
    const auto id=m.Read(*player),flags=m.Read(*player+0x11db0),speed=m.Read(*player+0x68);
    if(!id || (*id&0xffffff)!=0x020000 || !flags || (*flags&0x30800081) || !speed ||
       !std::isfinite(std::bit_cast<float>(*speed)) || Get(m,0x775314,2).value_or(0)==0)return;
    m.Write(*player+0x68,std::bit_cast<U32>(12.f));
}
template<class M> void Apply(M& m,Action action) {
    switch(action) {
    case Rupees500: case Rupees0:
        Set(m,0x775318,action==Rupees500?500:0,2);Set(m,0x7751fc,0,2);break;
    case Hearts20:Set(m,0x775312,320,2);[[fallthrough]];
    case Heal:if(auto cap=Get(m,0x775312,2))Set(m,0x775314,*cap,2);break;
    case Magic:FillMagic(m);break;
    case Masks:for(U32 i=0;i<24;++i)Set(m,0x77536c+i,0x32+i,1);break;
    case Items:
        for(U32 i:{1u,2u,3u,4u,6u,7u,8u,9u,10u,12u,13u,14u,15u,16u})Set(m,0x775354+i,i,1);
        for(U32 i=0;i<7;++i)Set(m,0x775384+i,0x12,1);
        break;
    case Defense:Set(m,0x775320,1,1);break;
    case Gilded:case Razor:Set(m,0x775352,action==Gilded?0x23:0x22,1);break;
    case Songs:
        // Four remains and actual MM songs, including the lullaby intro.
        if(auto flags=m.Read(0x7753d0))m.Write(0x7753d0,*flags|0x0101f7cfu);
        break;
    case Bags:
        if(auto upgrades=m.Read(0x7753cc))m.Write(0x7753cc,(*upgrades&~63u)|27u);
        break;
    case DungeonKeys:
        // Native item-grant code indexes these arrays at 0x233f30/0x233f4c.
        // Bit 0 is the boss key; the remaining dungeon-item bits stay intact.
        for(U32 i=0;i<4;++i) {
            Set(m,0x7753d4+i,Get(m,0x7753d4+i,1).value_or(0)|1u,1);
            Set(m,0x7753de + i,99,1);
        }
        break;
    case RemoveDungeonMap:case RemoveDungeonCompass:
        if(const auto dungeon=CurrentDungeon(m)) {
            const U32 address=0x7753d4+*dungeon,mask=action==RemoveDungeonMap?4u:2u;
            if(const auto items=Get(m,address,1))Set(m,address,*items&~mask,1);
        }
        break;
    default:break;
    }
}
template<class M> void Tick(M& m,bool supported_title) {
    const U32 requests=pending.exchange(0);
    bool supported=supported_title&&CodeMatches(m)&&InGame(m);
    // Preflight every save word used by these cheats before making any writes.
    if(supported)for(U32 at=0x775300;at<=0x7753d0;at+=4)if(!m.Read(at)){supported=false;break;}
    if(supported&&!m.Read(0x7751fc))supported=false;
    if(supported && (requests&(1u<<DungeonKeys))) {
        if(m.Read(0x234b18)!=0x7753d4u || m.Read(0x234b1c)!=0x7753deu)supported=false;
        for(U32 at=0x7753d4;at<=0x7753e0;at+=4)if(!m.Read(at))supported=false;
    }
    ready=supported;dungeon_ready=false;
    if(!supported){if(requests)result=-1;return;}
    dungeon_ready=CurrentDungeon(m).has_value();
    if((requests&((1u<<RemoveDungeonMap)|(1u<<RemoveDungeonCompass)))&&!dungeon_ready) {
        result=-2;return;
    }
    for(unsigned i=0;i<Count;++i)if(requests&(1u<<i))Apply(m,Action(i));
    const U32 held=continuous.load();
    for(unsigned i=0;i<Ammunition.size();++i)if(held&(1u<<i)) {
        const auto& a=Ammunition[i];
        if(Get(m,a.slot,1)==a.item)Set(m,a.count,a.amount,1);
    }
    if(held&(1u<<7))Apply(m,Heal);
    if(held&(1u<<8))FillMagic(m);
    if(requests)result=1;
}
} // namespace MM3D::SimpleCheats
