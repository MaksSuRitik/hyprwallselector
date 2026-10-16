// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "windowsdisplays.h"
#include <QFile>
#include <QJsonDocument>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

class WindowsStateTest : public QObject {
    Q_OBJECT
    static bool write(const QString &path, QJsonObject value) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(value).toJson()) > 0;
    }
  private slots:
    void freshProfile() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        Controller controller(true, nullptr, profile.path());
        const auto settings = controller.settings();
        QCOMPARE(settings["monitor"].toString(), QString("ALL"));
        QCOMPARE(settings["renderer"].toString(), QString("native"));
        QCOMPARE(settings["shortcut"].toString(), QString("Alt+T"));
        QVERIFY(settings["manageBackground"].toBool());
        QVERIFY(settings["syncLockScreen"].toBool());
        QVERIFY(!settings["highPriority"].toBool());
        QVERIFY(controller.currentPath().isEmpty());
        auto next = settings; next["folder"] = profile.path();
        QImage image(32,32,QImage::Format_RGB32); image.fill(Qt::blue);
        QVERIFY(image.save(profile.filePath("image.jpg")));
        QVERIFY(controller.saveSettings(next));
        QTRY_COMPARE(controller.items().size(), 1);
        QCOMPARE(controller.items().first().toMap()["path"].toString(), profile.filePath("image.jpg"));
    }
    void reportedDisplayNames() {
        const QList<WindowsDisplays::Display> displays{
            {"\\\\.\\DISPLAY1", "R27s"}, {"\\\\.\\DISPLAY2", "SA273 G0"}};
        QCOMPARE(WindowsDisplays::migrateTarget("SA273 G0", displays), QString("\\\\.\\DISPLAY2"));
        QCOMPARE(WindowsDisplays::migrateTarget("ALL", displays), QString("ALL"));
        const QJsonObject first{{"path", "one.jpg"}}, second{{"path", "two.mp4"}};
        auto state = WindowsDisplays::migrateState({{"R27s", first}, {"SA273 G0", second}}, displays);
        QCOMPARE(state["\\\\.\\DISPLAY1"].toObject(), first);
        QCOMPARE(state["\\\\.\\DISPLAY2"].toObject(), second);
        QCOMPARE(WindowsDisplays::migrateState(state, displays), state);
    }
    void identicalModelsAndDisconnectedDisplays() {
        const QList<WindowsDisplays::Display> displays{
            {"\\\\.\\DISPLAY1", "Same model"}, {"\\\\.\\DISPLAY2", "Same model"}};
        QCOMPARE(WindowsDisplays::migrateTarget("Same model", displays), QString("ALL"));
        QCOMPARE(WindowsDisplays::migrateTarget("\\\\.\\DISPLAY2", displays), QString("\\\\.\\DISPLAY2"));
        QCOMPARE(WindowsDisplays::migrateTarget("Disconnected", displays), QString("Disconnected"));
        const QJsonObject old{{"path", "old.jpg"}}, newer{{"path", "new.mp4"}};
        auto state = WindowsDisplays::migrateState({{"Same model", old}, {"\\\\.\\DISPLAY2", newer}}, displays);
        QCOMPARE(state["\\\\.\\DISPLAY1"].toObject(), old);
        QCOMPARE(state["\\\\.\\DISPLAY2"].toObject(), newer);
        QCOMPARE(WindowsDisplays::migrateState({}, displays), QJsonObject());
    }
    void existingProfileMigration() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        auto *screen = qGuiApp->primaryScreen();
        QVERIFY(screen);
        const QString id = WindowsPlatform::screenId(screen);
        QVERIFY(!id.isEmpty());
        QVERIFY(write(profile.filePath("config/hyprwallselector/config.json"),
                      {{"monitor", screen->name()}, {"language", "ru"}, {"manageBackground", false}}));
        QVERIFY(write(profile.filePath("state/hyprwallselector/state.json"),
                      {{screen->name(), QJsonObject{{"path", "kept.jpg"}}}}));
        Controller controller(true, nullptr, profile.path());
        QCOMPARE(controller.settings()["monitor"].toString(), id);
        QCOMPARE(controller.currentPath(), QString("kept.jpg"));
        QCOMPARE(controller.settings()["language"].toString(), QString("ru"));
        QVERIFY(!controller.settings()["manageBackground"].toBool());
    }
    void truncatedConfigUsesDefaults() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        const auto path = profile.filePath("config/hyprwallselector/config.json");
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile broken(path); QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{\"monitor\":"); broken.close();
        Controller controller(true, nullptr, profile.path());
        QCOMPARE(controller.settings()["monitor"].toString(), QString("ALL"));
        QVERIFY(controller.settings()["manageBackground"].toBool());
    }
};
QTEST_MAIN(WindowsStateTest)
#include "test-windows-state.moc"
