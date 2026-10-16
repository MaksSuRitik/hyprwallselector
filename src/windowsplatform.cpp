// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsplatform.h"
#include "windowsstartup.h"
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QGuiApplication>
#include <QKeySequence>
#include <QQuickView>
#include <QScreen>
#include <QtGui/qscreen_platform.h>
#include <QSettings>
#include <QSysInfo>
#include <dwmapi.h>
#include <wtsapi32.h>
#include <psapi.h>
#include <vector>

namespace {
constexpr GUID displayState{0x6fe69556, 0x704a, 0x47a0, {0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
QRect rect(const RECT &r) { return {r.left, r.top, r.right-r.left, r.bottom-r.top}; }
}

MONITORINFOEXW WindowsPlatform::monitorInfo(QScreen *screen, DWORD *error) {
    if (error) *error = ERROR_INVALID_MONITOR_HANDLE;
    if (!screen) return {};
    // QScreen::name() is a presentation label, not a Win32 device identifier.
    // The native handle also avoids guessing with DPI-scaled Qt coordinates.
    auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
    if (!native || !native->handle()) return {};
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(native->handle(), &info)) {
        if (error) *error = GetLastError();
        return {};
    }
    if (error) *error = ERROR_SUCCESS;
    return info;
}

WindowsPlatform::WindowsPlatform(QObject *parent) : QObject(parent) {
    m_messages.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    m_messages.setTitle("Hyprwallselector event receiver");
    const HWND handle = reinterpret_cast<HWND>(m_messages.winId());
    qApp->installNativeEventFilter(this);
    WTSRegisterSessionNotification(handle, NOTIFY_FOR_THIS_SESSION);
    m_power = RegisterPowerSettingNotification(handle, &displayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    m_taskbarMessage = RegisterWindowMessageW(L"TaskbarCreated");
    DWORD bytes = 0;
    LPWSTR data = nullptr;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, &data, &bytes)) {
        const auto *info = reinterpret_cast<WTSINFOEXW *>(data);
        if (bytes >= sizeof(WTSINFOEXW) && info->Level == 1)
            m_locked = info->Data.WTSInfoExLevel1.SessionFlags == WTS_SESSIONSTATE_LOCK;
        WTSFreeMemory(data);
    }
    // Four cheap Win32 queries per second; no subprocesses or window-tree scans.
    m_poll.setInterval(500);
    m_poll.setTimerType(Qt::CoarseTimer);
    connect(&m_poll, &QTimer::timeout, this, &WindowsPlatform::poll);
    m_poll.start();
}
WindowsPlatform::~WindowsPlatform() {
    m_poll.stop();
    const HWND handle = reinterpret_cast<HWND>(m_messages.winId());
    if (m_hotkeyId) UnregisterHotKey(nullptr, m_hotkeyId);
    if (m_power) UnregisterPowerSettingNotification(m_power);
    WTSUnRegisterSessionNotification(handle);
    qApp->removeNativeEventFilter(this);
}
bool WindowsPlatform::shortcutParts(const QString &sequence, UINT &mods, UINT &key) {
    auto seq = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    if (seq.count() != 1) return false;
    auto combo = seq[0]; mods = MOD_NOREPEAT; key = 0;
    if (combo.keyboardModifiers() & Qt::AltModifier) mods |= MOD_ALT;
    if (combo.keyboardModifiers() & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (combo.keyboardModifiers() & Qt::ShiftModifier) mods |= MOD_SHIFT;
    if (combo.keyboardModifiers() & Qt::MetaModifier) mods |= MOD_WIN;
    if (!(mods & (MOD_ALT | MOD_CONTROL | MOD_WIN))) return false;
    int k = combo.key();
    if ((k >= Qt::Key_A && k <= Qt::Key_Z) || (k >= Qt::Key_0 && k <= Qt::Key_9)) key = k;
    else if (k >= Qt::Key_F1 && k <= Qt::Key_F24) key = VK_F1 + k - Qt::Key_F1;
    else if (k == Qt::Key_Space) key = VK_SPACE;
    else if (k == Qt::Key_Home) key = VK_HOME;
    else if (k == Qt::Key_End) key = VK_END;
    else if (k == Qt::Key_Left) key = VK_LEFT;
    else if (k == Qt::Key_Right) key = VK_RIGHT;
    else if (k == Qt::Key_Up) key = VK_UP;
    else if (k == Qt::Key_Down) key = VK_DOWN;
    else if (k == Qt::Key_PageUp) key = VK_PRIOR;
    else if (k == Qt::Key_PageDown) key = VK_NEXT;
    return key != 0 && key != VK_F12; // F12 is reserved by Windows debuggers.
}
bool WindowsPlatform::registerShortcut(const QString &sequence) {
    if (sequence == m_shortcut && m_hotkeyId) return true;
    UINT mods, key;
    if (!shortcutParts(sequence, mods, key)) return false;
    static int nextHotkeyId = 100;
    const int nextId = nextHotkeyId++;
    // Keep the working shortcut if a new combination belongs to another app.
    if (!RegisterHotKey(nullptr, nextId, mods, key)) return false;
    if (m_hotkeyId) UnregisterHotKey(nullptr, m_hotkeyId);
    m_hotkeyId = nextId; m_shortcut = sequence;
    return true;
}
bool WindowsPlatform::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
    const auto *msg = static_cast<MSG *>(message);
    // Thread messages go through Qt's Windows event dispatcher even while every
    // visible application window is hidden. No keyboard hook or polling needed.
    if (msg->message == WM_HOTKEY && int(msg->wParam) == m_hotkeyId) {
        ++m_hotkeyHits;
        emit activated();
        return true;
    }
    if (msg->hwnd != reinterpret_cast<HWND>(m_messages.winId())) return false;
    if (msg->message == WM_WTSSESSION_CHANGE) {
        if (msg->wParam == WTS_SESSION_LOCK || msg->wParam == WTS_CONSOLE_DISCONNECT || msg->wParam == WTS_REMOTE_DISCONNECT)
            m_locked = true;
        else if (msg->wParam == WTS_SESSION_UNLOCK || msg->wParam == WTS_CONSOLE_CONNECT || msg->wParam == WTS_REMOTE_CONNECT)
            m_locked = false;
        emit environmentChanged();
    } else if (msg->message == WM_POWERBROADCAST) {
        if (msg->wParam == PBT_APMSUSPEND) m_suspended = true;
        if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) m_suspended = false;
        if (msg->wParam == PBT_POWERSETTINGCHANGE) {
            auto *setting = reinterpret_cast<POWERBROADCAST_SETTING *>(msg->lParam);
            if (setting && setting->PowerSetting == displayState && setting->DataLength >= sizeof(DWORD))
                m_displayOff = *reinterpret_cast<DWORD *>(setting->Data) == 0;
        }
        emit environmentChanged();
    } else if (msg->message == m_taskbarMessage) {
        // DPI and taskbar changes can also broadcast TaskbarCreated.
        if (!desktopReady()) { m_host = nullptr; emit desktopLost(); }
    }
    return false;
}
bool WindowsPlatform::battery() const {
    SYSTEM_POWER_STATUS state{};
    return GetSystemPowerStatus(&state) && state.ACLineStatus == 0;
}
bool WindowsPlatform::covers(const QRect &w, const QRect &m) {
    return !w.isEmpty() && !m.isEmpty() && w.left() <= m.left()+1 && w.top() <= m.top()+1 &&
           w.right() >= m.right()-1 && w.bottom() >= m.bottom()-1;
}
bool WindowsPlatform::covered(QScreen *screen) const {
    HWND window = GetForegroundWindow();
    if (!window || window == m_progman || window == m_worker || !IsWindowVisible(window) || IsIconic(window)) return false;
    DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId()) return false;
    wchar_t klass[64]{}; GetClassNameW(window, klass, 64);
    if (!wcscmp(klass, L"WorkerW") || !wcscmp(klass, L"Progman") || !wcscmp(klass, L"Shell_TrayWnd")) return false;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return false;
    RECT bounds{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds))) && !GetWindowRect(window, &bounds)) return false;
    const auto monitor = monitorInfo(screen);
    return monitor.cbSize && covers(rect(bounds), rect(monitor.rcMonitor));
}
bool WindowsPlatform::findDesktop() {
    m_progman = FindWindowW(L"Progman", nullptr);
    if (!m_progman) { m_desktopError = "Explorer has not created Progman yet."; return false; }
    GetWindowThreadProcessId(m_progman, &m_explorerPid);
    if (locateDesktop()) return true;
    DWORD_PTR result = 0;
    SendMessageTimeoutW(m_progman, 0x052c, 0xD, 1, SMTO_ABORTIFHUNG, 750, &result);
    if (locateDesktop()) return true;
    // Older Explorer builds use the original message parameters. Do not force
    // this path when a valid raised desktop already exists.
    SendMessageTimeoutW(m_progman, 0x052c, 0, 0, SMTO_ABORTIFHUNG, 750, &result);
    if (locateDesktop()) return true;
    m_desktopError = "Explorer wallpaper layer is unavailable; waiting for WorkerW.";
    return false;
}
bool WindowsPlatform::locateDesktop() {
    m_icons = FindWindowExW(m_progman, nullptr, L"SHELLDLL_DefView", nullptr);
    m_worker = FindWindowExW(m_progman, nullptr, L"WorkerW", nullptr);
    m_raised = m_icons && m_worker && (GetWindowLongPtrW(m_progman, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP);
    if (m_raised) m_host = m_progman;
    else {
        m_host = nullptr;
        EnumWindows([](HWND top, LPARAM data)->BOOL {
            auto *self = reinterpret_cast<WindowsPlatform *>(data);
            DWORD owner = 0; GetWindowThreadProcessId(top, &owner);
            if (owner != self->m_explorerPid) return TRUE;
            if (HWND icons = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
                HWND worker = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
                DWORD pid = 0; GetWindowThreadProcessId(worker, &pid);
                if (worker && pid == self->m_explorerPid && IsWindowVisible(worker) &&
                    !FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr)) {
                    self->m_icons = icons; self->m_host = self->m_worker = worker;
                    return FALSE;
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(this));
    }
    if (!desktopReady()) return false;
    m_desktopError.clear();
    return true;
}
bool WindowsPlatform::desktopReady() const {
    DWORD pid = 0;
    return m_host && IsWindow(m_host) && IsWindow(m_icons) &&
           GetWindowThreadProcessId(m_host, &pid) && pid == m_explorerPid &&
           IsWindow(m_worker) && m_progman == FindWindowW(L"Progman", nullptr) &&
           (!m_raised || (GetParent(m_icons) == m_host && GetParent(m_worker) == m_host &&
                         (GetWindowLongPtrW(m_icons, GWL_EXSTYLE) & WS_EX_LAYERED)));
}
bool WindowsPlatform::attachWallpaper(QQuickView *view, QScreen *screen) {
    if (!desktopReady() && !findDesktop()) return false;
    auto *parent = QWindow::fromWinId(reinterpret_cast<WId>(m_host));
    if (!parent) return false;
    parent->QObject::setParent(this);
    connect(view, &QObject::destroyed, parent, &QObject::deleteLater);
    view->setParent(parent);
    view->setFlags(Qt::SubWindow | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    const HWND handle = reinterpret_cast<HWND>(view->winId());
    if (GetParent(handle) != m_host) {
        m_desktopError = QString("Explorer rejected wallpaper parenting (%1).").arg(GetLastError());
        view->hide(); return false;
    }
    SetWindowLongPtrW(handle, GWL_EXSTYLE, GetWindowLongPtrW(handle, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT);
    if (m_raised) {
        SetWindowLongPtrW(handle, GWL_EXSTYLE, GetWindowLongPtrW(handle, GWL_EXSTYLE) | WS_EX_LAYERED);
        if (!SetLayeredWindowAttributes(handle, 0, 255, LWA_ALPHA)) {
            m_desktopError = QString("Cannot enable the Explorer wallpaper layer (%1).").arg(GetLastError());
            view->hide(); return false;
        }
    }
    view->show();
    if (!positionWallpaper(view, screen)) { view->hide(); return false; }
    m_views << view;
    connect(screen, &QScreen::geometryChanged, view, [this, view, screen] { positionWallpaper(view, screen); });
    connect(screen, &QScreen::logicalDotsPerInchChanged, view, [this, view, screen] { positionWallpaper(view, screen); });
    return true;
}
void WindowsPlatform::maintainDesktopOrder() {
    if (m_raised && desktopReady() && GetWindow(m_host, GW_CHILD) &&
        GetWindow(m_worker, GW_HWNDNEXT))
        SetWindowPos(m_worker, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
bool WindowsPlatform::positionWallpaper(QQuickView *view, QScreen *screen) {
    if (!desktopReady()) { view->hide(); return false; }
    DWORD monitorError = ERROR_SUCCESS;
    const auto monitor = monitorInfo(screen, &monitorError);
    if (!monitor.cbSize) {
        m_desktopError = QString("Cannot query Windows monitor %1 (%2). Waiting for the display to reconnect.")
                             .arg(screen ? screen->name() : QStringLiteral("<disconnected>"))
                             .arg(monitorError);
        view->hide(); return false;
    }
    const HWND handle = reinterpret_cast<HWND>(view->winId());
    POINT point{monitor.rcMonitor.left, monitor.rcMonitor.top};
    MapWindowPoints(HWND_DESKTOP, m_host, &point, 1);
    maintainDesktopOrder();
    const bool positioned = SetWindowPos(handle, m_raised ? m_icons : HWND_BOTTOM, point.x, point.y,
                 monitor.rcMonitor.right-monitor.rcMonitor.left, monitor.rcMonitor.bottom-monitor.rcMonitor.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (!positioned) m_desktopError = QString("Cannot position wallpaper (%1).").arg(GetLastError());
    else m_desktopError.clear();
    return positioned;
}
int WindowsPlatform::attachedCount() const {
    int count = 0;
    for (const auto &view : m_views)
        if (view && IsWindow(reinterpret_cast<HWND>(view->winId())) && GetParent(reinterpret_cast<HWND>(view->winId())) == m_host) ++count;
    return count;
}
void WindowsPlatform::poll() {
    m_views.removeIf([](const auto &view) { return view.isNull(); });
    if (m_host && !desktopReady()) {
        for (auto &view : m_views) if (view) view->hide();
        m_host = nullptr;
        emit desktopLost();
    }
    maintainDesktopOrder();
    emit environmentChanged();
}
void WindowsPlatform::showSelector(QWindow *view) {
    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    view->setScreen(screen);
    view->setGeometry(screen->geometry());
    const HWND handle = reinterpret_cast<HWND>(view->winId());
    // The transparent overlay has its own content fade; a second DWM window
    // transition can expose a stale surface while Qt recreates its scene graph.
    const BOOL disableTransition = TRUE;
    DwmSetWindowAttribute(handle, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransition, sizeof(disableTransition));
    view->show();
    view->raise();
    view->requestActivate();
    // Autostart launchers can supply STARTF_USESHOWWINDOW/SW_HIDE. An explicit
    // second native show is needed for the first user-requested activation.
    if (!IsWindowVisible(handle)) ShowWindow(handle, SW_SHOWNORMAL);
    SetForegroundWindow(handle);
}
bool WindowsPlatform::autostartEnabled() {
    return WindowsStartup::enabled();
}
bool WindowsPlatform::setHighPriority(bool enabled) {
    return SetPriorityClass(GetCurrentProcess(), enabled ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
}
bool WindowsPlatform::setAutostart(bool enabled) {
    return WindowsStartup::setEnabled(enabled);
}
QVariantMap WindowsPlatform::diagnostic() const {
    auto window = [](HWND hwnd) {
        RECT r{}; GetWindowRect(hwnd, &r);
        wchar_t klass[128]{}; GetClassNameW(hwnd, klass, 128);
        return QVariantMap{{"handle", QString::number(quintptr(hwnd), 16)}, {"class", QString::fromWCharArray(klass)},
            {"style", QString::number(quintptr(GetWindowLongPtrW(hwnd, GWL_STYLE)), 16)},
            {"extendedStyle", QString::number(quintptr(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), 16)},
            {"visible", bool(IsWindowVisible(hwnd))}, {"x", int(r.left)}, {"y", int(r.top)},
            {"width", int(r.right-r.left)}, {"height", int(r.bottom-r.top)}};
    };
    QVariantList views;
    for (const auto &view : m_views) if (view) views << window(reinterpret_cast<HWND>(view->winId()));
    QVariantList screens;
    for (auto *screen : QGuiApplication::screens()) {
        DWORD error = ERROR_SUCCESS;
        const auto info = monitorInfo(screen, &error);
        const auto geometry = screen->geometry();
        auto *native = screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        screens << QVariantMap{{"name", screen->name()}, {"model", screen->model()},
            {"primary", screen == QGuiApplication::primaryScreen()},
            {"nativeHandle", QString::number(native ? quintptr(native->handle()) : 0, 16)},
            {"device", QString::fromWCharArray(info.szDevice)}, {"queryError", qulonglong(error)},
            {"devicePixelRatio", screen->devicePixelRatio()},
            {"qtX", geometry.x()}, {"qtY", geometry.y()},
            {"qtWidth", geometry.width()}, {"qtHeight", geometry.height()},
            {"nativeX", int(info.rcMonitor.left)}, {"nativeY", int(info.rcMonitor.top)},
            {"nativeWidth", int(info.rcMonitor.right-info.rcMonitor.left)},
            {"nativeHeight", int(info.rcMonitor.bottom-info.rcMonitor.top)}};
    }
    return {{"windows", QSysInfo::prettyProductName()}, {"kernel", QSysInfo::kernelVersion()},
        {"qtVersion", QString::fromLatin1(qVersion())}, {"screens", screens},
        {"layout", m_raised ? "raised" : "classic"}, {"ready", desktopReady()},
        {"error", m_desktopError}, {"autostart", WindowsStartup::method()},
        {"progman", window(m_progman)}, {"worker", window(m_worker)}, {"icons", window(m_icons)}, {"views", views}};
}
QVariantMap WindowsPlatform::memoryStats() {
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    QVariantMap result;
    const auto process = GetCurrentProcess();
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
        result["workingSetMiB"] = double(memory.WorkingSetSize) / 1048576.;
        result["privateCommitMiB"] = double(memory.PrivateUsage) / 1048576.;
    }
    // Count private resident pages, rather than confusing committed virtual/GPU
    // allocations with the private RAM column shown by Windows Task Manager.
    std::vector<ULONG_PTR> storage(32768);
    while (!QueryWorkingSet(process, storage.data(), DWORD(storage.size()*sizeof(ULONG_PTR)))) {
        if (GetLastError() != ERROR_BAD_LENGTH || storage.size() >= 4*1024*1024) return result;
        storage.resize(storage.size()*2);
    }
    const auto *workingSet = reinterpret_cast<const PSAPI_WORKING_SET_INFORMATION *>(storage.data());
    size_t privatePages = 0;
    for (ULONG_PTR i=0; i<workingSet->NumberOfEntries; ++i)
        if (!workingSet->WorkingSetInfo[i].Shared) ++privatePages;
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    result["privateWorkingSetMiB"] = double(privatePages)*info.dwPageSize/1048576.;
    return result;
}
QSize WindowsPlatform::displayPixelSize() {
    QSize target(1, 1);
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data)->BOOL {
        MONITORINFO info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            auto *size = reinterpret_cast<QSize *>(data);
            *size = size->expandedTo(QSize(info.rcMonitor.right-info.rcMonitor.left, info.rcMonitor.bottom-info.rcMonitor.top));
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&target));
    return target;
}
