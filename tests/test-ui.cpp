// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "mediaitem.h"
#include <QFile>
#include <QJSValue>
#include <QJsonDocument>
#include <QPainter>
#include <QProcess>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtTest>
#include <clocale>
class TestUi : public QObject {
    Q_OBJECT
    Controller *backend = nullptr;
    QQuickView *view = nullptr;
    QTemporaryDir fixture;
  private slots:
    void initTestCase() {
        QVERIFY(fixture.isValid());
        qputenv("XDG_CONFIG_HOME", (fixture.path() + "/config").toUtf8());
        qputenv("XDG_STATE_HOME", (fixture.path() + "/state").toUtf8());
        qputenv("XDG_CACHE_HOME", (fixture.path() + "/cache").toUtf8());
        QDir().mkpath(fixture.path() + "/config/hyprwallselector");
        QDir().mkpath(fixture.path() + "/library");
        for (int i = 0; i < 5; i++) {
            QImage img(640, 360, QImage::Format_RGB32);
            img.fill(QColor::fromHsv(i * 60, 180, 170));
            QPainter painter(&img);
            painter.setPen(Qt::white);
            painter.drawText(img.rect(), Qt::AlignCenter, "TOP ↑  /  TEST " + QString::number(i));
            painter.end();
            QVERIFY(img.save(fixture.path() + "/library/image #" + QString::number(i) + ".png"));
        }
        QProcess ff;
        ff.start("ffmpeg",
                 {"-nostdin", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24",
                  "-t", "2", "-c:v", "libx264", "-threads", "2", "-pix_fmt", "yuv420p",
                  fixture.path() + "/library/video.mp4"});
        QVERIFY(ff.waitForFinished(15000));
        QCOMPARE(ff.exitCode(), 0);
        QFile config(fixture.path() + "/config/hyprwallselector/config.json");
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(QJsonDocument(QJsonObject{{"folder", fixture.path() + "/library"}}).toJson());
        config.close();
        backend = new Controller(true);
        view = new QQuickView;
        view->resize(1280, 800);
        view->setColor(Qt::transparent);
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->rootContext()->setContextProperty("backend", backend);
        backend->attach(view);
        view->setSource(QUrl("qrc:/qml/Main.qml"));
        QCOMPARE(view->status(), QQuickView::Ready);
        backend->start(true);
        QTRY_VERIFY_WITH_TIMEOUT(!backend->busy(), 20000);
        QVERIFY(backend->items().size() > 2);
        QVERIFY(QTest::qWaitForWindowExposed(view));
    }
    void navigation() {
        auto *root = view->rootObject();
        root->forceActiveFocus();
        int original = root->property("selected").toInt();
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(root->property("selected").toInt(), (original + 1) % backend->items().size());
        for (int i = 0; i < 20; i++)
            QTest::keyClick(view, i % 2 ? Qt::Key_Left : Qt::Key_Right);
        QTest::qWait(350);
        QVERIFY(view->isVisible());
    }
    void settingsReturn() {
        auto *root = view->rootObject();
        int selected = root->property("selected").toInt();
        for (int i = 0; i < 6; i++) {
            QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
            QVERIFY(root->property("settingsOpen").toBool());
            QTest::qWait(80);
            QTest::keyClick(view, Qt::Key_Escape);
            QTRY_VERIFY(!root->property("settingsOpen").toBool());
            QVERIFY(view->isVisible());
            QTest::keyClick(view, Qt::Key_Right);
            selected = (selected + 1) % backend->items().size();
            QCOMPARE(root->property("selected").toInt(), selected);
        }
    }
    void settingsButtons() {
        auto *root = view->rootObject();
        for (auto name : {"cancelSettings", "saveSettings"}) {
            QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
            QTest::qWait(80);
            auto *button = root->findChild<QQuickItem *>(name);
            QVERIFY(button);
            QTest::mouseClick(
                view, Qt::LeftButton, Qt::NoModifier,
                button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
            QTRY_VERIFY(!root->property("settingsOpen").toBool());
            QVERIFY(view->isVisible());
            auto index = root->property("selected").toInt();
            QTest::keyClick(view, Qt::Key_Right);
            QCOMPARE(root->property("selected").toInt(), (index + 1) % backend->items().size());
        }
    }
    void settingsDraftAndScreenshots() {
        auto *root = view->rootObject();
        auto *panel = root->findChild<QQuickItem *>("settingsPanel");
        QVERIFY(panel);
        auto click = [this, root](const char *name) {
            QQuickItem *item = nullptr;
            std::function<void(QQuickItem *)> find = [&](QQuickItem *node) {
                if (node->objectName() == name)
                    item = node;
                for (auto *child : node->childItems())
                    find(child);
            };
            find(root);
            if (!item)
                return false;
            QTest::mouseClick(
                view, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        for (const auto &language : {"en", "uk", "ru"}) {
            auto config = backend->settings();
            config["language"] = language;
            QVERIFY(backend->saveSettings(config));
            QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
            QTest::qWait(280);
            QCOMPARE(root->findChild<QQuickItem *>("settingsTitle")->property("text").toString(),
                     backend->text("settings"));
            for (int tab = 0; tab < 3; ++tab) {
                QVERIFY(click(qPrintable("settingsTab" + QString::number(tab))));
                QCOMPARE(panel->property("tab").toInt(), tab);
                QTest::qWait(260);
                if (!qEnvironmentVariable("HWS_TEST_SETTINGS_SCREENSHOT").isEmpty())
                    QVERIFY(view->grabWindow().save(
                        qEnvironmentVariable("HWS_TEST_SETTINGS_SCREENSHOT") + "-" + language +
                        "-" + QString::number(tab) + ".png"));
            }
            QVERIFY(click("dmsColorsSetting"));
            QCOMPARE(panel->property("draft").value<QJSValue>().property("syncDmsColors").toBool(),
                     true);
            QVERIFY(click("cancelSettings"));
            QTRY_VERIFY(!root->property("settingsOpen").toBool());
            QVERIFY(!backend->settings()["syncDmsColors"].toBool());
        }
        auto config = backend->settings();
        config["language"] = "en";
        QVERIFY(backend->saveSettings(config));
        QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
        QTest::qWait(280);
        QVERIFY(click("dmsColorsSetting"));
        QVERIFY(click("saveSettings"));
        QTRY_VERIFY(!root->property("settingsOpen").toBool());
        QVERIFY(backend->settings()["syncDmsColors"].toBool());
        QVERIFY(view->isVisible());
    }
    void videoPreview() {
        auto *root = view->rootObject();
        root->setProperty("selected", 0);
        root->forceActiveFocus();
        for (int i = 0; i < 3; i++) {
            QTest::keyClick(view, Qt::Key_P);
            QVERIFY(root->property("previewing").toBool());
            QTest::qWait(250);
            QList<MediaItem *> media;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *i) {
                if (auto *m = qobject_cast<MediaItem *>(i))
                    media << m;
                for (auto *child : i->childItems())
                    walk(child);
            };
            walk(view->rootObject());

            if (media.isEmpty())
                view->rootObject()->dumpItemTree();
            QVERIFY(!media.isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(media.first()->ready(), 10000);
            auto first = view->grabWindow();
            QTest::qWait(160);
            QVERIFY(first != view->grabWindow());
            media.first()->setPaused(true);
            QTest::qWait(150);
            double before = 0, after = 0;
            QCOMPARE(
                mpv_get_property(media.first()->handle(), "time-pos", MPV_FORMAT_DOUBLE, &before),
                0);
            QTest::qWait(200);
            QCOMPARE(
                mpv_get_property(media.first()->handle(), "time-pos", MPV_FORMAT_DOUBLE, &after),
                0);
            QVERIFY(qAbs(after - before) < 0.01);
            if (i == 0 && !qEnvironmentVariable("HWS_TEST_SCREENSHOT").isEmpty())
                QVERIFY(view->grabWindow().save(qEnvironmentVariable("HWS_TEST_SCREENSHOT")));
            media.first()->setPaused(false);
            QTest::keyClick(view, Qt::Key_P);
            QVERIFY(!root->property("previewing").toBool());
            QTest::qWait(150);
        }
    }
    void wallpaperTransitions() {
        backend->hide();
        QQuickView wall;
        wall.resize(960, 540);
        wall.setResizeMode(QQuickView::SizeRootObjectToView);
        wall.rootContext()->setContextProperty("backend", backend);
        wall.rootContext()->setContextProperty("outputName", "TEST");
        wall.setSource(QUrl("qrc:/qml/Wallpaper.qml"));
        QCOMPARE(wall.status(), QQuickView::Ready);
        wall.show();
        QString video, still;
        for (auto v : backend->items()) {
            auto m = v.toMap();
            if (m["video"].toBool())
                video = m["path"].toString();
            else
                still = m["path"].toString();
        }
        QVERIFY(!video.isEmpty());
        QVERIFY(!still.isEmpty());
        for (auto path : {still, video, still, video}) {
            bool isVideo = path == video;
            emit backend->wallpaperRequested("TEST", path, isVideo);
            QTRY_VERIFY_WITH_TIMEOUT(
                wall.rootObject()->property("activeDeck").value<QQuickItem *>() != nullptr, 12000);
            QTRY_COMPARE_WITH_TIMEOUT(wall.rootObject()
                                          ->property("activeDeck")
                                          .value<QQuickItem *>()
                                          ->property("path")
                                          .toString(),
                                      path, 12000);
            QTest::qWait(200);
        }
        emit backend->wallpaperRequested("TEST", fixture.path() + "/missing.png", false);
        QTRY_VERIFY(wall.rootObject()->property("pendingDeck").value<QQuickItem *>() == nullptr);
        QVERIFY(wall.rootObject()->property("activeDeck").value<QQuickItem *>() != nullptr);
        emit backend->wallpaperRequested("TEST", still, false);
        emit backend->wallpaperRequested("TEST", video, true);
        emit backend->wallpaperRequested("TEST", still, false);
        QTRY_COMPARE_WITH_TIMEOUT(wall.rootObject()
                                      ->property("activeDeck")
                                      .value<QQuickItem *>()
                                      ->property("path")
                                      .toString(),
                                  still, 8000);
        wall.hide();
        QTest::qWait(100);
        backend->toggle();
    }
    void hideShow() {
        backend->hide();
        QVERIFY(!view->isVisible());
        backend->toggle();
        QVERIFY(view->isVisible());
        QTest::qWait(100);
    }
    void cleanupTestCase() {
        delete view;
        delete backend;
    }
};
int main(int argc, char **argv) {
    if (qEnvironmentVariable("HWS_TEST_DISPLAY") != "isolated") {
        qCritical("Run tests/run-ui.sh: UI tests require a separate display.");
        return 2;
    }
    Q_INIT_RESOURCE(ui);
    Q_INIT_RESOURCE(shaders);
    QCoreApplication::setApplicationName("hyprwallselector");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    app.setQuitOnLastWindowClosed(false);
    qmlRegisterType<MediaItem>("Hyprwallselector", 1, 0, "VideoSurface");
    TestUi test;
    return QTest::qExec(&test, argc, argv);
}
#include "test-ui.moc"
