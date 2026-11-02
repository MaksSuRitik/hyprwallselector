// SPDX-License-Identifier: GPL-3.0-or-later
#include "caelestia.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString call(const QStringList &args, QByteArray *output = nullptr, int timeout = 5000) {
    const QString executable = QStandardPaths::findExecutable("caelestia");
    if (executable.isEmpty())
        return "Caelestia CLI is not installed.";
    QProcess process;
    process.start(executable, args);
    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished();
        return "Caelestia did not respond. Check that its shell is running.";
    }
    if (output)
        *output = process.readAllStandardOutput().trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return "Caelestia rejected the request: " +
               QString::fromUtf8(process.readAllStandardError()).trimmed();
    return {};
}
bool save(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson()) >= 0 &&
           file.commit();
}
QJsonObject flags(const QJsonObject &background) {
    QJsonObject result;
    for (const auto &key : {"enabled", "wallpaperEnabled"})
        result[key] = background.contains(key) ? background[key] : QJsonValue(QJsonValue::Null);
    return result;
}
void mergeFlags(QJsonObject &background, const QJsonObject &values) {
    for (const auto &key : {"enabled", "wallpaperEnabled"}) {
        if (values[key].isNull())
            background.remove(key);
        else
            background[key] = values[key];
    }
}
} // namespace

QString configureCaelestia(bool enabled, bool restore) {
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/caelestia";
    const QString stateHome =
        qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state");
    const QString stateDir = stateHome + "/hyprwallselector";
    if (!QDir().mkpath(stateDir))
        return "Cannot create the integration state directory.";
    QLockFile lock(stateDir + "/caelestia.lock");
    if (!lock.tryLock(100))
        return "Another Caelestia handover is in progress.";
    const QString settingsPath = configDir + "/shell.json";
    const QString backupPath = stateDir + "/caelestia-backup.json";
    QFile source(settingsPath);
    const bool existed = source.exists();
    if (existed && !source.open(QIODevice::ReadOnly))
        return "Cannot read Caelestia settings.";
    const QByteArray originalBytes = existed ? source.readAll() : QByteArray();
    source.close();
    QJsonParseError error;
    auto document = originalBytes.isEmpty() && !existed
                        ? QJsonDocument(QJsonObject())
                        : QJsonDocument::fromJson(originalBytes, &error);
    if (!document.isObject() || (existed && error.error != QJsonParseError::NoError))
        return "Caelestia settings are invalid; no settings were changed.";
    auto settings = document.object();
    if (settings.contains("background") && !settings["background"].isObject())
        return "Caelestia background settings are invalid.";
    auto background = settings["background"].toObject();
    for (const auto &key : {"enabled", "wallpaperEnabled"})
        if (background.contains(key) && !background[key].isBool())
            return "Caelestia background flags are invalid.";
    const auto currentFlags = flags(background);
    QFile backup(backupPath);
    QJsonObject saved;
    if (backup.exists()) {
        if (!backup.open(QIODevice::ReadOnly))
            return "Cannot read the Caelestia backup.";
        auto backupDocument = QJsonDocument::fromJson(backup.readAll(), &error);
        backup.close();
        saved = backupDocument.object();
        if (error.error != QJsonParseError::NoError || saved["version"].toInt() != 1 ||
            !saved["original"].isObject() || !saved["applied"].isObject())
            return "The Caelestia backup is invalid; no settings were changed.";
        if (currentFlags != saved["applied"].toObject() &&
            currentFlags != saved["original"].toObject() &&
            currentFlags != saved["previous"].toObject())
            return "Caelestia background flags were changed outside the selector; leaving them "
                   "untouched.";
    } else {
        if (restore)
            return "No saved Caelestia background settings.";
        saved = {{"version", 1},
                 {"backgroundExisted", settings.contains("background")},
                 {"original", currentFlags}};
    }
    if (enabled && !restore) {
        const auto failure = call({"shell", "wallpaper", "get"});
        if (!failure.isEmpty())
            return failure;
        background["enabled"] = true;
        background["wallpaperEnabled"] = true;
    } else {
        mergeFlags(background, saved["original"].toObject());
        // A handover to another renderer must leave only that renderer active.
        if (!restore)
            background["wallpaperEnabled"] = false;
    }
    if (background.isEmpty() && !saved["backgroundExisted"].toBool())
        settings.remove("background");
    else
        settings["background"] = background;
    if (!restore) {
        saved["previous"] = currentFlags;
        saved["applied"] = flags(background);
        if (!save(backupPath, saved))
            return "Cannot save the original Caelestia flags.";
    }
    // Re-read just before committing; preserve changes from the shell's settings window.
    QFile latest(settingsPath);
    if (latest.exists() != existed ||
        (existed && (!latest.open(QIODevice::ReadOnly) || latest.readAll() != originalBytes)))
        return "Caelestia settings changed during handover; retry the request.";
    if (!QDir().mkpath(configDir) || !save(settingsPath, settings))
        return "Cannot save Caelestia settings; the backup is preserved.";
    if (restore)
        QFile::remove(backupPath);
    return {};
}

