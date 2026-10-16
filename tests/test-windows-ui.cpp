// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "mediawindows.h"
#include "nativevideo.h"
#include <QQuickView>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQmlContext>
#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QScreen>

class WindowsUiTest : public QObject {
    Q_OBJECT
  private slots:
    void freshDesktopAndRecovery() {
        if (!qEnvironmentVariableIsSet("HWS_TEST_DESKTOP")) QSKIP("Opt-in isolated desktop profile test");
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        QImage image(64,64,QImage::Format_RGB32); image.fill(QColor("#243d4c"));
        const auto path = profile.filePath("image.jpg");
        QVERIFY(image.save(path));
        QDir().mkpath(profile.filePath("config/hyprwallselector"));
        QFile config(profile.filePath("config/hyprwallselector/config.json"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(QJsonDocument(QJsonObject{{"folder", profile.path()},
            {"syncLockScreen", false}, {"shortcut", "Ctrl+Alt+Shift+F9"}}).toJson());
        config.close();
        // No saved wallpaper/profile; leave user autostart and lock screen alone.
        Controller backend(false, nullptr, profile.path());
        backend.start(false);
        QVERIFY(backend.applyFile(path));
        auto report = [&] { return QJsonDocument::fromJson(backend.diagnostic().toUtf8()).object(); };
        const int count = qGuiApp->screens().size();
        QTRY_COMPARE_WITH_TIMEOUT(backend.currentPath(), path, 5000);
        QCOMPARE(report()["wallpaperWindows"].toInt(), count);
        QCOMPARE(report()["desktopAttached"].toInt(), count);
        QVERIFY(report()["firstWallpaperMs"].toInteger() >= 0);
        auto *platform = backend.findChild<WindowsPlatform *>();
        QVERIFY(platform);
        QSignalSpy requested(&backend, &Controller::wallpaperRequested);
        // Simulate Explorer invalidating its desktop without restarting Explorer.
        for (int cycle=0; cycle<3; ++cycle) {
            requested.clear();
            platform->desktopLost();
            QCOMPARE(report()["wallpaperWindows"].toInt(), 0);
            QTRY_COMPARE_WITH_TIMEOUT(report()["desktopAttached"].toInt(), count, 5000);
            QTRY_VERIFY(!requested.isEmpty());
            QCOMPARE(report()["wallpaperWindows"].toInt(), count);
            QCOMPARE(backend.currentPath(), path);
        }
        // A transient placement failure must recreate the affected view too.
        // Views have foreign QWindow parents; obtain one through Qt's window list.
        QQuickView *wallpaper = nullptr;
        for (auto *window : qGuiApp->allWindows())
            if (auto *view = qobject_cast<QQuickView *>(window); view && view->parent()) {
                wallpaper = view; break;
            }
        QVERIFY(wallpaper);
        QPointer<QQuickView> old = wallpaper;
        platform->wallpaperLost(wallpaper);
        QTRY_VERIFY(old.isNull());
        QTRY_COMPARE_WITH_TIMEOUT(report()["desktopAttached"].toInt(), count, 5000);
        QCOMPARE(report()["wallpaperWindows"].toInt(), count);
    }
    void selectorOpeningFrames() {
        if (!qEnvironmentVariableIsSet("HWS_TEST_UI")) QSKIP("Opt-in rendered UI test");
        QTemporaryDir library;
        QVERIFY(library.isValid());
        QImage thumbnail(720, 480, QImage::Format_RGB32);
        thumbnail.fill(QColor("#487693"));
        QVERIFY(thumbnail.save(library.filePath("wallpaper.jpg")));
        Controller backend(true);
        auto config = backend.settings(); config["folder"] = library.path();
        QVERIFY(backend.saveSettings(config));
        QTRY_COMPARE(backend.items().size(), 1);
        QQuickView view;
        view.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        view.setColor(Qt::transparent);
        view.setPersistentSceneGraph(false);
        view.resize(1000, 740);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty("backend", &backend);
        backend.attach(&view);
        connect(&backend, &Controller::visibleChanged, &view, [&] {
            if (backend.visible() && view.status() == QQuickView::Null)
                view.setSource(QUrl("qrc:/qml/Main.qml"));
        });
        // Exercise both lazy creation and rebuilding the released scene graph.
        for (int cycle = 0; cycle < 4; ++cycle) {
            backend.toggle();
            QCOMPARE(view.status(), QQuickView::Ready);
            auto *root = view.rootObject();
            QVERIFY(root);
            QCOMPARE(root->opacity(), qreal(0));
            QCOMPARE(view.opacity(), qreal(0));
            QVERIFY(!backend.selectorPresented());
            QList<qreal> opacity;
            auto changes = connect(root, &QQuickItem::opacityChanged, &view, [&] { opacity << root->opacity(); });
            QTRY_VERIFY_WITH_TIMEOUT(backend.selectorPresented(), 2000);
            QTRY_COMPARE_WITH_TIMEOUT(root->opacity(), qreal(1), 2000);
            QVERIFY(opacity.size() >= 2);
            QVERIFY(std::any_of(opacity.cbegin(), opacity.cend(), [](qreal value) { return value > 0 && value < 1; }));
            QVERIFY(std::is_sorted(opacity.cbegin(), opacity.cend()));
            disconnect(changes);
            QCOMPARE(view.opacity(), qreal(1));
            const auto frame = view.grabWindow();
            QVERIFY(!frame.isNull());
            QVERIFY(frame.pixelColor(1, 1).alpha() == 0);
            QVERIFY(frame.pixelColor(frame.width()/2, frame.height()/2).alpha() > 200);
            backend.hide();
            QCOMPARE(root->opacity(), qreal(0));
            QVERIFY(!backend.selectorPresented());
            QVERIFY(!view.isVisible());
            QTest::qWait(30);
        }
        // A queued frame from an interrupted opening must not reveal a closed
        // window, nor cancel the next opening's fade.
        backend.toggle(); backend.hide(); backend.toggle();
        QCOMPARE(view.rootObject()->opacity(), qreal(0));
        QTRY_COMPARE_WITH_TIMEOUT(view.rootObject()->opacity(), qreal(1), 2000);
        backend.hide();
        QTest::qWait(100);
        QVERIFY(!backend.selectorPresented());
        QCOMPARE(view.opacity(), qreal(0));
    }
    void prioritySettings() {
        if (!qEnvironmentVariableIsSet("HWS_TEST_UI")) QSKIP("Opt-in rendered UI test");
        Controller backend(true);
        QQuickView view;
        view.resize(1100,760);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.rootContext()->setContextProperty("backend",&backend);
        backend.attach(&view);
        view.setSource(QUrl("qrc:/qml/Main.qml"));
        QCOMPARE(view.status(),QQuickView::Ready);
        backend.toggle();
        auto *root=view.rootObject();
        auto *panel=root->findChild<QQuickItem *>("settingsPanel");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(root,"openSettings"));
        QTest::qWait(400);
        auto *priority=root->findChild<QQuickItem *>("highPrioritySetting");
        QVERIFY(priority); QVERIFY(priority->isVisible());
        QVERIFY(priority->height()>=48);
        QVERIFY(priority->width()>300);
        auto *lockScreen=root->findChild<QQuickItem *>("lockScreenSetting");
        QVERIFY(lockScreen);
        panel->setProperty("tab", 2);
        QTest::qWait(100);
        QVERIFY(lockScreen->isVisible());
        QVERIFY(lockScreen->height()>=48);
        panel->setProperty("tab", 0);
        QTest::qWait(100);
        const auto screenshot=view.grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(qEnvironmentVariable("HWS_TEST_UI_CAPTURE","settings-test.png")));
        // Exercise the actual settings save callback in the isolated controller.
        QVERIFY(QMetaObject::invokeMethod(panel,"setDraft",Q_ARG(QVariant,"highPriority"),Q_ARG(QVariant,true)));
        QVERIFY(QMetaObject::invokeMethod(panel,"setDraft",Q_ARG(QVariant,"syncLockScreen"),Q_ARG(QVariant,false)));
        auto *save=root->findChild<QObject *>("saveSettings");
        QVERIFY(save);
        QVERIFY(QMetaObject::invokeMethod(save,"clicked"));
        QTRY_VERIFY(!root->property("settingsOpen").toBool());
        QVERIFY(backend.settings()["highPriority"].toBool());
        QVERIFY(!backend.settings()["syncLockScreen"].toBool());
        QVERIFY(backend.visible());
        QVERIFY(QMetaObject::invokeMethod(root,"openSettings"));
        QTest::qWait(300);
        QVERIFY(priority->property("checked").toBool());
        auto *cancel=root->findChild<QObject *>("cancelSettings");
        QVERIFY(cancel); QVERIFY(QMetaObject::invokeMethod(cancel,"clicked"));
        QTRY_VERIFY(!root->property("settingsOpen").toBool());
        QVERIFY(backend.visible());
    }
};
int main(int argc,char **argv) {
    Q_INIT_RESOURCE(ui); Q_INIT_RESOURCE(shaders);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc,argv);
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<NativeVideo>("Hyprwallselector",1,0,"NativeVideo");
    qmlRegisterType<MediaItem>("Hyprwallselector",1,0,"VideoSession");
    qmlRegisterType(QUrl("qrc:/qml/WindowsVideo.qml"),"Hyprwallselector",1,0,"VideoSurface");
    WindowsUiTest test; return QTest::qExec(&test,argc,argv);
}
#include "test-windows-ui.moc"
