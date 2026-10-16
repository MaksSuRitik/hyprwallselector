// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsplatform.h"
#include "windowslockscreen.h"
#include <QGuiApplication>
#include <QQuickView>
#include <QScreen>
#include <QtGui/qscreen_platform.h>
#include <QtTest>
#include <thread>

class WindowsTest : public QObject {
    Q_OBJECT
  private slots:
    void nativeMonitors() {
        DWORD error = ERROR_SUCCESS;
        QCOMPARE(WindowsPlatform::monitorInfo(nullptr, &error).cbSize, DWORD(0));
        QCOMPARE(error, DWORD(ERROR_INVALID_MONITOR_HANDLE));
        if (QGuiApplication::platformName() != "windows") QSKIP("Requires the Windows QPA plugin");
        QList<HMONITOR> monitors;
        QVERIFY(EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data)->BOOL {
            reinterpret_cast<QList<HMONITOR> *>(data)->append(monitor);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors)));
        QVERIFY(!qGuiApp->screens().isEmpty());
        for (auto *screen : qGuiApp->screens()) {
            auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
            QVERIFY(native);
            QVERIFY(monitors.contains(native->handle()));
            const auto info = WindowsPlatform::monitorInfo(screen, &error);
            QCOMPARE(error, DWORD(ERROR_SUCCESS));
            QCOMPARE(info.cbSize, DWORD(sizeof(info)));
            MONITORINFOEXW expected{}; expected.cbSize = sizeof(expected);
            QVERIFY(GetMonitorInfoW(native->handle(), &expected));
            QCOMPARE(QString::fromWCharArray(info.szDevice), QString::fromWCharArray(expected.szDevice));
            QCOMPARE(info.rcMonitor.left, expected.rcMonitor.left);
            QCOMPARE(info.rcMonitor.top, expected.rcMonitor.top);
            QCOMPARE(info.rcMonitor.right, expected.rcMonitor.right);
            QCOMPARE(info.rcMonitor.bottom, expected.rcMonitor.bottom);
            QVERIFY(info.rcMonitor.right > info.rcMonitor.left);
            QVERIFY(info.rcMonitor.bottom > info.rcMonitor.top);
        }
    }
    void desktopAttachment() {
        if (!qEnvironmentVariableIsSet("HWS_TEST_DESKTOP")) QSKIP("Opt-in Explorer integration test");
        WindowsPlatform platform;
        QQuickView view;
        view.setColor(Qt::transparent);
        view.setScreen(qGuiApp->primaryScreen());
        QVERIFY2(platform.attachWallpaper(&view, view.screen()), qPrintable(platform.desktopError()));
        QCOMPARE(platform.attachedCount(), 1);
        auto handle = reinterpret_cast<HWND>(view.winId());
        QVERIFY(GetParent(handle));
        QVERIFY(IsWindowVisible(handle));
        QVERIFY(GetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD);
        QVERIFY(!(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST));
        RECT bounds{};
        QVERIFY(GetWindowRect(handle, &bounds));
        QVERIFY(bounds.right > bounds.left && bounds.bottom > bounds.top);
        auto data = platform.diagnostic();
        QVERIFY(data["ready"].toBool());
    }
    void lockScreenApi() {
        const QString image = qEnvironmentVariable("HWS_TEST_LOCKSCREEN_IMAGE");
        if (image.isEmpty()) QSKIP("Opt-in actual lock screen image test");
        // The test process may have an STA UI thread; production uses an MTA worker.
        QString result;
        std::thread worker([&] { result = WindowsLockScreen::setImage(image); });
        worker.join();
        QVERIFY2(result.isEmpty(), qPrintable(result));
    }
    void processPriority() {
        const DWORD original=GetPriorityClass(GetCurrentProcess());
        struct Restore { DWORD value; ~Restore() { SetPriorityClass(GetCurrentProcess(),value); } } restore{original};
        QVERIFY(WindowsPlatform::setHighPriority(true));
        QCOMPARE(GetPriorityClass(GetCurrentProcess()),DWORD(HIGH_PRIORITY_CLASS));
        QVERIFY(WindowsPlatform::setHighPriority(false));
        QCOMPARE(GetPriorityClass(GetCurrentProcess()),DWORD(NORMAL_PRIORITY_CLASS));
    }
    void shortcuts() {
        UINT mods = 0, key = 0;
        QVERIFY(WindowsPlatform::shortcutParts("Alt+T", mods, key));
        QCOMPARE(key, UINT('T'));
        QCOMPARE(mods, UINT(MOD_ALT | MOD_NOREPEAT));
        QVERIFY(WindowsPlatform::shortcutParts("Ctrl+Shift+F11", mods, key));
        QCOMPARE(key, UINT(VK_F11));
        QCOMPARE(mods, UINT(MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT));
        QVERIFY(!WindowsPlatform::shortcutParts("T", mods, key));
        QVERIFY(!WindowsPlatform::shortcutParts("Shift+A", mods, key));
        QVERIFY(!WindowsPlatform::shortcutParts("Alt+F12", mods, key));
        QVERIFY(!WindowsPlatform::shortcutParts("Ctrl+K, Ctrl+C", mods, key));
    }
    void monitorCoverage() {
        const QRect left(-1920, 0, 1920, 1080), right(0, 0, 2560, 1440);
        QVERIFY(WindowsPlatform::covers(left, left));
        QVERIFY(!WindowsPlatform::covers(left, right));
        QVERIFY(WindowsPlatform::covers(QRect(-1920,0,4480,1440), left));
        QVERIFY(WindowsPlatform::covers(QRect(-1920,0,4480,1440), right));
        QVERIFY(!WindowsPlatform::covers(QRect(0,0,2560,1400), right)); // maximized, taskbar visible
        QVERIFY(!WindowsPlatform::covers(QRect(), right));
        QVERIFY(!WindowsPlatform::covers(right, QRect()));
    }
    void shortcutConflictKeepsPrevious() {
        WindowsPlatform first, second;
        if (!first.registerShortcut("Ctrl+Alt+Shift+F10")) QSKIP("Test shortcut is in use");
        if (!second.registerShortcut("Ctrl+Alt+Shift+F11")) QSKIP("Test shortcut is in use");
        QVERIFY(!first.registerShortcut("Ctrl+Alt+Shift+F11"));
        QVERIFY(!second.registerShortcut("Ctrl+Alt+Shift+F10"));
        QVERIFY(first.registerShortcut("Ctrl+Alt+Shift+F10"));
    }
};
QTEST_MAIN(WindowsTest)
#include "test-windows.moc"
