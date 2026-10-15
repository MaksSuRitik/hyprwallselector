// SPDX-License-Identifier: GPL-3.0-or-later
#include "windowsstartup.h"
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QXmlStreamWriter>
#include <windows.h>
#include <sddl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
const auto runKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct BString {
    BSTR value;
    explicit BString(const QString &s) : value(SysAllocString(reinterpret_cast<const wchar_t *>(s.utf16()))) {}
    ~BString() { SysFreeString(value); }
    operator BSTR() const { return value; }
};
QString userSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<char> buffer(bytes);
    bool ok = GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes);
    CloseHandle(token);
    LPWSTR sid = nullptr;
    if (!ok || !ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &sid)) return {};
    const auto result = QString::fromWCharArray(sid);
    LocalFree(sid);
    return result;
}
struct Scheduler {
    Apartment apartment;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    QString sid = userSid();
    QString name = "Hyprwallselector-" + sid;
    Scheduler() {
        VARIANT empty{};
        if (sid.isEmpty() || FAILED(CoCreateInstance(__uuidof(TaskScheduler), nullptr, CLSCTX_INPROC_SERVER,
                                                     IID_PPV_ARGS(&service)))) return;
        if (FAILED(service->Connect(empty, empty, empty, empty))) return;
        service->GetFolder(BString("\\"), &folder);
    }
    bool active() const {
        ComPtr<IRegisteredTask> task;
        VARIANT_BOOL on = VARIANT_FALSE;
        return folder && SUCCEEDED(folder->GetTask(BString(name), &task)) &&
               SUCCEEDED(task->get_Enabled(&on)) && on == VARIANT_TRUE;
    }
};
QString taskXml(const QString &sid) {
    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartDocument();
    w.writeStartElement("Task");
    w.writeDefaultNamespace("http://schemas.microsoft.com/windows/2004/02/mit/task");
    w.writeAttribute("version", "1.2");
    w.writeStartElement("RegistrationInfo");
    w.writeTextElement("Description", "Restore Hyprwallselector wallpapers when this user signs in.");
    w.writeEndElement();
    w.writeStartElement("Triggers"); w.writeStartElement("LogonTrigger");
    w.writeTextElement("Enabled", "true"); w.writeTextElement("UserId", sid);
    w.writeEndElement(); w.writeEndElement();
    w.writeStartElement("Principals"); w.writeStartElement("Principal"); w.writeAttribute("id", "User");
    w.writeTextElement("UserId", sid);
    w.writeTextElement("LogonType", "InteractiveToken");
    w.writeTextElement("RunLevel", "LeastPrivilege");
    w.writeEndElement(); w.writeEndElement();
    w.writeStartElement("Settings");
    w.writeTextElement("MultipleInstancesPolicy", "IgnoreNew");
    w.writeTextElement("DisallowStartIfOnBatteries", "false");
    w.writeTextElement("StopIfGoingOnBatteries", "false");
    w.writeTextElement("ExecutionTimeLimit", "PT0S");
    w.writeTextElement("Priority", "6");
    w.writeEndElement();
    w.writeStartElement("Actions"); w.writeAttribute("Context", "User"); w.writeStartElement("Exec");
    w.writeTextElement("Command", QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    w.writeTextElement("Arguments", "--daemon");
    w.writeTextElement("WorkingDirectory", QDir::toNativeSeparators(QCoreApplication::applicationDirPath()));
    w.writeEndElement(); w.writeEndElement(); w.writeEndElement(); w.writeEndDocument();
    return xml;
}
}
bool WindowsStartup::enabled() {
    return Scheduler().active() || QSettings(runKey, QSettings::NativeFormat).contains("Hyprwallselector");
}
QString WindowsStartup::method() {
    if (Scheduler().active()) return "logon-task";
    return QSettings(runKey, QSettings::NativeFormat).contains("Hyprwallselector") ? "run-key" : "disabled";
}
bool WindowsStartup::setEnabled(bool enabled) {
    Scheduler scheduler;
    QSettings registry(runKey, QSettings::NativeFormat);
    if (!enabled) {
        if (scheduler.folder) {
            const HRESULT hr = scheduler.folder->DeleteTask(BString(scheduler.name), 0);
            if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return false;
        } else return false;
        registry.remove("Hyprwallselector"); registry.sync();
        return registry.status() == QSettings::NoError;
    }
    ComPtr<IRegisteredTask> registered;
    VARIANT empty{};
    if (scheduler.folder && SUCCEEDED(scheduler.folder->RegisterTask(BString(scheduler.name),
        BString(taskXml(scheduler.sid)), TASK_CREATE_OR_UPDATE, empty, empty,
        TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered))) {
        // Migrate only after registration succeeds, keeping exactly one launcher.
        registry.remove("Hyprwallselector"); registry.sync();
        return registry.status() == QSettings::NoError;
    }
    // Managed PCs can deny Task Scheduler access. Keep ordinary autostart working.
    registry.setValue("Hyprwallselector", '"' + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" --daemon");
    registry.sync();
    return registry.status() == QSettings::NoError;
}
