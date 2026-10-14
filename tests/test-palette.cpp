// SPDX-License-Identifier: GPL-3.0-or-later
#include "dmspalette.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

class TestPalette : public QObject {
    Q_OBJECT
    QTemporaryDir fixture;
    QString source;
    QList<QStringList> calls() {
        QFile f(fixture.path() + "/calls");
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QList<QStringList> result;
        for (auto line : f.readAll().split('\n')) {
            if (line.isEmpty())
                continue;
            QStringList args;
            for (auto v : QJsonDocument::fromJson(line).array())
                args << v.toString();
            result << args;
        }
        return result;
    }
  private slots:
    void initTestCase() {
        QVERIFY(fixture.isValid());
        QDir().mkpath(fixture.path() + "/bin");
        QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), fixture.path() + "/bin/dms"));
        qputenv("PATH", (fixture.path() + "/bin:").toUtf8() + qgetenv("PATH"));
        qputenv("HWS_MOCK_ROOT", fixture.path().toUtf8());
        qputenv("XDG_CACHE_HOME", (fixture.path() + "/cache").toUtf8());
        source = fixture.path() + "/image # with spaces.png";
        QFile image(source);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("fixture");
    }
    void init() {
        QFile::remove(fixture.path() + "/calls");
        qputenv("HWS_MOCK_MODE", "global");
        qunsetenv("HWS_MOCK_CURRENT");
    }
    void globalAndCoalescing() {
        DmsPalette palette;
        QSignalSpy done(&palette, &DmsPalette::completed);
        palette.setEnabled(true);
        for (int i = 0; i < 10; ++i)
            palette.request("DP-1", source, false);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.first().first().toBool());
        QCOMPARE(calls(), QList<QStringList>({{"ipc", "call", "wallpaper", "get"},
                                              {"ipc", "call", "wallpaper", "set", source}}));
    }
    void perMonitorAndAlreadyCurrent() {
        qputenv("HWS_MOCK_MODE", "monitor");
        DmsPalette palette;
        QSignalSpy done(&palette, &DmsPalette::completed);
        palette.setEnabled(true);
        palette.request("DP-2", source, false);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(calls().last(),
                 QStringList({"ipc", "call", "wallpaper", "setFor", "DP-2", source}));
        QFile::remove(fixture.path() + "/calls");
        qputenv("HWS_MOCK_CURRENT", source.toUtf8());
        palette.request("DP-2", source, false);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(calls().size(), 2);
        QCOMPARE(calls().last(), QStringList({"ipc", "call", "wallpaper", "getFor", "DP-2"}));
    }
    void disabledAndFailure() {
        DmsPalette palette;
        palette.setEnabled(true);
        palette.request("DP-1", source, false);
        palette.setEnabled(false);
        QTest::qWait(300);
        QVERIFY(calls().isEmpty());
        qputenv("HWS_MOCK_MODE", "failure");
        QSignalSpy done(&palette, &DmsPalette::completed);
        palette.setEnabled(true);
        palette.request("DP-1", source, false);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 7000);
        QVERIFY(!done.first().first().toBool()); // DMS can return ERROR with exit status zero.
    }
    void videoPoster() {
        const auto video = fixture.path() + "/clip #1.mp4";
        QProcess ff;
        ff.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=320x180:d=1",
                            "-c:v", "libx264", "-threads", "1", video});
        QVERIFY(ff.waitForFinished(10000));
        QCOMPARE(ff.exitCode(), 0);
        DmsPalette palette;
        QSignalSpy done(&palette, &DmsPalette::completed);
        palette.setEnabled(true);
        palette.request("DP-1", video, true);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QVERIFY(done.first().first().toBool());
        const auto sent = calls().last().last();
        QVERIFY(sent != video && sent.endsWith(".jpg"));
        QVERIFY(QFileInfo(sent).size() > 100);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == "dms") {
        const auto args = app.arguments().mid(1);
        QFile log(qEnvironmentVariable("HWS_MOCK_ROOT") + "/calls");
        if (!log.open(QIODevice::Append))
            return 1;
        log.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact) +
                  '\n');
        QString response;
        const auto mode = qEnvironmentVariable("HWS_MOCK_MODE");
        if (mode == "failure")
            response = "ERROR: mock failure";
        else if (args.value(3) == "get" && mode == "monitor")
            response = "ERROR: Per-monitor mode enabled. Use getFor(screenName) instead.";
        else if (args.value(3).startsWith("get"))
            response = qEnvironmentVariable("HWS_MOCK_CURRENT");
        else
            response = "SUCCESS: Wallpaper set";
        QTextStream(stdout) << response;
        return 0;
    }
    TestPalette test;
    return QTest::qExec(&test, argc, argv);
}
#include "test-palette.moc"
