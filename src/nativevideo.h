// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickItem>
#include <QElapsedTimer>
#include <QTimer>

// Decode and scale directly on the scene graph's D3D11 device.
class NativeVideo : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool contain READ contain WRITE setContain NOTIFY containChanged)
    QString m_source;
    bool m_paused = false, m_ready = false, m_contain = false;
    int m_generation = 0;
    QTimer m_tick;
    QElapsedTimer m_loadTime;
    void updateTimer();
  public:
    explicit NativeVideo(QQuickItem *parent = nullptr);
    QString source() const { return m_source; }
    bool paused() const { return m_paused; }
    bool ready() const { return m_ready; }
    bool contain() const { return m_contain; }
    void setSource(const QString &path);
    void setPaused(bool value);
    void setContain(bool value);
    static int livePlayers();
    static quint64 deliveredFrames();
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
  signals:
    void sourceChanged();
    void pausedChanged();
    void readyChanged();
    void containChanged();
    void failed(QString message);
};
