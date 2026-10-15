// SPDX-License-Identifier: GPL-3.0-or-later
#include "mediawindows.h"
#include <QVideoFrame>
#include <QDebug>
#include <QPlaybackOptions>
#include <atomic>
namespace {
std::atomic<int> players{0}, sinks{0};
std::atomic<quint64> frames{0}, gpuFrames{0};
}
int MediaItem::livePlayers() { return players.load(); }
int MediaItem::liveRenderers() { return sinks.load(); }
quint64 MediaItem::deliveredFrames() { return frames.load(); }
quint64 MediaItem::hardwareFrames() { return gpuFrames.load(); }
MediaItem::MediaItem(QObject *parent) : QObject(parent) {
    ++players;
    m_player.setLoops(QMediaPlayer::Infinite);
    QPlaybackOptions options;
    options.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
    options.setProbeSize(1024 * 1024);
    m_player.setPlaybackOptions(options);
    // No audio output: wallpapers remain silent without an unused sound pipeline.
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) { emit failed(message); });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia) {
            m_player.setActiveAudioTrack(-1);
            m_player.setActiveSubtitleTrack(-1);
        }
    });
}
MediaItem::~MediaItem() {
    m_player.stop();
    m_player.setVideoSink(nullptr);
    if (m_sink) { disconnect(m_sink, nullptr, this, nullptr); --sinks; }
    --players;
}
void MediaItem::setSource(const QString &source) {
    if (source == m_source) return;
    m_player.stop();
    m_source = source;
    m_ready = false;
    emit sourceChanged();
    emit readyChanged();
    load();
}
void MediaItem::load() {
    if (m_source.isEmpty()) return;
    const QUrl url(m_source);
    const QString source = url.isLocalFile() ? url.toLocalFile() : m_source;
    playPath(source);
}
void MediaItem::playPath(const QString &path) {
    m_waitingFrame = true;
    m_loadTime.start();
    m_player.setSource(QUrl::fromLocalFile(path));
    m_player.play(); // Produce the first frame even when the desktop is paused.
}
void MediaItem::setPaused(bool paused) {
    if (m_paused == paused) return;
    m_paused = paused;
    if (m_ready) {
        if (paused) m_player.pause();
        else m_player.play();
    }
    emit pausedChanged();
}
void MediaItem::setSink(QVideoSink *sink) {
    if (m_sink == sink) return;
    if (m_sink) { disconnect(m_sink, nullptr, this, nullptr); --sinks; }
    m_sink = sink;
    m_player.setVideoSink(sink);
    if (sink) {
        ++sinks;
        connect(sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
            if (!frame.isValid()) return;
            ++frames;
            if (frame.handleType() == QVideoFrame::RhiTextureHandle) ++gpuFrames;
            if (m_waitingFrame) {
                m_waitingFrame = false;
                if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo() << "Video first frame in" << m_loadTime.elapsed() << "ms; size" << frame.size();
                if (m_paused) m_player.pause();
            }
            if (!m_ready) {
                m_ready = true;
                if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo() << "First video frame" << frame.size() << "GPU texture:" << (frame.handleType() == QVideoFrame::RhiTextureHandle);
                emit readyChanged();
                if (m_paused) m_player.pause();
            }
        });
        connect(sink, &QObject::destroyed, this, [] { --sinks; });
    }
    emit sinkChanged();
}
