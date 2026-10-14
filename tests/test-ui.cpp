// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "mediaitem.h"
#include "memorypolicy.h"
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
#include <malloc.h>
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
        view->setPersistentSceneGraph(false);
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
    void embeddedFolderPicker() {
        auto *root = view->rootObject();
        auto *panel = root->findChild<QQuickItem *>("settingsPanel");
        auto *picker = panel->findChild<QObject *>("folderPicker");
        QVERIFY(picker);
        const QString original = backend->settings()["folder"].toString();
        const QString child = original + "/Папка #1 with spaces";
        QVERIFY(QDir().mkpath(child));
        QVERIFY(QDir().mkpath(original + "/.hidden"));
        const int windows = qGuiApp->allWindows().size();
        auto click = [this](const char *name) {
            QQuickItem *item = nullptr;
            std::function<void(QQuickItem *)> find = [&](QQuickItem *node) {
                if (node->objectName() == name)
                    item = node;
                for (auto *c : node->childItems())
                    find(c);
            };
            find(view->contentItem());
            if (!item)
                return false;
            QTest::mouseClick(
                view, Qt::LeftButton, Qt::NoModifier,
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        panel->setProperty("tab", 0);
        QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
        QTest::qWait(280);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(click("browseFolder"));
            QTRY_VERIFY(picker->property("opened").toBool());
            QCOMPARE(qGuiApp->allWindows().size(), windows);    // No portal or extra native window.
            QCOMPARE(picker->property("popupType").toInt(), 0); // Popup.Item
            QTest::keyClick(view, Qt::Key_Escape);
            QTRY_VERIFY(!picker->property("visible").toBool());
            QVERIFY(root->property("settingsOpen").toBool());
        }
        QVERIFY(click("browseFolder"));
        QTRY_VERIFY(picker->property("opened").toBool());
        auto *model = picker->findChild<QObject *>("folderPickerModel");
        QVERIFY(model);
        QTRY_COMPARE(model->property("count").toInt(), 1); // Hide files and hidden directories.
        QVERIFY(click("cancelSettings")); // Modal popup must block the settings behind it.
        QVERIFY(root->property("settingsOpen").toBool());
        auto *address = picker->findChild<QQuickItem *>("folderAddress");
        QVERIFY(address);
        address->setProperty("text", original + "/missing");
        QVERIFY(click("acceptFolder"));
        QVERIFY(picker->property("visible").toBool());
        QVERIFY(!picker->property("error").toString().isEmpty());
        address->setProperty("text", child);
        address->forceActiveFocus();
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(picker->property("currentPath").toString(), child);
        QVERIFY(click("folderUp"));
        QCOMPARE(picker->property("currentPath").toString(), original);
        auto *list = picker->findChild<QQuickItem *>("folderList");
        QVERIFY(list);
        QTRY_COMPARE(model->property("count").toInt(), 1);
        list->setProperty("currentIndex", 0);
        list->forceActiveFocus();
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(picker->property("currentPath").toString(), child);
        QTRY_COMPARE(model->property("count").toInt(), 0);
        if (!qEnvironmentVariable("HWS_TEST_FOLDER_SCREENSHOT").isEmpty())
            QVERIFY(view->grabWindow().save(qEnvironmentVariable("HWS_TEST_FOLDER_SCREENSHOT")));
        QVERIFY(click("acceptFolder"));
        QTRY_VERIFY(!picker->property("visible").toBool());
        QCOMPARE(root->findChild<QQuickItem *>("wallpaperFolder")->property("text").toString(),
                 child);
        QCOMPARE(backend->settings()["folder"].toString(), original);
        QVERIFY(click("cancelSettings"));
        QTRY_VERIFY(!root->property("settingsOpen").toBool());
        QCOMPARE(backend->settings()["folder"].toString(), original);
        QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
        QTest::qWait(280);
        QVERIFY(click("browseFolder"));
        QTRY_VERIFY(picker->property("opened").toBool());
        address->setProperty("text", child);
        QVERIFY(click("acceptFolder"));
        QTRY_VERIFY(!picker->property("visible").toBool());
        QVERIFY(click("saveSettings"));
        QTRY_VERIFY(!root->property("settingsOpen").toBool());
        QCOMPARE(backend->settings()["folder"].toString(), child);
        QTRY_VERIFY(!backend->busy());
        auto config = backend->settings();
        config["folder"] = original;
        QVERIFY(backend->saveSettings(config));
        QTRY_VERIFY(!backend->busy());
        QVERIFY(QMetaObject::invokeMethod(root, "openSettings"));
        QTest::qWait(280);
        QVERIFY(click("browseFolder"));
        QTRY_VERIFY(picker->property("opened").toBool());
        view->resize(960, 540);
        QTest::qWait(200);
        auto *content = picker->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        const QRectF bounds = content->mapRectToScene(content->boundingRect());
        QVERIFY(bounds.left() >= 0 && bounds.top() >= 0);
        QVERIFY(bounds.right() <= view->width() && bounds.bottom() <= view->height());
        backend->hide();
        QTRY_VERIFY(!picker->property("visible").toBool());
        view->resize(1280, 800);
        backend->toggle();
        QTest::qWait(180);
        QVERIFY(!root->property("settingsOpen").toBool());
        QCOMPARE(qGuiApp->allWindows().size(), windows);
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
            QTRY_COMPARE(MediaItem::livePlayers(), 0);
            QTRY_COMPARE(MediaItem::liveRenderers(), 0);
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
            QTRY_VERIFY(wall.rootObject()->property("pendingDeck").value<QQuickItem *>() ==
                        nullptr);
            QTRY_COMPARE(MediaItem::livePlayers(), isVideo ? 1 : 0);
            QTRY_COMPARE(MediaItem::liveRenderers(), isVideo ? 1 : 0);
            auto *active = wall.rootObject()->property("activeDeck").value<QQuickItem *>();
            QSignalSpy ready(backend, &Controller::currentChanged);
            emit backend->wallpaperRequested("TEST", path, isVideo);
            QCOMPARE(wall.rootObject()->property("activeDeck").value<QQuickItem *>(), active);
            QVERIFY(wall.rootObject()->property("pendingDeck").value<QQuickItem *>() == nullptr);
            QCOMPARE(ready.count(), 1);
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
    void wallpaperMemory() {
        if (!qEnvironmentVariableIsSet("HWS_MEMORY_TEST"))
            QSKIP("Opt-in memory stress test");
        backend->hide();
        QStringList paths;
        for (int i = 0; i < 8; ++i) {
            QImage image(3840, 2160, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 43, 130, 160));
            QPainter paint(&image);
            paint.setPen(Qt::white);
            paint.drawText(image.rect(), Qt::AlignCenter, "4K wallpaper " + QString::number(i));
            paint.end();
            auto path = fixture.path() + "/wall-" + QString::number(i) + ".jpg";
            QVERIFY(image.save(path, "JPG", 90));
            paths << path;
        }
        QStringList videos{fixture.path() + "/library/video.mp4"};
        const QString mediaDir = qEnvironmentVariable("HWS_MEMORY_MEDIA_DIR");
        if (!mediaDir.isEmpty()) {
            videos.clear();
            for (const auto &file : QDir(mediaDir).entryInfoList({"*.mp4"}, QDir::Files))
                videos << file.absoluteFilePath();
            QVERIFY(!videos.isEmpty());
        }
        QQuickView wall;
        wall.resize(1920, 1080);
        wall.setResizeMode(QQuickView::SizeRootObjectToView);
        wall.rootContext()->setContextProperty("backend", backend);
        wall.rootContext()->setContextProperty("outputName", "MEMORY");
        wall.setSource(QUrl("qrc:/qml/Wallpaper.qml"));
        QCOMPARE(wall.status(), QQuickView::Ready);
        wall.show();
        QVERIFY(QTest::qWaitForWindowExposed(&wall));
        qint64 privatePeak = 0;
        auto sample = [&](int cycle) {
            QFile smaps("/proc/self/smaps_rollup");
            if (!smaps.open(QIODevice::ReadOnly))
                return;
            auto lines = smaps.readAll().split('\n');
            QByteArray rss, pss, priv;
            for (auto line : lines) {
                if (line.startsWith("Rss:"))
                    rss = line.simplified();
                if (line.startsWith("Pss:"))
                    pss = line.simplified();
                if (line.startsWith("Private_Dirty:"))
                    priv = line.simplified();
            }
            qInfo().noquote() << "MEMORY" << cycle << rss << pss << priv << "players"
                              << MediaItem::livePlayers() << "renderers"
                              << MediaItem::liveRenderers() << "decks"
                              << wall.rootObject()->childItems().size();
            privatePeak = qMax(privatePeak, priv.split(' ').value(1).toLongLong());
            const auto heap = mallinfo2();
            qInfo() << "HEAP" << cycle << "used" << heap.uordblks / 1024 << "free"
                    << heap.fordblks / 1024 << "mapped" << heap.hblkhd / 1024;
        };
        sample(0);
        const int cycles = qEnvironmentVariableIntValue("HWS_MEMORY_CYCLES", nullptr) > 0
                               ? qEnvironmentVariableIntValue("HWS_MEMORY_CYCLES")
                               : 48;
        const int dwell = qEnvironmentVariableIntValue("HWS_MEMORY_DWELL_MS", nullptr) > 0
                              ? qEnvironmentVariableIntValue("HWS_MEMORY_DWELL_MS")
                              : 100;
        for (int i = 0; i < cycles; ++i) {
            // Revisit the same finite set, mixing images and video; memory should settle.
            bool isVideo = i % 3 != 0;
            QString path =
                isVideo ? videos[(i / 3 * 2 + i % 3) % videos.size()] : paths[i % paths.size()];
            emit backend->wallpaperRequested("MEMORY", path, isVideo);
            QTRY_VERIFY_WITH_TIMEOUT(
                wall.rootObject()->property("activeDeck").value<QQuickItem *>() != nullptr, 12000);
            QTRY_COMPARE_WITH_TIMEOUT(wall.rootObject()
                                          ->property("activeDeck")
                                          .value<QQuickItem *>()
                                          ->property("path")
                                          .toString(),
                                      path, 12000);
            QTRY_VERIFY_WITH_TIMEOUT(
                wall.rootObject()->property("pendingDeck").value<QQuickItem *>() == nullptr, 12000);
            QTest::qWait(dwell);
            QTRY_COMPARE(MediaItem::livePlayers(), isVideo ? 1 : 0);
            QTRY_COMPARE(MediaItem::liveRenderers(), isVideo ? 1 : 0);
            QCOMPARE(wall.rootObject()->childItems().size(), 1);
            sample(i + 1);
            if (qEnvironmentVariableIsSet("HWS_MEMORY_SHOW_SELECTOR")) {
                backend->toggle();
                QTest::qWait(250);
                backend->hide();
                QTest::qWait(100);
            }
        }
        const int soak = qEnvironmentVariableIntValue("HWS_MEMORY_SOAK_SECONDS");
        for (int second = 0; second < soak; ++second) {
            QTest::qWait(1000);
            if ((second + 1) % 15 == 0)
                sample(cycles + second + 1);
        }
        if (auto *active = wall.rootObject()->property("activeDeck").value<QQuickItem *>()) {
            std::function<void(QQuickItem *)> describeVideo = [&](QQuickItem *item) {
                if (auto *video = qobject_cast<MediaItem *>(item)) {
                    for (const auto *property : {"hwdec-current", "hwdec-interop", "time-pos",
                                                 "frame-drop-count", "decoder-frame-drop-count"}) {
                        char *value = mpv_get_property_string(video->handle(), property);
                        qInfo() << "PLAYBACK" << property << (value ? value : "unavailable");
                        mpv_free(value);
                    }
                }
                for (auto *child : item->childItems())
                    describeVideo(child);
            };
            describeVideo(active);
        }
        const auto limit = qEnvironmentVariableIntValue("HWS_MEMORY_MAX_PRIVATE_KB");
        if (limit > 0)
            QVERIFY2(privatePeak <= limit,
                     qPrintable(QString("Settled private memory peaked at %1 KiB (limit %2)")
                                    .arg(privatePeak)
                                    .arg(limit)));
        wall.hide();
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
    configureWallpaperMemory();
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
