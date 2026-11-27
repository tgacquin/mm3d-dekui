// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/notebook.h"
#include "mm3d/notebook_paths.h"
#include "mm3d/titles.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "common/file_util.h"
#include "mm3d/qt_user_path.h"
#ifdef ANDROID
#include "common/android_utils.h"
#endif
#include <fmt/format.h>
#include <QSaveFile>

namespace MM3D::Notebook {
namespace {
constexpr std::size_t kMflStart = 0x997C;
constexpr std::size_t kX = 0x99E8;
constexpr std::size_t kY = 0x99EC;
constexpr std::size_t kScaleX = 0x99F4;
constexpr std::size_t kScaleY = 0x99F8;

QString OriginalLayout(const char* name) {
#ifdef ENABLE_MM3D_GAME_FOLDER
    return QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir)) +
        "setup/original/romfs/layout/" + QString::fromLatin1(name);
#elif defined(__ANDROID__)
    // Android has no desktop home/Documents path. The Android setup layer owns
    // this directory inside the selected SAF user tree when original assets are installed.
    return QString::fromStdString(AndroidUtils::TranslateFilePath(
        FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "mm3d-original/romfs/layout/")) +
        QString::fromLatin1(name);
#else
    return QDir::homePath()+"/Documents/cci-test/romfs/layout/"+QString::fromLatin1(name);
#endif
}

bool Valid(float value, float lo, float hi) {
    return std::isfinite(value) && value >= lo && value <= hi;
}

void Store(std::string& bytes, std::size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

float Load(const std::string& bytes, std::size_t offset) {
    float value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}
} // namespace

std::string EventsOverridePath() {
    return QtUserPath(FileUtil::UserPath::LoadDir).toStdString() +
        fmt::format("mods/{:016X}/romfs/layout/Joker.Main.Play.SubMenu.gar", MM3D::Titles::Selected().load());
}

std::string DefaultSourceArchive() {
    const auto path = OriginalLayout("Joker.Main.Play.SubMenu.gar").toStdString();
    return QFileInfo(QString::fromStdString(path)).isFile() ? path : std::string{};
}

bool Load(Settings& settings, const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return false;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const auto object = doc.object();
    settings.enabled = object.value("enabled").toBool(true);
    settings.events_scale = std::clamp(float(object.value("events_scale").toDouble(.70)), .1f, 2.f);
    settings.events_x = std::clamp(float(object.value("events_x").toDouble()), -512.f, 512.f);
    settings.events_y = std::clamp(float(object.value("events_y").toDouble()), -512.f, 512.f);
    settings.source_archive = Paths::Resolve(object.value("source_archive").toString(),
        "Joker.Main.Play.SubMenu.gar", OriginalLayout("Joker.Main.Play.SubMenu.gar")).toStdString();
    settings.output_archive = object.value("output_archive").toString().toStdString();
    settings.fade_source = Paths::Resolve(object.value("fade_source").toString(),
        "Joker.gar", OriginalLayout("Joker.gar")).toStdString();
    settings.fade_width = float(object.value("fade_width").toDouble(426.666667));
    if(!Valid(settings.fade_width,400.f,1600.f)) settings.fade_width=426.666667f;
    settings.telop_x=float(object.value("telop_x").toDouble(40));
    if(!Valid(settings.telop_x,-400.f,400.f)) settings.telop_x=40.f;
    settings.telop_y=float(object.value("telop_y").toDouble(0));
    if(!Valid(settings.telop_y,-120.f,120.f)) settings.telop_y=0.f;
    settings.telop_text_y=float(object.value("telop_text_y").toDouble(0));
    if(!Valid(settings.telop_text_y,-120.f,120.f)) settings.telop_text_y=0.f;
    settings.telop_brightness=float(object.value("telop_brightness").toDouble(1));
    if(!Valid(settings.telop_brightness,.3f,1.f)) settings.telop_brightness=1.f;
    settings.telop_scale=float(object.value("telop_scale").toDouble(1));
    if(!Valid(settings.telop_scale,.5f,1.5f)) settings.telop_scale=1.f;
    settings.telop_text_scale=float(object.value("telop_text_scale").toDouble(1));
    if(!Valid(settings.telop_text_scale,.5f,1.5f)) settings.telop_text_scale=1.f;
    return true;
}

