// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/translation.h"
#include "mm3d/single_screen_text.h"
#include "mm3d/classic_double_time.h"
#include "mm3d/prices.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstdlib>
#include <iostream>

namespace T = MM3D::Translation;
using T::U32;
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << reason << '\n'; std::exit(1); }
}
QByteArray Read(const QString& path) {
    QFile f(path); Check(f.open(QIODevice::ReadOnly), "read fixture"); return f.readAll();
}
void Write(const QString& path, const QByteArray& bytes) {
    Check(QDir().mkpath(QFileInfo(path).absolutePath()), "fixture directory");
    QFile f(path); Check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), "write fixture");
}
U32 Word(const QByteArray& bytes, U32 offset) { return qFromLittleEndian<U32>(bytes.constData() + offset); }
void Put(QByteArray& bytes, U32 offset, U32 value) { qToLittleEndian(value, bytes.data() + offset); }
QByteArray Synthetic() {
    // Deliberately unrelated wording: no language detection or text matching.
    std::map<U32, std::pair<U32,U32>> ids{{0x0008,{0x3fffffff,0}}, {0xc01,{0x3fffffff,0xff0001}},
                                       {0x1b8a,{0x3fffffff,0}}};
    for (const auto& r : MM3D::SingleScreenText::Replacements) ids.emplace(r.id, std::pair{0x3fffffffu,0u});
    for (const auto& r : MM3D::Prices::Messages) ids.emplace(r.id, std::pair{r.metadata,0u});
    QByteArray bytes(16 + ids.size() * 20, '\0');
    Put(bytes,0,0x47534d47); Put(bytes,4,1); Put(bytes,8,U32(ids.size())); Put(bytes,12,8);
    U32 row = 16;
    for (const auto& [id, attrs] : ids) {
        Put(bytes,row,id); Put(bytes,row+4,attrs.first); Put(bytes,row+8,attrs.second);
        const QByteArray text = QByteArray("translated-") + QByteArray::number(id) + QByteArray("\x7f\0\0\0",4);
        const auto size = (text.size()+1)&~1;
        Put(bytes,row+12,U32(bytes.size())); Put(bytes,row+16,U32(size));
        bytes.append(text); if (text.size()!=size) bytes.append('\0'); row+=20;
    }
    return bytes;
}
struct Memory {
    static constexpr U32 Base=0x200000, Self=0x180000;
    QByteArray bytes;
    U32 denied=0;
    unsigned writes=0;
    explicit Memory(QByteArray data):bytes(std::move(data)) {}
    std::optional<U32> Read(U32 at) const {
        if (at==denied) return {};
        if (at==Self+4) return Base;
        if (at==Self+8) return Base+16;
        if (at==Self+12) return U32(bytes.size());
        if (at==Self+16) return 20;
        if (at<Base || at-Base+4>bytes.size()) return {};
        return Word(bytes,at-Base);
    }
    void Write(U32 at,U32 value) {
        Check(Read(at).has_value() && at>=Base,"write to invalid memory");
        Put(bytes,at-Base,value); ++writes;
    }
    U32 Row(U32 id) const {
        for (U32 i=0;i<Word(bytes,8);++i) if ((Word(bytes,16+i*20)&65535)==id) return 16+i*20;
        std::abort();
    }
};
void Dispatch(Memory& m,U32 id,T::Kind kind,unsigned variant) {
    switch (kind) {
    case T::Kind::Controls:
        MM3D::SingleScreenText::enabled.store(variant!=0);
        MM3D::StickControls::enabled.store(variant==2);
        Check(MM3D::SingleScreenText::Apply(m,Memory::Self,id),"control dispatch"); break;
    case T::Kind::Owl:
        MM3D::ClassicSaving::enabled.store(variant==1);
        MM3D::ClassicSaving::resume_text_active.store(variant==2);
        Check(MM3D::SingleScreenText::Apply(m,Memory::Self,id),"owl dispatch"); break;
    case T::Kind::Song:
        Check(MM3D::ClassicDoubleTime::Text(m,Memory::Self,int(variant)-1),"song dispatch"); break;
    case T::Kind::Price: MM3D::Prices::Message(m,Memory::Self,id,variant+1); break;
    }
}
void Exercise(const std::string& directory) {
    const auto mounted=T::Mount(directory);
    Check(!mounted.empty(),"mount translation");
    const auto pack=T::active.load();
    for (const auto& [language,archive] : pack->archives) {
        const auto slot=language==2?"use.gmsg":language==4?"usf.gmsg":"uss.gmsg";
        const auto bytes=Read(QString::fromStdString(mounted)+"message/us/"+slot);
        Check(Word(bytes,12)==language,"slot header normalized");
        for (const auto& [id,e] : archive.entries) {
            Memory m(bytes); Check(T::Owns(m,Memory::Self),"owns prepared archive");
            const auto row=m.Row(id);
            for (unsigned round=0;round<2;++round) for (unsigned v=0;v<e.variants.size();++v) {
                auto expected=bytes;
                expected.replace(e.offset,e.capacity,e.variants[v].text.data(),int(e.capacity));
                Put(expected,row+4,e.variants[v].metadata); Put(expected,row+8,e.variants[v].flags);
                Dispatch(m,id,e.kind,v);
                Check(m.bytes==expected,"exact adaptation; unrelated bytes must survive");
                m.writes=0; Dispatch(m,id,e.kind,v); Check(m.writes==0,"idempotent adaptation");
            }
            Dispatch(m,id,e.kind,0); Check(m.bytes==bytes,"restore translated original");
            for (unsigned scenario=0;scenario<5;++scenario) {
                Memory bad(bytes);
                if (scenario==0) bad.bytes[e.offset]=char(bad.bytes[e.offset]^1);
                if (scenario==1) Put(bad.bytes,row+4,Word(bad.bytes,row+4)^1);
                if (scenario==2) Put(bad.bytes,row+16,e.capacity+4);
                if (scenario==3) bad.denied=Memory::Base+e.offset+e.capacity-4;
                if (scenario==4) Put(bad.bytes,row+8,Word(bad.bytes,row+8)^1);
                const auto before=bad.bytes;
                Check(!T::Apply(bad,Memory::Self,id,e.kind,1),"reject unknown payload/attributes/unreadable memory");
                Check(bad.writes==0 && before==bad.bytes,"preflight failure makes no writes");
            }
        }
        Memory unrelated(bytes);
        Check(!MM3D::SingleScreenText::Apply(unrelated,Memory::Self,8),"unrelated controls untouched");
        MM3D::Prices::Message(unrelated,Memory::Self,8,3);
        Check(unrelated.bytes==bytes && !unrelated.writes,"unrelated translation untouched");
    }
    Check(T::SetEnabled(directory,false) && !T::Requested(directory),"disable preference");
    Check(T::active.load()==pack,"disable does not mutate active session");
    Check(T::Mount(directory).empty() && !T::active.load(),"next session disabled");
    Check(T::SetEnabled(directory,true),"reenable pack");
    Check(!T::Mount(directory).empty(),"next session reenabled");
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temp; Check(temp.isValid(),"temporary root");
    const auto input=temp.path()+"/input", config=temp.path()+"/config";
    const auto directory=config.toStdString();
    Check(!T::Requested(directory) && !T::Installed(directory) && T::Mount(directory).empty(),"default off");
    const auto original=Synthetic(); Write(input+"/message/us/use.gmsg",original);
    Write(input+"/message/us/font.gzf","opaque font fixture");
    Write(input+"/menu/daytelop/label.ctxb","opaque texture fixture");
    Write(input+"/code.bin","ignored"); Write(input+"/message/us/use.gmsg.bak","ignored");
    Check(T::Import(input.toStdString(),directory).empty(),"synthetic import");
    Check(!T::active.load(),"import does not change active session");
    Exercise(directory);
    const auto pack=T::active.load(); Check(pack->archives.size()==1,"partial slot support");
    Check(Read(QString::fromStdString(pack->romfs)+"message/us/font.gzf")=="opaque font fixture",
        "fonts beneath message/us are copied, not parsed as archives");
    Check(Read(QString::fromStdString(pack->romfs)+"menu/daytelop/label.ctxb")=="opaque texture fixture",
        "menu texture preserved");
    Check(!QFileInfo::exists(QString::fromStdString(pack->romfs)+"code.bin"),"unrelated code ignored");
    Memory other(original); Check(!T::Owns(other,Memory::Self),"unimported slot follows stock handling");
    const auto selection=Read(config+"/translations/selection.json");
    Write(input+"/message/us/use.gmsg","bad archive");
    Check(!T::Import(input.toStdString(),directory).empty(),"reject malformed archive");
    Check(Read(config+"/translations/selection.json")==selection && T::active.load()==pack,"failed import preserves selection and session");
    Write(QString::fromStdString(pack->romfs)+"code.bin","unlisted");
    Check(T::Mount(directory).empty(),"reject tampered mounted pack");
    // Optional local fixture: never copied into the repository or shipped.
    if (argc==2) {
        const auto real=(temp.path()+"/real").toStdString();
        const auto error=T::Import(argv[1],real);
        if (!error.empty()) std::cerr << error << '\n';
        Check(error.empty(),"provided pack imports"); Exercise(real);
        std::cout << "Provided pack passed all message variants and restart checks.\n";
    }
    std::cout << "Translation import/runtime checks passed.\n";
}
