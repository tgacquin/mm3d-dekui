// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/classic_saving.h"
#include "mm3d/classic_save_store.h"
#include <array>
#include <QFileInfo>
#include <mutex>
#include <fstream>
#include <chrono>

namespace MM3D::ClassicSaving {
namespace {
std::mutex mutex;
std::array<QString,3> paths;
std::string status;
std::chrono::steady_clock::time_point marker_checked{};
unsigned marker_mask=0;
std::uint32_t last_message=0xffffffffu;
void Trace(const std::string& message) {
    std::ofstream log("/tmp/dekui-classic-runtime.log", std::ios::app);
    if (log) log << message << '\n';
}
bool Fail(const QString& message) { status=message.toStdString(); Trace(status); return false; }
QByteArray ReadNative(unsigned slot) {
    QFile file(paths[slot]);
    if (!file.open(QIODevice::ReadOnly) || file.size()!=Store::SnapshotSize) return {};
    return file.readAll();
}
bool Valid(const QByteArray& bytes) {
    if (bytes.size()!=Store::SnapshotSize || bytes.first(4)!=QByteArray("MJR\x08",4)) return false;
    const auto byte=[&](int i){return static_cast<unsigned char>(bytes[i]);};
    const unsigned expected=unsigned(byte(0x1a88))|(unsigned(byte(0x1a89))<<8);
    unsigned crc=0x1017;
    for(int i=0;i<bytes.size();++i) {
        crc^=(i==0x1a88||i==0x1a89)?0:byte(i);
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xa001:0);
    }
    return crc==expected;
}
QString Directory(unsigned slot) { return QFileInfo(paths[slot]).absolutePath()+"/dekui-classic"; }
}
void ObserveMessage(std::uint32_t id) {
    std::scoped_lock lock(mutex);
    if (!enabled || id==last_message) return;
    last_message=id;
    Trace("message id=0x"+QString::number(id,16).toStdString());
}
void RegisterPath(const std::string& path) {
    // Exact title and slot; other games/system archives are never touched.
    const auto full=QDir::fromNativeSeparators(QString::fromStdString(path));
    for(unsigned slot=0;slot<3;++slot) {
        const bool exact_title=full.endsWith(QString("/title/00040000/00125500/data/00000001/save%1.bin").arg(slot)) ||
            full.endsWith(QString("/title/00040000/00125600/data/00000001/save%1.bin").arg(slot));
        if (!exact_title) continue;
        std::scoped_lock lock(mutex); paths[slot]=full;marker_checked={};
        Trace("path slot="+std::to_string(slot)+" "+full.toStdString());
    }
}
bool BeginWrite(unsigned slot,Kind kind) {
    std::scoped_lock lock(mutex);
    Trace("begin slot="+std::to_string(slot)+" kind="+std::to_string(int(kind)));
    if(slot>=3||paths[slot].isEmpty()) return Fail("Classic saving: native save path has not been observed.");
    if(kind==Kind::Cycle) return true;
    Store store(Directory(slot)); Store::State state; QString error;
    if(!store.Read(slot,state,error)) return Fail(error);
    const auto native=ReadNative(slot);
    if(state.permanent.isEmpty()||!Valid(state.permanent)||
       (native!=state.permanent&&native!=state.suspend))
        return Fail("Classic saving starts after the next successful Song of Time cycle save.");
    return true;
}
bool FinishWrite(unsigned slot,Kind kind) {
    std::scoped_lock lock(mutex);
    if(slot>=3||paths[slot].isEmpty()) return Fail("Classic saving: save path unavailable.");
    const auto native=ReadNative(slot);
    Trace("finish slot="+std::to_string(slot)+" kind="+std::to_string(int(kind))+" bytes="+std::to_string(native.size()));
    if(!Valid(native))return Fail("Classic saving: native save verification failed; not quitting.");
    const bool owl=static_cast<unsigned char>(native[0x351e])!=0;
    if((kind==Kind::Owl)!=owl)return Fail("Classic saving: unexpected snapshot type; not quitting.");
    Store store(Directory(slot));QString error;
    const bool ok=kind==Kind::Cycle?store.CommitCycle(slot,native,error):store.CommitSuspend(slot,native,error);
    if(!ok)return Fail(error);
    marker_checked={};
    status=kind==Kind::Cycle?"Classic cycle checkpoint saved.":"Classic owl save completed.";
    Trace(status);
    return true;
}
bool Resume(unsigned slot,std::span<const std::uint8_t> loaded) {
    std::scoped_lock lock(mutex);
    resume_notice=false;resume_text_active=false;
    Trace("resume slot="+std::to_string(slot)+" bytes="+std::to_string(loaded.size()));
    if(slot>=3||paths[slot].isEmpty())return false;
    Store store(Directory(slot));Store::State state;QString error;
    if(!store.Read(slot,state,error))return Fail(error);
    if(state.suspend.isEmpty())return true;
    const QByteArray bytes(reinterpret_cast<const char*>(loaded.data()),loaded.size());
    // Native disk identity prevents a copied/erased/replaced slot from reviving
    // stale checkpoints. File-select previews never call this method.
    if(bytes!=state.suspend||ReadNative(slot)!=bytes)return true;
    if(!Valid(state.permanent)||!Valid(bytes))return Fail("Classic resume checksum failed; owl save retained.");
    // Atomically replace the native slot FIRST. A process interruption between
    // these commits leaves a permanent slot plus a stale receipt, never a lost
    // permanent checkpoint. Stale receipts are inert unless the native bytes match.
    QSaveFile native(paths[slot]);native.setDirectWriteFallback(false);
    if(!native.open(QIODevice::WriteOnly)||native.write(state.permanent)!=state.permanent.size()||!native.commit())
        return Fail("Could not consume owl save; owl save remains available.");
    if(!store.Consume(slot,state.receipt,error))return Fail(error);
    marker_checked={};
    if(enabled.load()) resume_notice.store(true);
    status="Owl save resumed once; permanent cycle checkpoint retained.";
    Trace(status+" notice="+std::to_string(resume_notice.load()));
    return true;
}
unsigned PendingMask() {
    if(!enabled)return 0;
    std::scoped_lock lock(mutex);
    const auto now=std::chrono::steady_clock::now();
    if(now-marker_checked<std::chrono::milliseconds(250))return marker_mask;
    marker_checked=now;marker_mask=0;
    for(unsigned slot=0;slot<3;++slot) {
        if(paths[slot].isEmpty())continue;
        Store store(Directory(slot));Store::State state;QString error;
        if(store.Read(slot,state,error)&&!state.suspend.isEmpty()&&
           ReadNative(slot)==state.suspend&&Valid(state.suspend))marker_mask|=1u<<slot;
    }
    return marker_mask;
}
std::string Status() { std::scoped_lock lock(mutex);return status; }
} // namespace MM3D::ClassicSaving
