// SPDX-License-Identifier: GPL-3.0-or-later
#include "integration.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

QString configureDms(bool restore) {
    const QString executable = QStandardPaths::findExecutable("dms");
    if (executable.isEmpty())
        return "DMS is not installed.";
    auto call = [&](QStringList args, QByteArray &output) {
        QProcess process;
        process.start(executable, args);
        if (!process.waitForFinished(4000)) {
            process.kill();
            process.waitForFinished();
            return false;
        }
        output = process.readAllStandardOutput();
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    };
    QByteArray output;
    if (!call({"ipc", "call", "settings", "get", "screenPreferences"}, output))
        return "DMS is not running or its settings IPC is unavailable.";
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return "DMS returned an unsupported settings response.";
    auto preferences = doc.object();
    if (!preferences.contains("wallpaper") || !preferences["wallpaper"].isArray())
        return "This DMS version does not expose wallpaper screen preferences.";
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty())
        state = QDir::homePath() + "/.local/state";
    QString path = state + "/hyprwallselector/dms-backup.json";
    QFile backup(path);
    if (restore) {
        if (!backup.open(QIODevice::ReadOnly))
            return "No saved DMS wallpaper preference.";
        auto saved = QJsonDocument::fromJson(backup.readAll()).object();
        backup.close();
        if (!saved["wallpaper"].isArray())
            return "The DMS backup is invalid.";
        if (!preferences["wallpaper"].toArray().isEmpty())
            return "DMS wallpaper settings have changed; leaving them untouched.";
        preferences["wallpaper"] = saved["wallpaper"];
    } else {
        if (preferences["wallpaper"].toArray().isEmpty())
            return {};
        if (backup.exists()) {
            if (!backup.open(QIODevice::ReadOnly))
                return "Cannot read the existing DMS backup.";
            auto saved = QJsonDocument::fromJson(backup.readAll()).object();
            backup.close();
            if (saved["wallpaper"] != preferences["wallpaper"])
                return "DMS settings changed since the backup; leaving them untouched.";
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(
                QJsonDocument(QJsonObject{{"wallpaper", preferences["wallpaper"]}}).toJson()) < 0 ||
            !file.commit())
            return "Could not save the original DMS preference.";
        preferences["wallpaper"] = QJsonArray();
    }
    // DMS 1.6 rejects object-valued IPC setters. Its documented config file
    // supports external changes and reloads atomically through FileView.
    const QString settingsPath =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
        "/DankMaterialShell/settings.json";
    QFile source(settingsPath);
    if (!source.open(QIODevice::ReadOnly))
        return "Cannot read the DMS settings file.";
    auto settingsDocument = QJsonDocument::fromJson(source.readAll(), &error);
    source.close();
    if (error.error != QJsonParseError::NoError || !settingsDocument.isObject())
        return "The DMS settings file is invalid; leaving it untouched.";
    auto settings = settingsDocument.object();
    auto current = settings["screenPreferences"].toObject();
    const QJsonValue expected = doc.object().value("wallpaper");
    if (current["wallpaper"] != expected)
        return "DMS settings changed during setup; retry after closing its settings window.";
    current["wallpaper"] = preferences["wallpaper"];
    settings["screenPreferences"] = current;
    QSaveFile destination(settingsPath);
    if (!destination.open(QIODevice::WriteOnly) ||
        destination.write(QJsonDocument(settings).toJson()) < 0 || !destination.commit())
        return "Could not write the DMS settings file; the backup is preserved.";
    bool verified = false;
    for (int attempt = 0; attempt < 20 && !verified; ++attempt) {
        QThread::msleep(100);
        verified =
            call({"ipc", "call", "settings", "get", "screenPreferences"}, output) &&
            QJsonDocument::fromJson(output).object()["wallpaper"] == preferences["wallpaper"];
    }
    if (!verified)
        return "The DMS setting is saved but live reload was not confirmed; the backup is "
               "preserved.";
    if (restore)
        QFile::remove(path);
    return {};
}
