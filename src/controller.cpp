// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.h"
#include "mediaitem.h"
#include <LayerShellQt/Window>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QKeySequence>
#include <QProcess>
#include <QQmlContext>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QtDBus>
#include <systemd/sd-login.h>
#include <unistd.h>

namespace {
QString configHome() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}
QString stateHome() {
    QString s = qEnvironmentVariable("XDG_STATE_HOME");
    return s.isEmpty() ? QDir::homePath() + "/.local/state" : s;
}
QJsonObject readJson(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
bool writeJson(const QString &path, const QJsonObject &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(data).toJson()) >= 0 && f.commit();
}
bool videoFile(const QString &path) {
    return QStringList{"mp4", "mkv",  "webm", "mov", "avi", "m4v",
                       "mpg", "mpeg", "wmv",  "flv", "ogv"}
        .contains(QFileInfo(path).suffix().toLower());
}
void asyncRun(QObject *owner, const QString &program, const QStringList &args,
              std::function<void(int, QByteArray)> done = {}) {
    auto *p = new QProcess(owner);
    auto *timeout = new QTimer(p);
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, p, &QProcess::kill);
    QObject::connect(p, &QProcess::finished, owner, [p, done](int code, QProcess::ExitStatus) {
        if (done)
            done(code, p->readAllStandardOutput());
        p->deleteLater();
    });
    QObject::connect(p, &QProcess::errorOccurred, owner, [p, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            if (done)
                done(-1, {});
            p->deleteLater();
        }
    });
    p->start(program, args);
    timeout->start(15000);
}
QString localPath(QString path) {
    if (path.startsWith("file:"))
        return QUrl(path).toLocalFile();
    if (path.startsWith("~/"))
        path.replace(0, 1, QDir::homePath());
    return QDir::cleanPath(path);
}
QVariantList catalogue(const QString &folder) {
    QVariantList entries;
    QDir dir(folder);
    auto files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    const auto formats = QImageReader::supportedImageFormats();
    QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                    "/hyprwallselector/thumbs";
    QDir().mkpath(cache);
    for (const auto &f : files) {
        const bool video = videoFile(f.filePath());
        if (!video && !formats.contains(f.suffix().toLower().toUtf8()))
            continue;
        QByteArray key = (f.absoluteFilePath() + QString::number(f.size()) +
                          QString::number(f.lastModified().toMSecsSinceEpoch()))
                             .toUtf8();
        QString thumb = cache + "/" +
                        QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex() + ".jpg";
        if (!QFileInfo::exists(thumb)) {
            QString temporary =
                thumb + QString(".%1.part.jpg").arg(QCoreApplication::applicationPid());
            if (video) {
                QProcess ff;
                ff.start("ffmpeg", {"-nostdin", "-v", "error", "-y", "-threads", "2", "-ss", "0.1",
                                    "-i", f.absoluteFilePath(), "-frames:v", "1", "-vf",
                                    "scale=720:480:force_original_aspect_ratio=decrease",
                                    "-threads", "2", temporary});
                if (!ff.waitForFinished(15000)) {
                    ff.kill();
                    ff.waitForFinished(1000);
                }
                if (ff.exitStatus() == QProcess::NormalExit && ff.exitCode() == 0)
                    QFile::rename(temporary, thumb);
            } else {
                QImageReader reader(f.absoluteFilePath());
                reader.setAutoTransform(true);
                QSize size = reader.size();
                if (size.isValid())
                    reader.setScaledSize(size.scaled(720, 480, Qt::KeepAspectRatio));
                QImage image = reader.read();
                if (!image.isNull() && image.save(temporary, "JPG", 88))
                    QFile::rename(temporary, thumb);
            }
            QFile::remove(temporary);
        }
        entries.append(QVariantMap{{"path", f.absoluteFilePath()},
                                   {"url", QUrl::fromLocalFile(f.absoluteFilePath()).toString()},
                                   {"thumb", QUrl::fromLocalFile(thumb).toString()},
                                   {"title", f.completeBaseName()},
                                   {"format", f.suffix().toUpper()},
                                   {"video", video}});
    }
    std::stable_sort(entries.begin(), entries.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["video"].toBool() > b.toMap()["video"].toBool();
    });
    return entries;
}
} // namespace
Controller::Controller(bool sandbox, QObject *parent) : QObject(parent), m_sandbox(sandbox) {
    load();
    m_palette.setEnabled(!m_sandbox && m_config["syncDmsColors"].toBool() &&
                         m_config["manageBackground"].toBool() &&
                         m_config["renderer"].toString() == "native");
    connect(&m_palette, &DmsPalette::completed, this, [this](bool success) {
        if (!success)
            setStatus(text("paletteError"));
        else if (m_status == text("paletteError"))
            setStatus("");
    });
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(70);
    connect(&m_refresh, &QTimer::timeout, this, &Controller::refreshEnvironment);
    m_reconnect.setInterval(3000);
    connect(&m_reconnect, &QTimer::timeout, this, &Controller::watchHyprland);
    connect(&m_events, &QLocalSocket::connected, this, [this] {
        m_reconnect.stop();
        m_refresh.start();
    });
    connect(&m_events, &QLocalSocket::readyRead, this, [this] {
        m_eventBuffer += m_events.readAll();
        while (m_eventBuffer.contains('\n')) {
            int end = m_eventBuffer.indexOf('\n');
            auto line = m_eventBuffer.left(end);
            m_eventBuffer.remove(0, end + 1);
            auto event = line.left(line.indexOf(">>"));
            if (QList<QByteArray>{"workspace", "workspacev2", "focusedmon", "fullscreen",
                                  "activewindow", "openwindow", "closewindow", "movewindow",
                                  "monitoradded", "monitorremoved", "activespecial",
                                  "configreloaded", "dpms"}
                    .contains(event) &&
                !m_refresh.isActive())
                m_refresh.start();
        }
    });
    connect(&m_events, &QLocalSocket::disconnected, this, [this] {
        if (!m_sandbox)
            m_reconnect.start();
    });
    m_scanDelay.setSingleShot(true);
    m_scanDelay.setInterval(350);
    connect(&m_scanDelay, &QTimer::timeout, this, &Controller::scan);
    connect(&m_files, &QFileSystemWatcher::directoryChanged, this, [this] { m_scanDelay.start(); });
    connect(&m_scan, &QFutureWatcher<QVariantList>::finished, this, [this] {
        m_items = m_scan.result();
        m_busy = false;
        emit itemsChanged();
        emit busyChanged();
        if (!m_pendingFolder.isEmpty()) {
            m_pendingFolder.clear();
            scan();
        }
    });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *s) {
        emit screensChanged();
        if (!m_sandbox && m_config["manageBackground"].toBool() &&
            m_config["renderer"].toString() != "end4")
            createWallpaper(s);
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *s) {
        for (auto *w : m_wallpapers)
            if (w->screen() == s) {
                m_wallpapers.removeOne(w);
                w->deleteLater();
                break;
            }
        emit screensChanged();
    });
}
Controller::~Controller() {
    m_scan.waitForFinished();
    qDeleteAll(m_wallpapers);
}
void Controller::load() {
    m_configPath = configHome() + "/hyprwallselector/config.json";
    m_statePath = stateHome() + "/hyprwallselector/state.json";
    m_config = {{"folder", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
                               "/Wallpapers"},
                {"language", "en"},
                {"shortcut", "56+20"},
                {"shortcutLabel", "Alt+T"},
                {"monitor", "ALL"},
                {"fit", "cover"},
                {"pauseBattery", true},
                {"pauseFullscreen", true},
                {"renderer", "native"},
                {"syncDmsColors", false},
                {"manageBackground", false},
                {"autostart", false}};
    auto stored = readJson(m_configPath);
    if (stored.isEmpty()) {
        auto legacy = readJson(configHome() + "/hyprwallselector.json");
        if (!legacy.isEmpty()) {
            m_config["folder"] = legacy["wallpaper_folder"];
            m_config["language"] = legacy["language"];
            m_config["pauseBattery"] = legacy["pause_battery"].toBool(true);
        }
    }
    for (auto it = stored.begin(); it != stored.end(); ++it)
        m_config[it.key()] = it.value();
    m_state = readJson(m_statePath);
    if (m_state.isEmpty()) {
        auto old = readJson(stateHome() + "/hyprwallselector/selection.json");
        if (old.contains("path"))
            m_state["ALL"] = old;
    }
}
QString Controller::currentPath() const {
    QString monitor = m_config["monitor"].toString("ALL");
    auto v = m_state.value(monitor).toObject();
    if (v.isEmpty())
        v = m_state.value("ALL").toObject();
    return v["path"].toString();
}
QStringList Controller::screens() const {
    QStringList names{"ALL"};
    for (auto *s : qGuiApp->screens())
        names << s->name();
    return names;
}
void Controller::attach(QQuickView *view) {
    m_view = view;
    view->installEventFilter(this);
}
void Controller::start(bool show) {
    scan();
    if (!m_sandbox) {
        auto bus = QDBusConnection::systemBus();
        auto *serviceWatcher = new QDBusServiceWatcher(
            "org.hyprwallselector.Hotkeys", bus, QDBusServiceWatcher::WatchForRegistration, this);
        connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this,
                [this] { registerHotkey(); });
        bus.connect("org.hyprwallselector.Hotkeys", "/org/hyprwallselector/Hotkeys",
                    "org.hyprwallselector.Hotkeys", "Activated", this, SLOT(activated()));
        bus.connect("org.freedesktop.login1", QString(), "org.freedesktop.DBus.Properties",
                    "PropertiesChanged", this,
                    SLOT(sessionChanged(QString, QVariantMap, QStringList)));
        bus.connect("org.freedesktop.UPower", "/org/freedesktop/UPower",
                    "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                    SLOT(sessionChanged(QString, QVariantMap, QStringList)));
        refreshSession();
        registerHotkey();
        watchHyprland();
        if (m_config["manageBackground"].toBool() && m_config["renderer"].toString() != "end4")
            for (auto *s : qGuiApp->screens())
                createWallpaper(s);
    } else
        m_locked = false;
    if (show)
        toggle();
}
void Controller::registerHotkey() {
    if (m_sandbox)
        return;
    auto msg = QDBusMessage::createMethodCall("org.hyprwallselector.Hotkeys",
                                              "/org/hyprwallselector/Hotkeys",
                                              "org.hyprwallselector.Hotkeys", "Register");
    msg << m_config["shortcut"].toString();
    auto *call =
        new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(msg, 3000), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, call] {
        QDBusPendingReply<bool> reply = *call;
        if (reply.isError() || !reply.value())
            setStatus(text("hotkeyError"));
        call->deleteLater();
    });
}
void Controller::activated() {
    if (m_capture || (m_captureCooldown.isValid() && m_captureCooldown.elapsed() < 500))
        return;
    refreshSession();
    if (!m_locked)
        toggle();
}
void Controller::toggle() {
    if (m_visible) {
        hide();
        return;
    }
    if (m_locked && !m_sandbox) {
        setStatus(text("locked"));
        return;
    }
    m_visible = true;
    emit visibleChanged();
    if (m_view)
        m_view->show();
}
void Controller::hide() {
    if (!m_visible)
        return;
    m_visible = false;
    emit visibleChanged();
    if (m_view)
        m_view->hide();
}
void Controller::quit() {
    hide();
    qGuiApp->quit();
}
void Controller::scan() {
    if (m_scan.isRunning()) {
        m_pendingFolder = m_config["folder"].toString();
        return;
    }
    const QString folder = m_config["folder"].toString();
    if (!m_files.directories().isEmpty())
        m_files.removePaths(m_files.directories());
    if (QDir(folder).exists())
        m_files.addPath(folder);
    m_busy = true;
    emit busyChanged();
    m_scan.setFuture(QtConcurrent::run(catalogue, folder));
}
void Controller::setStatus(const QString &value) {
    m_status = value;
    emit statusChanged();
}
void Controller::reportError(QString message) {
    setStatus(message);
}
QString Controller::filePath(QString url) const {
    return localPath(url);
}
QUrl Controller::directoryUrl(QString path) const {
    if (path.isEmpty())
        return {};
    if (path == "~")
        path = QDir::homePath();
    QFileInfo directory(localPath(path));
    if (!directory.isDir() || !directory.isReadable() || !directory.isExecutable())
        return {};
    return QUrl::fromLocalFile(directory.absoluteFilePath());
}
void Controller::openFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_config["folder"].toString()));
}
bool Controller::saveSettings(QVariantMap values) {
    QJsonObject next = QJsonObject::fromVariantMap(values);
    QString folder = localPath(next["folder"].toString());
    if (!QFileInfo(folder).isDir()) {
        setStatus(text("invalidFolder"));
        return false;
    }
    next["folder"] = folder;
    if (!QStringList{"en", "uk", "ru"}.contains(next["language"].toString()))
        next["language"] = "en";
    if (!m_sandbox && !writeJson(m_configPath, next)) {
        setStatus(text("saveError"));
        return false;
    }
    bool folderChanged = folder != m_config["folder"].toString();
    bool keyChanged = next["shortcut"] != m_config["shortcut"];
    bool backgroundChanged = next["manageBackground"] != m_config["manageBackground"] ||
                             next["renderer"] != m_config["renderer"];
    m_config = next;
    m_palette.setEnabled(!m_sandbox && next["syncDmsColors"].toBool() &&
                         next["manageBackground"].toBool() &&
                         next["renderer"].toString() == "native");
    emit settingsChanged();
    emit currentChanged();
    setStatus("");
    if (folderChanged)
        scan();
    if (keyChanged)
        registerHotkey();
    if (!m_sandbox) {
        QString autopath = configHome() + "/autostart/hyprwallselector.desktop";
        if (next["autostart"].toBool()) {
            QDir().mkpath(QFileInfo(autopath).absolutePath());
            QSaveFile f(autopath);
            if (f.open(QIODevice::WriteOnly)) {
                f.write("[Desktop "
                        "Entry]\nType=Application\nName=Hyprwallselector\nExec=hyprwallselector "
                        "--daemon\nIcon=hyprwallselector\nTerminal=false\nNoDisplay=true\n");
                if (!f.commit())
                    setStatus(text("saveError"));
            }
        } else
            QFile::remove(autopath);
        if (backgroundChanged) {
            qDeleteAll(m_wallpapers);
            m_wallpapers.clear();
            if (next["manageBackground"].toBool() && next["renderer"].toString() != "end4")
                for (auto *s : qGuiApp->screens())
                    createWallpaper(s);
        }
        m_refresh.start();
        syncPalette();
    }
    return true;
}
void Controller::setupDms() {
    if (m_sandbox) {
        setStatus("Preview mode: shell settings unchanged.");
        return;
    }
    asyncRun(this, QCoreApplication::applicationFilePath(), {"--integrate-dms"},
             [this](int code, QByteArray out) {
                 setStatus(QString::fromUtf8(out).trimmed());
                 if (code == 0)
                     takeBackground();
             });
}
void Controller::takeBackground() {
    auto next = settings();
    next["manageBackground"] = true;
    saveSettings(next);
}
void Controller::apply(int index) {
    if (index < 0 || index >= m_items.size())
        return;
    auto entry = m_items[index].toMap();
    QString path = entry["path"].toString();
    if (!QFileInfo(path).isReadable()) {
        setStatus(text("missing"));
        return;
    }
    if (m_sandbox) {
        setStatus("Preview mode: wallpaper unchanged");
        return;
    }
    if (!m_config["manageBackground"].toBool()) {
        setStatus(text("enableBackground"));
        return;
    }
    setStatus("");
    if (m_config["renderer"].toString() == "end4") {
        asyncRun(this, "qs", {"-c", "ii", "ipc", "call", "wallpapers", "apply", path},
                 [this, path](int code, QByteArray) {
                     if (code == 0) {
                         m_allRequest = path;
                         wallpaperReady("ALL", path);
                         hide();
                     } else
                         setStatus("end-4 wallpaper IPC is unavailable.");
                 });
        return;
    }
    QString target = m_config["monitor"].toString("ALL");
    if (target != "ALL" && !screens().contains(target)) {
        setStatus("The selected display is disconnected.");
        return;
    }
    m_allRequest = target == "ALL" ? path : QString();
    emit wallpaperRequested(target, path, entry["video"].toBool());
    hide();
}
void Controller::saveState() {
    if (!m_sandbox && !writeJson(m_statePath, m_state))
        setStatus(text("saveError"));
}
void Controller::wallpaperPrepared(QString screen, QString path) {
    if (!m_sandbox)
        m_palette.request(screen, path, videoFile(path));
}
void Controller::wallpaperReady(QString screen, QString path) {
    m_state[screen] = QJsonObject{{"path", path}, {"video", videoFile(path)}};
    if (!m_allRequest.isEmpty() && path == m_allRequest)
        m_state["ALL"] = m_state[screen];
    saveState();
    emit currentChanged();
}
void Controller::syncPalette() {
    for (auto *screen : qGuiApp->screens()) {
        auto state = m_state.value(screen->name()).toObject();
        if (state.isEmpty())
            state = m_state.value("ALL").toObject();
        const QString path = state["path"].toString();
        m_palette.request(screen->name(), path, videoFile(path));
    }
}
void Controller::createWallpaper(QScreen *screen) {
    for (auto *view : m_wallpapers)
        if (view->screen() == screen)
            return;
    auto *view = new QQuickView;
    view->setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
    view->setScreen(screen);
    view->setColor(Qt::transparent);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->rootContext()->setContextProperty("backend", this);
    view->rootContext()->setContextProperty("outputName", screen->name());
    auto *layer = LayerShellQt::Window::get(view);
    layer->setLayer(LayerShellQt::Window::LayerBackground);
    layer->setScope("hyprwallselector-background");
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
                      LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft |
                      LayerShellQt::Window::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScreen(screen);
    layer->setActivateOnShow(false);
    view->setSource(QUrl("qrc:/qml/Wallpaper.qml"));
    view->resize(screen->size());
    m_wallpapers << view;
    view->show();
    auto saved = m_state[screen->name()].toObject();
    if (saved.isEmpty())
        saved = m_state["ALL"].toObject();
    QString path = saved["path"].toString();
    if (QFileInfo(path).isReadable())
        QTimer::singleShot(0, this, [this, screen, path] {
            emit wallpaperRequested(screen->name(), path, videoFile(path));
        });
}
void Controller::refreshSession() {
    if (m_sandbox) {
        m_locked = false;
        return;
    }
    bool allowed = false;
    char **sessions = nullptr;
    int n = sd_uid_get_sessions(getuid(), 0, &sessions);
    for (int i = 0; i < n; ++i) {
        char *sessionType = nullptr;
        char *sessionSeat = nullptr;
        bool graphical = sd_session_get_type(sessions[i], &sessionType) >= 0 && sessionType &&
                         (!strcmp(sessionType, "wayland") || !strcmp(sessionType, "x11"));
        bool seated = sd_session_get_seat(sessions[i], &sessionSeat) >= 0;
        free(sessionType);
        free(sessionSeat);
        if (graphical && seated && sd_session_is_active(sessions[i]) > 0 &&
            sd_session_is_remote(sessions[i]) == 0) {
            QDBusInterface manager("org.freedesktop.login1", "/org/freedesktop/login1",
                                   "org.freedesktop.login1.Manager", QDBusConnection::systemBus());
            manager.setTimeout(500);
            QDBusReply<QDBusObjectPath> p = manager.call("GetSession", QString(sessions[i]));
            if (p.isValid()) {
                QDBusInterface props("org.freedesktop.login1", p.value().path(),
                                     "org.freedesktop.DBus.Properties",
                                     QDBusConnection::systemBus());
                props.setTimeout(500);
                QDBusReply<QDBusVariant> locked =
                    props.call("Get", "org.freedesktop.login1.Session", "LockedHint");
                if (locked.isValid() && !locked.value().variant().toBool())
                    allowed = true;
            }
        }
        free(sessions[i]);
    }
    free(sessions);
    m_locked = !allowed;
    if (m_locked)
        hide();
    QDBusInterface up("org.freedesktop.UPower", "/org/freedesktop/UPower",
                      "org.freedesktop.DBus.Properties", QDBusConnection::systemBus());
    up.setTimeout(500);
    QDBusReply<QDBusVariant> battery = up.call("Get", "org.freedesktop.UPower", "OnBattery");
    if (battery.isValid())
        m_battery = battery.value().variant().toBool();
}
void Controller::sessionChanged(QString, QVariantMap, QStringList) {
    bool wasLocked = m_locked;
    refreshSession();
    if (wasLocked && !m_locked)
        registerHotkey();
    m_refresh.start();
}
void Controller::watchHyprland() {
    if (m_sandbox)
        return;
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    QString sig = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (sig.isEmpty())
        return;
    if (m_events.state() != QLocalSocket::UnconnectedState)
        return;
    m_events.connectToServer(runtime + "/hypr/" + sig + "/.socket2.sock");
    if (!m_reconnect.isActive())
        m_reconnect.start();
}
bool Controller::pausedFor(QString screen) const {
    return m_locked || (m_battery && m_config["pauseBattery"].toBool()) ||
           m_screenPaused.value(screen, false);
}
void Controller::refreshEnvironment() {
    if (m_sandbox)
        return;
    if (m_environmentBusy) {
        m_environmentPending = true;
        return;
    }
    m_environmentBusy = true;
    asyncRun(this, "hyprctl", {"-j", "monitors"}, [this](int code, QByteArray out) {
        if (code == 0)
            m_monitors = QJsonDocument::fromJson(out).array();
        asyncRun(this, "hyprctl", {"-j", "workspaces"}, [this](int code, QByteArray out) {
            if (code == 0)
                m_workspaces = QJsonDocument::fromJson(out).array();
            m_screenPaused.clear();
            for (auto m : m_monitors) {
                auto mon = m.toObject();
                bool covered = false;
                for (auto w : m_workspaces) {
                    auto ws = w.toObject();
                    if (ws["id"] == mon["activeWorkspace"].toObject()["id"] ||
                        ws["id"] == mon["specialWorkspace"].toObject()["id"])
                        covered |= ws["hasfullscreen"].toBool();
                }
                m_screenPaused[mon["name"].toString()] =
                    !mon["dpmsStatus"].toBool(true) ||
                    (covered && m_config["pauseFullscreen"].toBool());
            }
            m_paused = m_locked || (m_battery && m_config["pauseBattery"].toBool());
            emit pausedChanged();
            m_environmentBusy = false;
            if (m_environmentPending) {
                m_environmentPending = false;
                m_refresh.start();
            }
        });
    });
}
QString Controller::diagnostic() const {
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{"version", "0.1.0"},
                                  {"visible", m_visible},
                                  {"paused", m_paused},
                                  {"locked", m_locked},
                                  {"items", m_items.size()},
                                  {"wallpaperWindows", m_wallpapers.size()},
                                  {"videoPlayers", MediaItem::livePlayers()},
                                  {"videoRenderers", MediaItem::liveRenderers()},
                                  {"status", m_status},
                                  {"settings", m_config}})
            .toJson());
}
QString Controller::text(QString key) const {
    static const QMap<QString, QStringList> words = {
        {"home", {"Home", "Домівка", "Домой"}},
        {"disks", {"Disks", "Диски", "Диски"}},
        {"parentFolder", {"Parent folder", "Батьківська папка", "На уровень выше"}},
        {"hiddenFolders", {"Hidden folders", "Приховані папки", "Скрытые папки"}},
        {"noSubfolders",
         {"No subfolders — you can choose this folder.", "Немає підпапок — можна вибрати цю папку.",
          "Нет подпапок — можно выбрать эту папку."}},
        {"chooseFolder", {"Choose this folder", "Вибрати цю папку", "Выбрать эту папку"}},
        {"chooseFolderHint",
         {"Open a folder, then choose it. Changes apply after saving Settings.",
          "Відкрийте папку та виберіть її. Зміни застосуються після збереження налаштувань.",
          "Откройте папку и выберите её. Изменения применятся после сохранения настроек."}},
        {"settingsSaveHint",
         {"Changes apply when you save.", "Зміни застосуються після збереження.",
          "Изменения применятся после сохранения."}},
        {"general", {"General", "Загальні", "Общие"}},
        {"playback", {"Playback", "Відтворення", "Воспроизведение"}},
        {"integration", {"Shell & colors", "Оболонка та кольори", "Оболочка и цвета"}},
        {"settingsHint", {"Make it yours", "Налаштуйте під себе", "Настройте под себя"}},
        {"generalHint",
         {"Your library, language and shortcut.", "Ваша колекція, мова та сполучення.",
          "Ваша коллекция, язык и сочетание."}},
        {"playbackHint",
         {"A quieter desktop when you need it.", "Менше навантаження, коли це потрібно.",
          "Меньше нагрузки, когда это нужно."}},
        {"integrationHint",
         {"Keep your wallpaper and shell in harmony.", "Шпалери та оболонка в одному стилі.",
          "Обои и оболочка в одном стиле."}},
        {"syncDmsColors",
         {"DMS colors & lock screen", "Кольори та екран блокування DMS",
          "Цвета и экран блокировки DMS"}},
        {"paletteHint",
         {"Syncs the image or a still video frame. For colors, select Dynamic in DMS.",
          "Синхронізує картинку або кадр відео. Для кольорів виберіть динамічну тему в DMS.",
          "Синхронизирует картинку или кадр видео. Для цветов выберите динамическую тему в DMS."}},
        {"paletteError",
         {"Wallpaper applied; DMS colors could not be updated. Check that DMS is running.",
          "Шпалери встановлено; кольори DMS не оновлено. Перевірте, чи працює DMS.",
          "Обои установлены; цвета DMS не обновлены. Проверьте, запущен ли DMS."}},
        {"energy", {"Energy saving", "Енергозбереження", "Энергосбережение"}},
        {"pauseHint",
         {"Video resumes automatically when you return.",
          "Відео автоматично продовжиться, коли ви повернетеся.",
          "Видео автоматически продолжится, когда вы вернётесь."}},
        {"dmsHint",
         {"One-time setup: let Hyprwallselector draw the wallpaper while DMS manages the shell.",
          "Одноразове налаштування: шпалери малює Hyprwallselector, а оболонкою керує DMS.",
          "Однократная настройка: обои рисует Hyprwallselector, а оболочкой управляет DMS."}},
        {"close", {"Close", "Закрити", "Закрыть"}},
        {"renderer", {"Wallpaper renderer", "Рушій шпалер", "Движок обоев"}},
        {"dmsMode", {"Set up DMS", "Налаштувати DMS", "Настроить DMS"}},
        {"settings", {"Settings", "Налаштування", "Настройки"}},
        {"folder", {"Wallpaper folder", "Папка зі шпалерами", "Папка с обоями"}},
        {"language", {"Language", "Мова", "Язык"}},
        {"save", {"Save", "Зберегти", "Сохранить"}},
        {"cancel", {"Cancel", "Скасувати", "Отмена"}},
        {"browse", {"Browse…", "Вибрати…", "Выбрать…"}},
        {"preview", {"Preview", "Перегляд", "Просмотр"}},
        {"stop", {"Stop preview", "Зупинити", "Остановить"}},
        {"video", {"VIDEO", "ВІДЕО", "ВИДЕО"}},
        {"image", {"IMAGE", "ЗОБРАЖЕННЯ", "ИЗОБРАЖЕНИЕ"}},
        {"current", {"Current", "Встановлено", "Установлено"}},
        {"empty",
         {"No wallpapers in this folder", "У цій папці немає шпалер", "В этой папке нет обоев"}},
        {"loading", {"Preparing previews…", "Підготовка прев’ю…", "Подготовка превью…"}},
        {"keys",
         {"← → Browse  ·  Enter Apply  ·  Esc Close",
          "← → Вибір  ·  Enter Застосувати  ·  Esc Закрити",
          "← → Выбор  ·  Enter Применить  ·  Esc Закрыть"}},
        {"shortcut", {"Global shortcut", "Глобальне сполучення", "Глобальное сочетание"}},
        {"record", {"Press a combination…", "Натисніть сполучення…", "Нажмите сочетание…"}},
        {"monitor", {"Display", "Екран", "Экран"}},
        {"all", {"All displays", "Усі екрани", "Все экраны"}},
        {"background",
         {"Manage desktop wallpaper", "Керувати шпалерами робочого столу",
          "Управлять обоями рабочего стола"}},
        {"backgroundHint",
         {"Disable the other wallpaper renderer in your shell first.",
          "Спочатку вимкніть інший рушій шпалер в оболонці.",
          "Сначала отключите другой движок обоев в оболочке."}},
        {"autostart", {"Start at login", "Запускати при вході", "Запускать при входе"}},
        {"battery",
         {"Pause video on battery", "Призупиняти відео від батареї",
          "Приостанавливать видео от батареи"}},
        {"fullscreen",
         {"Pause behind fullscreen apps", "Пауза за повноекранними вікнами",
          "Пауза за полноэкранными окнами"}},
        {"hotkeyError",
         {"Shortcut service unavailable. Run --diagnose.",
          "Служба сполучень недоступна. Запустіть --diagnose.",
          "Служба сочетаний недоступна. Запустите --diagnose."}},
        {"invalidFolder",
         {"Choose an existing folder.", "Виберіть доступну папку.", "Выберите доступную папку."}},
        {"saveError",
         {"Cannot save settings.", "Не вдалося зберегти налаштування.",
          "Не удалось сохранить настройки."}},
        {"missing",
         {"This file is no longer available.", "Цей файл більше недоступний.",
          "Этот файл больше недоступен."}},
        {"enableBackground",
         {"Enable wallpaper management in Settings first.",
          "Спочатку увімкніть керування шпалерами в налаштуваннях.",
          "Сначала включите управление обоями в настройках."}},
        {"locked",
         {"Session is locked or inactive.", "Сеанс заблоковано або неактивний.",
          "Сеанс заблокирован или неактивен."}},
        {"quit", {"Quit application", "Завершити програму", "Завершить приложение"}}};
    int i = QStringList{"en", "uk", "ru"}.indexOf(m_config["language"].toString());
    return words.value(key, {key, key, key}).value(qMax(0, i));
}

bool Controller::eventFilter(QObject *, QEvent *event) {
    if (!m_capture || event->type() != QEvent::KeyPress)
        return false;
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_Escape) {
        m_capture = false;
        emit shortcutCaptured("", "");
        return true;
    }
    if (key->key() == Qt::Key_Alt || key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift ||
        key->key() == Qt::Key_Meta)
        return true;
    QStringList codes;
    if (key->modifiers() & Qt::ControlModifier)
        codes << "29";
    if (key->modifiers() & Qt::AltModifier)
        codes << "56";
    if (key->modifiers() & Qt::ShiftModifier)
        codes << "42";
    if (key->modifiers() & Qt::MetaModifier)
        codes << "125";
    int scan = int(key->nativeScanCode()) - 8;
    if (codes.isEmpty() || scan <= 0 || scan > 767)
        return true;
    codes << QString::number(scan);
    m_capture = false;
    m_captureCooldown.start();
    emit shortcutCaptured(codes.join('+'),
                          QKeySequence(key->keyCombination()).toString(QKeySequence::NativeText));
    return true;
}
