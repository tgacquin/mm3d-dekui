// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace MM3D {
// Explicit user action, distinct from conservative automatic migration.
inline QString ResetMenuLayouts(const QString& config, const QString& user) {
    const auto read=[](const QString& path) {
        QFile f(path);
        if(QFileInfo(path).isSymLink() || !f.open(QIODevice::ReadOnly) || f.size()>1024*1024)
            return QJsonObject{};
        return QJsonDocument::fromJson(f.readAll()).object();
    };
    auto current=read(config);
    const auto baseline=read(user+"setup/layout-baseline.json");
    const auto defaults=baseline["defaults"].toObject();
    const auto layout_version=baseline["version"].toInt();
    if(current["version"].toInt()!=1 || layout_version<1 || layout_version>3 || defaults.isEmpty())
        return "Menu layout reset failed: settings or release defaults are unavailable.";
    for(auto it=defaults.begin();it!=defaults.end();++it) {
        const auto key=it.key();
        if(key.startsWith("song_") || key.startsWith("ocarina_prompt") || key=="ocarina_icon_scale" ||
           key.startsWith("file_select_") || key.startsWith("item_present_") ||
           key.startsWith("mask_present_") || key.startsWith("gear_6_")) current[key]=it.value();
    }
    const auto backups=user+"setup/layout-reset-backups/";
    if(!QDir().mkpath(backups) || !QFile::copy(config,backups+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json"))
        return "Menu layout reset failed: could not back up settings.";
    QSaveFile file(config);
    const auto bytes=QJsonDocument(current).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
        return "Menu layout reset failed: could not save settings. Backup retained.";
    return {};
}
}
