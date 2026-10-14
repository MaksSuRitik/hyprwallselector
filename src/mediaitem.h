// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickFramebufferObject>
#include <memory>
#include <mpv/client.h>
class MediaItem : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    QString m_source;
    bool m_paused = false, m_ready = false, m_renderReady = false;
    std::shared_ptr<mpv_handle> m_player;
    mpv_handle *m_mpv = nullptr;

  public:
    static int livePlayers();
    static int liveRenderers();
    explicit MediaItem(QQuickItem *parent = nullptr);
    ~MediaItem() override;
    Renderer *createRenderer() const override;
    QString source() const {
        return m_source;
    }
    bool paused() const {
        return m_paused;
    }
    bool ready() const {
        return m_ready;
    }
    void setSource(const QString &source);
    void setPaused(bool paused);
    mpv_handle *handle() const {
        return m_mpv;
    }
    std::shared_ptr<mpv_handle> player() const {
        return m_player;
    }
    void firstFrame();
    void rendererReady();
    void loadSource();
  signals:
    void sourceChanged();
    void pausedChanged();
    void readyChanged();
    void failed(QString message);

  private:
    void drainEvents();
};
