// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMediaPlayer>
#include <QPointer>
#include <QVideoSink>
#include <QElapsedTimer>

// Keep decoded D3D11 frames in the Qt video sink. No CPU image conversion or GL upload.
class MediaItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(QVideoSink *sink READ sink WRITE setSink NOTIFY sinkChanged)
  public:
    explicit MediaItem(QObject *parent = nullptr);
    ~MediaItem() override;
    QString source() const { return m_source; }
    bool paused() const { return m_paused; }
    bool ready() const { return m_ready; }
    QVideoSink *sink() const { return m_sink; }
    void setSource(const QString &source);
    void setPaused(bool paused);
    void setSink(QVideoSink *sink);
    static int livePlayers();
    static int liveRenderers();
    static quint64 deliveredFrames();
    static quint64 hardwareFrames();
  signals:
    void sourceChanged();
    void pausedChanged();
    void readyChanged();
    void sinkChanged();
    void failed(QString message);
  private:
    QMediaPlayer m_player;
    QPointer<QVideoSink> m_sink;
    QString m_source;
    bool m_paused = false, m_ready = false;
    bool m_waitingFrame = false;
    QElapsedTimer m_loadTime;
    void load();
    void playPath(const QString &path);
};
