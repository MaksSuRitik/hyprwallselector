// SPDX-License-Identifier: GPL-3.0-or-later
#include "chord.h"
#include <QtTest>
class Test : public QObject {
    Q_OBJECT
  private slots:
    void configure() {
        Chord c;
        QVERIFY(!c.configure({KEY_T}));
        QVERIFY(!c.configure({KEY_LEFTALT, KEY_T, KEY_Z}));
        QVERIFY(!c.configure({KEY_LEFTALT, KEY_LEFTALT, KEY_T}));
        QVERIFY(c.configure({KEY_LEFTALT, KEY_T}));
    }
    void pressRelease() {
        Chord c;
        QVERIFY(!c.event(KEY_LEFTALT, 1));
        QVERIFY(c.event(KEY_T, 1));
        QVERIFY(!c.event(KEY_T, 2));
        QVERIFY(!c.event(KEY_T, 1));
        QVERIFY(!c.event(KEY_T, 0));
        QVERIFY(c.event(KEY_T, 1));
    }
    void extraModifiers() {
        Chord c;
        c.event(KEY_LEFTALT, 1);
        c.event(KEY_LEFTCTRL, 1);
        QVERIFY(!c.event(KEY_T, 1));
        QVERIFY(!c.event(KEY_LEFTCTRL, 0));
        c.reset();
        QVERIFY(!c.event(KEY_T, 1));
    }
    void rightModifiers() {
        Chord c;
        QVERIFY(!c.event(KEY_RIGHTALT, 1));
        QVERIFY(c.event(KEY_T, 1));
    }
    void locks() {
        Chord c;
        c.event(KEY_CAPSLOCK, 1);
        c.event(KEY_CAPSLOCK, 0);
        c.event(KEY_LEFTALT, 1);
        QVERIFY(c.event(KEY_T, 1));
    }
};
QTEST_GUILESS_MAIN(Test)
#include "test-chord.moc"