QString applyCaelestiaWallpaper(const QString &path) {
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable())
        return "The wallpaper is not a readable local file.";
    QString expected = file.canonicalFilePath();
    if (file.fileName() == "project.json") {
        QFile project(path);
        if (!project.open(QIODevice::ReadOnly))
            return "Cannot read the Wallpaper Engine project.";
        const auto object = QJsonDocument::fromJson(project.readAll()).object();
        expected = QFileInfo(file.dir().filePath(object["preview"].toString())).canonicalFilePath();
        if (expected.isEmpty() || !QFileInfo(expected).isFile() ||
            !QFileInfo(expected).isReadable() || !expected.startsWith(file.canonicalPath() + "/"))
            return "The Wallpaper Engine project has no readable local preview.";
    }
    // Some MiDnight versions interpolate these names into their shell commands
    // and strip URL delimiters. Do not pass a path that that API cannot handle.
    for (const auto &candidate : {file.canonicalFilePath(), expected})
        if (candidate.contains('\'') || candidate.contains('\n') || candidate.contains('\r') ||
            candidate.contains('#') || candidate.contains('?'))
            return "This shell IPC does not support quotes, control characters, # or ? in "
                   "wallpaper filenames.";
    if (file.fileName() == "project.json") {
        if (QStandardPaths::findExecutable("linux-wallpaperengine").isEmpty())
            return "Wallpaper Engine projects require linux-wallpaperengine.";
        QByteArray help;
        const auto failure = call({"wallpaper", "--help"}, &help);
        if (!failure.isEmpty() || !help.contains("--extract-thumbs"))
            return "Wallpaper Engine projects require a compatible MiDnight CLI and shell.";
    }
    const auto extension = file.suffix().toLower();
    if (QStringList{"mp4", "mkv", "webm", "mov", "avi", "m4v", "mpg", "mpeg", "wmv", "flv", "ogv"}
            .contains(extension)) {
        if (!QStringList{"mp4", "mkv", "webm"}.contains(extension))
            return "The Caelestia/MiDnight renderer supports MP4, MKV and WebM video wallpapers.";
        // A still-only Caelestia CLI must reject video before the shell state changes.
        const auto failure =
            call({"wallpaper", "--print", file.canonicalFilePath()}, nullptr, 20000);
        if (!failure.isEmpty())
            return failure;
    }
    auto failure = call({"shell", "wallpaper", "set", file.canonicalFilePath()});
    if (!failure.isEmpty())
        return failure;
    QByteArray current;
    failure = call({"shell", "wallpaper", "get"}, &current);
    if (!failure.isEmpty())
        return failure;
    if (QFileInfo(QString::fromUtf8(current)).canonicalFilePath() != expected)
        return "Caelestia did not accept this wallpaper. Check the shell's supported formats.";
    return {};
}

QString openCaelestiaSettings() {
    return call({"shell", "nexus", "open"});
}

namespace {
QString updatePlaybackJson(const QString &path, const QJsonObject &updates, bool background) {
    QFile source(path);
    QJsonObject object;
    QByteArray before;
    const bool existed = source.exists();
    if (existed) {
        if (!source.open(QIODevice::ReadOnly))
            return "Cannot read native wallpaper playback settings.";
        before = source.readAll();
        source.close();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(before, &error);
        if (!document.isObject() || error.error != QJsonParseError::NoError)
            return "Invalid native wallpaper playback settings; leaving them untouched.";
        object = document.object();
    }
    auto values = object;
    if (background) {
        if (object.contains("background") && !object["background"].isObject())
            return "Invalid native background settings.";
        values = object["background"].toObject();
    }
    bool changed = false;
    for (auto it = updates.begin(); it != updates.end(); ++it) {
        if (values[it.key()] != it.value()) {
            values[it.key()] = it.value();
            changed = true;
        }
    }
    if (!changed)
        return {};
    if (background)
        object["background"] = values;
    else
        object = values;
    QFile current(path);
    if (current.exists() != existed ||
        (existed && (!current.open(QIODevice::ReadOnly) || current.readAll() != before)))
        return "Playback settings changed during the request; retry the operation.";
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !save(path, object))
        return "Cannot save native wallpaper playback settings.";
    return {};
}
QString pauserPath() {
    return qEnvironmentVariable("XDG_STATE_HOME", QDir::homePath() + "/.local/state") +
           "/caelestia/wallpaper/pauser.json";
}
} // namespace

QString configureCaelestiaPlayback(bool battery, bool fullscreen, bool overlap) {
    const QString shellPath =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
        "/caelestia/shell.json";
    auto error =
        updatePlaybackJson(shellPath, {{"videoWallpaperPauseOnFullscreen", fullscreen}}, true);
    if (!error.isEmpty())
        return error;
    return updatePlaybackJson(
        pauserPath(), {{"pauseOnBattery", battery}, {"pauseOnWindowOverlap", overlap}}, false);
}

QString pauseCaelestia(bool paused) {
    return updatePlaybackJson(pauserPath(), {{"manualPause", paused}}, false);
}
