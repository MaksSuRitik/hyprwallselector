// SPDX-License-Identifier: GPL-3.0-or-later
#include "updatemanager.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <memory>

namespace {
constexpr qint64 maxInstallerSize = 768LL * 1024 * 1024;
const QString releasesPath = "/MaksSuRitik/hyprwallselector/releases/download/";
QString updateDirectory() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/updates"; }
}

UpdateManager::UpdateManager(QObject *parent) : QObject(parent) {}
UpdateManager::~UpdateManager() {
    disconnect(this, nullptr, nullptr, nullptr);
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
}
WindowsRelease UpdateManager::selectRelease(const QByteArray &json, const QString &current) {
    WindowsRelease best;
    QVersionNumber highest = QVersionNumber::fromString(current);
    const QRegularExpression tagPattern("^windows-v([0-9]+\\.[0-9]+\\.[0-9]+)$");
    const QRegularExpression digestPattern("^sha256:([a-fA-F0-9]{64})$");
    for (const auto &entry : QJsonDocument::fromJson(json).array()) {
        const auto release = entry.toObject();
        if (release["draft"].toBool() || release["prerelease"].toBool()) continue;
        const auto match = tagPattern.match(release["tag_name"].toString());
        if (!match.hasMatch()) continue;
        const QString version = match.captured(1);
        const auto candidate = QVersionNumber::fromString(version);
        if (candidate <= highest) continue;
        const QString filename = "Hyprwallselector-" + version + "-Setup.exe";
        for (const auto &assetValue : release["assets"].toArray()) {
            const auto asset = assetValue.toObject();
            if (asset["name"].toString() != filename || asset["state"].toString() != "uploaded") continue;
            const auto digest = digestPattern.match(asset["digest"].toString());
            const QUrl url(asset["browser_download_url"].toString());
            const qint64 size = asset["size"].toInteger();
            if (!digest.hasMatch() || size <= 0 || size > maxInstallerSize ||
                url != QUrl("https://github.com" + releasesPath + match.captured(0) + "/" + filename)) continue;
            best = {version, filename, url, QByteArray::fromHex(digest.captured(1).toLatin1()), size};
            highest = candidate;
        }
    }
    return best;
}
bool UpdateManager::trustedRedirect(const QUrl &url) {
    if (url.scheme() != "https" || !url.userInfo().isEmpty() || url.port(443) != 443) return false;
    const QString host = url.host().toLower();
    return host == "github.com" || host == "api.github.com" ||
           host == "release-assets.githubusercontent.com" || host == "objects.githubusercontent.com";
}
QNetworkReply *UpdateManager::get(const QUrl &url) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Hyprwallselector/" + QCoreApplication::applicationVersion().toUtf8());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &next) {
        if (trustedRedirect(next)) reply->redirectAllowed();
        else reply->abort();
    });
    m_reply = reply;
    return reply;
}
bool UpdateManager::automatic() const { return m_preferences.value("updates/automatic", true).toBool(); }
void UpdateManager::setAutomatic(bool enabled) { m_preferences.setValue("updates/automatic", enabled); }
void UpdateManager::start() {
    auto checkDue = [this] {
        const qint64 last = m_preferences.value("updates/lastCheck", 0).toLongLong();
        if (automatic() && QDateTime::currentSecsSinceEpoch() - last >= 24 * 3600) check(false);
    };
    QTimer::singleShot(15000, this, checkDue);
    auto *timer = new QTimer(this);
    timer->setInterval(3600 * 1000);
    connect(timer, &QTimer::timeout, this, checkDue);
    timer->start();
}
void UpdateManager::check(bool manual) {
    if (m_busy) return;
    m_busy = true;
    m_state = "checking";
    auto *reply = get(QUrl("https://api.github.com/repos/MaksSuRitik/hyprwallselector/releases?per_page=100"));
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 4 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        const auto data = reply->readAll();
        QString error;
        if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else if (!QJsonDocument::fromJson(data).isArray()) error = "Invalid release response";
        if (error.isEmpty()) {
            const auto next = selectRelease(data, QCoreApplication::applicationVersion());
            const bool isNew = next.valid() && next.version != m_release.version;
            m_release = next;
            m_preferences.setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
            m_state = next.valid() ? "available:" + next.version : "up-to-date";
            if (isNew && !manual) emit available(next.version);
        } else m_state = "check-failed";
        m_busy = false;
        m_reply = nullptr;
        reply->deleteLater();
        emit checked(manual, error);
    });
}
void UpdateManager::download() {
    if (m_busy || !m_release.valid()) return;
    m_verifiedPath.clear();
    m_cancelled = false;
    QDir().mkpath(updateDirectory());
    const QString destination = updateDirectory() + '/' + m_release.name;
    auto file = std::make_shared<QSaveFile>(destination);
    if (!file->open(QIODevice::WriteOnly)) { emit downloaded({}, file->errorString()); return; }
    auto digest = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
    auto size = std::make_shared<qint64>(0);
    const WindowsRelease expected = m_release;
    auto *reply = get(expected.url);
    m_busy = true;
    m_state = "downloading";
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, digest, size, expected] {
        const auto chunk = reply->readAll();
        *size += chunk.size();
        if (*size > expected.size || file->write(chunk) != chunk.size()) { reply->abort(); return; }
        digest->addData(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateManager::progress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, digest, size, expected, destination] {
        QString error;
        if (reply->error() != QNetworkReply::NoError) error = reply->errorString();
        else if (*size != expected.size || digest->result() != expected.sha256) error = "Installer checksum mismatch";
        else if (!file->commit()) error = file->errorString();
        if (!error.isEmpty()) file->cancelWriting();
        else m_verifiedPath = destination;
        m_busy = false;
        m_state = m_cancelled ? "cancelled" : error.isEmpty() ? "ready" : "download-failed";
        m_reply = nullptr;
        reply->deleteLater();
        emit downloaded(error.isEmpty() ? destination : QString{}, m_cancelled ? QString{} : error);
    });
}
void UpdateManager::cancel() {
    m_cancelled = true;
    if (m_reply) m_reply->abort();
}
bool UpdateManager::launchInstaller(const QString &path) {
    // Recheck the exact downloaded bytes immediately before execution.
    if (path.isEmpty() || path != m_verifiedPath) return false;
    QFile file(path);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || file.size() != m_release.size || !digest.addData(&file) || digest.result() != m_release.sha256) return false;
    file.close();
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    const QString tasks = run.contains("Hyprwallselector") ? "autostart" : "!autostart";
    return QProcess::startDetached(path, {"/UPDATE=1", "/SILENT", "/NORESTART", "/MERGETASKS=" + tasks, "/DIR=" + QCoreApplication::applicationDirPath()});
}
