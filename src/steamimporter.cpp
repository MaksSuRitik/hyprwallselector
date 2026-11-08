// SPDX-License-Identifier: GPL-3.0-or-later
#include "steamimporter.h"
#include "mediaformats.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryFile>
#include <QtConcurrent>
#include <functional>

namespace {
bool samePath(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}
// Compare in bounded chunks so cancelling a duplicate check is responsive too.
bool identical(const QString &source, const QString &target, const std::atomic_bool &cancelled) {
    if (QFileInfo(source).size() != QFileInfo(target).size()) return false;
    QFile a(source), b(target);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly)) return false;
    while (!cancelled.load() && !a.atEnd()) {
        const auto chunk = a.read(1024 * 1024);
        if (chunk.isEmpty() || chunk != b.read(chunk.size())) return false;
    }
    return !cancelled.load() && a.error() == QFile::NoError && b.atEnd();
}
SteamImportResult importVideos(const QString &source, const QString &destination,
                              const std::atomic_bool &cancelled,
                              const std::function<void(int, int, QString)> &progress) {
    SteamImportResult result;
    auto fail = [&](const QString &path, const QString &reason) {
        ++result.errors;
        if (result.details.size() < 5) result.details << path + ": " + reason;
    };
    QStringList pending{source}, files;
    QElapsedTimer throttle;
    throttle.start();
    auto update = [&](int processed, const QString &path, bool force = false) {
        if (force || throttle.elapsed() >= 100) {
            progress(result.found, processed, path);
            throttle.restart();
        }
    };
    while (!pending.isEmpty() && !cancelled.load()) {
        const QString directory = pending.takeLast();
        if (!QFileInfo(directory).isReadable()) {
            fail(directory, QStringLiteral("Cannot read directory"));
            continue;
        }
        const auto entries = QDir(directory).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::Name | QDir::IgnoreCase);
        for (const auto &entry : entries) {
            if (cancelled.load()) break;
            // Junctions/reparse links may escape the chosen Steam tree or loop.
            if (entry.isSymLink() || entry.isJunction()) continue;
            if (entry.isDir()) {
                if (!samePath(entry.canonicalFilePath(), destination))
                    pending << entry.absoluteFilePath();
            } else if (entry.isFile() && videoFile(entry.filePath())) {
                files << entry.absoluteFilePath();
                ++result.found;
            }
            update(-1, QDir(source).relativeFilePath(entry.filePath()));
        }
    }
    int processed = 0;
    // Match existing copies by their contents, including legacy 0.3.6 names.
    // Switching from a library root to 431960 must not duplicate old imports.
    QHash<qint64, QStringList> existingBySize;
    for (const auto &entry : QDir(destination).entryInfoList(QDir::Files | QDir::Readable | QDir::Hidden))
        if (videoFile(entry.filePath())) existingBySize[entry.size()] << entry.absoluteFilePath();
    update(0, {}, true);
    for (const auto &path : files) {
        if (cancelled.load()) break;
        const QFileInfo info(path);
        const auto relative = QDir(source).relativeFilePath(path);
        update(processed, relative, true);
        QString target = QDir(destination).filePath(info.fileName());
        // Keep the original basename if possible. Relative-path suffixes keep
        // common Workshop names (video.mp4) distinct and stable across imports.
        const auto suffix = QString::fromLatin1(QCryptographicHash::hash(
            relative.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
        int collision = 0;
        bool duplicate = false;
        for (const auto &existing : existingBySize.value(info.size())) {
            if (cancelled.load()) break;
            if (identical(path, existing, cancelled)) { duplicate = true; break; }
        }
        while (!duplicate && QFileInfo::exists(target)) {
            if (identical(path, target, cancelled)) { duplicate = true; break; }
            if (cancelled.load()) break;
            target = QDir(destination).filePath(info.completeBaseName().left(160) + " - " + suffix +
                (collision ? "-" + QString::number(collision) : QString()) + "." + info.suffix());
            ++collision;
        }
        if (cancelled.load()) break;
        if (duplicate) {
            ++result.skipped;
        } else {
            QFile input(path);
            // A unique temporary file in the destination allows an atomic,
            // no-overwrite rename; errors/cancellation leave no partial video.
            QTemporaryFile output(QDir(destination).filePath(".hws-import-XXXXXX.part"));
            if (!input.open(QIODevice::ReadOnly)) {
                fail(relative, input.errorString());
            } else if (!output.open()) {
                fail(relative, output.errorString());
            } else {
                qint64 copied = 0;
                bool ok = true;
                while (!input.atEnd() && !cancelled.load()) {
                    const auto chunk = input.read(1024 * 1024);
                    if (chunk.isEmpty() || output.write(chunk) != chunk.size()) {
                        fail(relative, input.error() != QFile::NoError ? input.errorString() : output.errorString());
                        ok = false;
                        break;
                    }
                    copied += chunk.size();
                }
                if (cancelled.load()) break;
                if (ok && (copied != info.size() || QFileInfo(path).lastModified() != info.lastModified())) {
                    fail(relative, QStringLiteral("Source changed during copying; try again"));
                    ok = false;
                }
                if (ok && !output.flush()) {
                    fail(relative, output.errorString());
                    ok = false;
                }
                output.close();
                if (ok) {
                    if (!output.rename(target)) fail(relative, output.errorString());
                    else {
                        output.setAutoRemove(false);
                        existingBySize[info.size()] << target;
                        ++result.copied;
                    }
                }
            }
        }
        update(++processed, relative);
    }
    result.cancelled = cancelled.load();
    return result;
}
} // namespace

