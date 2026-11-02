// SPDX-License-Identifier: GPL-3.0-or-later
#include "caelestia.h"
#include "wallpaperpause.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

class TestCaelestia : public QObject {
    Q_OBJECT
    QTemporaryDir fixture;
    QString settingsPath, backupPath;
    void write(const QJsonObject &settings) {
        QFile file(settingsPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
    }
    QJsonObject read() {
        QFile file(settingsPath);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
  private slots:
    void initTestCase() {
        QVERIFY(fixture.isValid());
        QDir().mkpath(fixture.path() + "/bin");
        QDir().mkpath(fixture.path() + "/config/caelestia");
        QVERIFY(QFile::link(QCoreApplication::applicationFilePath(),
                            fixture.path() + "/bin/caelestia"));
        qputenv("PATH", (fixture.path() + "/bin").toUtf8());
        qputenv("XDG_CONFIG_HOME", (fixture.path() + "/config").toUtf8());
        qputenv("XDG_STATE_HOME", (fixture.path() + "/state").toUtf8());
        settingsPath = fixture.path() + "/config/caelestia/shell.json";
        backupPath = fixture.path() + "/state/hyprwallselector/caelestia-backup.json";
    }
    void init() {
        QFile::remove(backupPath);
        qunsetenv("HWS_CAEL_BAD");
        qunsetenv("HWS_CAEL_EXPECTED");
    }
    void takeoverPreservesEffectsAndRestores() {
        const QJsonObject initial{
            {"general", QJsonObject{{"language", "uk_UA"}}},
            {"background", QJsonObject{{"enabled", false},
                                       {"wallpaperEnabled", false},
                                       {"wallpaperTransition", "Wipe"},
                                       {"videoWallpaperSoundEnabled", true},
                                       {"visualiser", QJsonObject{{"enabled", true}}}}}};
        write(initial);
        QCOMPARE(configureCaelestia(true), QString());
        auto changed = read();
        auto background = changed["background"].toObject();
        QVERIFY(background["enabled"].toBool());
        QVERIFY(background["wallpaperEnabled"].toBool());
        QCOMPARE(background["wallpaperTransition"].toString(), QString("Wipe"));
        QCOMPARE(background["visualiser"], initial["background"].toObject()["visualiser"]);
        QCOMPARE(changed["general"], initial["general"]);
        QCOMPARE(configureCaelestia(true), QString());
        QCOMPARE(configureCaelestia(false), QString());
        QCOMPARE(read(), initial);
        QCOMPARE(configureCaelestia(true), QString());
        QCOMPARE(configureCaelestia(false, true), QString());
        QCOMPARE(read(), initial);
        QVERIFY(!QFile::exists(backupPath));
    }
    void nativeHandoverDisablesEvenOriginallyEnabledShell() {
        write({{"background", QJsonObject{{"enabled", true}, {"wallpaperEnabled", true}}}});
        QCOMPARE(configureCaelestia(true), QString());
        QCOMPARE(configureCaelestia(false), QString());
        QVERIFY(!read()["background"].toObject()["wallpaperEnabled"].toBool());
        QVERIFY(read()["background"].toObject()["enabled"].toBool());
        QCOMPARE(configureCaelestia(false, true), QString());
        QVERIFY(read()["background"].toObject()["wallpaperEnabled"].toBool());
    }
    void absentDefaultsAndConcurrentUserChoices() {
        write({{"untouched", 42}});
        QCOMPARE(configureCaelestia(true), QString());
        auto changed = read();
        auto background = changed["background"].toObject();
        background["wallpaperRecolorStrength"] = .8;
        changed["background"] = background;
        write(changed);
        QCOMPARE(configureCaelestia(false, true), QString());
        QCOMPARE(read(),
                 (QJsonObject{{"untouched", 42},
                              {"background", QJsonObject{{"wallpaperRecolorStrength", .8}}}}));
    }
    void refusesDamagedSettingsAndExternalFlagChanges() {
        write({{"background", QJsonObject{{"enabled", true}, {"wallpaperEnabled", false}}}});
        QCOMPARE(configureCaelestia(true), QString());
        auto changed = read();
        changed["background"] = QJsonObject{{"enabled", false}, {"wallpaperEnabled", true}};
        write(changed);
        QVERIFY(!configureCaelestia(false, true).isEmpty());
        QCOMPARE(read(), changed);
        QFile::remove(backupPath);
        QFile file(settingsPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{");
        file.close();
        QVERIFY(!configureCaelestia(true).isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{"));
    }
    void missingShellLeavesSettingsUntouched() {
        const QJsonObject initial{{"background", QJsonObject{{"wallpaperEnabled", false}}}};
        write(initial);
        qputenv("HWS_CAEL_BAD", "1");
        QVERIFY(!configureCaelestia(true).isEmpty());
        QCOMPARE(read(), initial);
        QVERIFY(!QFile::exists(backupPath));
    }
    void playbackPreferencesAndManualPausePreserveDecoder() {
        write({{"background", QJsonObject{{"wallpaperEnabled", true}}}, {"untouched", 42}});
        const QString pauser = fixture.path() + "/state/caelestia/wallpaper/pauser.json";
        QDir().mkpath(QFileInfo(pauser).absolutePath());
        QFile file(pauser);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"hwDecoder\":\"vaapi\",\"custom\":42}");
        file.close();
        QCOMPARE(configureCaelestiaPlayback(true, true, false), QString());
        QCOMPARE(read()["untouched"].toInt(), 42);
        QVERIFY(read()["background"].toObject()["videoWallpaperPauseOnFullscreen"].toBool());
        QCOMPARE(pauseCaelestia(true), QString());
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto values = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QVERIFY(values["manualPause"].toBool());
        QVERIFY(values["pauseOnBattery"].toBool());
        QVERIFY(!values["pauseOnWindowOverlap"].toBool());
        QCOMPARE(values["hwDecoder"].toString(), QString("vaapi"));
        QCOMPARE(values["custom"].toInt(), 42);
        QCOMPARE(pauseCaelestia(false), QString());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!QJsonDocument::fromJson(file.readAll()).object()["manualPause"].toBool());
        file.close();
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{");
        file.close();
        QVERIFY(!pauseCaelestia(true).isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("{"));
    }
    void fullscreenUsesVisibleClientsNotRemovedWorkspaceFlags() {
        const QJsonObject monitor{{"activeWorkspace", QJsonObject{{"name", "1"}}},
                                  {"specialWorkspace", QJsonObject{{"name", "special:music"}}}};
        QJsonObject client{
            {"workspace", QJsonObject{{"name", "1"}}}, {"fullscreen", 2}, {"mapped", true}};
        QVERIFY(wallpaperFullscreen(monitor, {client}));
        client["workspace"] = QJsonObject{{"name", "2"}};
        QVERIFY(!wallpaperFullscreen(monitor, {client}));
        client["workspace"] = QJsonObject{{"name", "special:music"}};
        QVERIFY(wallpaperFullscreen(monitor, {client}));
        client["fullscreen"] = 1;
        QVERIFY(!wallpaperFullscreen(monitor, {client}));
        client["fullscreen"] = 2;
        client["hidden"] = true;
        QVERIFY(!wallpaperFullscreen(monitor, {client}));
    }
    void wallpaperEngineProjectDelegatesManifest() {
        QVERIFY(QFile::link(QCoreApplication::applicationFilePath(),
                            fixture.path() + "/bin/linux-wallpaperengine"));
        const QString folder = fixture.path() + "/project";
        QDir().mkpath(folder);
        QFile preview(folder + "/preview.png");
        QVERIFY(preview.open(QIODevice::WriteOnly));
        preview.write("test");
        preview.close();
        QFile manifest(folder + "/project.json");
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{\"preview\":\"preview.png\"}");
        manifest.close();
        qputenv("HWS_CAEL_EXPECTED", preview.fileName().toUtf8());
        QCOMPARE(applyCaelestiaWallpaper(manifest.fileName()), QString());
        QFile args(fixture.path() + "/state/argv.json");
        QVERIFY(args.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(args.readAll()).array(),
                 (QJsonArray{"shell", "wallpaper", "set", manifest.fileName()}));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{\"preview\":\"../outside.png\"}");
        manifest.close();
        QVERIFY(!applyCaelestiaWallpaper(manifest.fileName()).isEmpty());
    }
    void ipcPathsAreLiteralAndFailuresAreReported() {
        const QString path = fixture.path() + "/Українські шпалери.png";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
        file.close();
        qputenv("HWS_CAEL_EXPECTED", path.toUtf8());
        QCOMPARE(applyCaelestiaWallpaper(path), QString());
        QFile args(fixture.path() + "/state/argv.json");
        QVERIFY(args.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(args.readAll()).array(),
                 (QJsonArray{"shell", "wallpaper", "set", path}));
        qputenv("HWS_CAEL_BAD", "1");
        QVERIFY(!applyCaelestiaWallpaper(path).isEmpty());
        qunsetenv("HWS_CAEL_BAD");
        qputenv("HWS_CAEL_EXPECTED", "/not-accepted");
        QVERIFY(!applyCaelestiaWallpaper(path).isEmpty());
        QVERIFY(!applyCaelestiaWallpaper(path + ".missing").isEmpty());
        const QString unsafe = fixture.path() + "/unsafe'filename.png";
        QVERIFY(file.copy(unsafe));
        QVERIFY(applyCaelestiaWallpaper(unsafe).contains("filenames"));
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == "caelestia") {
        if (qEnvironmentVariableIsSet("HWS_CAEL_BAD"))
            return 2;
        const auto arguments = app.arguments().mid(1);
        if (arguments == QStringList{"shell", "wallpaper", "get"})
            QTextStream(stdout) << qEnvironmentVariable("HWS_CAEL_EXPECTED", "/old-wallpaper");
        else if (arguments == QStringList{"wallpaper", "--help"})
            QTextStream(stdout) << "--extract-thumbs";
        else {
            QDir().mkpath(qEnvironmentVariable("XDG_STATE_HOME"));
            QFile out(qEnvironmentVariable("XDG_STATE_HOME") + "/argv.json");
            if (!out.open(QIODevice::WriteOnly))
                return 3;
            out.write(QJsonDocument(QJsonArray::fromStringList(arguments)).toJson());
        }
        return 0;
    }
    TestCaelestia test;
    return QTest::qExec(&test, argc, argv);
}
#include "test-caelestia.moc"