bool Save(const Settings& settings, const std::string& path) {
    QSaveFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const auto bytes = QJsonDocument(QJsonObject{
        {"version", 1}, {"enabled", settings.enabled}, {"events_scale", settings.events_scale},
        {"events_x", settings.events_x}, {"events_y", settings.events_y},
        {"source_archive", Paths::Stored(QString::fromStdString(settings.source_archive), "Joker.Main.Play.SubMenu.gar")},
        {"output_archive", QString::fromStdString(settings.output_archive)},
        {"fade_source", Paths::Stored(QString::fromStdString(settings.fade_source), "Joker.gar")},
        {"fade_width",settings.fade_width}, {"telop_x",settings.telop_x},
        {"telop_scale",settings.telop_scale}, {"telop_text_scale",settings.telop_text_scale}, {"telop_y",settings.telop_y}, {"telop_text_y",settings.telop_text_y}, {"telop_brightness",settings.telop_brightness}}).toJson();
    return file.write(bytes) == bytes.size() && file.commit();
}

PatchResult PatchEventsArchive(const std::string& source, const std::string& output,
                               float x, float y, float scale) {
    if (source.empty())
        return {false, "Source archive path is required."};
    if (output.empty())
        return {false, "Output archive path is required."};
    if (QFileInfo(QString::fromStdString(source)).absoluteFilePath() ==
        QFileInfo(QString::fromStdString(output)).absoluteFilePath())
        return {false, "Refusing to overwrite the source archive."};
    if (!Valid(x, -512.f, 512.f) || !Valid(y, -512.f, 512.f) || !Valid(scale, .1f, 2.f))
        return {false, "Notebook transform values are outside the safe range."};

    QFile input(QString::fromStdString(source));
    if (!input.open(QIODevice::ReadOnly))
        return {false, "Could not open the source archive."};
    if (input.size() > 16 * 1024 * 1024)
        return {false, "Source archive is unexpectedly large."};
    std::string bytes = input.readAll().toStdString();
    if (bytes.size() <= kScaleY + sizeof(float) || bytes.compare(0, 4, "GAR\x02", 4) != 0)
        return {false, "Source is not the expected GAR v2 archive."};
    if (bytes.compare(kMflStart, 4, "MFL ", 4) != 0)
        return {false, "UpQuestlist MFL anchor was not found at the expected offset."};

    const float old_x = Load(bytes, kX), old_y = Load(bytes, kY);
    const float old_scale_x = Load(bytes, kScaleX), old_scale_y = Load(bytes, kScaleY);
    if (!Valid(old_x, -10000.f, 10000.f) || !Valid(old_y, -10000.f, 10000.f) ||
        !Valid(old_scale_x, .01f, 100.f) || !Valid(old_scale_y, .01f, 100.f))
        return {false, "Expected UpQuestlist transform fields did not validate."};

    // Only the copied output is changed. The original remains suitable for
    // comparison and can always be restored by selecting it again.
    Store(bytes, kX, x);
    Store(bytes, kY, y);
    Store(bytes, kScaleX, scale);
    Store(bytes, kScaleY, scale);

    QSaveFile out(QString::fromStdString(output));
    if (!out.open(QIODevice::WriteOnly) || out.write(QByteArray::fromRawData(
            bytes.data(), static_cast<qsizetype>(bytes.size()))) != static_cast<qsizetype>(bytes.size()) ||
        !out.commit())
        return {false, "Could not write the patched output archive."};
    return {true, "Patched UpQuestlist all: " + std::to_string(old_x) + "," +
                       std::to_string(old_y) + ", scale " + std::to_string(old_scale_x) +
                       "," + std::to_string(old_scale_y) + " -> " + std::to_string(scale)};
}

PatchResult InstallEventsOverride(const std::string& source, float x, float y, float scale) {
    const auto actual_source = source.empty() ? DefaultSourceArchive() : source;
    const auto output = EventsOverridePath();
    if (!QDir().mkpath(QFileInfo(QString::fromStdString(output)).absolutePath()))
        return {false, "Could not create the MM3D LayeredFS override directory."};
    auto result = PatchEventsArchive(actual_source, output, x, y, scale);
    if (result.ok)
        result.message += " | Installed override: " + output + " | restart required";
    return result;
}

bool RemoveEventsOverride() {
    const auto path = QString::fromStdString(EventsOverridePath());
    return !QFileInfo::exists(path) || QFile::remove(path);
}