SteamImporter::SteamImporter(QObject *parent) : QObject(parent) {
    connect(&m_job, &QFutureWatcher<SteamImportResult>::finished, this, [this] {
        m_result = m_job.result();
        m_found = m_result.found;
        m_processed = m_result.copied + m_result.skipped + m_result.errors;
        m_currentFile.clear();
        m_running = false;
        emit changed();
        emit finished();
    });
}
SteamImporter::~SteamImporter() {
    cancel();
    m_job.waitForFinished();
}
QString SteamImporter::workshopDirectory(QString source) {
    if (source.trimmed().isEmpty()) return {};
    source = QFileInfo(source).canonicalFilePath();
    if (source.isEmpty() || !QFileInfo(source).isDir()) return {};
    const auto name = QFileInfo(source).fileName().toLower();
    QString relative = "steamapps/workshop/content/431960";
    if (name == "431960") relative.clear();
    else if (name == "content") relative = "431960";
    else if (name == "workshop") relative = "content/431960";
    else if (name == "steamapps") relative = "workshop/content/431960";
    const QFileInfo workshop(relative.isEmpty() ? source : QDir(source).filePath(relative));
    return workshop.isDir() && workshop.isReadable() ? workshop.canonicalFilePath() : QString();
}
bool SteamImporter::start(QString source, QString destination) {
    if (m_running) return false;
    source = workshopDirectory(source);
    destination = QFileInfo(destination).canonicalFilePath();
    if (source.isEmpty() || destination.isEmpty() || samePath(source, destination) ||
        !QFileInfo(source).isDir() || !QFileInfo(source).isReadable() ||
        !QFileInfo(destination).isDir() || !QFileInfo(destination).isWritable()) return false;
    m_destination = destination;
    m_result = {};
    m_found = 0;
    m_processed = -1;
    m_currentFile.clear();
    m_cancelled.store(false);
    m_running = true;
    emit changed();
    m_job.setFuture(QtConcurrent::run([this, source, destination] {
        return importVideos(source, destination, m_cancelled, [this](int found, int processed, QString path) {
            QMetaObject::invokeMethod(this, [this, found, processed, path] {
                m_found = found;
                m_processed = processed;
                m_currentFile = path;
                emit changed();
            }, Qt::QueuedConnection);
        });
    }));
    return true;
}
void SteamImporter::cancel() { m_cancelled.store(true); }
