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
        if (backup.exists())
            return "A previous DMS backup exists. Restore it before preparing DMS again.";
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(
                QJsonDocument(QJsonObject{{"wallpaper", preferences["wallpaper"]}}).toJson()) < 0 ||
            !file.commit())
            return "Could not save the original DMS preference.";
        preferences["wallpaper"] = QJsonArray();
    }
    if (!call({"ipc", "call", "settings", "set", "screenPreferences",
               QString::fromUtf8(QJsonDocument(preferences).toJson(QJsonDocument::Compact))},
              output))
        return "DMS could not apply the setting; the backup is preserved.";
    if (!call({"ipc", "call", "settings", "get", "screenPreferences"}, output) ||
        QJsonDocument::fromJson(output).object()["wallpaper"] != preferences["wallpaper"])
        return "Could not verify the DMS setting; the backup is preserved.";
    if (restore)
        QFile::remove(path);
    return {};
}
