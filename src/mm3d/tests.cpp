// Copyright 2026 MM3D presentation contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include "mm3d/diagnostics.h"
#include "mm3d/camera_live.h"
#include "mm3d/pause_control.h"
#include "mm3d/draw_probe.h"
#include "mm3d/settings.h"
#include "mm3d/game_folder.h"
#include "mm3d/notebook_paths.h"
#include "mm3d/widescreen.h"
#include "mm3d/trace.h"
#include "mm3d/camera_writer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        {
            QTemporaryDir directory;
            const auto path=directory.filePath("photos.json");
            QFile file(path);Require(file.open(QIODevice::WriteOnly),"photo setting fixture");
            file.write("{\"version\":1}");file.close();
            MM3D::Settings settings;
            Require(settings.hd_pictograph_photos,"HD photos default on for new installations");
            Require(settings.Load(path)&&settings.hd_pictograph_photos,"old settings default to HD photos");
            for(bool enabled:{false,true}) {
                settings.hd_pictograph_photos=enabled;
                Require(settings.Save(path),"photo preference saves");
                MM3D::Settings restored;
                Require(restored.Load(path)&&restored.hd_pictograph_photos==enabled,
                        "HD photo fallback choice survives restart");
            }
        }
        {
            QTemporaryDir directory;
            const auto path=directory.filePath("audio.json");
            const auto load=[&](QJsonObject object) {
                object.insert("version",1);
                QFile file(path);Require(file.open(QIODevice::WriteOnly),"audio fixture opens");
                file.write(QJsonDocument(object).toJson());file.close();
                MM3D::Settings settings;Require(settings.Load(path),"audio settings load");return settings;
            };
            Require(MM3D::Settings{}.hle_audio_accuracy==1,"new installations default to Balanced");
            Require(load({}).hle_audio_accuracy==1,"absent audio settings use Balanced");
            for(bool legacy:{false,true}) {
                Require(load({{"accurate_hle_audio",legacy}}).hle_audio_accuracy==1,
                    "both legacy boolean preferences migrate to Balanced");
                for(int mode=0;mode<3;++mode) {
                    auto settings=load({{"accurate_hle_audio",legacy},{"hle_audio_accuracy",mode}});
                    Require(settings.hle_audio_accuracy==1,"every old audio mode migrates to Balanced");
                    Require(settings.Save(path),"migrated preference saves");
                    MM3D::Settings restored;
                    Require(restored.Load(path)&&restored.hle_audio_accuracy==1,"migration survives reload");
                    restored.hle_audio_accuracy=2;
                    Require(restored.Save(path),"post-migration Accurate HLE selection saves");
                    MM3D::Settings chosen;
                    Require(chosen.Load(path)&&chosen.hle_audio_accuracy==2,"migration does not reset later Accurate HLE choice");
                }
            }
            for(int mode:{1,2}) {
                auto settings=load({{"hle_audio_mode_version",1},{"hle_audio_accuracy",mode},
                    {"accurate_hle_audio",mode!=2}});
                Require(settings.hle_audio_accuracy==mode,"current mode overrides obsolete boolean");
                Require(settings.Save(path),"current mode saves");
                MM3D::Settings restored;
                Require(restored.Load(path)&&restored.hle_audio_accuracy==mode,"both current modes roundtrip");
            }
            for(QJsonValue bad:{QJsonValue(0),QJsonValue(-1),QJsonValue(3),QJsonValue(1.5),QJsonValue("1"),QJsonValue(true)})
                Require(load({{"hle_audio_mode_version",1},{"accurate_hle_audio",true},{"hle_audio_accuracy",bad}}).hle_audio_accuracy==1,
                    "retired or invalid modes use Balanced");
        }
        {
            QTemporaryDir games;Require(games.isValid(),"game discovery fixture");
            const auto root=games.path();
            Require(MM3D::GameFolder::FindRom(root).isEmpty(),"no game blocks launch");
            QFile rom(games.filePath("My Game.3DS"));Require(rom.open(QIODevice::WriteOnly),"game fixture");rom.close();
            Require(MM3D::GameFolder::FindRom(root)==rom.fileName(),"game beside app with arbitrary name and case");
            QFile second(games.filePath("second.cci"));Require(second.open(QIODevice::WriteOnly),"second game fixture");second.close();
            Require(MM3D::GameFolder::FindRom(root).isEmpty(),"ambiguous games block launch");
            Require(second.remove()&&rom.remove(),"remove game fixtures");
            Require(MM3D::GameFolder::FindRom(root).isEmpty(),"removed game blocks subsequent launch");
            Require(QDir(root).mkdir("game"),"legacy game directory");
            QFile legacy(games.filePath("game/mm3d.cci"));Require(legacy.open(QIODevice::WriteOnly),"legacy game fixture");legacy.close();
            Require(MM3D::GameFolder::FindRom(root)==legacy.fileName(),"legacy portable game remains supported");
        }
        {
            using namespace MM3D::Notebook::Paths;
            QTemporaryDir old_folder, new_folder;
            Require(old_folder.isValid() && new_folder.isValid(), "notebook relocation fixtures");
            for(const char* name : {"Joker.Main.Play.SubMenu.gar", "Joker.gar"}) {
                const auto relative=Relative(name);
                const auto old_path=old_folder.filePath("user/"+relative);
                const auto new_path=new_folder.filePath("user/"+relative);
                Require(QDir().mkpath(QFileInfo(old_path).absolutePath()), "old notebook folder");
                QFile old_file(old_path);Require(old_file.open(QIODevice::WriteOnly),"old notebook file");
                old_file.write("old");old_file.close();
                for(const auto& saved : {QString{},relative,old_path,
                     QStringLiteral("C:\\old\\user\\")+QString(relative).replace('/', '\\')}) {
                    Require(Resolve(saved,name,new_path)==new_path,"standard source follows current user folder");
                    Require(Stored(saved,name)==relative,"standard source saved portably");
                }
                Require(!QFileInfo(new_path).exists(),"missing current file does not select existing old file");
                Require(QDir().mkpath(QFileInfo(new_path).absolutePath()),"new notebook folder");
                QFile new_file(new_path);Require(new_file.open(QIODevice::WriteOnly),"new notebook file");
                new_file.write("new");new_file.close();
                QFile resolved(Resolve(old_path,name,new_path));
                Require(resolved.open(QIODevice::ReadOnly)&&resolved.readAll()=="new","reads current installation");
                Require(Resolve(Stored(new_path,name),name,old_path)==old_path,"relative save survives another move");
                for(const auto& custom : {QStringLiteral("/chosen/custom/")+name,
                     QStringLiteral("D:\\chosen\\")+name,QStringLiteral("my-layout/")+name}) {
                    Require(Resolve(custom,name,new_path)==custom,"custom source preserved");
                    Require(Stored(custom,name)==custom,"custom source roundtrip preserved");
                }
                Require(!Standard(old_path+".backup",name),"similar custom filename is not migrated");
            }
        }
        using namespace MM3D::Widescreen;
        Require(IsTopViewport(480,400),"captured MM3D top viewport receives projection correction");
        Require(IsTopViewport(240,400),"native top viewport receives projection correction");
        Require(!IsTopViewport(240,320),"bottom viewport is not widened");
        Require(std::abs(HorizontalScale*Aspect-NativeAspect)<.00001f,"HUD proportions cancel the widescreen expansion");
        Require(1.04f*HorizontalScale<1.f,"world beyond original side plane enters the wider frustum");
        const auto wide=FitTop({0,0,1600,960},{},1600,false);
        Require(wide.GetWidth()==1600 && wide.GetHeight()==900,"16:9 fits the top slot");
        const auto beside=FitTop({0,0,1000,600},{1000,0,1800,600},1800,true);
        Require(beside.right<=1000,"wider top must not overlap a separate bottom screen");
        const auto expanded=FitTop({100,0,1100,600},{},1200,false);
        Require(expanded.GetWidth()>1000 && expanded.GetHeight()==600,"use spare horizontal space");
        MM3D::PauseClock inspection;
        inspection.SetPaused(true, 1000);
        const auto hide_deadline = inspection.Now(1000) + 5000;
        Require(inspection.Now(61000) == 1000, "inspection timer holds throughout pause");
        inspection.SetPaused(false, 61000);
        Require(inspection.Now(65999) < hide_deadline, "hide remains for five running seconds");
        Require(inspection.Now(66000) == hide_deadline, "hide expires after five running seconds");
        inspection.SetPaused(true, 67000);
        inspection.SetPaused(false, 77000);
        Require(inspection.Now(77000) == 7000, "multiple pauses excluded");
        MM3D::DrawProbe probe;
        constexpr std::uint64_t signature = 0x9b343fea686d98e0ULL;
        Require(!probe.Hide(0, signature, 6, true), "probe defaults off");
        probe.Arm(1);
        Require(!probe.Hide(1, signature, 6, true), "first occurrence preserved");
        Require(probe.Hide(1, signature, 6, true), "selected occurrence hidden");
        Require(!probe.Hide(1, signature, 6, true), "other occurrence preserved");
        Require(!probe.Hide(2, 123, 6, true), "other shader preserved");
        probe.expires.store(0);
        Require(!probe.Hide(2, signature, 6, true), "expiry restores rendering");
        for (const int hz : {60, 90, 120}) {
            MM3D::Timing timing;
            for (int i = 0; i <= hz * 4; ++i)
                timing.Present(double(i) / hz, static_cast<std::uint64_t>(i * 30 / hz));
            Require(std::abs(timing.host_fps - hz) < .01, "host cadence");
            Require(std::abs(timing.guest_fps - 30) < 1.01, "independent 30 Hz guest cadence");
            Require(timing.host_frames == static_cast<unsigned>(hz * 4 + 1), "once per present");
            timing.Present(5, 0);
            Require(timing.guest_fps == 0, "new guest resets timing without underflow");
        }
        MM3D::ResetGuestFrames();
        for (int i = 0; i < 30; ++i) MM3D::OnGuestFrame();
        Require(MM3D::guest_frames == 30, "guest observation counter");
        Require(!MM3D::ToggleOverlay(), "F1 passes through when unavailable");
        MM3D::presentation_available = true;
        Require(MM3D::ToggleOverlay() && MM3D::overlay_visible, "F1 opens");
        Require(MM3D::ToggleOverlay() && !MM3D::overlay_visible, "F1 closes");
        Require(MM3D::guest_frames == 30, "UI cannot advance guest frames");
        MM3D::OnGuestFrame(1033333);
        Require(MM3D::guest_frames == 31 && MM3D::guest_frame_time_us == 1033333,
                "completed guest frame retains emulated submission time");
        MM3D::ResetGuestFrames();
        Require(MM3D::guest_frames == 0 && MM3D::guest_frame_time_us == -1,
                "reset cannot retain another session's cadence timestamp");
        QTemporaryDir directory;
        Require(directory.isValid(), "temporary directory");
        const auto path = directory.filePath("settings.json");
        // Synthetic PICA DP4 c20 and MOV with relative c24. No guest code.
        const std::array<std::uint32_t, 2> code{(2u << 26) | (52u << 12),
                                                (0x13u << 26) | (56u << 12) | (1u << 19)};
        const auto usage = MM3D::Trace::AnalyzeUsage(code);
        Require(usage.direct[20] && usage.dot[20] && usage.relative_base[24] && !usage.direct[24],
                "static direct versus relative uniform references");
        MM3D::Trace::Uniforms uniforms{};
        MM3D::Trace::Start();
        MM3D::Trace::Observe(10, 1, 2, 36, true, uniforms);
        uniforms[12][3] = 4;
        MM3D::Trace::Observe(11, 1, 2, 36, true, uniforms);
        MM3D::Trace::Observe(11, 1, 2, 36, true, uniforms);
        MM3D::Trace::Observe(12, 1, 2, 36, false, uniforms);
        MM3D::Trace::Observe(310, 1, 2, 36, true, uniforms);
        Require(!MM3D::Trace::active.load(), "trace frame cap");
        const auto trace_path = directory.filePath("trace.json");
        Require(MM3D::Trace::Export(trace_path), "trace export");
        QFile trace_file(trace_path);
        Require(trace_file.open(QIODevice::ReadOnly), "read trace");
        const auto trace = QJsonDocument::fromJson(trace_file.readAll()).object();
        const auto group = trace["groups"].toArray().first().toObject();
        Require(trace["fallback_draws"].toInt() == 1, "fallback count");
        Require(group["repeated_in_frame"].toInt() == 1, "ambiguous draw signatures");
        Require(group["changing_slots"].toArray().first().toObject()["slot"].toInt() == 12,
                "synthetic transform slot detected");
        MM3D::Trace::Start();
        MM3D::Trace::Observe(50, 1, 2, 36, true, uniforms);
        MM3D::Trace::Observe(0, 1, 2, 36, true, uniforms);
        Require(!MM3D::Trace::active.load(), "trace session rollback stops capture");
        const auto camera_path = directory.filePath("camera.json");
        MM3D::CameraCapture::Start(0);
        uniforms[12] = {1.25f, 2.f, 3.f, 4.f};
        MM3D::CameraCapture::Observe(100, 1, 2, 36, true, 240, 320, 0, uniforms);
        MM3D::CameraCapture::Observe(100, 1, 2, 36, false, 480, 400, 0, uniforms);
        MM3D::CameraCapture::Observe(100, 1, 2, 36, true, 480, 400, 0, uniforms);
        Require(!MM3D::CameraCapture::Export(camera_path, "test"), "active capture cannot export");
        MM3D::CameraCapture::Observe(110, 1, 2, 36, true, 480, 400, 0, uniforms);
        Require(!MM3D::CameraCapture::active.load(), "camera capture stops at ten frames");
        Require(MM3D::CameraCapture::Export(camera_path, "test"), "camera export");
        QFile camera_file(camera_path);
        Require(camera_file.open(QIODevice::ReadOnly), "read camera capture");
        const auto camera = QJsonDocument::fromJson(camera_file.readAll()).object();
        const auto camera_draws = camera["draws"].toArray();
        Require(camera_draws.size() == 1 && camera["fallback_draws"].toInt() == 1,
                "camera excludes bottom draws and counts software fallback");
        Require(camera_draws[0].toObject()["uniforms"].toArray()[12].toArray()[0].toDouble() == 1.25,
                "camera exports actual pre-correction constants");
        MM3D::CameraCapture::Start(0);
        MM3D::CameraCapture::Observe(100, 1, 2, 36, true, 480, 400, 0, uniforms);
        MM3D::CameraCapture::Observe(0, 1, 2, 36, true, 480, 400, 0, uniforms);
        Require(!MM3D::CameraCapture::active.load(), "camera session reset stops capture");
        MM3D::CameraCapture::Start(0, true);
        MM3D::CameraCapture::ObserveUpload(200, 0x89b1ab98da3d9241ULL, 0x20001000, 0x20001008,
            0, true, 0x401a827a, false, {});
        MM3D::CameraCapture::ObserveUpload(200, 0x89b1ab98da3d9241ULL, 0x20001000, 0x20001018,
            0, true, 0, true, {0.f, 2.4142137f, 0.f, 0.f});
        MM3D::CameraCapture::ObserveUpload(200, 1, 0, 0, 4, true, 0, true, {});
        MM3D::CameraCapture::Stop();
        camera_file.close();
        Require(MM3D::CameraCapture::Export(camera_path, "uploads"), "upload trace export");
        Require(camera_file.open(QIODevice::ReadOnly), "read upload trace");
        const auto upload_doc = QJsonDocument::fromJson(camera_file.readAll()).object();
        const auto words = upload_doc["projection_upload_words"].toArray();
        Require(words.size() == 2, "only projection slots recorded");
        Require(words[0].toObject()["word_physical"].toString() == "20001008" &&
                words[0].toObject()["raw_word"].toString() == "401a827a" &&
                words[0].toObject()["decoded_xyzw"].toArray().isEmpty(), "raw source word preserved without premature decode");
        Require(words[1].toObject()["decoded_xyzw"].toArray()[1].toDouble() > 2.414,
                "completed projection row exported");
        MM3D::CameraCapture::Start(0, true);
        for(unsigned i=0; i<16385; ++i)
            MM3D::CameraCapture::ObserveUpload(200, 1, 0x20001000, 0x20001008, 0, true, i, false, {});
        MM3D::CameraCapture::Stop();
        camera_file.close();
        Require(MM3D::CameraCapture::Export(camera_path, "bounded"), "bounded upload export");
        Require(camera_file.open(QIODevice::ReadOnly), "read bounded upload trace");
        const auto bounded = QJsonDocument::fromJson(camera_file.readAll()).object();
        Require(bounded["uploads_truncated"].toBool() &&
                bounded["projection_upload_words"].toArray().size() == 16384, "upload trace memory bound and truncation marker");
        MM3D::CameraCapture::Start(0, false, 1000);
        const MM3D::CameraCapture::DrawSource draw_source{
            .vertex_base=0x20001000, .first_vertex=3, .last_vertex=20,
            .index_offset=64, .topology=1, .indexed=true, .index16=true, .valid=true};
        MM3D::CameraCapture::Observe(300, 1, 2, 36, true, 480, 400, 0, uniforms, draw_source);
        MM3D::CameraCapture::Observe(359, 1, 2, 36, true, 480, 400, 0, uniforms, draw_source);
        Require(MM3D::CameraCapture::active.load(), "temporal capture survives beyond ten frames");
        MM3D::CameraCapture::Observe(360, 1, 2, 36, true, 480, 400, 0, uniforms, draw_source);
        Require(!MM3D::CameraCapture::active.load(), "temporal frame limit clamps to sixty");
        const auto temporal_path=directory.filePath("interpolation.json");
        Require(MM3D::CameraCapture::Export(temporal_path, "camera orbit"), "temporal export");
        QFile temporal_file(temporal_path);
        Require(temporal_file.open(QIODevice::ReadOnly), "read temporal capture");
        const auto temporal=QJsonDocument::fromJson(temporal_file.readAll()).object();
        Require(temporal["schema"].toInt()==4 && temporal["requested_frames"].toInt()==60,
                "temporal schema and bounded frame limit exported");
        const auto temporal_draws=temporal["draws"].toArray();
        Require(temporal_draws.size()==2,"terminal frame not partially captured");
        const auto source=temporal_draws[0].toObject()["source"].toObject();
        Require(source["vertex_base"].toString()=="20001000" && source["first_vertex"].toInt()==3 &&
                source["last_vertex"].toInt()==20 && source["index_offset"].toInt()==64 &&
                source["topology"].toInt()==1 && source["indexed"].toBool() &&
                source["index16"].toBool() && source["valid"].toBool(),
                "draw source metadata survives export without modifying constants");
        MM3D::CameraCapture::StartInterpolation(0);
        Require(MM3D::CameraLive::capture_requested.load(), "orbit requests direct CPU camera sampling");
        uniforms[3]={0,0,-1,0};
        for(unsigned frame=500;frame<620;++frame)
            MM3D::CameraCapture::Observe(frame,1,2,36,true,480,400,0,uniforms,draw_source);
        Require(MM3D::CameraCapture::active.load(), "orbit does not stop at sixty frames");
        MM3D::CameraCapture::Stop();
        Require(!MM3D::CameraLive::capture_requested.load(), "stop releases CPU camera sampling request");
        const auto orbit_path=directory.filePath("orbit.json");
        Require(MM3D::CameraCapture::Export(orbit_path,"orbit"), "orbit export");
        QFile orbit_file(orbit_path);
        Require(orbit_file.open(QIODevice::ReadOnly), "read orbit export");
        const auto orbit=QJsonDocument::fromJson(orbit_file.readAll()).object();
        Require(orbit["capture_seconds"].toInt()==20 && orbit["requested_frames"].isNull(),
                "orbit uses twenty-second window instead of frame cutoff");
        Require(orbit["camera_frames"].toArray().size()==120 && orbit["draws"].toArray().size()==12,
                "camera timeline continuous while expensive draw samples are sparse");
        const auto camera_frame=orbit["camera_frames"].toArray()[0].toObject();
        Require(camera_frame.contains("elapsed_ms") && camera_frame["cpu"].toObject().contains("sample_age_ms") &&
                camera_frame["render_candidates"].toArray().size()==1,
                "timestamp, CPU freshness and renderer candidate recorded together");
        MM3D::CameraCapture::Start(0);
        Require(!MM3D::CameraCapture::UploadsActive(), "ordinary capture restores normal command path");
        MM3D::CameraCapture::Stop();
        Require(MM3D::CameraWriter::Matches(0x401a827a) && !MM3D::CameraWriter::Matches(0),
                "CPU writer matches captured coefficient, excludes unrelated values");
        MM3D::CameraWriter::Start(0);
        Require(MM3D::CameraWriter::ShouldTrace(), "CPU writer starts without delay for tests");
        MM3D::CameraWriter::Hit writer_hit;
        writer_hit.instruction = {0x105000, 0x106000, 0x10001000, false};
        writer_hit.value = 0x401a827a; writer_hit.address = 0x14003c74;
        writer_hit.process = 1; writer_hit.stack[0] = 0x107000; writer_hit.stack_valid = 1;
        MM3D::CameraWriter::Observe(writer_hit);
        MM3D::CameraWriter::Observe(writer_hit);
        const auto writer_path = directory.filePath("writers.json");
        Require(!MM3D::CameraWriter::Export(writer_path), "CPU writer refuses active export");
        MM3D::CameraWriter::Stop();
        Require(!MM3D::CameraWriter::ShouldTrace(), "CPU writer stop restores normal mode");
        Require(MM3D::CameraWriter::Export(writer_path), "CPU writer export");
        QFile writer_file(writer_path);
        Require(writer_file.open(QIODevice::ReadOnly), "read CPU writer export");
        const auto writer_doc = QJsonDocument::fromJson(writer_file.readAll()).object();
        const auto writer_hits = writer_doc["hits"].toArray();
        Require(writer_hits.size()==1 && writer_doc["duplicate_writes"].toInt()==1, "CPU writer deduplicates repeated copies");
        Require(writer_hits[0].toObject()["pc"].toString()=="105000" &&
                writer_hits[0].toObject()["destination_virtual"].toString()=="14003c74", "CPU writer preserves PC and virtual destination");
        Require(writer_hits[0].toObject()["stack_words"].toArray()[1].isNull(), "unreadable stack words stay null");
        MM3D::CameraWriter::Start(0);
        for(unsigned i=0;i<128;++i) {
            writer_hit.address = 0x14000000 + i*4;
            MM3D::CameraWriter::Observe(writer_hit);
        }
        Require(!MM3D::CameraWriter::active.load(), "CPU writer stops at bounded hit count");
        MM3D::Settings settings;
        Require(!settings.stick_click_controls,"stick-click controls default off");
        settings.stick_click_controls=true;
        const auto default_photo=settings.inventory_menu.gear.poses[MM3D::GearMenu::Photo];
        Require(default_photo.x==78.7f&&default_photo.y==-20.6f&&default_photo.scale==.88f,
                "photo defaults to released right-side layout");
        Require(!settings.disable_fog,"distance fog is enabled by default");
        settings.disable_fog=true;
        Require(settings.interpolation_60fps,"interpolation is enabled for new installations");
        Require(settings.interpolation_cpu_vertices,"CPU interpolation is the default method");
        settings.interpolation_60fps=true;
        settings.interpolation_cpu_vertices=true;
        Require(settings.game_camera_fov==65.f,"release camera zoom defaults to 65 degrees");
        Require(!settings.classic_saving&&!settings.classic_double_time&&
                !settings.sword_activate_owls&&!settings.disable_bomber_approach,"Restoration defaults all off");
        Require(settings.damage_multiplier==1&&!settings.disable_heart_drops&&!settings.disable_rupee_drops&&
                !settings.disable_fairy_drops&&!settings.disable_magic_drops&&!settings.disable_ammo_pickups&&
                !settings.lose_rupees_on_death&&!settings.world_ends_on_death&&!settings.advance_six_hours_on_death,"difficulty modifiers default off");
        Require(!settings.fast_time,"fast time defaults off");
        Require(!settings.depth_of_field.enabled&&!settings.ambient_occlusion.enabled,"scene effects default off");
        {
            auto p=MM3D::CameraMidpoint::Identity();
            p[0]={0,2,0,0};p[1]={-1.2,0,0,0};p[2]={0,0,1.0005,5.0025};p[3]={0,0,-1,0};
            for(bool w:{false,true})for(float sign:{-1.f,1.f}) {
                const float scale=w?sign/4000.f:sign;
                const auto d=MM3D::DepthOfField::Decode(p,scale,0.f,w,sign>0);
                Require(d.has_value(),"perspective depth convention decoded");
                for(float distance:{100.f,500.f,2500.f}) {
                    const float z=w?d->a*distance+d->b:d->a+d->b/distance;
                    Require(std::abs(MM3D::DepthOfField::Distance(z,*d)-distance)<.1f,
                            "Z/W depth reconstructs world distance");
                }
            }
            Require(!MM3D::DepthOfField::Decode(MM3D::CameraMidpoint::Identity(),1,0,false,false),
                    "orthographic UI is not scene depth");
            Require(MM3D::DepthOfField::Radius(100,{true,600,1200,2})==0,
                    "near scene remains in focus");
            Require(MM3D::DepthOfField::Radius(1800,{true,600,1200,2})==2,
                    "far blur reaches the requested limit");
            const auto safe=MM3D::DepthOfField::Sanitize({true,-1,0,100});
            Require(safe.focus==50&&safe.transition==100&&safe.strength==4,"invalid lens controls are bounded");
        }
        settings.depth_of_field={true,800.f,1600.f,2.f};
        settings.ambient_occlusion={true,.75f,60.f};
        Require(settings.player_hud.banner_y==0,"location banner uses editor position by default");
        settings.player_hud.banner_y=24;
        Require(!settings.double_prices&&settings.price_multiplier==2,"price difficulty defaults off with 2x selected");
        settings.double_prices=true;settings.price_multiplier=3;
        settings.advance_six_hours_on_death=true;
        settings.fast_time=true;
        Require(!settings.weaker_enhanced_defense,"enhanced defense defaults native");
        settings.weaker_enhanced_defense=true;
        Require(settings.actor_draw_distance==1.f,"actor draw distance defaults native");
        settings.actor_draw_distance=3.f;
        Require(settings.prop_draw_distance==1.f,"bush distance defaults native");
        settings.prop_draw_distance=3.f;
        Require(settings.song_list.panel_color_opacity==0.f,"song panel color defaults off");
        settings.song_list.panel_color={.2f,.4f,.6f};settings.song_list.panel_color_opacity=.75f;
        Require(!settings.classic_double_time,"classic Double Time defaults off");
        settings.classic_double_time=true;
        settings.scale = 1.5;
        Require(settings.player_hud.opacity==1.f&&settings.player_hud.banner_darkness==0.f,"HUD appearance defaults unchanged");
        settings.player_hud.opacity=.375f;settings.player_hud.banner_darkness=.625f;settings.minimap_opacity=0.f;
        Require(!settings.mute_music, "music audible by default");
        Require(!settings.disable_tatl_reminders, "Tatl reminders enabled by default");
        settings.disable_tatl_reminders=true;
        Require(!settings.sword_activate_owls,"sword activation off by default");
        settings.sword_activate_owls=true;
        Require(!settings.disable_bomber_approach,"Bomber approaches enabled by default");
        settings.disable_bomber_approach=true;
        Require(!settings.disable_low_health_beep, "low heart alarm enabled by default");
        settings.disable_low_health_beep = true;
        settings.mute_music = true;
        settings.widescreen = true;
        settings.camera_zoom_out = 1.25f;
        settings.game_camera_fov = 55.f;
        settings.cutscene_camera_fov = 60.f;
        settings.show_performance = true;
        Require(settings.show_startup_settings_hint,"startup hint enabled by default");
        settings.show_startup_settings_hint=false;
        settings.item_present.poses[MM3D::NativeItemPresent::Panel]={10,20,.5f};
        settings.mask_present.enabled=false;
        settings.mask_present.poses[MM3D::NativeItemPresent::Panel]={35,40,.75f};
        settings.mask_present.poses[MM3D::NativeItemPresent::Prompt]={-20,-80,.6f};
        settings.mask_present.poses[MM3D::NativeItemPresent::Description]={45,-30,.8f};
        settings.mask_present.help_pose={15,210,.7f};settings.mask_present.help_spacing=9;
        settings.fullscreen_draws={MM3D::WidescreenProbe::Draw{
            0x89b1ab98da3d9241ULL,0xfedcba9876543210ULL,4,480,400,0,65536,4294967295u}};
        settings.file_select.entrance_ease=2.75f;
        settings.inventory_menu.gear.poses[MM3D::GearMenu::Photo]={100,15,.5f};
        Require(settings.Save(path), "initial atomic save");
        settings.scale = 1.75;
        Require(settings.Save(path), "atomic replacement");
        MM3D::Settings loaded;
        Require(loaded.Load(path) && loaded.scale == 1.75, "settings roundtrip");
        Require(loaded.disable_fog,"disable fog persists");
        Require(loaded.stick_click_controls,"stick-click controls persist");
        Require(loaded.depth_of_field.enabled&&loaded.depth_of_field.focus==800.f&&
                loaded.depth_of_field.transition==1600.f&&loaded.depth_of_field.strength==2.f,
                "depth of field controls persist");
        Require(loaded.ambient_occlusion.enabled&&loaded.ambient_occlusion.strength==.75f&&
                loaded.ambient_occlusion.radius==60.f,"ambient occlusion controls persist");
        const auto photo=loaded.inventory_menu.gear.poses[MM3D::GearMenu::Photo];
        Require(photo.x==100&&photo.y==15&&photo.scale==.5f,"photo placement persists");
        Require(loaded.file_select.entrance_ease==2.75f,"file entrance ease persists");
        Require(loaded.item_present.poses[MM3D::NativeItemPresent::Panel].x==10&&
            loaded.mask_present.poses[MM3D::NativeItemPresent::Panel].x==35&&
            loaded.mask_present.poses[MM3D::NativeItemPresent::Panel].y==40&&
            loaded.mask_present.poses[MM3D::NativeItemPresent::Panel].scale==.75f,"independent item and mask panels persist");
        Require(!loaded.mask_present.enabled&&loaded.item_present.enabled&&
            loaded.mask_present.poses[MM3D::NativeItemPresent::Prompt].x==-20&&
            loaded.mask_present.poses[MM3D::NativeItemPresent::Description].x==45&&
            loaded.mask_present.poses[MM3D::NativeItemPresent::Description].y==-30&&
            loaded.mask_present.poses[MM3D::NativeItemPresent::Description].scale==.8f&&
            loaded.mask_present.help_pose.x==15&&loaded.mask_present.help_pose.y==210&&
            loaded.mask_present.help_pose.scale==.7f&&loaded.mask_present.help_spacing==9,"mask title, description and prompts persist independently");
        Require(loaded.double_prices&&loaded.price_multiplier==3,"3x purchase prices persist");
        Require(loaded.advance_six_hours_on_death,"six-hour death penalty persists");
        Require(loaded.fast_time,"fast time persists");
        Require(loaded.classic_double_time,"classic Double Time persists");
        Require(loaded.song_list.panel_color==settings.song_list.panel_color&&loaded.song_list.panel_color_opacity==.75f,"song panel color persists");
        Require(loaded.prop_draw_distance==3.f,"bush distance persists");
        Require(loaded.actor_draw_distance==3.f,"actor draw distance persists");
        Require(loaded.weaker_enhanced_defense,"weaker enhanced defense persists");
        Require(loaded.player_hud.banner_y==24,"location banner offset persists");
        Require(loaded.player_hud.opacity==.375f&&loaded.player_hud.banner_darkness==.625f&&loaded.minimap_opacity==0.f,"HUD appearance and transparent minimap persist");
        Require(loaded.mute_music, "music mute preference persists");
        Require(loaded.disable_low_health_beep, "low heart mute preference persists");
        Require(loaded.disable_tatl_reminders, "Tatl reminder preference persists");
        Require(loaded.sword_activate_owls,"sword activation preference persists");
        Require(loaded.disable_bomber_approach,"Bomber approach preference persists");
        Require(loaded.widescreen,"widescreen persists");
        Require(loaded.show_performance,"frame-rate display preference persists");
        Require(loaded.interpolation_60fps,"interpolation opt-in persists");
        Require(loaded.interpolation_cpu_vertices,"CPU interpolation method persists");
        Require(!loaded.show_startup_settings_hint,"startup hint opt-out persists");
        Require(loaded.camera_zoom_out == 1.25f, "camera zoom persists");
        Require(loaded.game_camera_fov == 55.f, "game FOV persists");
        Require(loaded.cutscene_camera_fov == 60.f, "cutscene FOV persists independently");
        Require(loaded.fullscreen_draws==settings.fullscreen_draws,"full draw signature persists without hash precision loss");
        QFile corrupt(path);
        Require(corrupt.open(QIODevice::WriteOnly), "open legacy shared mask controls fixture");
        corrupt.write("{\"version\":1,\"item_present_1_x\":27,\"item_present_1_y\":55,\"item_present_1_scale\":0.6,\"item_present_help_x\":19,\"dialogue_y\":-25,\"dialogue_scale\":0.7}");corrupt.close();
        MM3D::Settings legacy_masks;Require(legacy_masks.Load(path),"load legacy shared mask controls");
        Require(legacy_masks.mask_present.poses[MM3D::NativeItemPresent::Panel].x==27&&
            legacy_masks.mask_present.poses[MM3D::NativeItemPresent::Panel].y==55&&
            legacy_masks.mask_present.poses[MM3D::NativeItemPresent::Panel].scale==.6f&&
            legacy_masks.mask_present.help_pose.x==19&&
            legacy_masks.mask_present.poses[MM3D::NativeItemPresent::Description].x==0&&
            legacy_masks.mask_present.poses[MM3D::NativeItemPresent::Description].y==-25&&
            legacy_masks.mask_present.poses[MM3D::NativeItemPresent::Description].scale==.7f,"legacy mask placement is preserved");
        Require(corrupt.open(QIODevice::WriteOnly), "open malformed fixture");
        corrupt.write("{truncated"); corrupt.close();
        Require(!loaded.Load(path) && loaded.scale == 1.75, "invalid input preserves settings");
        Require(corrupt.open(QIODevice::WriteOnly), "open bounds fixture");
        corrupt.write("{\"version\":1,\"scale\":999,\"opacity\":-1}"); corrupt.close();
        Require(loaded.Load(path) && loaded.scale == 2 && loaded.opacity == .2f, "bound settings");
        Require(!loaded.disable_fog,"legacy settings retain distance fog");
        Require(!loaded.stick_click_controls,"legacy settings retain original controls");
        Require(loaded.song_list.panel_color_opacity==0.f,"legacy song panel color defaults off");
        Require(loaded.prop_draw_distance==1.f,"legacy bush distance defaults native");
        Require(loaded.actor_draw_distance==1.f,"legacy actor draw distance defaults native");
        Require(!loaded.weaker_enhanced_defense,"legacy enhanced defense defaults native");
        Require(!loaded.double_prices&&loaded.price_multiplier==2,"legacy price defaults");
        Require(!loaded.classic_double_time,"legacy restoration defaults off");
        Require(loaded.player_hud.banner_y==0,"legacy banner offset defaults unchanged");
        Require(loaded.player_hud.opacity==1.f&&loaded.player_hud.banner_darkness==0.f,"legacy HUD appearance defaults");
        Require(loaded.fullscreen_draws.empty(),"legacy settings clear saved draw");
        Require(loaded.camera_zoom_out == 1.f, "legacy settings restore original FOV");
        Require(loaded.interpolation_60fps,"settings without a saved frame rate default to interpolation");
        Require(loaded.interpolation_cpu_vertices,"settings without a saved method default to CPU interpolation");
        Require(loaded.game_camera_fov == 65.f, "legacy settings default to user-facing game FOV");
        Require(loaded.cutscene_camera_fov == 49.f, "legacy settings preserve previous cutscene FOV");
        Require(!loaded.mute_music, "legacy settings keep music audible");
        Require(!loaded.disable_low_health_beep, "legacy settings keep low heart alarm audible");
        Require(!loaded.disable_tatl_reminders, "legacy settings retain Tatl reminders");
        Require(!loaded.sword_activate_owls,"legacy settings retain native owl activation");
        Require(loaded.show_startup_settings_hint,"legacy settings enable startup hint");
        Require(!loaded.disable_bomber_approach,"legacy settings retain Bomber approaches");
        Require(corrupt.open(QIODevice::WriteOnly), "open legacy interpolation fixture");
        corrupt.write("{\"version\":1,\"interpolation_60fps\":true}"); corrupt.close();
        Require(loaded.Load(path) && loaded.interpolation_60fps && loaded.interpolation_cpu_vertices,
                "existing enabled interpolation stays enabled with the default CPU method");
        loaded.interpolation_60fps=false;
        loaded.interpolation_cpu_vertices=true;
        Require(loaded.Save(path) && loaded.Load(path) && !loaded.interpolation_60fps && loaded.interpolation_cpu_vertices,
                "turning interpolation off retains the selected CPU method");
        loaded.interpolation_cpu_vertices=false;
        Require(loaded.Save(path) && loaded.Load(path) && !loaded.interpolation_cpu_vertices,
                "switching back to the GPU method persists");
        Require(corrupt.open(QIODevice::WriteOnly), "open zoom bounds fixture");
        corrupt.write("{\"version\":1,\"camera_zoom_out\":999}"); corrupt.close();
        Require(loaded.Load(path) && loaded.camera_zoom_out == 1.5f, "camera zoom upper bound");
        Require(!loaded.Save(directory.filePath("missing/settings.json")), "failed save reported");
        std::cout << "PASS: 60/90/120 host versus 30 guest, resets, F1, atomic settings and malformed input\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
