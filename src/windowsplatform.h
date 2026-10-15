// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWindow>
#include <QVariantMap>
#include <memory>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class QQuickView;
class QScreen;

// Windows-only desktop integration. Wallpaper windows are children of Explorer;
// they are never displayed as top-level fallback windows.
class WindowsPlatform final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit WindowsPlatform(QObject *parent = nullptr);
    ~WindowsPlatform() override;
    bool registerShortcut(const QString &sequence);
    bool attachWallpaper(QQuickView *view, QScreen *screen);
    void showSelector(QWindow *view);
    bool locked() const { return m_locked || m_suspended; }
    bool battery() const;
    bool displayOff() const { return m_displayOff; }
    bool covered(QScreen *screen) const;
    bool desktopReady() const;
    int attachedCount() const;
    bool shortcutRegistered() const { return m_hotkeyId != 0; }
    quint64 shortcutActivations() const { return m_hotkeyHits; }
    static bool setAutostart(bool enabled);
    static bool autostartEnabled();
    static bool setHighPriority(bool enabled);
    static QVariantMap memoryStats();
    static QSize displayPixelSize();
    static bool covers(const QRect &window, const QRect &monitor);
    static bool shortcutParts(const QString &sequence, UINT &modifiers, UINT &key);
    QVariantMap diagnostic() const;
    QString desktopError() const { return m_desktopError; }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override;
  signals:
    void activated();
    void environmentChanged();
    void desktopLost();
  private:
    QWindow m_messages;
    QTimer m_poll;
    HWND m_progman = nullptr, m_host = nullptr, m_icons = nullptr, m_worker = nullptr;
    DWORD m_explorerPid = 0;
    bool m_raised = false, m_locked = false, m_suspended = false, m_displayOff = false;
    HPOWERNOTIFY m_power = nullptr;
    UINT m_taskbarMessage = 0;
    int m_hotkeyId = 0;
    quint64 m_hotkeyHits = 0;
    QString m_shortcut;
    QString m_desktopError;
    QList<QPointer<QQuickView>> m_views;
    bool findDesktop();
    bool locateDesktop();
    void maintainDesktopOrder();
    void poll();
    bool positionWallpaper(QQuickView *view, QScreen *screen);
};
