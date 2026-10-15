// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsplatform.h"
#include <QtTest>

class WindowsTest : public QObject {
    Q_OBJECT
  private slots:
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
