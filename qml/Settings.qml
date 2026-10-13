// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: panel
    objectName: "settingsPanel"
    width: 560
    height: 690
    radius: 20
    color: "#f521272f"
    border.color: "#657180"
    property var draft: ({})
    property bool recording: false
    property int languageRevision: 0
    signal finished
    function tr(k) {
        let r = languageRevision;
        return backend.text(k);
    }
    function openPanel() {
        draft = Object.assign({}, backend.settings);
        folder.text = draft.folder;
        language.currentIndex = Math.max(0, ["en", "uk", "ru"].indexOf(draft.language));
        display.currentIndex = Math.max(0, backend.screens.indexOf(draft.monitor));
        recording = false;
        forceActiveFocus();
    }
    function close() {
        recording = false;
        backend.captureShortcut(false);
    }
    function leave() {
        close();
        finished();
    }
    focus: true
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            leave();
            event.accepted = true;
        }
    }
    Connections {
        target: backend
        function onSettingsChanged() {
            panel.draft.manageBackground = backend.settings.manageBackground;
            manage.checked = backend.settings.manageBackground;
        }
        function onShortcutCaptured(codes, label) {
            if (codes) {
                panel.draft.shortcut = codes;
                panel.draft.shortcutLabel = label;
                shortcut.text = label;
            }
            panel.recording = false;
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: panel.forceActiveFocus()
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 26
        spacing: 14
        Text {
            text: panel.tr("settings")
            color: "#f3f5f8"
            font.pixelSize: 22
            font.weight: Font.DemiBold
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: "#44505c"
        }
        RowLayout {
            Text {
                text: panel.tr("language")
                color: "#d9e0e8"
                Layout.fillWidth: true
            }
            ComboBox {
                id: language
                palette.button: "#303b48"
                palette.buttonText: "#edf3fa"
                palette.base: "#151b22"
                palette.text: "#edf3fa"
                palette.highlight: "#516778"
                palette.highlightedText: "#ffffff"
                model: ["English", "Українська", "Русский"]
                Layout.preferredWidth: 220
                onActivated: panel.draft.language = ["en", "uk", "ru"][currentIndex]
            }
        }
        Text {
            text: panel.tr("folder")
            color: "#d9e0e8"
        }
        RowLayout {
            TextField {
                id: folder
                Layout.fillWidth: true
                color: "#e9eef5"
                selectByMouse: true
                placeholderText: "~/Pictures/Wallpapers"
                background: Rectangle {
                    radius: 8
                    color: "#151b22"
                    border.color: folder.activeFocus ? "#a6b9cc" : "#53606c"
                }
            }
            SoftButton {
                text: panel.tr("browse")
                onClicked: folderDialog.open()
            }
        }
        RowLayout {
            Text {
                text: panel.tr("shortcut")
                color: "#d9e0e8"
                Layout.fillWidth: true
            }
            SoftButton {
                id: shortcut
                Layout.preferredWidth: 220
                text: panel.recording ? panel.tr("record") : (panel.draft.shortcutLabel || "Alt+T")
                onClicked: {
                    panel.recording = true;
                    backend.captureShortcut(true);
                    panel.forceActiveFocus();
                }
            }
        }
        RowLayout {
            Text {
                text: panel.tr("monitor")
                color: "#d9e0e8"
                Layout.fillWidth: true
            }
            ComboBox {
                id: display
                palette.button: "#303b48"
                palette.buttonText: "#edf3fa"
                palette.base: "#151b22"
                palette.text: "#edf3fa"
                palette.highlight: "#516778"
                palette.highlightedText: "#ffffff"
                model: backend.screens
                Layout.preferredWidth: 220
                onActivated: panel.draft.monitor = backend.screens[currentIndex]
            }
        }
        RowLayout {
            Text {
                text: panel.tr("renderer")
                color: "#d9e0e8"
                Layout.fillWidth: true
            }
            ComboBox {
                model: ["Hyprwallselector", "end-4 (ii)"]
                currentIndex: panel.draft.renderer === "end4" ? 1 : 0
                Layout.preferredWidth: 220
                palette.button: "#303b48"
                palette.buttonText: "#edf3fa"
                onActivated: panel.draft.renderer = currentIndex === 1 ? "end4" : "native"
            }
        }
        CheckBox {
            id: manage
            text: panel.tr("background")
            checked: panel.draft.manageBackground || false
            onToggled: panel.draft.manageBackground = checked
            palette.windowText: "#e2e8ef"
        }
        SoftButton {
            text: panel.tr("dmsMode")
            visible: panel.draft.renderer !== "end4"
            onClicked: backend.setupDms()
        }
        Text {
            text: panel.tr("backgroundHint")
            color: "#aab7c5"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: 11
        }
        CheckBox {
            text: panel.tr("autostart")
            checked: panel.draft.autostart || false
            onToggled: panel.draft.autostart = checked
            palette.windowText: "#e2e8ef"
        }
        CheckBox {
            text: panel.tr("battery")
            checked: panel.draft.pauseBattery || false
            onToggled: panel.draft.pauseBattery = checked
            palette.windowText: "#e2e8ef"
        }
        CheckBox {
            text: panel.tr("fullscreen")
            checked: panel.draft.pauseFullscreen || false
            onToggled: panel.draft.pauseFullscreen = checked
            palette.windowText: "#e2e8ef"
        }
        Text {
            text: backend.status
            color: "#f0b9a8"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: 11
            visible: text.length > 0
        }
        Item {
            Layout.fillHeight: true
        }
        RowLayout {
            SoftButton {
                text: panel.tr("quit")
                onClicked: backend.quit()
            }
            Item {
                Layout.fillWidth: true
            }
            SoftButton {
                objectName: "cancelSettings"
                text: panel.tr("cancel")
                onClicked: panel.leave()
            }
            SoftButton {
                objectName: "saveSettings"
                text: panel.tr("save")
                primary: true
                onClicked: {
                    panel.draft.folder = folder.text;
                    if (backend.saveSettings(panel.draft)) {
                        panel.languageRevision++;
                        panel.leave();
                    }
                }
            }
        }
    }
    FolderDialog {
        id: folderDialog
        title: panel.tr("folder")
        onAccepted: folder.text = backend.filePath(selectedFolder.toString())
    }
}
