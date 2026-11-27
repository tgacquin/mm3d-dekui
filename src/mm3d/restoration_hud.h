// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>
#include <cstring>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include "common/file_util.h"
#include "mm3d/qt_user_path.h"
#include "mm3d/titles.h"

namespace MM3D::RestorationHud {
inline QString Install(float scale, float magic_y=0.f, float magic_height=1.f, float dpad_y=0.f) {
    if(!std::isfinite(scale) || scale<.5f || scale>1.5f) return "Invalid HUD scale.";
    if(!std::isfinite(magic_y) || magic_y < -64.f || magic_y > 64.f) return "Invalid magic bar Y offset.";
    if(!std::isfinite(magic_height) || magic_height < .5f || magic_height > 2.f) return "Invalid magic bar height.";
    if(!std::isfinite(dpad_y) || dpad_y < -120.f || dpad_y > 120.f) return "Invalid D-pad Y offset.";
    const QString path=QtUserPath(FileUtil::UserPath::LoadDir)+
        "mods/"+QString::fromStdString(MM3D::Titles::SelectedHex())+"/romfs/layout/Joker.Main.New.gar";
    QFile source(path+".mm3d-hud-source");
    if(!source.open(QIODevice::ReadOnly)) return "HUD source backup missing; scale was not applied.";
    auto bytes=source.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()!=
       "3b5918df9251366f3688965bed882e56d1056d5e6dee2a479a8596cb884fc639")
        return "Unrecognized HUD source; scale was not applied.";
    // Native screen-space anchors, before the 16:9 margin shift. Transform whole
    // groups so their children, counters and animation tracks stay together.
    struct Group { unsigned offset; float anchor_x,anchor_y,shift; };
    constexpr float margin=(240.f*16.f/9.f-400.f)/2.f;
    constexpr Group groups[]{
        {0x1388,373,15,margin}, // item set
        {0x13cc,373,15,margin}, // ocarina
        {0x1498,20,42,-margin}, // D-pad
        {0x6ae8,20,42,-margin}, // Tatl reminder (Hud.chat_alart), follows D-pad
        {0x1c4c,8,230,-margin}, // rupees, bottom left
        {0x1e28,8,220,-margin}, // keys
        {0x2994,19,8,-margin},  // hearts and magic
        {0x6ee4,373,15,margin}, // action buttons
        {0x5c08,200,230,0},    // centered clock
    };
    const auto read=[&](unsigned at) { float v; std::memcpy(&v,bytes.constData()+at,4); return v; };
    const auto write=[&](unsigned at,float v) { std::memcpy(bytes.data()+at,&v,4); };
    for(const auto& g:groups) {
        const float x=read(g.offset),y=read(g.offset+4);
        write(g.offset,g.anchor_x+scale*(x-g.anchor_x)+g.shift);
        write(g.offset+4,g.anchor_y+scale*(y-g.anchor_y)+((g.offset==0x1498 || g.offset==0x6ae8) ? dpad_y : 0.f));
        write(g.offset+12,read(g.offset+12)*scale);
        write(g.offset+16,read(g.offset+16)*scale);
    }
    // Hud2.magic_gauge_g Y scale; this parent contains both the frame and fill.
    write(0x2f7c,magic_height);
    // The meter is its own child of Hud2.heart_g. Move only this named group;
    // hearts, counters, and the parent animation remain untouched.
    unsigned magic_count=0;
    for(size_t start=0;;) {
        const auto found=bytes.indexOf("MFL ",qsizetype(start));
        if(found<0) break;
        start=static_cast<size_t>(found);
        if(start+32>static_cast<size_t>(bytes.size())) break;
        const auto u16=[&](size_t at) { return static_cast<unsigned char>(bytes[at]) |
            (static_cast<unsigned char>(bytes[at+1])<<8); };
        const auto u32=[&](size_t at) { return static_cast<unsigned>(static_cast<unsigned char>(bytes[at])) |
            (static_cast<unsigned>(static_cast<unsigned char>(bytes[at+1]))<<8) |
            (static_cast<unsigned>(static_cast<unsigned char>(bytes[at+2]))<<16) |
            (static_cast<unsigned>(static_cast<unsigned char>(bytes[at+3]))<<24); };
        const unsigned count=u16(start+10), panes=u16(start+14), string_offset=u32(start+28);
        const size_t strings=start+string_offset;
        if(strings>=static_cast<size_t>(bytes.size())) break;
        const auto string_at=[&](size_t at) { return QByteArray(bytes.constData()+at,bytes.size()-static_cast<qsizetype>(at)); };
        const QByteArray layout=string_at(strings).split('\0').value(0);
        size_t cursor=strings;
        for(unsigned n=0;n<1+panes;++n) cursor=static_cast<size_t>(bytes.indexOf('\0',qsizetype(cursor)))+1;
        for(unsigned index=0;index<count && cursor<static_cast<size_t>(bytes.size());++index) {
            const QByteArray name=string_at(cursor).split('\0').value(0);
            if(layout=="Hud2" && name=="magic_gauge_g") {
                const size_t offset=start+0x20+index*0x44+12;
                float old; std::memcpy(&old,bytes.constData()+offset,4);
                write(static_cast<unsigned>(offset),old+magic_y); ++magic_count;
            }
            const auto end=bytes.indexOf('\0',qsizetype(cursor));
            if(end<0) break;
            cursor=static_cast<size_t>(end)+1;
        }
        start+=4;
    }
    if(magic_count!=1) return "Magic bar layout group was not found exactly once.";
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit())
        return "Could not save HUD layout; scale was not applied.";
    return {};
}
}
