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
        QVERIFY(seed(source + "/.hidden/clip.mp4", "hidden"));
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
    }
    void collisionsAndRepeatedImports() {
        QTemporaryDir fixture;
        const auto source = fixture.filePath("Steam"), target = fixture.filePath("Wallpapers");
        QVERIFY(seed(source + "/1/video.mp4", "first video"));
        QVERIFY(seed(source + "/2/video.mp4", "second video"));
        QVERIFY(seed(source + "/3/video.mp4", "second video"));
        QVERIFY(seed(target + "/video.mp4", "existing personal video"));
        SteamImporter importer;
        QSignalSpy finished(&importer, &SteamImporter::finished);
        QVERIFY(importer.start(source, target));
        QTRY_COMPARE(finished.size(), 1);
        // Different Workshop videos with the same filename survive together.
        QCOMPARE(importer.result().copied, 3);
        QCOMPARE(importer.result().errors, 0);
        QCOMPARE(read(target + "/video.mp4"), QByteArray("existing personal video"));
        const auto names = QDir(target).entryList(QDir::Files);
        QCOMPARE(names.size(), 4);
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
        QCOMPARE(QDir(target).entryList(QDir::Files).size(), 5);
        QCOMPARE(read(target + "/video.mp4"), QByteArray("existing personal video"));
    }
    void identicalExistingFileIsSkipped() {
        QTemporaryDir fixture;
        QVERIFY(seed(fixture.filePath("Steam/sub/clip.mp4"), "same bytes"));
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
        const auto source = fixture.filePath("Steam"), target = source + "/nested/Wallpapers";
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
        const auto source = fixture.filePath("Steam"), target = fixture.filePath("Wallpapers");
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
        const auto source = fixture.filePath("Steam"), target = fixture.filePath("Wallpapers");
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
        const auto source = fixture.filePath("Steam"), target = fixture.filePath("Wallpapers");
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
};
QTEST_GUILESS_MAIN(SteamImportTest)
#include "test-steamimport.moc"
