// SPDX-License-Identifier: GPL-3.0-or-later
#include "integration.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>
class TestIntegration : public QObject {
    Q_OBJECT
    QTemporaryDir fixture;
    QString settingsFile;
    QJsonObject read() {
        QFile f(settingsFile);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(f.readAll()).object();
    }
    void write(QJsonObject o) {
        QFile f(settingsFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(o).toJson());
    }
  private slots:
    void initTestCase() {
        QVERIFY(fixture.isValid());
        QDir().mkpath(fixture.path() + "/config/DankMaterialShell");
        QDir().mkpath(fixture.path() + "/bin");
        QVERIFY(QFile::link(QCoreApplication::applicationFilePath(), fixture.path() + "/bin/dms"));
        qputenv("PATH", (fixture.path() + "/bin:").toUtf8() + qgetenv("PATH"));
        qputenv("XDG_CONFIG_HOME", (fixture.path() + "/config").toUtf8());
        qputenv("XDG_STATE_HOME", (fixture.path() + "/state").toUtf8());
        settingsFile = fixture.path() + "/config/DankMaterialShell/settings.json";
    }
    void preserveAndRestore() {
        QJsonObject initial{{"untouched", QJsonObject{{"value", 123}}},
                            {"screenPreferences", QJsonObject{{"wallpaper", QJsonArray{"all"}},
                                                              {"dock", QJsonArray{"DP-2"}}}}};
        write(initial);
        QCOMPARE(configureDms(false), QString());
        auto changed = read();
        QCOMPARE(changed["untouched"], initial["untouched"]);
        QCOMPARE(changed["screenPreferences"].toObject()["dock"],
                 initial["screenPreferences"].toObject()["dock"]);
        QVERIFY(changed["screenPreferences"].toObject()["wallpaper"].toArray().isEmpty());
        QCOMPARE(configureDms(false), QString());
        QCOMPARE(configureDms(true), QString());
        QCOMPARE(read(), initial);
    }
    void doNotOverwriteNewChoices() {
        write(QJsonObject{{"screenPreferences", QJsonObject{{"wallpaper", QJsonArray{"all"}}}}});
        QCOMPARE(configureDms(false), QString());
        auto changed = read();
        changed["screenPreferences"] = QJsonObject{{"wallpaper", QJsonArray{"DP-3"}}};
        write(changed);
        QVERIFY(!configureDms(true).isEmpty());
        QCOMPARE(read(), changed);
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (QFileInfo(QString::fromLocal8Bit(argv[0])).fileName() == "dms") {
        QFile f(qEnvironmentVariable("XDG_CONFIG_HOME") + "/DankMaterialShell/settings.json");
        if (!f.open(QIODevice::ReadOnly))
            return 1;
        QTextStream(stdout)
            << QJsonDocument(
                   QJsonDocument::fromJson(f.readAll()).object()["screenPreferences"].toObject())
                   .toJson(QJsonDocument::Compact);
        return 0;
    }
    TestIntegration test;
    return QTest::qExec(&test, argc, argv);
}
#include "test-integration.moc"
