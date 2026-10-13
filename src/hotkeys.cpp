// SPDX-License-Identifier: GPL-3.0-or-later
// Passive input observer: never grabs devices, injects events, or forwards raw keys.
#include "chord.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>
#include <QtDBus>
#include <fcntl.h>
#include <libudev.h>
#include <linux/input.h>
#include <map>
#include <memory>
#include <sys/ioctl.h>
#include <systemd/sd-login.h>
#include <unistd.h>

static constexpr auto BUS = "org.hyprwallselector.Hotkeys";
struct Subscription {
    QString owner, session, seat;
    uid_t uid;
    Chord chord;
    qint64 last = -1000;
};
struct Device {
    int fd;
    QString seat;
    std::unique_ptr<QSocketNotifier> notifier;
};
class Hotkeys final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.hyprwallselector.Hotkeys")
    std::map<QString, Subscription> clients;
    std::map<QString, Device> devices;
    udev *context = udev_new();
    udev_monitor *monitor = nullptr;
    QSocketNotifier *hotplug = nullptr;
    QDBusServiceWatcher watcher;
    QElapsedTimer clock;
    bool sessionAllowed(const Subscription &s) {
        if (sd_session_is_active(s.session.toUtf8()) <= 0 ||
            sd_session_is_remote(s.session.toUtf8()) != 0)
            return false;
        QDBusInterface manager("org.freedesktop.login1", "/org/freedesktop/login1",
                               "org.freedesktop.login1.Manager", QDBusConnection::systemBus());
        manager.setTimeout(600);
        QDBusReply<QDBusObjectPath> path = manager.call("GetSession", s.session);
        if (!path.isValid())
            return false;
        QDBusInterface props("org.freedesktop.login1", path.value().path(),
                             "org.freedesktop.DBus.Properties", QDBusConnection::systemBus());
        props.setTimeout(600);
        QDBusReply<QDBusVariant> locked =
            props.call("Get", "org.freedesktop.login1.Session", "LockedHint");
        return locked.isValid() && !locked.value().variant().toBool();
    }
    void addDevice(udev_device *dev) {
        const char *node = udev_device_get_devnode(dev);
        const char *keyboard = udev_device_get_property_value(dev, "ID_INPUT_KEYBOARD");
        if (!node || !keyboard || strcmp(keyboard, "1") ||
            !QString(node).startsWith("/dev/input/event"))
            return;
        QString path = QString::fromUtf8(node);
        if (devices.contains(path))
            return;
        int fd = open(node, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            return;
        const char *seat = udev_device_get_property_value(dev, "ID_SEAT");
        Device d{fd, seat ? QString(seat) : QString("seat0"),
                 std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read)};
        connect(d.notifier.get(), &QSocketNotifier::activated, this,
                [this, path] { readDevice(path); });
        devices.emplace(path, std::move(d));
    }
    void removeDevice(const QString &path) {
        auto it = devices.find(path);
        if (it == devices.end())
            return;
        it->second.notifier.reset();
        close(it->second.fd);
        devices.erase(it);
        for (auto &[_, s] : clients)
            s.chord.reset();
    }
    void readDevice(const QString &path) {
        auto it = devices.find(path);
        if (it == devices.end())
            return;
        input_event events[32];
        ssize_t bytes;
        while ((bytes = read(it->second.fd, events, sizeof(events))) > 0) {
            for (ssize_t i = 0; i < bytes / ssize_t(sizeof(input_event)); ++i) {
                auto &e = events[i];
                if (e.type == EV_SYN && e.code == SYN_DROPPED) {
                    for (auto &[_, s] : clients)
                        s.chord.reset();
                    continue;
                }
                if (e.type != EV_KEY)
                    continue;
                for (auto &[_, s] : clients) {
                    if (s.seat != it->second.seat || !s.chord.event(e.code, e.value))
                        continue;
                    const auto now = clock.elapsed();
                    if (now - s.last < 180)
                        continue;
                    s.last = now;
                    if (!sessionAllowed(s)) {
                        s.chord.reset();
                        continue;
                    }
                    auto signal = QDBusMessage::createTargetedSignal(
                        s.owner, "/org/hyprwallselector/Hotkeys", BUS, "Activated");
                    QDBusConnection::systemBus().send(signal);
                }
            }
        }
        if (bytes == 0 || (bytes < 0 && errno != EAGAIN && errno != EINTR))
            removeDevice(path);
    }
    void startDevices() {
        if (monitor || !context)
            return;
        monitor = udev_monitor_new_from_netlink(context, "udev");
        udev_monitor_filter_add_match_subsystem_devtype(monitor, "input", nullptr);
        udev_monitor_enable_receiving(monitor);
        hotplug = new QSocketNotifier(udev_monitor_get_fd(monitor), QSocketNotifier::Read, this);
        connect(hotplug, &QSocketNotifier::activated, this, [this] {
            while (auto *dev = udev_monitor_receive_device(monitor)) {
                const char *action = udev_device_get_action(dev);
                const char *node = udev_device_get_devnode(dev);
                if (action && !strcmp(action, "remove") && node)
                    removeDevice(QString(node));
                else
                    addDevice(dev);
                udev_device_unref(dev);
            }
        });
        auto *en = udev_enumerate_new(context);
        udev_enumerate_add_match_subsystem(en, "input");
        udev_enumerate_scan_devices(en);
        udev_list_entry *entry;
        udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(en)) {
            auto *dev = udev_device_new_from_syspath(context, udev_list_entry_get_name(entry));
            if (dev) {
                addDevice(dev);
                udev_device_unref(dev);
            }
        }
        udev_enumerate_unref(en);
    }
    void stopDevices() {
        delete hotplug;
        hotplug = nullptr;
        for (auto &[_, d] : devices) {
            d.notifier.reset();
            close(d.fd);
        }
        devices.clear();
        if (monitor) {
            udev_monitor_unref(monitor);
            monitor = nullptr;
        }
    }

  public:
    Hotkeys() : watcher(this) {
        clock.start();
        watcher.setConnection(QDBusConnection::systemBus());
        watcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(&watcher, &QDBusServiceWatcher::serviceUnregistered, this,
                [this](const QString &owner) {
                    clients.erase(owner);
                    watcher.removeWatchedService(owner);
                    if (clients.empty())
                        stopDevices();
                });
    }
    ~Hotkeys() {
        stopDevices();
        if (context)
            udev_unref(context);
    }
  public slots:
    bool Register(const QString &combination) {
        if (!calledFromDBus())
            return false;
        const QString owner = message().service();
        auto uidReply = connection().interface()->serviceUid(owner);
        if (!uidReply.isValid() || uidReply.value() == 0)
            return false;
        Subscription s;
        s.owner = owner;
        s.uid = uidReply.value();
        std::vector<unsigned> codes;
        for (const auto &part : combination.split('+')) {
            bool ok = false;
            uint c = part.toUInt(&ok);
            if (!ok)
                return false;
            codes.push_back(c);
        }
        if (!s.chord.configure(codes))
            return false;
        char **sessions = nullptr;
        int n = sd_uid_get_sessions(s.uid, 0, &sessions);
        for (int i = 0; i < n; ++i) {
            char *type = nullptr;
            char *seat = nullptr;
            if (sd_session_get_type(sessions[i], &type) >= 0 && type &&
                (!strcmp(type, "wayland") || !strcmp(type, "x11")) &&
                sd_session_get_seat(sessions[i], &seat) >= 0 && seat &&
                sd_session_is_active(sessions[i]) > 0 && sd_session_is_remote(sessions[i]) == 0) {
                s.session = QString(sessions[i]);
                s.seat = QString(seat);
            }
            free(type);
            free(seat);
            free(sessions[i]);
        }
        free(sessions);
        if (s.session.isEmpty() || !sessionAllowed(s))
            return false;
        if (clients.size() >= 16 && !clients.contains(owner))
            return false;
        clients[owner] = s;
        watcher.addWatchedService(owner);
        startDevices();
        return true;
    }
    void Unregister() {
        if (calledFromDBus()) {
            QString owner = message().service();
            clients.erase(owner);
            watcher.removeWatchedService(owner);
            if (clients.empty())
                stopDevices();
        }
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (getuid() != 0)
        return 1;
    Hotkeys service;
    auto bus = QDBusConnection::systemBus();
    if (!bus.registerObject("/org/hyprwallselector/Hotkeys", &service,
                            QDBusConnection::ExportAllSlots) ||
        !bus.registerService(BUS))
        return 1;
    return app.exec();
}
#include "hotkeys.moc"
