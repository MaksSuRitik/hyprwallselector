// SPDX-License-Identifier: GPL-3.0-or-later
#include "steamimporter.h"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class SteamImportTest : public QObject {
    Q_OBJECT
    static bool seed(const QString &path, const QByteArray &contents) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
  private slots:
    void recursiveFormatsAndUnicode() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("Steam бібліотека");
        const auto target = fixture.filePath("Шпалери");
        QVERIFY(QDir().mkpath(target));
        const QStringList extensions{"MP4", "webm", "avi", "m4v", "mov", "wmv", "mkv", "mpg", "mpeg", "flv", "ogv"};
        for (const auto &extension : extensions)
            QVERIFY(seed(source + "/steamapps/workshop/content/431960/123/" + extension + "/відео." + extension, extension.toUtf8()));
        QVERIFY(seed(source + "/steamapps/workshop/content/431960/.hidden/clip.mp4", "hidden"));
        QVERIFY(seed(source + "/steamui/intro.webm", "Steam interface video"));
        QVERIFY(seed(source + "/steamapps/common/Game/cutscene.mp4", "game cutscene"));
        QVERIFY(seed(source + "/steamapps/workshop/content/999/other.mp4", "another application"));
        QVERIFY(seed(source + "/game/data/project.json", "{}"));
        QVERIFY(seed(source + "/game/data/scene.pkg", "packed scene"));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
        const auto result = importer.result();
        QCOMPARE(result.found, extensions.size() + 1);
        QCOMPARE(result.copied, result.found);
        QCOMPARE(result.errors, 0);
        QVERIFY(!result.cancelled);
        QCOMPARE(QDir(target).entryList(QDir::Files | QDir::Hidden).size(), result.copied);
        for (const auto &extension : extensions) {
            QCOMPARE(read(target + "/відео." + extension), extension.toUtf8());
            QCOMPARE(read(source + "/steamapps/workshop/content/431960/123/" + extension + "/відео." + extension), extension.toUtf8());
        }
        QVERIFY(!QFileInfo::exists(target + "/scene.pkg"));
        QVERIFY(!QFileInfo::exists(target + "/intro.webm"));
        QVERIFY(!QFileInfo::exists(target + "/cutscene.mp4"));
        QVERIFY(!QFileInfo::exists(target + "/other.mp4"));
    }
    void collisionsAndRepeatedImports() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("Steam/steamapps/workshop/content/431960"), target = fixture.filePath("Wallpapers");
        QVERIFY(seed(source + "/1/video.mp4", "first video"));
        QVERIFY(seed(source + "/2/video.mp4", "second video"));
        QVERIFY(seed(source + "/3/video.mp4", "second video"));
        QVERIFY(seed(target + "/video.mp4", "existing personal video"));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 1);
        // Different Workshop videos with the same filename survive together.
        QCOMPARE(importer.result().copied, 2);
        QCOMPARE(importer.result().skipped, 1);
        QCOMPARE(importer.result().errors, 0);
        QCOMPARE(read(target + "/video.mp4"), QByteArray("existing personal video"));
        const auto names = QDir(target).entryList(QDir::Files);
        QCOMPARE(names.size(), 3);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(importer.result().copied, 0);
        QCOMPARE(importer.result().skipped, 3);
        QCOMPARE(QDir(target).entryList(QDir::Files), names);
        // A changed source is imported without overwriting its previous copy.
        QVERIFY(seed(source + "/1/video.mp4", "updated video"));
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(importer.result().copied, 1);
        QCOMPARE(importer.result().skipped, 2);
        QCOMPARE(QDir(target).entryList(QDir::Files).size(), 4);
        QCOMPARE(read(target + "/video.mp4"), QByteArray("existing personal video"));
    }
    void identicalExistingFileIsSkipped() {
        QTemporaryDir fixture;
        QVERIFY(seed(fixture.filePath("Steam/steamapps/workshop/content/431960/sub/clip.mp4"), "same bytes"));
        QVERIFY(seed(fixture.filePath("Wallpapers/clip.mp4"), "same bytes"));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(fixture.filePath("Steam"), fixture.filePath("Wallpapers")));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(importer.result().copied, 0);
        QCOMPARE(importer.result().skipped, 1);
        QCOMPARE(importer.result().errors, 0);
    }
    void destinationSubtreeIsExcluded() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("Steam/steamapps/workshop/content/431960"), target = source + "/nested/Wallpapers";
        QVERIFY(seed(source + "/workshop/clip.mp4", "source"));
        QVERIFY(seed(target + "/personal.mp4", "keep"));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(importer.result().found, 1);
        QCOMPARE(importer.result().copied, 1);
        QCOMPARE(read(target + "/personal.mp4"), QByteArray("keep"));
    }
    void emptyTreeAndValidation() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("Steam/steamapps/workshop/content/431960"), target = fixture.filePath("Wallpapers");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(target));
        SteamImporter importer;
        QVERIFY(!importer.start(source, source));
        QVERIFY(!importer.start(source, source + "/."));
        QVERIFY(!importer.start(source + "/missing", target));
        QVERIFY(!importer.start(source, target + "/missing"));
        QVERIFY(!importer.running());
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(importer.result().found, 0);
        QCOMPARE(importer.result().errors, 0);
    }
    void cancellationAndSingleJob() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("Steam/steamapps/workshop/content/431960"), target = fixture.filePath("Wallpapers");
        QVERIFY(QDir().mkpath(target));
        QVERIFY(seed(source + "/clip.mp4", QByteArray(16 * 1024 * 1024, 'v')));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(source, target));
        QVERIFY(!importer.start(source, target));
        importer.cancel();
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(importer.result().cancelled);
        QVERIFY(!importer.running());
        QCOMPARE(QDir(target).entryList(QDir::Files | QDir::Hidden).size(), 0);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!importer.result().cancelled);
        QCOMPARE(importer.result().copied, 1);
    }
    void directoryLinksStayOutsideScan() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("Steam/steamapps/workshop/content/431960"), target = fixture.filePath("Wallpapers");
        QVERIFY(QDir().mkpath(source));
        QVERIFY(QDir().mkpath(target));
        QVERIFY(seed(fixture.filePath("Outside/external.mp4"), "outside"));
