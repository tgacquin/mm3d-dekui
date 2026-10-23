// SPDX-License-Identifier: GPL-2.0-or-later
#include "mm3d/translation.h"
#include <algorithm>
#include <stdexcept>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include "common/logging/log.h"
#include "mm3d/single_screen_text_data.h"
#include "mm3d/classic_owl_text_data.h"
#include "mm3d/classic_owl_return_text.h"
#include "mm3d/classic_double_time_text.h"
#include "mm3d/price_text_data.h"

namespace MM3D::Translation {
namespace {
constexpr int Version = 1;
constexpr qint64 MaxFile = 16 * 1024 * 1024;
const std::map<QString, U32> Slots{{"use.gmsg", 2}, {"usf.gmsg", 4}, {"uss.gmsg", 8}};
void Require(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
QByteArray Read(const QString& path) {
    QFile file(path);
    Require(!QFileInfo(path).isSymLink(), "Translation files must not be symbolic links.");
    Require(file.open(QIODevice::ReadOnly), "Could not read a translation file.");
    Require(file.size() > 0 && file.size() <= MaxFile, "Invalid translation file size.");
    const auto bytes = file.readAll();
    Require(bytes.size() == file.size(), "Incomplete translation file read.");
    return bytes;
}
QString Hash(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
U32 Word(const QByteArray& b, qsizetype at) {
    Require(at >= 0 && at + 4 <= b.size(), "Truncated message archive.");
    return qFromLittleEndian<U32>(b.constData() + at);
}
void Put(QByteArray& b, qsizetype at, U32 value) {
    Require(at >= 0 && at + 4 <= b.size(), "Invalid message destination.");
    qToLittleEndian(value, b.data() + at);
}
void Write(const QString& path, const QByteArray& bytes) {
    Require(QDir().mkpath(QFileInfo(path).absolutePath()), "Could not create translation directory.");
    QSaveFile file(path);
    Require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit(),
            "Could not save translation files.");
}
QString Root(const std::string& directory) {
    return QDir(QString::fromStdString(directory)).filePath("translations");
}
QJsonObject Selection(const std::string& directory) {
    const auto path = Root(directory) + "/selection.json";
    if (!QFileInfo::exists(path)) return {};
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(Read(path), &error);
    Require(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid translation selection.");
    return doc.object();
}
QString PackName(const QJsonObject& selection) {
    const auto id = selection.value("pack").toString();
    const QUuid uuid(id);
    Require(!uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id,
            "Invalid translation pack selection.");
    return id;
}
struct Message {
    U32 row{}, metadata{}, flags{};
    std::string text;
};
std::map<U32, Message> Parse(const QByteArray& bytes) {
    Require(Word(bytes, 0) == 0x47534d47 && Word(bytes, 4) == 1, "Unsupported GMSG version.");
    const U32 count = Word(bytes, 8), language = Word(bytes, 12);
    Require(count > 0 && count <= 20000 && 16 + count * 20 <= bytes.size(), "Invalid message table.");
    // One language per archive. Header bits are normalized to its USA filename
    // below; this also repairs packs with a Spanish header in usf.gmsg.
    Require(language && !(language & (language - 1)) && language <= 0x100,
            "Expected a single-language USA-compatible GMSG archive.");
    std::map<U32, Message> messages;
    U32 previous = 0;
    std::vector<std::pair<U32,U32>> ranges;
    for (U32 i = 0; i < count; ++i) {
        const U32 row = 16 + i * 20, id = Word(bytes, row) & 65535;
        const U32 offset = Word(bytes, row + 12), length = Word(bytes, row + 16);
        Require((!i || id > previous) && length && !(length & 1) && !(offset & 1) &&
                offset >= 16 + count * 20 && offset <= bytes.size() && length <= bytes.size() - offset,
                "Invalid, duplicate, unsorted or out-of-bounds message entry.");
        previous = id;
        ranges.emplace_back(offset, offset + length);
        messages.emplace(id, Message{row, Word(bytes, row + 4), Word(bytes, row + 8),
                                    std::string(bytes.constData() + offset, length)});
    }
    std::sort(ranges.begin(), ranges.end());
    for (size_t i = 1; i < ranges.size(); ++i)
        Require(ranges[i].first >= ranges[i-1].second || ranges[i] == ranges[i-1],
                "Overlapping translation message payloads.");
    return messages;
}
// Build local runtime variants from existing, tested DekUI messages. No fan
// translation text is compiled into the executable. All untouched entries keep
// their original bytes, attributes and IDs. Extra capacity is allocated in the
// imported archive, not borrowed from another message or the guest stack.
Archive Prepare(QByteArray& bytes, U32 language) {
    const auto messages = Parse(bytes);
    Archive archive;
    archive.count = Word(bytes, 8);
    Put(bytes, 12, language);
    const auto add = [&](U32 id, Kind kind, const std::vector<Variant>& replacements) {
        const auto found = messages.find(id);
        Require(found != messages.end(), "Translation is missing a message required by DekUI.");
        const auto& original = found->second;
        Entry e;
        e.kind = kind;
        e.variants.push_back({original.text, original.metadata, original.flags});
        e.variants.insert(e.variants.end(), replacements.begin(), replacements.end());
        size_t capacity = 0;
        for (const auto& v : e.variants) capacity = std::max(capacity, v.text.size());
        Require(capacity <= 65536, "Translation message is too large.");
        e.capacity = U32((capacity + 3) & ~size_t(3));
        for (auto& v : e.variants) v.text.resize(e.capacity, '\0');
        while (bytes.size() % 4) bytes.append('\0');
        e.offset = U32(bytes.size());
        bytes.append(e.variants[0].text.data(), qsizetype(e.capacity));
        Put(bytes, original.row + 12, e.offset);
        Put(bytes, original.row + 16, e.capacity);
        Require(archive.entries.emplace(id, std::move(e)).second, "Overlapping DekUI message adaptations.");
    };
    for (const auto& r : SingleScreenText::Replacements) {
        const auto found = messages.find(r.id);
        Require(found != messages.end(), "Translation lacks a control-instruction message.");
        auto stick = r.text;
        for (const auto& s : SingleScreenText::StickReplacements) if (s.id == r.id) stick = s.text;
        const auto& o = found->second;
        add(r.id, Kind::Controls, {{std::string(r.text), o.metadata, o.flags},
                                  {std::string(stick), o.metadata, o.flags}});
    }
    const auto owl = messages.find(0xc01), song = messages.find(0x1b8a);
    Require(owl != messages.end() && song != messages.end() && owl->second.flags == 0xff0001 &&
            song->second.metadata == 0x3fffffff, "Incompatible save/song message attributes.");
    add(0xc01, Kind::Owl, {
        {std::string(SingleScreenText::ClassicOwlReplacements[0].text), owl->second.metadata, 0xff0000},
        {std::string(SingleScreenText::ClassicOwlReturnText), owl->second.metadata, 0xff0001}});
    std::vector<Variant> questions;
    for (auto text : ClassicDoubleTime::Questions)
        questions.push_back({std::string(text), song->second.metadata, song->second.flags});
    add(0x1b8a, Kind::Song, questions);
    for (const auto& twice : Prices::Messages) {
        const auto found = messages.find(twice.id);
        Require(found != messages.end() && found->second.metadata == twice.metadata,
                "Incompatible shopping message attributes; import was not installed.");
        const Prices::MessageEdit* thrice = nullptr;
        for (const auto& v : Prices::TripleMessages) if (v.id == twice.id) thrice = &v;
        Require(thrice != nullptr, "Missing 3x price adaptation.");
        add(twice.id, Kind::Price, {{std::string(twice.text), twice.updated, found->second.flags},
                                  {std::string(thrice->text), thrice->updated, found->second.flags}});
    }
    archive.archive_size = U32(bytes.size());
    Require(archive.archive_size <= MaxFile, "Prepared translation is too large.");
    return archive;
}
bool AssetPath(const QString& path) {
    if (path.startsWith('/') || path.contains('\\') || path.contains(':')) return false;
    for (const auto& part : path.split('/')) if (part.isEmpty() || part == "." || part == "..") return false;
    if (path.startsWith("message/us/") && Slots.contains(path.mid(11))) return true;
    if (path.startsWith("message/") && path.endsWith(".gzf")) return true;
    return path.startsWith("menu/") && path.endsWith(".ctxb");
}
std::shared_ptr<Pack> LoadPack(const QString& path) {
    const auto manifest = QJsonDocument::fromJson(Read(path + "/manifest.json")).object();
    Require(manifest.value("version").toInt() == Version, "Translation needs to be imported again for this version.");
    const auto files = manifest.value("files").toObject();
    Require(!files.isEmpty() && files.size() <= 2048, "Invalid translation manifest.");
    auto pack = std::make_shared<Pack>();
    qint64 total = 0;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto relative = it.key();
        Require(AssetPath(relative), "Unsupported translation asset path.");
        const auto original = Read(path + "/source/" + relative);
        Require(Hash(original) == it.value().toString(), "Imported translation changed or is damaged; reimport it.");
        total += original.size();
        Require(total <= 128 * 1024 * 1024, "Translation pack is too large.");
        QByteArray expected = original;
        if (relative.startsWith("message/us/") && Slots.contains(relative.mid(11))) {
            const U32 language = Slots.at(relative.mid(11));
            pack->archives.emplace(language, Prepare(expected, language));
        }
        Require(Read(path + "/romfs/" + relative) == expected,
                "Prepared translation changed or is damaged; reimport it.");
    }
    Require(!pack->archives.empty(), "Translation contains no supported message archives.");
    // Refuse unlisted files, including code/layout patches, in the mounted layer.
    QDirIterator iterator(path + "/romfs", QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        Require(!iterator.fileInfo().isSymLink(), "Imported translation contains a symbolic link.");
        if (iterator.fileInfo().isFile())
            Require(files.contains(QDir(path + "/romfs").relativeFilePath(iterator.filePath())),
                    "Unregistered file in imported translation; reimport it.");
    }
    pack->romfs = (path + "/romfs/").toStdString();
    return pack;
}
}

bool Requested(const std::string& directory) {
    try { return Selection(directory).value("enabled").toBool(false); }
    catch (...) { return false; }
}
bool Installed(const std::string& directory) {
    try { return QFileInfo::exists(Root(directory) + "/" + PackName(Selection(directory)) + "/manifest.json"); }
    catch (...) { return false; }
}
bool SetEnabled(const std::string& directory, bool enabled) {
    try {
        auto selection = Selection(directory);
        if (enabled) (void)LoadPack(Root(directory) + "/" + PackName(selection));
        selection.insert("enabled", enabled);
        Write(Root(directory) + "/selection.json", QJsonDocument(selection).toJson());
        ++preferences_revision;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Translation preference: {}", e.what());
        return false;
    }
}
std::string Import(const std::string& source, const std::string& directory) {
    try {
        QString input = QString::fromStdString(source);
        if (QDir(input + "/romfs").exists()) input += "/romfs";
        Require(QDir(input + "/message/us").exists(), "Choose a romfs folder containing message/us/*.gmsg.");
        Require(!QFileInfo(input).isSymLink(), "Choose a real translation directory.");
        Require(QDir().mkpath(Root(directory)), "Could not create translation storage.");
        QTemporaryDir stage(Root(directory) + "/.import-XXXXXX");
        Require(stage.isValid(), "Could not prepare translation import.");
        QJsonObject hashes;
        qint64 total = 0;
        unsigned archives = 0;
        QDirIterator iterator(input, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            iterator.next();
            Require(!iterator.fileInfo().isSymLink(), "Translation folders must not contain symbolic links.");
            if (!iterator.fileInfo().isFile()) continue;
            const auto relative = QDir(input).relativeFilePath(iterator.filePath());
            if (!AssetPath(relative)) continue; // Ignore backups and unrelated patches.
            const auto original = Read(iterator.filePath());
            total += original.size();
            Require(total <= 128 * 1024 * 1024 && hashes.size() < 2048, "Translation pack is too large.");
            QByteArray prepared = original;
            if (relative.startsWith("message/us/") && Slots.contains(relative.mid(11))) {
                (void)Prepare(prepared, Slots.at(relative.mid(11)));
                ++archives;
            }
            Write(stage.path() + "/source/" + relative, original);
            Write(stage.path() + "/romfs/" + relative, prepared);
            hashes.insert(relative, Hash(original));
        }
        Require(archives > 0, "No supported USA message archives were found.");
        Write(stage.path() + "/manifest.json", QJsonDocument(QJsonObject{{"version", Version}, {"files", hashes}}).toJson());
        (void)LoadPack(stage.path());
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto destination = Root(directory) + "/" + id;
        Require(QDir().rename(stage.path(), destination), "Could not finish translation import.");
        stage.setAutoRemove(false);
        // Publish last. Existing selections, standard mods, saves and settings
        // remain intact if validation or staging fails. Previous packs are kept.
        Write(Root(directory) + "/selection.json",
              QJsonDocument(QJsonObject{{"pack", id}, {"enabled", true}}).toJson());
        ++preferences_revision;
        return {};
    } catch (const std::exception& e) { return e.what(); }
}
std::string Mount(const std::string& directory) {
    active.store(nullptr);
    try {
        const auto selection = Selection(directory);
        if (!selection.value("enabled").toBool(false)) return {};
        const auto pack = LoadPack(Root(directory) + "/" + PackName(selection));
        active.store(pack);
        LOG_INFO(Frontend, "Imported translation enabled with guarded DekUI message fallbacks");
        return pack->romfs;
    } catch (const std::exception& e) {
        LOG_ERROR(Frontend, "Translation not mounted; using standard resources: {}", e.what());
        return {};
    }
}
}
