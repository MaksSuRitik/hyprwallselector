// SPDX-License-Identifier: GPL-3.0-or-later
#include "mediaitem.h"
#include <QGuiApplication>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QPointer>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QUrl>
#include <QtGui/qguiapplication_platform.h>
#include <atomic>
#include <mpv/render_gl.h>
#include <vector>

namespace {
std::atomic<int> playerCount{0}, rendererCount{0};
}
int MediaItem::livePlayers() {
    return playerCount.load();
}
int MediaItem::liveRenderers() {
    return rendererCount.load();
}

class VideoRenderer final : public QQuickFramebufferObject::Renderer {
    mpv_render_context *context = nullptr;
    QPointer<MediaItem> item;
    std::shared_ptr<mpv_handle> player;
    static void *resolve(void *, const char *name) {
        return reinterpret_cast<void *>(QOpenGLContext::currentContext()->getProcAddress(name));
    }

  public:
    explicit VideoRenderer(MediaItem *owner) : item(owner), player(owner->player()) {
        ++rendererCount;
    }
    ~VideoRenderer() override {
        if (context)
            mpv_render_context_free(context);
        --rendererCount;
    }
    void render() override {
        if (!item || !player)
            return;
        QQuickOpenGLUtils::resetOpenGLState();
        auto *gl = QOpenGLContext::currentContext()->extraFunctions();
        gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl->glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        gl->glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        if (!context) {
            if (qEnvironmentVariableIsSet("HWS_DEBUG"))
                qInfo() << "Creating mpv GL context";
            mpv_opengl_init_params init{resolve, nullptr};
            std::vector<mpv_render_param> params{
                {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
                {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init}};
#if QT_CONFIG(wayland)
            if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
                params.push_back({MPV_RENDER_PARAM_WL_DISPLAY, native->display()});
#endif
#if QT_CONFIG(xcb)
            if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
                params.push_back({MPV_RENDER_PARAM_X11_DISPLAY, native->display()});
#endif
            params.push_back({MPV_RENDER_PARAM_INVALID, nullptr});
            if (mpv_render_context_create(&context, player.get(), params.data()) < 0) {
                QMetaObject::invokeMethod(
                    item,
                    [p = QPointer<MediaItem>(item)] {
                        if (p)
                            emit p->failed("Cannot initialise video rendering");
                    },
                    Qt::QueuedConnection);
                return;
            }
            QMetaObject::invokeMethod(
                item,
                [p = QPointer<MediaItem>(item)] {
                    if (p)
                        p->rendererReady();
                },
                Qt::QueuedConnection);
            mpv_render_context_set_update_callback(
                context,
                [](void *ptr) {
                    auto *renderer = static_cast<VideoRenderer *>(ptr);
                    if (renderer->item)
                        QMetaObject::invokeMethod(renderer->item, "update", Qt::QueuedConnection);
                },
                this);
        }
        auto *fbo = framebufferObject();
        mpv_opengl_fbo target{int(fbo->handle()), fbo->width(), fbo->height(), 0};
        int flip = 0;
        int block = 0;
        mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &target},
                                     {MPV_RENDER_PARAM_FLIP_Y, &flip},
                                     {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
                                     {MPV_RENDER_PARAM_INVALID, nullptr}};
        auto changed = mpv_render_context_update(context);
        mpv_render_context_render(context, params);
        if (changed & MPV_RENDER_UPDATE_FRAME) {
            QMetaObject::invokeMethod(
                item,
                [p = QPointer<MediaItem>(item)] {
                    if (p)
                        p->firstFrame();
                },
                Qt::QueuedConnection);
        }
        QQuickOpenGLUtils::resetOpenGLState();
    }
    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        // mpv draws a color target; a full-size depth/stencil attachment is unused.
        return new QOpenGLFramebufferObject(size);
    }
};
MediaItem::MediaItem(QQuickItem *parent) : QQuickFramebufferObject(parent) {
    setMirrorVertically(false);
    if (qEnvironmentVariableIsSet("HWS_DEBUG"))
        qInfo() << "MediaItem created";
    m_mpv = mpv_create();
    if (!m_mpv)
        return;
    ++playerCount;
    m_player = std::shared_ptr<mpv_handle>(m_mpv, [](mpv_handle *handle) {
        mpv_terminate_destroy(handle);
        --playerCount;
    });
    for (const auto &p : {std::pair{"vo", "libmpv"},
                          {"config", "no"},
                          {"load-scripts", "no"},
                          {"terminal", "no"},
                          {"audio", "no"},
                          {"sub", "no"},
                          {"ytdl", "no"},
                          {"hwdec", "auto-safe"},
                          // Only local wallpaper files are supported. Bound packet queues
                          // instead of keeping the default 150+50 MiB media-player cache.
                          {"cache", "no"},
                          {"demuxer-max-bytes", "16MiB"},
                          {"demuxer-max-back-bytes", "0"},
                          {"loop-file", "inf"},
                          {"keep-open", "yes"},
                          {"input-default-bindings", "no"},
                          {"input-vo-keyboard", "no"},
                          {"panscan", "1.0"}})
        mpv_set_option_string(m_mpv, p.first, p.second);
    if (mpv_initialize(m_mpv) < 0) {
        m_player.reset();
        m_mpv = nullptr;
        return;
    }
    mpv_set_wakeup_callback(
        m_mpv,
        [](void *ptr) {
            auto *self = static_cast<MediaItem *>(ptr);
            QMetaObject::invokeMethod(self, [self] { self->drainEvents(); }, Qt::QueuedConnection);
        },
        this);
}
MediaItem::~MediaItem() {
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
    }
}
QQuickFramebufferObject::Renderer *MediaItem::createRenderer() const {
    return new VideoRenderer(const_cast<MediaItem *>(this));
}
void MediaItem::setSource(const QString &value) {
    if (m_source == value)
        return;
    m_source = value;
    m_ready = false;
    emit sourceChanged();
    emit readyChanged();
    if (!m_mpv) {
        emit failed("Cannot initialise libmpv");
        return;
    }
    if (m_renderReady)
        loadSource();
    update();
}
void MediaItem::rendererReady() {
    m_renderReady = true;
    loadSource();
}
void MediaItem::loadSource() {
    if (!m_renderReady || m_source.isEmpty() || !m_mpv)
        return;
    QString path = QUrl(m_source).isLocalFile() ? QUrl(m_source).toLocalFile() : m_source;
    QByteArray bytes = path.toUtf8();
    const char *command[] = {"loadfile", bytes.constData(), "replace", nullptr};
    mpv_command_async(m_mpv, 1, command);
    update();
    if (qEnvironmentVariableIsSet("HWS_DEBUG"))
        qInfo() << "Load media" << path;
}
void MediaItem::setPaused(bool value) {
    if (m_paused == value)
        return;
    m_paused = value;
    int pause = value;
    if (m_mpv)
        mpv_set_property_async(m_mpv, 2, "pause", MPV_FORMAT_FLAG, &pause);
    emit pausedChanged();
}
void MediaItem::firstFrame() {
    if (m_ready || !m_mpv)
        return;
    double time = 0;
    if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &time) >= 0) {
        m_ready = true;
        if (qEnvironmentVariableIsSet("HWS_DEBUG")) {
            char *decoder = mpv_get_property_string(m_mpv, "hwdec-current");
            qInfo() << "Video decoder" << (decoder ? decoder : "none");
            mpv_free(decoder);
        }
        emit readyChanged();
    }
}
void MediaItem::drainEvents() {
    if (!m_mpv)
        return;
    while (auto *e = mpv_wait_event(m_mpv, 0)) {
        if (e->event_id == MPV_EVENT_NONE)
            break;
        if (qEnvironmentVariableIsSet("HWS_DEBUG"))
            qInfo() << "mpv event" << mpv_event_name(e->event_id);
        if (e->event_id == MPV_EVENT_END_FILE) {
            auto *end = static_cast<mpv_event_end_file *>(e->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR)
                emit failed(QString::fromUtf8(mpv_error_string(end->error)));
        }
    }
}