std::string FadeOverridePath() {
    return QFileInfo(QString::fromStdString(EventsOverridePath())).dir().filePath("Joker.gar").toStdString();
}
std::string DefaultFadeArchive() {
    return OriginalLayout("Joker.gar").toStdString();
}
PatchResult PatchFadeArchive(const std::string& source,const std::string& output,float width) {
    const QFileInfo src(QString::fromStdString(source)), dst(QString::fromStdString(output));
    if(src.absoluteFilePath()==dst.absoluteFilePath() ||
       (dst.exists() && src.canonicalFilePath()==dst.canonicalFilePath()))
        return {false,"Refusing to overwrite source GAR"};
    if(!Valid(width,400.f,1600.f)) return {false,"Fade width must be 400..1600"};
    QFile input(src.absoluteFilePath());
    if(!input.open(QIODevice::ReadOnly) || input.size()>16*1024*1024)
        return {false,"Cannot read FadeMask source"};
    auto bytes=input.readAll().toStdString();
    if(bytes.size()<0x598 || bytes.compare(0,4,"GAR\x02",4)!=0 ||
       bytes.compare(0x494,4,"MFL ",4)!=0 || bytes.compare(0x580,4,"\x04\0\x18\0",4)!=0 ||
       bytes.compare(0x4A8,4,"\xEC\0\0\0",4)!=0 ||
       Load(bytes,0x584)!=0.f || Load(bytes,0x588)!=0.f || Load(bytes,0x58C)!=0.f ||
       Load(bytes,0x590)!=400.f || Load(bytes,0x594)!=240.f ||
       bytes.find("FadeMask")==std::string::npos)
        return {false,"Source does not match the verified original 400x240 FadeMask layout"};
    Store(bytes,0x590,width);
    QSaveFile out(dst.absoluteFilePath());
    if(!out.open(QIODevice::WriteOnly) || out.write(bytes.data(),bytes.size())!=qsizetype(bytes.size()) || !out.commit())
        return {false,"Could not write FadeMask override"};
    return {true,"FadeMask width patched; restart the game to test coverage"};
}
namespace {
bool OwnFadeOverride(const QString& output,const QString& backup) {
    QFile out(output), original(backup);
    if(!out.open(QIODevice::ReadOnly) || !original.open(QIODevice::ReadOnly) ||
       out.size()>16*1024*1024 || original.size()>16*1024*1024) return false;
    auto bytes=out.readAll(); const auto base=original.readAll();
    if(bytes.size()<0x598 || bytes.size()!=base.size()) return false;
    // Only our one float may differ. Refuse to delete another mod's changes.
    std::memcpy(bytes.data()+0x590,base.constData()+0x590,4);
    return bytes==base;
}
}
PatchResult InstallFadeOverride(const std::string& source,float width) {
    const auto output=QString::fromStdString(FadeOverridePath());
    const auto backup=output+".mm3d-fade-source";
    if(QFileInfo::exists(output) && !OwnFadeOverride(output,backup))
        return {false,"Existing Joker.gar override is not ours; it has been left intact"};
    if(!QDir().mkpath(QFileInfo(output).absolutePath())) return {false,"Cannot create override directory"};
    // Stage the patch before installing or claiming ownership of any override.
    const auto actual=source.empty()?DefaultFadeArchive():source;
    const auto staged=output+".mm3d-fade-staged";
    auto result=PatchFadeArchive(actual,staged.toStdString(),width);
    if(!result.ok) return result;
    QFile input(QString::fromStdString(actual));
    if(!input.open(QIODevice::ReadOnly)) { QFile::remove(staged); return {false,"Cannot back up source"}; }
    const auto original=input.readAll();
    if(QFileInfo::exists(output)) {
        QFile previous(backup);
        if(!previous.open(QIODevice::ReadOnly) || previous.readAll()!=original) {
            QFile::remove(staged); return {false,"Source changed since installation; restore the existing FadeMask override first"};
        }
    }
    QSaveFile saved(backup);
    if(!saved.open(QIODevice::WriteOnly) || saved.write(original)!=original.size() || !saved.commit()) {
        QFile::remove(staged); return {false,"Cannot save source backup"};
    }
    QFile patch(staged);
    if(!patch.open(QIODevice::ReadOnly)) return {false,"Cannot read staged FadeMask patch"};
    const auto bytes=patch.readAll(); patch.close();
    if(bytes.size()!=original.size()) { QFile::remove(staged); return {false,"Staged FadeMask size mismatch"}; }
    QSaveFile installed(output);
    const bool ok=installed.open(QIODevice::WriteOnly) && installed.write(bytes)==bytes.size() && installed.commit();
    QFile::remove(staged);
    return {ok,ok?"FadeMask override installed; restart required":"FadeMask install failed"};
}
PatchResult RemoveFadeOverride() {
    const auto output=QString::fromStdString(FadeOverridePath()), backup=output+".mm3d-fade-source";
    if(!QFileInfo::exists(output)) return {true,"Original FadeMask active"};
    if(!OwnFadeOverride(output,backup)) return {false,"Override has other changes; refusing to remove it"};
    if(!QFile::remove(output)) return {false,"Cannot remove FadeMask override"};
    QFile::remove(backup);
    return {true,"Original FadeMask restored; restart required"};
}
namespace {
constexpr std::size_t kTelop=0xCCE8, kTelopX=kTelop+0x354;
constexpr unsigned kTelopFields[]{0x138,0x13C,0x144,0x148,0x290,0x354,0x36C,0x370,0x3AC,0x3B8,0x3BC,0x3C0,0x3C4,0x3C8,0x3CC};
QString TelopOutput() {
    return QtUserPath(FileUtil::UserPath::LoadDir) +
        QString::fromStdString(fmt::format("mods/{:016X}/romfs/layout/Joker.Main.gar",MM3D::Titles::Selected().load()));
}
bool OwnTelop(const QString& output) {
    const auto backup=output+".mm3d-telop-source";
    QFile current(output),original(QFileInfo::exists(backup) ? backup :
        OriginalLayout("Joker.Main.gar"));
    if(!current.open(QIODevice::ReadOnly) || !original.open(QIODevice::ReadOnly) ||
       current.size()>16*1024*1024 || original.size()>16*1024*1024) return false;
    auto bytes=current.readAll(); const auto base=original.readAll();
    if(bytes.size()!=base.size() || bytes.size()<qsizetype(kTelop+0x828)) return false;
    for(auto offset:kTelopFields)
        std::memcpy(bytes.data()+kTelop+offset,base.constData()+kTelop+offset,4);
    return bytes.mid(kTelop,0x828)==base.mid(kTelop,0x828);
}
bool AtomicWrite(const QString& path,const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
}
PatchResult PatchTelopArchive(const std::string& source,const std::string& output,float x,float scale,float y,float text_scale,float text_y,float brightness) {
    const QFileInfo src(QString::fromStdString(source)),dst(QString::fromStdString(output));
    if(src.absoluteFilePath()==dst.absoluteFilePath() ||
       (dst.exists() && src.canonicalFilePath()==dst.canonicalFilePath()))
        return {false,"Refusing to overwrite source GAR"};
    if(!Valid(x,-400.f,400.f)) return {false,"Location text X must be -400..400"};
    if(!Valid(scale,.5f,1.5f)) return {false,"Location banner size must be 0.5..1.5"};
    if(!Valid(text_scale,.5f,1.5f)) return {false,"Location text size must be 0.5..1.5"};
    if(!Valid(y,-120.f,120.f)) return {false,"Location banner Y must be -120..120"};
    if(!Valid(text_y,-120.f,120.f)) return {false,"Location text Y must be -120..120"};
    if(!Valid(brightness,.3f,1.f)) return {false,"Location banner brightness must be 0.30..1.00"};
    QFile input(src.absoluteFilePath());
    if(!input.open(QIODevice::ReadOnly) || input.size()>16*1024*1024)
        return {false,"Cannot read cci-test/romfs/layout/Joker.Main.gar"};
    auto bytes=input.readAll().toStdString();
    if(bytes.size()<kTelop+0x828 || bytes.compare(0,4,"GAR\x02",4)!=0 ||
       bytes.compare(kTelop,4,"MFL ",4)!=0 ||
       bytes.compare(kTelop+0x350,4,"\x03\0\x28\0",4)!=0 ||
       bytes.compare(kTelop+0x808,6,"Telop\0",6)!=0 ||
       bytes.compare(kTelop+0x80E,16,"e_map_telop_text\0",16)!=0 ||
       Load(bytes,kTelopX)!=40.f || Load(bytes,kTelop+0x358)!=66.f ||
       Load(bytes,kTelop+0x360)!=256.f || Load(bytes,kTelop+0x364)!=24.f ||
       Load(bytes,kTelop+0x36C)!=1.f || Load(bytes,kTelop+0x370)!=1.f ||
       bytes.compare(kTelop+0x3A8,4,"\x05\0\x24\0",4)!=0 ||
       Load(bytes,kTelop+0x3AC)!=0.f || Load(bytes,kTelop+0x3B8)!=400.f ||
       Load(bytes,kTelop+0x3BC)!=24.f)
        return {false,"Source does not match the inspected Telop text layout"};
    Store(bytes,kTelopX,x);
    // Compensate the 15/16 display-X compression about the native screen centre.
    // Extend the plate, without stretching glyphs or moving the entire banner.
    Store(bytes,kTelop+0x3AC,-400.f/30.f);
    Store(bytes,kTelop+0x3B8,400.f*16.f/15.f);
    // Scale the text widget, anchored at its pane's configured X and native Y=66.
    // Its animated parent remains untouched. Move the plate by the same Y offset.
    Store(bytes,kTelop+0x138,x*(1.f-scale));
    Store(bytes,kTelop+0x13C,66.f*(1.f-scale)+y+text_y);
    Store(bytes,kTelop+0x144,text_scale);
    Store(bytes,kTelop+0x148,text_scale);
    Store(bytes,kTelop+0x290,y);
    Store(bytes,kTelop+0x3BC,24.f*scale);
    // Tint only mapTelop_pane00. Keep alpha untouched so its gradient remains.
    for(unsigned offset : {0x3C0u,0x3C4u,0x3C8u,0x3CCu}) {
        const auto at=kTelop+offset;
        for(unsigned channel : {0u,2u}) {
            const auto value=static_cast<unsigned char>(bytes[at+channel]);
            bytes[at+channel]=static_cast<char>(std::clamp(int(value*brightness+0.5f),0,255));
        }
    }
    const bool ok=AtomicWrite(dst.absoluteFilePath(),QByteArray(bytes.data(),qsizetype(bytes.size())));
    return {ok,ok?"Location text position patched; restart required":"Could not write Telop override"};
}
PatchResult InstallTelopOverride(float x,float scale,float y,float text_scale,float text_y,float brightness) {
    const auto output=TelopOutput(),backup=output+".mm3d-telop-source";
    if(QFileInfo::exists(output) && !OwnTelop(output))
        return {false,"Existing Telop layout has other edits; left intact"};
    if(!QDir().mkpath(QFileInfo(output).absolutePath())) return {false,"Cannot create override directory"};
    const auto source=OriginalLayout("Joker.Main.gar");
    const auto staged=output+".mm3d-telop-staged";
    auto result=PatchTelopArchive(source.toStdString(),staged.toStdString(),x,scale,y,text_scale,text_y,brightness);
    if(!result.ok) return result;
    QFile input(source),patch(staged),previous(backup);
    if(!input.open(QIODevice::ReadOnly) || !patch.open(QIODevice::ReadOnly)) {
        QFile::remove(staged); return {false,"Cannot read staged override/source"};
    }
    const auto original=input.readAll(),bytes=patch.readAll(); patch.close();
    if(QFileInfo::exists(backup) && (!previous.open(QIODevice::ReadOnly) || previous.readAll()!=original)) {
        QFile::remove(staged); return {false,"Source changed; restore Telop override first"};
    }
    auto merged=bytes;
    if(QFileInfo::exists(output)) {
        QFile current(output);
        if(!current.open(QIODevice::ReadOnly)) {
            QFile::remove(staged); return {false,"Cannot read current archive"};
        }
        merged=current.readAll();
        if(merged.size()!=bytes.size()) {
            QFile::remove(staged); return {false,"Archive size changed; left intact"};
        }
        for(auto offset:kTelopFields)
            std::memcpy(merged.data()+kTelop+offset,bytes.constData()+kTelop+offset,4);
    }
    const bool ok=bytes.size()==original.size() && AtomicWrite(backup,original) && AtomicWrite(output,merged);
    QFile::remove(staged);
    return {ok,ok?"Location banner override installed; restart game":"Location banner install failed"};
}
PatchResult RemoveTelopOverride() {
    const auto output=TelopOutput();
    if(!QFileInfo::exists(output)) return {true,"Original location banner active"};
    if(!OwnTelop(output)) return {false,"Override has other changes; refusing to remove it"};
    QFile current(output),original(output+".mm3d-telop-source");
    if(!current.open(QIODevice::ReadOnly) || !original.open(QIODevice::ReadOnly))
        return {false,"Cannot read banner backup"};
    auto bytes=current.readAll(); const auto base=original.readAll(); current.close();
    for(auto offset:kTelopFields)
        std::memcpy(bytes.data()+kTelop+offset,base.constData()+kTelop+offset,4);
    if(!AtomicWrite(output,bytes)) return {false,"Cannot restore location banner"};
    return {true,"Original location banner restored; restart game"};
}
} // namespace MM3D::Notebook
