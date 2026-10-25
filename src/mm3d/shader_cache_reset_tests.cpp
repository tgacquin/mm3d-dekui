// SPDX-License-Identifier: GPL-2.0-or-later
#include "shader_cache_reset.h"
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>

namespace Reset = MM3D::ShaderCacheReset;
static void Require(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
static void Put(const QString& path, const QByteArray& bytes = "cache data") {
    Require(QDir().mkpath(QFileInfo(path).absolutePath()), "fixture directory");
    QFile file(path);
    Require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "fixture write");
}
static QByteArray Read(const QString& path) {
    QFile file(path); Require(file.open(QIODevice::ReadOnly), "fixture read"); return file.readAll();
}

int main() {
    QTemporaryDir temp; Require(temp.isValid(), "temporary profile");
    const auto config = temp.filePath(QStringLiteral("user/config/mm3d-local"));
    const auto shaders = temp.filePath(QStringLiteral("user/shaders"));
    // Explicitly enumerate the renderer's actual on-disk formats, independently
    // of the reset helper, to catch missed backends, device variants and temp files.
    QStringList caches{
        QStringLiteral("opengl/transferable/0004000000125500.bin"),
        QStringLiteral("opengl/precompiled/separable/0004000000125500.bin"),
        QStringLiteral("opengl/precompiled/conventional/0004000000125500.bin"),
        QStringLiteral("vulkan/pipeline/0004000000125500-10027310.bin"),
        QStringLiteral("vulkan/pipeline/0004000000125500-106B0-hybrid-strict.bin"),
        QStringLiteral("vulkan/transferable/0004000000125500_vs.vkch"),
        QStringLiteral("vulkan/transferable/0004000000125500_fs.vkch"),
        QStringLiteral("vulkan/transferable/0004000000125500_gs.vkch"),
        QStringLiteral("vulkan/transferable/0004000000125500_pl.vkch"),
        QStringLiteral("vulkan/transferable/0004000000125500_vs_temp.vkch"),
        QStringLiteral("vulkan/transferable/0004000000125500_fs_temp.vkch"),
        QStringLiteral("vulkan/transferable/0004000000125500_gs_temp.vkch"),
        QStringLiteral("vulkan/transferable/0004000000125500_pl_temp.vkch"),
    };
    QStringList keep{
        QStringLiteral("opengl/transferable/0004000000033500.bin"),
        QStringLiteral("vulkan/transferable/0004000000033500_fs.vkch"),
        QStringLiteral("vulkan/pipeline/0004000000033500-10027310.bin"),
        QStringLiteral("vulkan/pipeline/00040000001255001-10027310.bin"),
        QStringLiteral("vulkan/pipeline/0004000000125500-notes.txt"),
        QStringLiteral("unrelated.bin"),
    };
    for (const auto& name : caches + keep) Put(QDir(shaders).filePath(name));
    Put(QDir(config).filePath(QStringLiteral("presentation.json")), "custom HUD settings");
    Put(temp.filePath(QStringLiteral("user/sdmc/save.bin")), "save data");
    Put(temp.filePath(QStringLiteral("user/load/textures/texture.png")), "texture pack");
    auto result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(!result.reset && result.error.isEmpty(), "no request is a no-op");
    Require(Reset::Request(config).isEmpty() && Reset::Pending(config), "queue reset");
    Require(Reset::Request(config).isEmpty(), "repeat click is harmless");
    for (const auto& name : caches) Require(Read(QDir(shaders).filePath(name)) == "cache data", "request must not clear live caches");
    result = Reset::ApplyPending(config, shaders, 0x0004000000033500ULL, false);
    Require(!result.reset && result.error.isEmpty() && Reset::Pending(config), "another game cannot consume reset");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, true);
    Require(!result.reset && result.error.isEmpty() && Reset::Pending(config), "live renderer defers reset");
    for (const auto& name : caches) Require(Read(QDir(shaders).filePath(name)) == "cache data", "live renderer files preserved");
    // Simulate a normal renderer shutdown writing additional cache data.
    Put(QDir(shaders).filePath(caches[0]), "shutdown wrote this cache");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(result.reset && result.error.isEmpty() && !Reset::Pending(config), "next launch consumes reset");
    for (const auto& name : caches) Require(!QFileInfo::exists(QDir(shaders).filePath(name)), "all target caches removed");
    for (const auto& name : keep) Require(Read(QDir(shaders).filePath(name)) == "cache data", "other files preserved");
    Require(Read(QDir(config).filePath(QStringLiteral("presentation.json"))) == "custom HUD settings", "settings preserved");
    Require(Read(temp.filePath(QStringLiteral("user/sdmc/save.bin"))) == "save data", "save preserved");
    Require(Read(temp.filePath(QStringLiteral("user/load/textures/texture.png"))) == "texture pack", "texture pack preserved");
    Put(QDir(shaders).filePath(caches[0]), "rebuilt cache");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(!result.reset && result.error.isEmpty() && Read(QDir(shaders).filePath(caches[0])) == "rebuilt cache",
            "later launches keep rebuilt caches");
    Require(Reset::Request(config).isEmpty(), "new request");
    // A directory posing as a cache must never be recursively removed; preflight
    // also prevents earlier valid cache files being removed on this failure.
    const auto blocked = QDir(shaders).filePath(caches[1]);
    Require(QDir().mkpath(blocked), "blocked cache fixture");
    Put(QDir(blocked).filePath(QStringLiteral("keep")), "not a cache");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(!result.reset && !result.error.isEmpty() && Reset::Pending(config), "failure preserves pending request");
    Require(Read(QDir(shaders).filePath(caches[0])) == "rebuilt cache", "preflight before any deletion");
    Require(Read(QDir(blocked).filePath(QStringLiteral("keep"))) == "not a cache", "directory contents preserved");
    Require(QFile::remove(QDir(blocked).filePath(QStringLiteral("keep"))) && QDir().rmdir(blocked), "remove fixture blocker");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(result.reset && result.error.isEmpty(), "retry succeeds");
    Require(Reset::Request(config).isEmpty(), "request with no target caches");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(result.reset && result.error.isEmpty(), "missing target caches succeed");
    Put(Reset::MarkerPath(config), "invalid request");
    Put(QDir(shaders).filePath(caches[0]), "must remain");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(!result.reset && !result.error.isEmpty() && Read(QDir(shaders).filePath(caches[0])) == "must remain",
            "invalid marker cannot trigger deletion");
#ifndef Q_OS_WIN
    // QFile::link produces shortcuts on Windows, so use the actual symlink check
    // on platforms where this creates filesystem symbolic links.
    Require(Reset::Request(config).isEmpty(), "replace invalid request explicitly");
    const auto redirected = QDir(shaders).filePath(caches[1]);
    const auto external = temp.filePath(QStringLiteral("outside.bin")); Put(external, "outside cache");
    Require(QFile::link(external, redirected), "symlink fixture");
    result = Reset::ApplyPending(config, shaders, Reset::TitleId, false);
    Require(!result.reset && !result.error.isEmpty() && Read(external) == "outside cache" &&
            Read(QDir(shaders).filePath(caches[0])) == "must remain", "redirected cache rejected before deletion");
#endif
    std::cout << "Shader cache reset checks passed\n";
}
