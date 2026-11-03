// SPDX-License-Identifier: GPL-3.0-or-later
#include "caelestiapreview.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

static QJsonObject read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
static void write(const QString &path, const QJsonObject &value) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(value).toJson()) < 0 ||
        !file.commit())
        qFatal("Cannot save isolated preview fixture");
}
class TestPreview : public QObject {
    Q_OBJECT
    QTemporaryDir fixture;
    QString path;
    QJsonArray operations() const {
        return read(path)["operations"].toArray();
    }
    int count(const QString &name) const {
        int n = 0;
        for (const auto &op : operations())
            if (op.toObject()["op"] == name)
                ++n;
        return n;
    }
  private slots:
    void initTestCase() {
        QVERIFY(fixture.isValid());
        QDir().mkpath(fixture.path() + "/bin");
        QVERIFY(QFile::link(QCoreApplication::applicationFilePath(),
                            fixture.path() + "/bin/caelestia"));
        qputenv("PATH", (fixture.path() + "/bin").toUtf8());
        path = fixture.path() + "/state.json";
        qputenv("HWS_PREVIEW_STATE", path.toUtf8());
    }
    void init() {
        qunsetenv("HWS_PREVIEW_MISSING");
        qunsetenv("HWS_PREVIEW_SLOW");
        write(path, {{"actual", "/original.mp4"},
                     {"shown", "/original.mp4"},
                     {"operations", QJsonArray{}},
                     {"commits", 0}});
    }
    void rapidFocusCoalescesAndDoesNotCommit() {
        CaelestiaPreview preview;
        QSignalSpy shown(&preview, &CaelestiaPreview::previewed);
        preview.open();
        for (int i = 0; i < 30; ++i)
            preview.focus(QString("/wall%1.mp4").arg(i), QString("/wall%1.mp4").arg(i));
        QTRY_COMPARE(shown.size(), 1);
        QCOMPARE(shown[0][0].toString(), QString("/wall29.mp4"));
        QCOMPARE(count("preview"), 1);
        QCOMPARE(read(path)["actual"].toString(), QString("/original.mp4"));
        preview.cancel();
        QTRY_VERIFY(!preview.active());
        QCOMPARE(read(path)["shown"].toString(), QString("/original.mp4"));
        QCOMPARE(read(path)["commits"].toInt(), 0);
    }
    void closingBeforeDwellNeverPreviews() {
        CaelestiaPreview preview;
        preview.open();
        preview.focus("/candidate.png", "/candidate.png");
        preview.cancel();
        QTRY_VERIFY(!preview.active());
        QCOMPARE(count("preview"), 0);
        QCOMPARE(count("commit"), 0);
    }
    void focusedConfirmedWallpaperDoesNotReload() {
        CaelestiaPreview preview;
        QSignalSpy baseline(&preview, &CaelestiaPreview::baselineReady);
        preview.open();
        preview.focus("/original.mp4", "/original.mp4");
        QTRY_COMPARE(baseline.size(), 1);
        QTest::qWait(150);
        QCOMPARE(count("preview"), 0);
    }
    void closeFencesInflightPreviewAndQuickReopen() {
        qputenv("HWS_PREVIEW_SLOW", "1");
        CaelestiaPreview preview;
        preview.open();
        preview.focus("/first.png", "/first.png");
        QTRY_COMPARE(count("preview"), 1);
        preview.focus("/discard.png", "/discard.png");
        preview.cancel();
        QTRY_VERIFY(!preview.active());
        QCOMPARE(read(path)["shown"].toString(), QString("/original.mp4"));
        preview.open();
        preview.focus("/second.png", "/second.png");
        QSignalSpy shown(&preview, &CaelestiaPreview::previewed);
        QTRY_VERIFY(!shown.isEmpty());
        QCOMPARE(shown.last()[0].toString(), QString("/second.png"));
        preview.shutdown();
        QCOMPARE(read(path)["shown"].toString(), QString("/original.mp4"));
        QCOMPARE(count("commit"), 0);
    }
    void immediateConfirmPersistsOnceWithoutPreviewRestart() {
        CaelestiaPreview preview;
        QSignalSpy committed(&preview, &CaelestiaPreview::committed);
        preview.open();
        preview.focus("/chosen.mp4", "/chosen.mp4");
        preview.confirm("/chosen.mp4", "/chosen.mp4");
        QTRY_COMPARE(committed.size(), 1);
        QCOMPARE(committed[0][0].toString(), QString("/chosen.mp4"));
        QCOMPARE(count("preview"), 0);
        QCOMPARE(read(path)["commits"].toInt(), 1);
        preview.cancel();
        QTest::qWait(30);
        QCOMPARE(read(path)["actual"].toString(), QString("/chosen.mp4"));
    }
    void latestConfirmationWaitsForOldPreview() {
        qputenv("HWS_PREVIEW_SLOW", "1");
        CaelestiaPreview preview;
        QSignalSpy committed(&preview, &CaelestiaPreview::committed);
        preview.open();
        preview.focus("/old.png", "/old.png");
        QTRY_COMPARE(count("preview"), 1);
        preview.confirm("/final.png", "/final.png");
        QTRY_COMPARE(committed.size(), 1);
        QCOMPARE(read(path)["actual"].toString(), QString("/final.png"));
        QCOMPARE(read(path)["commits"].toInt(), 1);
        QCOMPARE(operations().last().toObject()["op"].toString(), QString("commit"));
    }
    void unavailableBridgeTerminatesAndAllowsLegacyFallback() {
        qputenv("HWS_PREVIEW_MISSING", "1");
        CaelestiaPreview preview;
        QSignalSpy errors(&preview, &CaelestiaPreview::failed);
        preview.open();
        QTRY_VERIFY(preview.unavailable());
        QTRY_VERIFY(!preview.active());
        QVERIFY(!errors.isEmpty());
        preview.confirm("/wall.png", "/wall.png");
        QVERIFY(!preview.confirming());
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (QFileInfo(app.arguments()[0]).fileName() == "caelestia") {
        if (qEnvironmentVariableIsSet("HWS_PREVIEW_MISSING"))
            return 2;
        const auto args = app.arguments().mid(1);
        const QString path = qEnvironmentVariable("HWS_PREVIEW_STATE");
        QLockFile lock(path + ".lock");
        if (!lock.tryLock(0))
            return 3;
        auto state = read(path);
        auto ops = state["operations"].toArray();
        const QString op = args.value(2), session = args.value(3), wall = args.value(5);
        ops.append(QJsonObject{{"op", op}, {"path", wall}});
        state["operations"] = ops;
        write(path, state);
        if (op == "preview" && qEnvironmentVariableIsSet("HWS_PREVIEW_SLOW"))
            QTest::qSleep(200);
        if (op == "begin")
            state["owner"] = session;
        if (op == "preview")
            state["shown"] = wall;
        if (op == "commit") {
            state["actual"] = wall;
            state["shown"] = wall;
            state["commits"] = state["commits"].toInt() + 1;
            state["owner"] = "";
        }
        if (op == "cancel" && state["owner"] == session) {
            state["shown"] = state["actual"];
            state["owner"] = "";
        }
        write(path, state);
        QTextStream(stdout) << QJsonDocument(QJsonObject{{"ok", true}, {"actual", state["actual"]}})
                                   .toJson(QJsonDocument::Compact);
        return 0;
    }
    TestPreview test;
    return QTest::qExec(&test, argc, argv);
}
#include "test-preview.moc"
