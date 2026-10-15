// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "mediawindows.h"
#include "nativevideo.h"
#include <QQuickView>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQmlContext>
#include <QtTest>

class WindowsUiTest : public QObject {
    Q_OBJECT
  private slots:
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
        const auto screenshot=view.grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(qEnvironmentVariable("HWS_TEST_UI_CAPTURE","settings-test.png")));
        // Exercise the actual settings save callback in the isolated controller.
        QVERIFY(QMetaObject::invokeMethod(panel,"setDraft",Q_ARG(QVariant,"highPriority"),Q_ARG(QVariant,true)));
        auto *save=root->findChild<QObject *>("saveSettings");
        QVERIFY(save);
        QVERIFY(QMetaObject::invokeMethod(save,"clicked"));
        QTRY_VERIFY(!root->property("settingsOpen").toBool());
        QVERIFY(backend.settings()["highPriority"].toBool());
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
