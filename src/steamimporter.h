// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <atomic>

struct SteamImportResult {
    int found = 0, copied = 0, skipped = 0, errors = 0;
    bool cancelled = false;
    QStringList details;
};

class SteamImporter final : public QObject {
    Q_OBJECT
  public:
    explicit SteamImporter(QObject *parent = nullptr);
    ~SteamImporter() override;
    // Accept a Steam library, an ancestor within its Workshop path, or 431960 itself.
    // Never fall back to scanning a Steam/game directory if Workshop is missing.
    static QString workshopDirectory(QString source);
    // Both paths must be existing directories. No source files are changed.
    bool start(QString source, QString destination);
    void cancel();
    bool running() const { return m_running; }
    bool scanning() const { return m_processed < 0; }
    int found() const { return m_found; }
    int processed() const { return m_processed; }
    QString currentFile() const { return m_currentFile; }
    QString destination() const { return m_destination; }
    SteamImportResult result() const { return m_result; }
  signals:
    void changed();
    void finished();
  private:
    QFutureWatcher<SteamImportResult> m_job;
    std::atomic_bool m_cancelled{false};
    bool m_running = false;
    int m_found = 0, m_processed = -1;
    QString m_currentFile, m_destination;
    SteamImportResult m_result;
};