#ifdef Q_OS_WIN
        QProcess command;
        command.start("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(source + "/linked"),
                                  QDir::toNativeSeparators(fixture.filePath("Outside"))});
        QVERIFY(command.waitForFinished());
        QCOMPARE(command.exitCode(), 0);
#else
        QVERIFY(QFile::link(fixture.filePath("Outside"), source + "/linked"));
#endif
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(importer.result().found, 0);
        QVERIFY(QDir(target).isEmpty());
    }
    void supportedSelections_data() {
        QTest::addColumn<QString>("selection");
        QTest::newRow("library") << "Steam";
        QTest::newRow("steamapps") << "Steam/steamapps";
        QTest::newRow("workshop") << "Steam/steamapps/workshop";
        QTest::newRow("content") << "Steam/steamapps/workshop/content";
        QTest::newRow("app-id") << "Steam/steamapps/workshop/content/431960";
    }
    void supportedSelections() {
        QFETCH(QString, selection);
        QTemporaryDir fixture;
        const auto workshop = fixture.filePath("Steam/steamapps/workshop/content/431960");
        const auto target = fixture.filePath("Wallpapers");
        QVERIFY(seed(workshop + "/123/wallpaper.mp4", "wallpaper"));
        QVERIFY(seed(fixture.filePath("Steam/steamapps/common/game/intro.webm"), "game"));
        QVERIFY(seed(fixture.filePath("Steam/steamapps/workshop/content/12345/unrelated.mp4"), "other app"));
        QVERIFY(QDir().mkpath(target));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QCOMPARE(SteamImporter::workshopDirectory(fixture.filePath(selection)), QFileInfo(workshop).canonicalFilePath());
        QVERIFY(importer.start(fixture.filePath(selection), target));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(importer.result().found, 1);
        QCOMPARE(importer.result().copied, 1);
        QCOMPARE(read(target + "/wallpaper.mp4"), QByteArray("wallpaper"));
    }
    void missingWorkshopNeverScansGames() {
        QTemporaryDir fixture;
        QVERIFY(seed(fixture.filePath("Steam/steamapps/common/game/intro.mp4"), "game cutscene"));
        QVERIFY(seed(fixture.filePath("Steam/steamapps/workshop/content/12345/clip.mp4"), "different app"));
        const auto target = fixture.filePath("Wallpapers");
        QVERIFY(QDir().mkpath(target));
        SteamImporter importer;
        for (const auto &path : {QString(), fixture.filePath("Steam"),
                                 fixture.filePath("Steam/steamapps/common/game"),
                                 fixture.filePath("Steam/steamapps/workshop/content/12345")}) {
            QVERIFY(!importer.start(path, target));
            QVERIFY(!importer.running());
        }
        QVERIFY(QDir(target).isEmpty());
    }
    void legacyCopiesAndChangingSelection() {
        QTemporaryDir fixture;
        const auto root = fixture.filePath("Steam");
        const auto workshop = root + "/steamapps/workshop/content/431960";
        const auto target = fixture.filePath("Wallpapers");
        QVERIFY(seed(workshop + "/123/video.mp4", "wallpaper"));
        QVERIFY(seed(target + "/video.mp4", "different personal video"));
        QVERIFY(seed(target + "/video - 0123456789ab.mp4", "wallpaper"));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        for (const auto &selection : {root, workshop}) {
            QVERIFY(importer.start(selection, target));
            QTRY_COMPARE(finished.size(), selection == root ? 1 : 2);
            QCOMPARE(importer.result().copied, 0);
            QCOMPARE(importer.result().skipped, 1);
            QCOMPARE(importer.result().errors, 0);
        }
        QCOMPARE(QDir(target).entryList(QDir::Files).size(), 2);
        QCOMPARE(read(target + "/video.mp4"), QByteArray("different personal video"));
    }
};
QTEST_GUILESS_MAIN(SteamImportTest)
#include "test-steamimport.moc"
