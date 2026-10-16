// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "dmspalette.h"
#ifdef Q_OS_WIN
#include "windowsplatform.h"
#include "windowslockscreen.h"
#endif
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QQuickView>
#include <QTimer>
#include <QVariantList>
class Controller final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
    Q_PROPERTY(bool selectorPresented READ selectorPresented NOTIFY selectorPresentedChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(QStringList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    DmsPalette m_palette;
#ifdef Q_OS_WIN
    WindowsPlatform *m_windows = nullptr;
    WindowsLockScreen m_lockScreen;
    QElapsedTimer m_startupTimer;
    qint64 m_firstWallpaperMs = -1;
    QHash<QString, QString> m_requestedPaths;
    int m_desktopRetries = 0;
#endif
    bool m_manualPause = false;
    QVariantList m_items;
    QJsonObject m_config;
    QJsonObject m_state;
    QString m_configPath, m_statePath, m_status;
    bool m_capture = false;
    QElapsedTimer m_captureCooldown;
    bool m_visible = false, m_paused = false, m_locked = true, m_battery = false, m_busy = false,
         m_sandbox = false;
    QQuickView *m_view = nullptr;
    bool m_selectorPresented = false;
    quint64 m_selectorGeneration = 0;
    QMetaObject::Connection m_selectorFrame;
    QList<QQuickView *> m_wallpapers;
    QHash<QQuickView *, QPointer<QScreen>> m_wallpaperScreens;
    QString m_storageRoot;
    QLocalSocket m_events;
    QTimer m_refresh, m_reconnect, m_scanDelay;
    QFileSystemWatcher m_files;
    QFutureWatcher<QVariantList> m_scan;
    QString m_pendingFolder, m_allRequest;
    QByteArray m_eventBuffer;
    bool m_environmentBusy = false, m_environmentPending = false;
    QHash<QString, bool> m_screenPaused;
    QJsonArray m_monitors, m_workspaces;
    int m_generation = 0;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void load();
    void createWallpaper(QScreen *screen);
    void refreshEnvironment();
    void refreshSession();
    void registerHotkey();
    void watchHyprland();
    void setStatus(const QString &value);
    void saveState();
    void syncPalette();
    void migrateDisplays();

  public:
    // Optional storage root isolates settings/state for automated tests.
    explicit Controller(bool sandbox = false, QObject *parent = nullptr, QString storageRoot = {});
    ~Controller() override;
    QVariantList items() const {
        return m_items;
    }
    QVariantMap settings() const {
        return m_config.toVariantMap();
    }
    QString currentPath() const;
    QString status() const {
        return m_status;
    }
    bool visible() const {
        return m_visible;
    }
    bool selectorPresented() const { return m_selectorPresented; }
    bool paused() const {
        return m_paused;
    }
    bool busy() const {
        return m_busy;
    }
    QStringList screens() const;
    Q_INVOKABLE QString screenLabel(QString id) const;
    void attach(QQuickView *view);
    void start(bool show);
    Q_PROPERTY(bool windows READ isWindows CONSTANT)
    bool isWindows() const {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    }
    Q_INVOKABLE QStringList drives() const;
    Q_INVOKABLE bool applyFile(QString path);
    void setManualPause(bool paused);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void hide();
    Q_INVOKABLE void scan();
    Q_INVOKABLE bool saveSettings(QVariantMap values);
    Q_INVOKABLE void apply(int index);
    Q_INVOKABLE void wallpaperPrepared(QString screen, QString path);
    Q_INVOKABLE void wallpaperReady(QString screen, QString path);
    Q_INVOKABLE void reportError(QString message);
    Q_INVOKABLE QString text(QString key) const;
    Q_INVOKABLE QString cachedThumbnailUrl(QString path) const;
    Q_INVOKABLE QString filePath(QString url) const;
    Q_INVOKABLE QUrl directoryUrl(QString path) const;
    Q_INVOKABLE QUrl fileUrl(QString path) const {
        return QUrl::fromLocalFile(path);
    }
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void takeBackground();
    Q_INVOKABLE void setupDms();
    Q_INVOKABLE bool pausedFor(QString screen) const;
    Q_INVOKABLE void quit();
    Q_INVOKABLE void checkUpdates() { emit updateRequested(); }
    Q_INVOKABLE void captureShortcut(bool active) {
        m_capture = active;
    }
    Q_INVOKABLE QString diagnostic() const;
  signals:
    void errorReported(const QString &message);
    void updateRequested();
    void shortcutCaptured(QString codes, QString label);
    void itemsChanged();
    void settingsChanged();
    void currentChanged();
    void statusChanged();
    void visibleChanged();
    void selectorPresentedChanged();
    void pausedChanged();
    void screensChanged();
    void busyChanged();
    void wallpaperRequested(QString screen, QString path, bool video);
  private slots:
    void activated();
    void sessionChanged(QString interfaceName, QVariantMap changed, QStringList invalidated);
};
