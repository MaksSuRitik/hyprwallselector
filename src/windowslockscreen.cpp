// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowslockscreen.h"
#include "windowsplatform.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QtConcurrent>
#include <roapi.h>
#include <winstring.h>
#include <asyncinfo.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace HwsWinRt {
// MinGW does not yet declare these WinRT interfaces. These ABI declarations
// follow Microsoft's SDK windows.storage.h / windows.system.userprofile.h.
// Only the methods used here are needed; no registry personalization hacks.
template<typename T> struct AsyncOperation : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(T *) = 0;
};
struct StorageStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetFileFromPathAsync(HSTRING, AsyncOperation<IInspectable *> **) = 0;
};
struct AsyncAction : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown *) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults() = 0;
};
struct LockScreenStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_OriginalImageFile(IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetImageStream(IInspectable **) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetImageFileAsync(IInspectable *, AsyncAction **) = 0;
};
}
namespace {
using namespace HwsWinRt;
constexpr GUID storageId{0x5984c710,0xdaf2,0x43c8,{0x8b,0xb4,0xa4,0xd3,0xea,0xcf,0xd0,0x3f}};
constexpr GUID lockScreenId{0x3ee9d3ad,0xb607,0x40ae,{0xb4,0x26,0x76,0x31,0xd9,0x82,0x12,0x69}};
struct HString {
    HSTRING value = nullptr;
    explicit HString(const QString &s) { WindowsCreateString(reinterpret_cast<const wchar_t *>(s.utf16()), UINT32(s.size()), &value); }
    ~HString() { WindowsDeleteString(value); }
    operator HSTRING() const { return value; }
};
struct Apartment {
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) RoUninitialize(); }
};
HRESULT waitFor(IInspectable *op) {
    ComPtr<IAsyncInfo> info;
    HRESULT hr = op->QueryInterface(IID_PPV_ARGS(&info));
    if (FAILED(hr)) return hr;
    QElapsedTimer timer; timer.start();
    for (;;) {
        AsyncStatus status;
        if (FAILED(hr = info->get_Status(&status))) return hr;
        if (status == Completed) return S_OK;
        if (status == Error) { info->get_ErrorCode(&hr); return FAILED(hr) ? hr : E_FAIL; }
        if (status == Canceled) return E_ABORT;
        if (timer.elapsed() > 10000) { info->Cancel(); return HRESULT_FROM_WIN32(ERROR_TIMEOUT); }
        QThread::msleep(10);
    }
}
QString error(HRESULT hr) { return QString("Lock screen: Windows returned 0x%1.").arg(quint32(hr), 8, 16, QChar('0')); }
QString sync(const QString &source, bool video, QSize size) {
    const QFileInfo info(source);
    if (!info.isFile()) return "Lock screen: wallpaper file is unavailable.";
    const auto key = QCryptographicHash::hash((info.absoluteFilePath() + QString::number(info.size()) +
        QString::number(info.lastModified().toMSecsSinceEpoch())).toUtf8(), QCryptographicHash::Sha256).toHex();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/lockscreen";
    if (!QDir().mkpath(dir)) return "Lock screen: cannot create image cache.";
    QFile last(dir + "/last-source");
    if (last.open(QIODevice::ReadOnly) && last.readAll() == key) return {};
    last.close();
    // Windows requires a new filename when replacing the previous lock image.
    const QString image = dir + "/wallpaper-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
    size = size.scaled(2560, 1440, Qt::KeepAspectRatio);
    if (video) {
        QProcess ffmpeg;
        ffmpeg.start(QCoreApplication::applicationDirPath() + "/ffmpeg.exe",
            {"-nostdin", "-v", "error", "-threads", "2", "-ss", "0.1", "-i", source,
             "-frames:v", "1", "-vf", QString("scale=%1:%2:force_original_aspect_ratio=decrease").arg(size.width()).arg(size.height()),
             "-threads", "2", "-q:v", "3", image});
        if (!ffmpeg.waitForFinished(10000)) { ffmpeg.kill(); ffmpeg.waitForFinished(1000); }
        if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0 || !QFileInfo(image).size()) {
            QFile::remove(image); return "Lock screen: cannot extract a video frame.";
        }
    } else {
        QImageReader reader(source); reader.setAutoTransform(true);
        if (reader.size().isValid()) reader.setScaledSize(reader.size().scaled(size, Qt::KeepAspectRatio));
        const QImage frame = reader.read();
        if (frame.isNull() || !frame.save(image, "JPG", 92)) return "Lock screen: cannot prepare image.";
    }
    const QString result = WindowsLockScreen::setImage(image);
    if (result.isEmpty()) {
        QSaveFile save(dir + "/last-source");
        if (save.open(QIODevice::WriteOnly)) { save.write(key); save.commit(); }
        // This app-owned cache contains only generated JPEGs, never source files.
        for (const auto &old : QDir(dir).entryList({"wallpaper-*.jpg"}, QDir::Files))
            if (dir + "/" + old != image) QFile::remove(dir + "/" + old);
    } else QFile::remove(image);
    return result;
}
}
QString WindowsLockScreen::setImage(const QString &path) {
    Apartment apartment;
    if (FAILED(apartment.result)) return error(apartment.result);
    ComPtr<LockScreenStatics> factory;
    HRESULT hr = RoGetActivationFactory(HString("Windows.System.UserProfile.LockScreen"),
        lockScreenId, reinterpret_cast<void **>(factory.GetAddressOf()));
    if (FAILED(hr)) return error(hr);
    ComPtr<StorageStatics> storage;
    hr = RoGetActivationFactory(HString("Windows.Storage.StorageFile"), storageId,
        reinterpret_cast<void **>(storage.GetAddressOf()));
    if (FAILED(hr)) return error(hr);
    ComPtr<AsyncOperation<IInspectable *>> open;
    if (FAILED(hr = storage->GetFileFromPathAsync(HString(QDir::toNativeSeparators(path)), &open))) return error(hr);
    if (FAILED(hr = waitFor(open.Get()))) return error(hr);
    ComPtr<IInspectable> file;
    if (FAILED(hr = open->GetResults(&file))) return error(hr);
    ComPtr<AsyncAction> operation;
    if (FAILED(hr = factory->SetImageFileAsync(file.Get(), &operation))) return error(hr);
    if (FAILED(hr = waitFor(operation.Get()))) return error(hr);
    if (FAILED(hr = operation->GetResults())) return error(hr);
    return {};
}
WindowsLockScreen::WindowsLockScreen(QObject *parent) : QObject(parent) {
    m_delay.setSingleShot(true); m_delay.setInterval(400);
    connect(&m_delay, &QTimer::timeout, this, &WindowsLockScreen::start);
    connect(&m_job, &QFutureWatcher<QString>::finished, this, [this] {
        if (!m_enabled) return;
        if (m_path != m_activePath) { start(); return; }
        m_status = m_job.result().isEmpty() ? "synced" : m_job.result();
        if (!m_job.result().isEmpty()) emit failed(m_job.result());
    });
}
WindowsLockScreen::~WindowsLockScreen() { m_job.waitForFinished(); }
void WindowsLockScreen::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) { m_delay.stop(); m_path.clear(); m_status = "disabled"; }
}
void WindowsLockScreen::request(const QString &path, bool video) {
    if (!m_enabled || path.isEmpty() || (m_path == path && m_status == "synced")) return;
    m_path = path; m_video = video; m_status = "pending"; m_delay.start();
}
void WindowsLockScreen::start() {
    if (!m_enabled || m_path.isEmpty() || m_job.isRunning()) return;
    m_activePath = m_path; m_status = "syncing";
    m_job.setFuture(QtConcurrent::run(sync, m_path, m_video, WindowsPlatform::displayPixelSize()));
}
