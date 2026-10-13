// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "integration.h"
#include "mediaitem.h"
#include <LayerShellQt/Window>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickView>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <clocale>

int main(int argc, char **argv) {
    Q_INIT_RESOURCE(ui);
    Q_INIT_RESOURCE(shaders);
    QCoreApplication::setApplicationName("hyprwallselector");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCoreApplication::setOrganizationName("Hyprwallselector");
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    std::setlocale(LC_NUMERIC, "C");
    app.setQuitOnLastWindowClosed(false);
    app.setDesktopFileName("hyprwallselector");
    QCommandLineParser args;
    args.setApplicationDescription("A standalone wallpaper carousel for Hyprland");
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
    args.process(app);
    if (args.isSet("integrate-dms") || args.isSet("restore-dms")) {
        QString error = configureDms(args.isSet("restore-dms"));
        QTextStream(stdout) << (error.isEmpty() ? "DMS wallpaper preference updated." : error)
                            << "\n";
        return error.isEmpty() ? 0 : 1;
    }
    bool preview = args.isSet("preview");
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) +
                      "/hyprwallselector-native";
    QDir().mkpath(runtime);
    QFile::setPermissions(runtime, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QString socket = runtime + "/control";
    QString command = args.isSet("quit")       ? "quit"
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
    qmlRegisterType<MediaItem>("Hyprwallselector", 1, 0, "VideoSurface");
    Controller backend(preview);
    QQuickView view;
    backend.attach(&view);
    view.setColor(Qt::transparent);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setTitle("Hyprwallselector");
    view.setPersistentSceneGraph(false);
    view.rootContext()->setContextProperty("backend", &backend);
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
    view.resize(app.primaryScreen()->size());
    view.setSource(QUrl("qrc:/qml/Main.qml"));
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
                    if (msg == "toggle")
                        backend.toggle();
                    else if (msg == "quit")
                        QTimer::singleShot(20, &app, &QGuiApplication::quit);
                    client->write(msg == "diagnose" ? backend.diagnostic().toUtf8()
                                                    : QByteArray("OK\n"));
                    client->flush();
                    client->disconnectFromServer();
                });
                QTimer::singleShot(1500, client, [client] { client->disconnectFromServer(); });
            }
        });
    }
    backend.start(!args.isSet("daemon"));
    if (args.isSet("exit-after"))
        QTimer::singleShot(args.value("exit-after").toInt(), &app, &QGuiApplication::quit);
    return app.exec();
}
