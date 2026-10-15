// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QFutureWatcher>
#include <QTimer>
class WindowsLockScreen final : public QObject {
    Q_OBJECT
public:
    explicit WindowsLockScreen(QObject *parent = nullptr);
    ~WindowsLockScreen() override;
    void setEnabled(bool enabled);
    void request(const QString &path, bool video);
    QString status() const { return m_status; }
    // Runs on an MTA worker; public for an opt-in integration test.
    static QString setImage(const QString &path);
signals:
    void failed(const QString &message);
private:
    void start();
    bool m_enabled = false, m_video = false;
    QString m_path, m_activePath, m_status = "disabled";
    QTimer m_delay;
    QFutureWatcher<QString> m_job;
};
