// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    objectName: "settingsPanel"
    width: 760
    height: 600
    radius: 24
    color: "#fa171e27"
    border.color: "#465767"
    property var draft: ({})
    property bool recording: false
    property bool closing: false
    property int tab: 0
    property int languageRevision: 0
    signal finished
    transform: Translate {
        id: lift
        y: 0
    }
    function tr(k) {
        let r = languageRevision;
        return backend.text(k);
    }
    function setDraft(key, value) {
        let next = Object.assign({}, draft);
        next[key] = value;
        draft = next;
    }
    function openPanel() {
        exitAnimation.stop();
        closing = false;
        draft = Object.assign({}, backend.settings);
        folder.text = draft.folder;
        language.currentIndex = Math.max(0, ["en", "uk", "ru"].indexOf(draft.language));
        display.currentIndex = Math.max(0, backend.screens.indexOf(draft.monitor));
        recording = false;
        forceActiveFocus();
        enterAnimation.restart();
    }
    function close() {
        enterAnimation.stop();
        exitAnimation.stop();
        recording = false;
        closing = false;
        backend.captureShortcut(false);
        language.popup.close();
        display.popup.close();
        renderer.popup.close();
        folderPicker.close();
    }
    function leave() {
        if (closing)
            return;
        recording = false;
        backend.captureShortcut(false);
        enterAnimation.stop();
        closing = true;
        exitAnimation.start();
    }
    enabled: !closing
    focus: true
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            if (folderPicker.visible)
                folderPicker.close();
            else
                leave();
            event.accepted = true;
        }
    }
    ParallelAnimation {
        id: enterAnimation
        NumberAnimation {
            target: panel
            property: "opacity"
            from: 0
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: lift
            property: "y"
            from: 16
            to: 0
            duration: 240
            easing.type: Easing.OutCubic
        }
    }
    SequentialAnimation {
        id: exitAnimation
        ParallelAnimation {
            NumberAnimation {
                target: panel
                property: "opacity"
                to: 0
                duration: 140
            }
            NumberAnimation {
                target: lift
                property: "y"
                to: 10
                duration: 140
                easing.type: Easing.InCubic
            }
        }
        ScriptAction {
            script: panel.finished()
        }
    }
    Connections {
        target: backend
        function onSettingsChanged() {
            panel.languageRevision++;
            panel.setDraft("manageBackground", backend.settings.manageBackground);
        }
        function onShortcutCaptured(codes, label) {
            if (codes) {
                panel.setDraft("shortcut", codes);
                panel.setDraft("shortcutLabel", label);
            }
            panel.recording = false;
        }
    }
    MouseArea {
        anchors.fill: parent
        onClicked: panel.forceActiveFocus()
    }
    // A small slanted accent echoes the carousel without another full-screen layer.
    Rectangle {
        x: 28
        y: 27
        width: 5
        height: 30
        radius: 2
        rotation: 17
        color: "#aac6d6"
    }
    Text {
        x: 46
        y: 22
        objectName: "settingsTitle"
        text: panel.tr("settings")
        color: "#f1f5f9"
        font.pixelSize: 23
        font.weight: Font.DemiBold
    }
    Text {
        x: 47
        y: 53
        text: "HYPRWALLSELECTOR  /  " + panel.tr("settingsHint")
        color: "#8fa4b6"
        font.pixelSize: 10
        font.letterSpacing: 0.7
    }
    SoftButton {
        anchors.right: parent.right
        anchors.rightMargin: 22
        y: 23
        text: "×"
        font.pixelSize: 22
        Accessible.name: panel.tr("close")
        onClicked: panel.leave()
    }
    Rectangle {
        x: 22
        y: 90
        width: parent.width - 44
        height: 1
        color: "#34414d"
    }
    Rectangle {
        x: 16
        y: 108
        width: 173
        height: 404
        radius: 16
        color: "#111820"
        Rectangle {
            x: 8
            width: parent.width - 16
            height: 48
            radius: 11
            y: 10 + panel.tab * 58
            color: "#2d4050"
            Behavior on y {
                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }
        }
        Column {
            x: 8
            y: 10
            spacing: 10
            Repeater {
                model: ["general", "playback", "integration"]
                delegate: Button {
                    required property int index
                    required property string modelData
                    objectName: "settingsTab" + index
                    width: 157
                    height: 48
                    hoverEnabled: true
                    contentItem: Text {
                        text: panel.tr(modelData)
                        color: panel.tab === index ? "#edf6fd" : "#a4b2bf"
                        font.pixelSize: 12
                        font.weight: panel.tab === index ? Font.DemiBold : Font.Normal
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                    leftPadding: 15
                    rightPadding: 10
                    background: Rectangle {
                        color: parent.hovered && panel.tab !== index ? "#202b35" : "transparent"
                        radius: 11
                    }
                    onClicked: {
                        if (panel.tab === index)
                            return;
                        panel.tab = index;
                        pageReveal.restart();
                    }
                }
            }
        }
        Text {
            x: 16
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 17
            width: parent.width - 32
            text: panel.tr("settingsSaveHint")
            wrapMode: Text.WordWrap
            lineHeight: 1.6
            color: "#6e8496"
            font.pixelSize: 10
        }
    }
    Item {
        id: body
        x: 211
        y: 112
        width: parent.width - x - 28
        height: 400
        clip: true
        ParallelAnimation {
            id: pageReveal
            NumberAnimation {
                target: pages
                property: "opacity"
                from: 0
                to: 1
                duration: 180
            }
            NumberAnimation {
                target: slide
                property: "y"
                from: 8
                to: 0
                duration: 220
                easing.type: Easing.OutCubic
            }
        }
        StackLayout {
            id: pages
            anchors.fill: parent
            currentIndex: panel.tab
            transform: Translate {
                id: slide
            }
            ScrollView {
                id: generalPage
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: generalPage.availableWidth
                    spacing: 14
                    Text {
                        text: panel.tr("generalHint")
                        color: "#a3b6c7"
                        font.pixelSize: 12
                        Layout.bottomMargin: 4
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: panel.tr("language")
                            color: "#e2eaf2"
                            font.pixelSize: 13
                            Layout.fillWidth: true
                        }
                        SettingsCombo {
                            id: language
                            objectName: "languageSetting"
                            model: ["English", "Українська", "Русский"]
                            onActivated: panel.setDraft("language", ["en", "uk", "ru"][currentIndex])
                        }
                    }
                    Text {
                        text: panel.tr("folder")
                        color: "#e2eaf2"
                        font.pixelSize: 13
                        Layout.topMargin: 2
                    }
                    RowLayout {
                        TextField {
                            id: folder
                            objectName: "wallpaperFolder"
                            Layout.fillWidth: true
                            implicitHeight: 42
                            color: "#e9eef5"
                            selectByMouse: true
                            font.pixelSize: 12
                            leftPadding: 12
                            rightPadding: 12
                            background: Rectangle {
                                radius: 10
                                color: "#111921"
                                border.color: folder.activeFocus ? "#aac6d6" : "#41505e"
                            }
                        }
                        SoftButton {
                            text: panel.tr("browse")
                            objectName: "browseFolder"
                            onClicked: folderPicker.choose(folder.text)
                        }
                    }
                    RowLayout {
                        Layout.topMargin: 6
                        Text {
                            text: panel.tr("shortcut")
                            color: "#e2eaf2"
                            font.pixelSize: 13
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
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: "#34414d"
                        Layout.topMargin: 6
                    }
                    SettingsToggle {
                        Layout.fillWidth: true
                        text: panel.tr("autostart")
                        checked: panel.draft.autostart || false
                        onToggled: panel.setDraft("autostart", checked)
                    }
                    SettingsToggle {
                        Layout.fillWidth: true
                        visible: backend.windows
                        objectName: "highPrioritySetting"
                        text: panel.tr("highPriority")
                        detail: panel.tr("priorityHint")
                        checked: panel.draft.highPriority || false
                        onToggled: panel.setDraft("highPriority", checked)
                    }
                    }
            }
            ColumnLayout {
                spacing: 13
                Text {
                    text: panel.tr("playbackHint")
                    color: "#a3b6c7"
                    font.pixelSize: 12
                    Layout.bottomMargin: 5
                }
                RowLayout {
                    Text {
                        text: panel.tr("monitor")
                        color: "#e2eaf2"
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                    SettingsCombo {
                        id: display
                        model: backend.screens.map(s => s === "ALL" ? panel.tr("all") : s)
                        onActivated: panel.setDraft("monitor", backend.screens[currentIndex])
                    }
                }
                Text {
                    text: panel.tr("energy")
                    color: "#d2e4f1"
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    Layout.topMargin: 20
                }
                Text {
                    text: panel.tr("pauseHint")
                    color: "#95a5b5"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                SettingsToggle {
                    Layout.fillWidth: true
                    text: panel.tr("battery")
                    checked: panel.draft.pauseBattery || false
                    enabled: panel.draft.renderer !== "end4"
                    onToggled: panel.setDraft("pauseBattery", checked)
                }
                SettingsToggle {
                    Layout.fillWidth: true
                    text: panel.tr("fullscreen")
                    checked: panel.draft.pauseFullscreen || false
                    enabled: panel.draft.renderer !== "end4"
                    onToggled: panel.setDraft("pauseFullscreen", checked)
                }
                Item {
                    Layout.fillHeight: true
                }
            }
            ColumnLayout {
                spacing: 10
                Text {
                    text: panel.tr("integrationHint")
                    color: "#a3b6c7"
                    font.pixelSize: 12
                    Layout.bottomMargin: 6
                }
                RowLayout {
                    Text {
                        text: panel.tr("renderer")
                        color: "#e2eaf2"
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                    SettingsCombo {
                        id: renderer
                        model: backend.windows ? ["Hyprwallselector"] : ["Hyprwallselector", "end-4 (ii)"]
                        currentIndex: panel.draft.renderer === "end4" ? 1 : 0
                        onActivated: panel.setDraft("renderer", currentIndex === 1 ? "end4" : "native")
                    }
                }
                SettingsToggle {
                    Layout.fillWidth: true
                    text: panel.tr("background")
                    checked: panel.draft.manageBackground || false
                    onToggled: panel.setDraft("manageBackground", checked)
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: "#34414d"
                }
                SettingsToggle {
                    objectName: "dmsColorsSetting"
                    visible: !backend.windows
                    Layout.fillWidth: true
                    text: panel.tr("syncDmsColors")
                    detail: panel.tr("paletteHint")
                    checked: panel.draft.syncDmsColors || false
                    enabled: panel.draft.renderer !== "end4"
                    onToggled: panel.setDraft("syncDmsColors", checked)
                }
                SettingsToggle {
                    Layout.fillWidth: true
                    visible: backend.windows
                    objectName: "lockScreenSetting"
                    text: panel.tr("syncLockScreen")
                    detail: panel.tr("lockScreenHint")
                    checked: panel.draft.syncLockScreen || false
                    onToggled: panel.setDraft("syncLockScreen", checked)
                }
                Text {
                    Layout.fillWidth: true
                    text: panel.tr("dmsHint")
                    color: "#95a5b5"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                    visible: !backend.windows && panel.draft.renderer !== "end4"
                }
                SoftButton {
                    text: panel.tr("dmsMode")
                    visible: !backend.windows && panel.draft.renderer !== "end4"
                    onClicked: backend.setupDms()
                }
                Text {
                    text: panel.tr("backgroundHint")
                    color: "#95a5b5"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    font.pixelSize: 11
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
    Text {
        x: 24
        y: 517
        width: parent.width - 48
        height: 25
        text: backend.status
        color: "#efba9e"
        font.pixelSize: 10
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        HoverHandler {
            id: statusHover
        }
        ToolTip.visible: statusHover.hovered && text.length > 0
        ToolTip.text: text
    }
    Rectangle {
        x: 22
        y: 544
        width: parent.width - 44
        height: 1
        color: "#34414d"
    }
    RowLayout {
        x: 22
        y: 554
        width: parent.width - 44
        spacing: 10
        SoftButton {
            text: panel.tr("quit")
            font.pixelSize: 11
            onClicked: backend.quit()
        }
        SoftButton {
            visible: backend.windows
            text: panel.tr("checkUpdates")
            font.pixelSize: 11
            onClicked: backend.checkUpdates()
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
            tint: "#b5cedc"
            onClicked: {
                panel.setDraft("folder", folder.text);
                if (backend.saveSettings(panel.draft)) {
                    panel.languageRevision++;
                    panel.leave();
                }
            }
        }
    }
    FolderPicker {
        id: folderPicker
        parent: panel
        scale: panel.scale
        translate: key => panel.tr(key)
        onChosen: path => folder.text = path
        onClosed: if (panel.visible && !panel.closing)
            folder.forceActiveFocus()
    }
}
