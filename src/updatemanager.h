// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>
#include <functional>

struct WindowsRelease {
    QString version, name;
    QUrl url;
    QByteArray sha256;
    qint64 size = 0;
    bool valid() const { return !version.isEmpty(); }
};

class UpdateManager final : public QObject {
    Q_OBJECT
  public:
    explicit UpdateManager(QObject *parent = nullptr);
    ~UpdateManager() override;
    static WindowsRelease selectRelease(const QByteArray &json, const QString &current);
    static bool trustedRedirect(const QUrl &url);
    WindowsRelease release() const { return m_release; }
    bool busy() const { return m_busy; }
    bool automatic() const;
    void setAutomatic(bool enabled);
    void start();
    void check(bool manual = true);
    void download();
    void cancel();
    bool launchInstaller(const QString &path);
    QString state() const { return m_state; }
  signals:
    void checked(bool manual, QString error);
    void available(QString version);
    void progress(qint64 received, qint64 total);
    void downloaded(QString path, QString error);
  private:
    QNetworkAccessManager m_network;
    QSettings m_preferences;
    QPointer<QNetworkReply> m_reply;
    WindowsRelease m_release;
    bool m_busy = false, m_cancelled = false;
    QString m_state = "idle", m_verifiedPath;
    QNetworkReply *get(const QUrl &url);
};
