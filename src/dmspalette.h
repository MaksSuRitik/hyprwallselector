// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Serial, asynchronous DMS IPC. Only posters are sent for videos; DMS never
// starts a second video player. Pending changes replace older ones per screen.
class DmsPalette final : public QObject {
    Q_OBJECT
    struct Request {
        QString path;
        bool video = false;
        int attempts = 0;
    };
    QMap<QString, Request> m_pending;
    QProcess m_process;
    QTimer m_debounce, m_timeout;
    Request m_request;
    QString m_screen, m_image, m_temporary;
    enum Stage { Idle, Poster, Mode, Current, Set } m_stage = Idle;
    bool m_enabled = false, m_perMonitor = false;
    void next();
    void run(Stage stage, const QString &program, const QStringList &args);
    void finished(int code);
    void finish(bool success, bool retry = false);

  public:
    explicit DmsPalette(QObject *parent = nullptr);
    ~DmsPalette() override;
    void setEnabled(bool enabled);
    void request(const QString &screen, const QString &path, bool video);
  signals:
    void completed(bool success);
};
