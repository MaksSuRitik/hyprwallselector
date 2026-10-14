// SPDX-License-Identifier: GPL-3.0-or-later
#include "dmspalette.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

DmsPalette::DmsPalette(QObject *parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_timeout.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, &DmsPalette::next);
    connect(&m_timeout, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        finished(status == QProcess::NormalExit ? code : -1);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finished(-1);
    });
}
DmsPalette::~DmsPalette() {
    m_process.disconnect(this);
    m_process.kill();
    m_process.waitForFinished(1000);
    if (!m_temporary.isEmpty())
        QFile::remove(m_temporary);
}
void DmsPalette::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) {
        m_pending.clear();
        m_debounce.stop();
        m_process.kill();
    }
}
void DmsPalette::request(const QString &screen, const QString &path, bool video) {
    if (!m_enabled || screen.isEmpty() || !QFileInfo(path).isReadable())
        return;
    m_pending[screen] = {path, video, 0};
    // Coalesce requests from this event-loop turn without adding visible latency.
    m_debounce.start(0);
}
void DmsPalette::run(Stage stage, const QString &program, const QStringList &args) {
    m_stage = stage;
    m_timeout.start(15000);
    m_process.start(program, args);
}
void DmsPalette::next() {
    if (!m_enabled || m_stage != Idle || m_pending.isEmpty())
        return;
    auto it = m_pending.begin();
    m_screen = it.key();
    m_request = it.value();
    m_pending.erase(it);
    m_image = m_request.path;
    if (m_request.video) {
        QFileInfo f(m_request.path);
        QByteArray key = (f.absoluteFilePath() + QString::number(f.size()) +
                          QString::number(f.lastModified().toMSecsSinceEpoch()))
                             .toUtf8();
        QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                        "/hyprwallselector/thumbs";
        QDir().mkpath(cache);
        m_image = cache + "/" + QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex() +
                  ".jpg";
        if (!QFileInfo::exists(m_image)) {
            m_temporary =
                m_image + QString(".%1.palette.jpg").arg(QCoreApplication::applicationPid());
            run(Poster, "ffmpeg",
                {"-nostdin", "-v", "error", "-y", "-threads", "2", "-ss", "0.1", "-i",
                 m_request.path, "-frames:v", "1", "-vf",
                 "scale=720:480:force_original_aspect_ratio=decrease", "-threads", "2",
                 m_temporary});
            return;
        }
    }
    run(Mode, "dms", {"ipc", "call", "wallpaper", "get"});
}
void DmsPalette::finished(int code) {
    m_timeout.stop();
    const auto response = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
    // A disabled or superseded request must not change the shell later.
    if (!m_enabled || m_pending.contains(m_screen)) {
        finish(false);
        return;
    }
    if (code != 0) {
        finish(false, true);
        return;
    }
    switch (m_stage) {
    case Poster:
        if (!QFile::rename(m_temporary, m_image) && !QFileInfo::exists(m_image)) {
            finish(false);
            return;
        }
        QFile::remove(m_temporary);
        m_temporary.clear();
        run(Mode, "dms", {"ipc", "call", "wallpaper", "get"});
        return;
    case Mode:
        m_perMonitor = response.startsWith("ERROR: Per-monitor mode enabled.");
        if (m_perMonitor) {
            run(Current, "dms", {"ipc", "call", "wallpaper", "getFor", m_screen});
            return;
        }
        [[fallthrough]];
    case Current:
        if (response.startsWith("ERROR:") || response.contains("FAILURE")) {
            finish(false, true);
            return;
        }
        if (response == m_image) {
            finish(true);
            return;
        }
        if (m_perMonitor)
            run(Set, "dms", {"ipc", "call", "wallpaper", "setFor", m_screen, m_image});
        else
            run(Set, "dms", {"ipc", "call", "wallpaper", "set", m_image});
        return;
    case Set:
        finish(response.startsWith("SUCCESS:"), !response.startsWith("SUCCESS:"));
        return;
    case Idle:
        return;
    }
}
void DmsPalette::finish(bool success, bool retry) {
    m_timeout.stop();
    if (!m_temporary.isEmpty())
        QFile::remove(m_temporary);
    m_temporary.clear();
    m_stage = Idle;
    if (!m_enabled)
        return;
    // DMS and this app may start simultaneously at login. Retry briefly, never poll forever.
    bool superseded = m_pending.contains(m_screen);
    if (!success && retry && !superseded && ++m_request.attempts < 3) {
        m_pending[m_screen] = m_request;
        m_debounce.start(2000);
        return;
    }
    if (!superseded)
        emit completed(success);
    if (!m_pending.isEmpty())
        m_debounce.start(0);
}
