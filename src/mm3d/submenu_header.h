// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <utility>
#include <QCryptographicHash>
#include <QFile>
#include <QSaveFile>
#include "common/file_util.h"
#include "mm3d/qt_user_path.h"
#include "mm3d/titles.h"

namespace MM3D::SubmenuHeader {
struct Options {
    float width=480.f, y=0.f;
    float title_scale=1.f, title_x=0.f, title_y=0.f;
    float time_scale=1.f, time_x=0.f, time_y=0.f;
};
struct Field { const char* label; const char* key; float Options::*member; float lo,hi; };
inline constexpr std::array<Field,8> Fields{{
    {"Background width","header_width",&Options::width,400,540},
    {"Header Y","header_y",&Options::y,-60,40},
    {"Title scale","header_title_scale",&Options::title_scale,.5f,1.3f},
    {"Title X","header_title_x",&Options::title_x,-80,80},
    {"Title Y","header_title_y",&Options::title_y,-40,40},
    {"Day/time scale","header_time_scale",&Options::time_scale,.6f,1.3f},
    {"Day/time X","header_time_x",&Options::time_x,-100,50},
    {"Day/time Y","header_time_y",&Options::time_y,-40,40},
}};
inline Options Original() { Options o; o.width=400; return o; }
inline Options Widescreen() { return {240.f*16.f/9.f,-6.f,.8f,-13.333333f,1.f,1.f,13.333333f,0.f}; }
constexpr unsigned Start=0xb2b8, Size=0x1a30;
// MFL v4 named widget records and geometry verified by inspect_submenu_header.py.
inline QString Patch(QByteArray& current,const QByteArray& baseline,const Options& o) {
    for(const auto& f:Fields) if(!std::isfinite(o.*f.member) || o.*f.member<f.lo || o.*f.member>f.hi)
        return "Header value out of range; no layout written.";
    if(baseline.size()<Start+Size || current.size()!=baseline.size() ||
       QCryptographicHash::hash(baseline.mid(Start,Size),QCryptographicHash::Sha256).toHex()!=
       "5879a0804b9d67b0ea711e2f694812a1dd721747a4104ea6938a5526dc804290")
        return "Unrecognized SubMenuTitle source; no layout written.";
    std::vector<std::pair<unsigned,float>> changes;
    const auto set=[&](unsigned at,float v) { changes.emplace_back(at,v); };
    const float margin=(o.width-400.f)/2.f;
    set(0x112c,-margin); set(0x1138,o.width); // menu_title_base geometry
    set(0x115c,-10.f-margin); set(0x1168,o.width+20.f); // shadow overhang
    set(0xcec,o.y); set(0xd30,o.y); // background and shadow widget Y
    const auto title=[&](unsigned widget,float anchor_x,float anchor_y) {
        set(widget+8,o.title_x+anchor_x*(1.f-o.title_scale));
        set(widget+12,o.y+o.title_y+anchor_y*(1.f-o.title_scale));
        set(widget+20,o.title_scale); set(widget+24,o.title_scale);
    };
    title(0x7d4,6.f,4.f); // title_text: visibility and atlas animation kept
    title(0x74c,4.f,4.f); // alternate hint title
    title(0xa8,4.f,4.f); // accompanying language-specific completion icon
    set(0x864,o.time_x+400.f*(1.f-o.time_scale)); // time_g, anchor right edge
    set(0x868,o.y+o.time_y+14.f*(1.f-o.time_scale));
    set(0x870,o.time_scale); set(0x874,o.time_scale);
    std::array<bool,Size> managed{};
    for(auto [at,v]:changes) for(unsigned byte=0;byte<4;++byte) managed[at+byte]=true;
    for(unsigned i=0;i<Size;++i) if(!managed[i] && current[Start+i]!=baseline[Start+i])
        return "SubMenuTitle has other edits; no layout written.";
    for(auto [at,v]:changes) std::memcpy(current.data()+Start+at,&v,4);
    return {};
}
inline QString Install(const Options& o) {
    const QString path=QtUserPath(FileUtil::UserPath::LoadDir)+
        "mods/"+QString::fromStdString(MM3D::Titles::SelectedHex())+"/romfs/layout/Joker.Main.gar";
    QFile source(path+".mm3d-header-source"), live(path);
    if(!source.open(QIODevice::ReadOnly) || !live.open(QIODevice::ReadOnly))
        return "Header archive or source backup missing; no layout written.";
    const auto baseline=source.readAll(); auto current=live.readAll(); live.close();
    const auto error=Patch(current,baseline,o); if(!error.isEmpty()) return error;
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(current)!=current.size() || !output.commit())
        return "Could not save header layout.";
    return {};
}
}
