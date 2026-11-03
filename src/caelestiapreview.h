// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>

// One native preview transaction. Only one IPC request may be in flight;
// scrolling replaces the pending candidate rather than starting more processes.
class CaelestiaPreview final : public QObject {
    Q_OBJECT
    enum Operation { None, Begin, Preview, Commit, Cancel };
    QProcess m_process;
    QTimer m_debounce, m_timeout;
    Operation m_operation = None;
    QString m_session, m_baseline, m_wanted, m_display, m_sent, m_sentDisplay, m_shown, m_error;
    quint64 m_sequence = 0;
    bool m_open = false, m_closing = false, m_ready = false, m_confirm = false, m_failed = false,
         m_unavailable = false, m_finishing = false;
    void pump();
    void send(Operation operation, const QStringList &arguments);
    void finished(int code);
    void clear();

  public:
    explicit CaelestiaPreview(QObject *parent = nullptr);
    ~CaelestiaPreview() override;
    void open();
    void focus(const QString &path, const QString &displayPath);
    void confirm(const QString &path, const QString &displayPath);
    void cancel();
    void shutdown();
    QString baseline() const {
        return m_baseline;
    }
    bool active() const {
        return !m_session.isEmpty();
    }
    bool confirming() const {
        return m_confirm || m_operation == Commit;
    }
    bool unavailable() const {
        return m_unavailable;
    }
  signals:
    void baselineReady(QString path);
    void previewed(QString path);
    void committed(QString path);
    void failed(QString reason);
};
