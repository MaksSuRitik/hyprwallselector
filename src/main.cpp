// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "integration.h"
#include "mediaitem.h"
#include "memorypolicy.h"
#ifdef Q_OS_WIN
#include <QApplication>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QCryptographicHash>
#include <QMessageBox>
#include <QProgressDialog>
#include "updatemanager.h"
#include "nativevideo.h"
#else
#include <LayerShellQt/Window>
#endif
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickView>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QThread>
#include <QElapsedTimer>
#include <clocale>

int main(int argc, char **argv) {
    configureWallpaperMemory();
    Q_INIT_RESOURCE(ui);
    Q_INIT_RESOURCE(shaders);
    QCoreApplication::setApplicationName("hyprwallselector");
    QCoreApplication::setApplicationVersion(HWS_APP_VERSION);
    QCoreApplication::setOrganizationName("Hyprwallselector");
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#else
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    QQuickStyle::setStyle("Basic");
#ifdef Q_OS_WIN
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif
    std::setlocale(LC_NUMERIC, "C");
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(QIcon(":/packaging/hyprwallselector.svg"));
    app.setDesktopFileName("hyprwallselector");
    QCommandLineParser args;
    args.setApplicationDescription("A standalone wallpaper carousel for Windows and Hyprland");
    args.addHelpOption();
    args.addVersionOption();
    args.addOption(
        {"integrate-dms",
         "Enable supported DMS external-wallpaper mode, saving the original preference"});
    args.addOption({"restore-dms", "Restore the saved DMS wallpaper preference"});
    args.addOptions({{"daemon", "Start hidden and restore wallpapers"},
                     {"toggle", "Show/hide the selector"},
                     {"quit", "Quit the running instance"},
                     {"diagnose", "Print running instance status"},
                     {"preview", "Isolated UI preview: no hotkeys or wallpaper changes"},
                     {"exit-after", "Exit after N milliseconds (testing)", "ms"}});
    args.addOption({"wallpaper", "Apply a local image or video", "path"});
    args.addOption({"folder", "Use a wallpaper library directory", "path"});
    args.addOption({"pause", "Pause video wallpapers"});
    args.addOption({"resume", "Resume video wallpapers"});
    args.addOption({"check-updates", "Check published Windows releases"});
    args.process(app);
    if (args.isSet("integrate-dms") || args.isSet("restore-dms")) {
        QString error = configureDms(args.isSet("restore-dms"));
        QTextStream(stdout) << (error.isEmpty() ? "DMS wallpaper preference updated." : error)
                            << "\n";
        return error.isEmpty() ? 0 : 1;
    }
    bool preview = args.isSet("preview");
    if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo("Startup: parsed arguments");
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
                      "/hyprwallselector-native";
#ifdef Q_OS_WIN
    runtime = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/runtime";
#endif
    QDir().mkpath(runtime);
    QFile::setPermissions(runtime, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QString socket = runtime + "/control";
#ifdef Q_OS_WIN
    socket = "hyprwallselector-" + QCryptographicHash::hash(runtime.toUtf8(), QCryptographicHash::Sha256).toHex().left(24);
#endif
    QString command = args.isSet("wallpaper") ? "wallpaper:" + QFileInfo(args.value("wallpaper")).absoluteFilePath()
                      : args.isSet("check-updates") ? "check-updates"
                      : args.isSet("pause") ? "pause"
                      : args.isSet("resume") ? "resume"
                      : args.isSet("quit")       ? "quit"
                      : args.isSet("diagnose") ? "diagnose"
                      : args.isSet("daemon")   ? "ping"
                                               : "toggle";
    if (!preview) {
        QLocalSocket client;
        client.connectToServer(socket);
        if (client.waitForConnected(250)) {
            client.write(command.toUtf8() + "\n");
            client.waitForBytesWritten(500);
            if (client.waitForReadyRead(1000))
                QTextStream(stdout) << client.readAll();
            if (command == "quit") {
                QLockFile exiting(runtime + "/instance.lock");
                exiting.setStaleLockTime(0);
                QElapsedTimer elapsed; elapsed.start();
                while (!exiting.tryLock()) {
                    if (elapsed.elapsed() >= 10000) return 1;
                    QThread::msleep(50);
                }
            }
            return 0;
        }
        if (args.isSet("quit") || args.isSet("diagnose")) {
            QTextStream(stderr) << "No running Hyprwallselector instance.\n";
            return 1;
        }
    }
    QLockFile lock(runtime + "/instance.lock");
    lock.setStaleLockTime(0);
    if (!preview && !lock.tryLock()) {
        QTextStream(stderr) << "Another instance is starting.\n";
        return 1;
    }
#ifdef Q_OS_WIN
    qmlRegisterType<MediaItem>("Hyprwallselector", 1, 0, "VideoSession");
    qmlRegisterType<NativeVideo>("Hyprwallselector", 1, 0, "NativeVideo");
    qmlRegisterType(QUrl("qrc:/qml/WindowsVideo.qml"), "Hyprwallselector", 1, 0, "VideoSurface");
#else
    qmlRegisterType<MediaItem>("Hyprwallselector", 1, 0, "VideoSurface");
#endif
    Controller backend(preview);
#ifdef Q_OS_WIN
    UpdateManager updates;
#endif
    if (args.isSet("folder")) {
        auto settings = backend.settings();
        settings["folder"] = args.value("folder");
        if (!backend.saveSettings(settings)) return 2;
    }
    if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo("Startup: controller ready");
    QQuickView view;
    backend.attach(&view);
    view.setColor(Qt::transparent);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setTitle("Hyprwallselector");
    view.setPersistentSceneGraph(false);
    view.rootContext()->setContextProperty("backend", &backend);
#ifdef Q_OS_WIN
    view.setFlags(preview ? Qt::Window : (Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint));
    view.setGeometry(preview ? QRect(80, 80, 1100, 760) : app.primaryScreen()->geometry());
#else
    auto *layer = LayerShellQt::Window::get(&view);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setScope("hyprwallselector");
    layer->setExclusiveZone(-1);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
                      LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft |
                      LayerShellQt::Window::AnchorRight);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
    layer->setWantsToBeOnActiveScreen(true);
    layer->setCloseOnDismissed(false);
#endif
#ifdef Q_OS_WIN
    if (!preview) view.resize(app.primaryScreen()->size());
#else
    view.resize(app.primaryScreen()->size());
#endif
    view.setSource(QUrl("qrc:/qml/Main.qml"));
    if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo("Startup: QML ready");
    if (view.status() == QQuickView::Error)
        return 1;
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!preview) {
        QLocalServer::removeServer(socket);
        if (!server.listen(socket))
            return 1;
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (auto *client = server.nextPendingConnection()) {
                QObject::connect(client, &QLocalSocket::disconnected, client,
                                 &QObject::deleteLater);
                QObject::connect(client, &QLocalSocket::readyRead, &app, [&, client] {
                    if (client->bytesAvailable() > 4096) {
                        client->disconnectFromServer();
                        return;
                    }
                    if (!client->canReadLine())
                        return;
                    QByteArray msg = client->readLine().trimmed();
                    if (msg.startsWith("wallpaper:"))
                        backend.applyFile(QString::fromUtf8(msg.mid(10)));
                    else if (msg == "pause" || msg == "resume")
                        backend.setManualPause(msg == "pause");
                    else if (msg == "toggle")
                        backend.toggle();
                    else if (msg == "quit")
                        QTimer::singleShot(20, &app, &QGuiApplication::quit);
#ifdef Q_OS_WIN
                    if (msg == "check-updates") updates.check(false);
#endif
                    auto diagnostic = QJsonDocument::fromJson(backend.diagnostic().toUtf8()).object();
#ifdef Q_OS_WIN
                    diagnostic["updateState"] = updates.state();
                    diagnostic["version"] = QCoreApplication::applicationVersion();
#endif
                    client->write(msg == "diagnose" ? QJsonDocument(diagnostic).toJson() : QByteArray("OK\n"));
                    client->flush();
                    client->disconnectFromServer();
                });
                QTimer::singleShot(1500, client, [client] { client->disconnectFromServer(); });
            }
        });
    }
