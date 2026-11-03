// SPDX-License-Identifier: GPL-3.0-or-later
#include "caelestiapreview.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

CaelestiaPreview::CaelestiaPreview(QObject *parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(100); // Same dwell time as Caelestia's WallpaperList.
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(4000);
    connect(&m_debounce, &QTimer::timeout, this, &CaelestiaPreview::pump);
    connect(&m_timeout, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { finished(code); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finished(-1);
    });
}
CaelestiaPreview::~CaelestiaPreview() {
    shutdown();
}
void CaelestiaPreview::open() {
    if (!m_open) {
        m_failed = false;
        m_unavailable = false;
    }
    m_open = true;
    pump();
}
void CaelestiaPreview::focus(const QString &path, const QString &displayPath) {
    if (confirming())
        return;
    if (m_wanted == path && m_display == displayPath && (!m_failed || m_unavailable))
        return;
    m_wanted = path;
    m_display = displayPath;
    if (!m_unavailable)
        m_failed = false;
    m_debounce.start();
}
void CaelestiaPreview::confirm(const QString &path, const QString &displayPath) {
    if (confirming())
        return;
    m_wanted = path;
    m_display = displayPath;
    m_confirm = true;
    m_open = true;
    m_debounce.stop();
    if (m_failed) {
        m_confirm = false;
        emit failed(m_error);
        return;
    }
    pump();
}
void CaelestiaPreview::cancel() {
    m_open = false;
    m_confirm = false;
    m_closing = true;
    m_debounce.stop();
    pump();
}
void CaelestiaPreview::clear() {
    m_session.clear();
    m_baseline.clear();
    m_shown.clear();
    m_ready = false;
    m_closing = false;
}
void CaelestiaPreview::send(Operation operation, const QStringList &arguments) {
    m_operation = operation;
    m_sent = m_wanted;
    m_sentDisplay = m_display;
    const QString executable = QStandardPaths::findExecutable("caelestia");
    m_timeout.start();
    m_process.start(executable.isEmpty() ? "caelestia" : executable,
                    QStringList{"shell", "hws-preview"} + arguments);
}
void CaelestiaPreview::pump() {
    if (m_finishing || m_operation != None || m_process.state() != QProcess::NotRunning)
        return;
    if (m_closing) {
        if (!m_session.isEmpty()) {
            send(Cancel, {"cancel", m_session, QString::number(++m_sequence)});
            return;
        }
        clear();
    }
    if (!m_open || m_failed)
        return;
    if (m_session.isEmpty()) {
        m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
        send(Begin, {"begin", m_session, QString::number(QCoreApplication::applicationPid())});
    } else if (m_ready && m_confirm) {
        send(Commit, {"commit", m_session, QString::number(++m_sequence), m_wanted});
    } else if (m_ready && !m_debounce.isActive() && !m_wanted.isEmpty() && m_shown != m_display) {
        send(Preview, {"preview", m_session, QString::number(++m_sequence), m_display});
    }
}
void CaelestiaPreview::finished(int code) {
    if (m_operation == None)
        return;
    m_finishing = true;
    m_timeout.stop();
    const auto operation = m_operation;
    m_operation = None;
    const auto result =
        QJsonDocument::fromJson(m_process.readAllStandardOutput().trimmed()).object();
    const QString error = result["error"].toString("unavailable");
    m_process.readAllStandardError();
    const bool ok = code == 0 && result["ok"].toBool();
    if (operation == Cancel) {
        clear();
        if (!ok)
            emit failed(error);
    } else if (!ok) {
        m_confirm = false;
        if (operation == Begin) {
            m_unavailable = result.isEmpty();
        }
        m_error = error;
        m_failed = true;
        m_closing = true;
        if (m_open)
            emit failed(error);
    } else if (operation == Begin) {
        m_ready = true;
        m_baseline = result["actual"].toString();
        m_shown = m_baseline;
        emit baselineReady(m_baseline);
    } else if (operation == Preview) {
        m_shown = m_sentDisplay;
        if (!m_closing)
            emit previewed(m_sent);
    } else if (operation == Commit) {
        const auto path = m_sent;
        m_open = false;
        m_confirm = false;
        clear();
        emit committed(path);
    }
    // QProcess::finished can be emitted inside start()/waitForFinished().
    // Queue the next request so a completed process is never reused reentrantly.
    m_finishing = false;
    QTimer::singleShot(0, this, &CaelestiaPreview::pump);
}
void CaelestiaPreview::shutdown() {
    m_open = false;
    m_confirm = false;
    m_debounce.stop();
    m_timeout.stop();
    if (m_session.isEmpty())
        return;
    const auto session = m_session;
    m_operation = None;
    m_process.blockSignals(true);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(200);
    }
    m_process.blockSignals(false);
    // Cancel also tombstones an as-yet-unreceived begin request in the bridge.
    QProcess cancel;
    cancel.start("caelestia",
                 {"shell", "hws-preview", "cancel", session, QString::number(m_sequence += 1000)});
    if (!cancel.waitForFinished(1500)) {
        cancel.kill();
        cancel.waitForFinished(200);
    }
    clear();
    m_failed = false;
}
