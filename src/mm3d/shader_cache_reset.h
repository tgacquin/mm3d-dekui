// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

namespace MM3D::ShaderCacheReset {
inline constexpr quint64 TitleId = 0x0004000000125500ULL;
inline constexpr auto Title = "0004000000125500";
inline const QString Marker = QStringLiteral("shader-cache-reset.pending");
inline std::atomic<bool> completed{false};

inline QString MarkerPath(const QString& config) { return QDir(config).filePath(Marker); }
inline bool Pending(const QString& config) {
    const QFileInfo info(MarkerPath(config));
    return info.exists() || info.isSymLink();
}

// All paths below the supplied roots are owned cache/marker names. Never follow
// a redirected cache directory or recursively delete a directory tree.
inline QString CheckPath(const QString& root, const QString& relative, bool file = true) {
    if (root.isEmpty()) return QStringLiteral("Shader cache location is unavailable.");
    QString path = QDir(root).absolutePath();
    const QFileInfo base(path);
    if (base.isSymLink() || (base.exists() && !base.isDir()))
        return QStringLiteral("Shader cache reset cannot use this directory: ") + path;
    const auto parts = relative.split(QLatin1Char('/'));
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (parts[i].isEmpty() || parts[i] == QStringLiteral(".") || parts[i] == QStringLiteral(".."))
            return QStringLiteral("Invalid shader cache path.");
        path = QDir(path).filePath(parts[i]);
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() &&
            (i + 1 == parts.size() && file ? !info.isFile() : !info.isDir())))
            return QStringLiteral("Shader cache reset cannot remove this path: ") + path;
    }
    return {};
}

inline QString Request(const QString& config) {
    if (const auto error = CheckPath(config, Marker); !error.isEmpty()) return error;
    if (!QDir().mkpath(config)) return QStringLiteral("Could not create the shader reset request directory.");
    QSaveFile file(MarkerPath(config));
    if (!file.open(QIODevice::WriteOnly) || file.write("pending-v1\n") != 11 || !file.commit())
        return QStringLiteral("Could not save the shader cache reset request. No caches were changed.");
    completed.store(false);
    return {};
}

struct Result { bool reset = false; QString error; };

// Call only after the previous emulation thread/renderer has shut down, and
// before loading the new renderer. Shutdown can otherwise write the cache back.
inline Result ApplyPending(const QString& config, const QString& shaders, quint64 title_id, bool renderer_active) {
    if (renderer_active || title_id != TitleId || !Pending(config)) return {};
    if (const auto error = CheckPath(config, Marker); !error.isEmpty()) return {false, error};
    QFile marker(MarkerPath(config));
    if (!marker.open(QIODevice::ReadOnly) || marker.read(64) != "pending-v1\n")
        return {false, QStringLiteral("Could not read a valid shader cache reset request.")};
    marker.close();

    const auto title = QString::fromLatin1(Title);
    QStringList files{ QStringLiteral("opengl/transferable/") + title + QStringLiteral(".bin"),
                       QStringLiteral("opengl/precompiled/separable/") + title + QStringLiteral(".bin"),
                       QStringLiteral("opengl/precompiled/conventional/") + title + QStringLiteral(".bin") };
    for (const auto* kind : {"vs", "fs", "gs", "pl"}) {
        const QString base = QStringLiteral("vulkan/transferable/") + title + QStringLiteral("_") + QString::fromLatin1(kind);
        files << base + QStringLiteral(".vkch") << base + QStringLiteral("_temp.vkch");
    }
    if (const auto error = CheckPath(shaders, QStringLiteral("vulkan/pipeline"), false); !error.isEmpty())
        return {false, error};
    const QDir pipelines(QDir(shaders).filePath(QStringLiteral("vulkan/pipeline")));
    if (pipelines.exists() && !pipelines.isReadable())
        return {false, QStringLiteral("Could not read the Vulkan pipeline cache directory.")};
    for (const auto& entry : pipelines.entryInfoList({title + QStringLiteral("-*.bin")},
             QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        files << QStringLiteral("vulkan/pipeline/") + entry.fileName();

    // Validate every candidate before touching any cache file.
    for (const auto& relative : files)
        if (const auto error = CheckPath(shaders, relative); !error.isEmpty()) return {false, error};
    for (const auto& relative : files) {
        const auto path = QDir(shaders).filePath(relative);
        if (QFileInfo::exists(path) && !QFile::remove(path))
            return {false, QStringLiteral("Could not remove shader cache: ") + path + QStringLiteral(". The reset remains pending.")};
    }
    if (!QFile::remove(MarkerPath(config)))
        return {false, QStringLiteral("Caches cleared, but the reset request could not be removed.")};
    completed.store(true);
    return {true, {}};
}
} // namespace MM3D::ShaderCacheReset