#ifdef Q_OS_WIN
    QSystemTrayIcon tray(QIcon(":/packaging/hyprwallselector.svg"));
    tray.setToolTip("Hyprwallselector");
    QMenu trayMenu;
    auto *open = trayMenu.addAction("Hyprwallselector");
    QObject::connect(open, &QAction::triggered, &backend, &Controller::toggle);
    auto *pause = trayMenu.addAction("Pause video");
    pause->setCheckable(true);
    QObject::connect(pause, &QAction::toggled, &backend, &Controller::setManualPause);
    trayMenu.addSeparator();
    auto *updateAction = trayMenu.addAction(backend.text("checkUpdates"));
    auto *autoUpdates = trayMenu.addAction(backend.text("automaticUpdates"));
    autoUpdates->setCheckable(true);
    autoUpdates->setChecked(updates.automatic());
    QObject::connect(autoUpdates, &QAction::toggled, &updates, &UpdateManager::setAutomatic);
    auto manualCheck = [&] {
        if (updates.busy()) return;
        backend.hide();
        updateAction->setEnabled(false);
        updates.check();
    };
    QObject::connect(updateAction, &QAction::triggered, &updates, manualCheck);
    QObject::connect(&backend, &Controller::updateRequested, &updates, manualCheck);
    auto inform = [&](const QString &message) {
        auto *box = new QMessageBox(QMessageBox::Information, "Hyprwallselector", message, QMessageBox::Ok);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    };
    QPointer<QProgressDialog> downloadProgress;
    QObject::connect(&updates, &UpdateManager::checked, &app, [&](bool manual, const QString &error) {
        updateAction->setEnabled(true);
        if (!manual) return;
        if (!error.isEmpty()) { inform(backend.text("updateFailed") + "\n" + error); return; }
        if (!updates.release().valid()) { inform(backend.text("upToDate") + " (" + app.applicationVersion() + ")"); return; }
        auto *prompt = new QMessageBox(QMessageBox::Information, "Hyprwallselector",
            backend.text("installUpdate").arg(updates.release().version), QMessageBox::Yes | QMessageBox::No);
        prompt->setDefaultButton(QMessageBox::No);
        prompt->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(prompt, &QMessageBox::finished, &app, [&](int result) {
            if (result != QMessageBox::Yes || updates.busy()) return;
            downloadProgress = new QProgressDialog(backend.text("downloadingUpdate"), backend.text("cancel"), 0, 100);
            downloadProgress->setWindowTitle("Hyprwallselector");
            downloadProgress->setMinimumDuration(0);
            downloadProgress->setAutoClose(false);
            QObject::connect(downloadProgress, &QProgressDialog::canceled, &updates, &UpdateManager::cancel);
            updates.download();
        });
        prompt->open();
    });
    QObject::connect(&updates, &UpdateManager::progress, &app, [&](qint64 received, qint64 total) {
        if (downloadProgress) downloadProgress->setValue(total > 0 ? int(100 * received / total) : 0);
    });
    QObject::connect(&updates, &UpdateManager::downloaded, &app, [&](const QString &path, const QString &error) {
        if (downloadProgress) { downloadProgress->hide(); downloadProgress->deleteLater(); }
        if (!error.isEmpty()) { inform(backend.text("updateFailed") + "\n" + error); return; }
        if (path.isEmpty()) return; // cancelled
        if (updates.launchInstaller(path)) app.quit();
        else inform(backend.text("updateFailed"));
    });
    QObject::connect(&updates, &UpdateManager::available, &tray, [&](const QString &version) {
        tray.showMessage("Hyprwallselector", backend.text("updateAvailable").arg(version));
    });
    QObject::connect(&tray, &QSystemTrayIcon::messageClicked, &updates, manualCheck);
    if (!preview) updates.start();
    trayMenu.addSeparator();
    auto *exit = trayMenu.addAction(backend.text("quit"));
    QObject::connect(exit, &QAction::triggered, &backend, &Controller::quit);
    tray.setContextMenu(&trayMenu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &backend, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) backend.toggle();
    });
    if (!preview) tray.show();
#endif
    backend.start(!args.isSet("daemon") && !args.isSet("wallpaper"));
    if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo("Startup: controller started");
    if (args.isSet("wallpaper")) QTimer::singleShot(0, &backend, [&] { backend.applyFile(args.value("wallpaper")); });
    if (args.isSet("exit-after"))
        QTimer::singleShot(args.value("exit-after").toInt(), &app, &QGuiApplication::quit);
    return app.exec();
}
