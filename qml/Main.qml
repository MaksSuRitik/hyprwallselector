// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Item {
    id: root
    width: 1920
    height: 1080
    focus: true
    property bool settingsOpen: false
    property bool previewing: false
    property int selected: 0
    property var entries: backend.items
    property var entry: entries.length ? entries[Math.min(selected, entries.length - 1)] : ({})
    property real uiScale: Math.min(1.0, width / 1000, height / 740)
    property int languageRevision: 0
    opacity: backend.visible ? 1 : 0
    Behavior on opacity {
        NumberAnimation {
            duration: 140
            easing.type: Easing.OutCubic
        }
    }
    function tr(key) {
        let dummy = languageRevision;
        return backend.text(key);
    }
    function step(delta) {
        if (!entries.length || settingsOpen)
            return;
        previewing = false;
        selected = (selected + delta + entries.length) % entries.length;
    }
    function openSettings() {
        previewing = false;
        settingsOpen = true;
        settings.openPanel();
    }
    function apply() {
        previewing = false;
        backend.apply(selected);
    }
    Keys.onPressed: event => {
        if (settingsOpen)
            return;
        if (event.key === Qt.Key_Escape) {
            backend.hide();
            event.accepted = true;
        } else if (event.key === Qt.Key_Left) {
            step(-1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Right) {
            step(1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
            apply();
            event.accepted = true;
        } else if (event.key === Qt.Key_P || event.text.toLowerCase() === "з") {
            if (entry.video)
                previewing = !previewing;
            event.accepted = true;
        }
    }
    Connections {
        target: backend
        function onItemsChanged() {
            let i = backend.items.findIndex(e => e.path === backend.currentPath);
            selected = i >= 0 ? i : Math.min(selected, Math.max(0, backend.items.length - 1));
        }
        function onSettingsChanged() {
            languageRevision++;
        }
        function onVisibleChanged() {
            if (!backend.visible) {
                previewing = false;
                settingsOpen = false;
                settings.close();
            } else
                root.forceActiveFocus();
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: !root.settingsOpen
        onClicked: backend.hide()
        onWheel: wheel => {
            root.step(wheel.angleDelta.y > 0 ? -1 : 1);
            wheel.accepted = true;
        }
    }
    Item {
        id: carousel
        enabled: !root.settingsOpen
        anchors.centerIn: parent
        width: root.width / root.uiScale
        height: 540
        scale: root.uiScale
        opacity: root.settingsOpen ? 0.24 : 1
        Behavior on opacity {
            NumberAnimation {
                duration: 170
            }
        }
        SoftButton {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 35
            text: root.tr("settings")
            onClicked: root.openSettings()
        }
        Repeater {
            model: root.entries
            delegate: Loader {
                id: slot
                required property int index
                required property var modelData
                property int dist: (index - root.selected + root.entries.length + Math.floor(root.entries.length / 2)) % root.entries.length - Math.floor(root.entries.length / 2)
                property int tier: Math.min(Math.abs(dist), 4)
                property bool motion: true
                property int oldDist: dist
                onDistChanged: {
                    motion = Math.abs(dist - oldDist) <= 2;
                    oldDist = dist;
                }
                active: backend.visible && Math.abs(dist) <= Math.ceil(carousel.width / 300) + 2
                visible: active
                width: dist === 0 ? 600 : 180
                height: dist === 0 ? 300 : [252, 228, 208, 192][Math.max(0, tier - 1)]
                x: dist === 0 ? (carousel.width - 600) / 2 : dist > 0 ? (carousel.width - 600) / 2 + 600 - 42 + 14 + (dist - 1) * (180 - 42 + 14) : (carousel.width - 600) / 2 + 42 - 14 - 180 - (Math.abs(dist) - 1) * (180 - 42 + 14)
                y: 240 - height / 2 + tier * 3
                z: 20 - Math.abs(dist)
                Behavior on x {
                    enabled: slot.motion
                    NumberAnimation {
                        duration: 290
                        easing.type: Easing.OutQuart
                    }
                }
                Behavior on y {
                    NumberAnimation {
                        duration: 290
                        easing.type: Easing.OutQuart
                    }
                }
                Behavior on width {
                    NumberAnimation {
                        duration: 290
                        easing.type: Easing.OutQuart
                    }
                }
                Behavior on height {
                    NumberAnimation {
                        duration: 290
                        easing.type: Easing.OutQuart
                    }
                }
                sourceComponent: Card {
                    entry: slot.modelData
                    focused: slot.dist === 0
                    distance: slot.dist
                    current: entry.path === backend.currentPath
                    previewing: root.previewing && focused && backend.visible
                    videoLabel: root.tr("video")
                    currentLabel: root.tr("current")
                    onChosen: {
                        root.selected = slot.index;
                        root.apply();
                    }
                }
            }
        }
        Rectangle {
            visible: root.entries.length === 0
            width: 560
            height: 140
            anchors.horizontalCenter: parent.horizontalCenter
            y: 170
            radius: 18
            color: "#ee222831"
            border.color: "#4c5664"
            Column {
                anchors.centerIn: parent
                spacing: 16
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: backend.busy ? root.tr("loading") : root.tr("empty")
                    color: "#e7ecf2"
                    font.pixelSize: 18
                }
                SoftButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.tr("settings")
                    onClicked: root.openSettings()
                }
            }
        }
        Rectangle {
            id: info
            visible: root.entries.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: 415
            width: 600
            height: 78
            radius: 14
            color: "#ed1e242c"
            border.color: "#535e6d"
            MouseArea {
                anchors.fill: parent
            }
            Text {
                x: 18
                y: 13
                width: parent.width - 115
                text: root.entry.title || ""
                elide: Text.ElideRight
                color: "#f2f5fa"
                font.pixelSize: 15
                font.weight: Font.Medium
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 18
                y: 15
                text: (root.selected + 1) + " / " + root.entries.length
                color: "#a2b3c4"
                font.pixelSize: 12
            }
            Text {
                x: 18
                y: 47
                text: root.tr(root.entry.video ? "video" : "image") + " · " + (root.entry.format || "")
                color: "#b0bfce"
                font.pixelSize: 10
                font.letterSpacing: 0.5
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 18
                y: 48
                text: root.entry.video ? "← →  ·  Enter  ·  Esc" : root.tr("keys")
                color: "#8c9ba9"
                font.pixelSize: 10
            }
            SoftButton {
                visible: root.entry.video || false
                x: 143
                y: 39
                height: 31
                text: root.previewing ? "Ⅱ  " + root.tr("stop") : "▷  " + root.tr("preview")
                onClicked: {
                    root.previewing = !root.previewing;
                    root.forceActiveFocus();
                }
            }
        }
        SoftButton {
            x: info.x - 49
            y: info.y + 18
            text: "‹"
            font.pixelSize: 24
            visible: info.visible
            onClicked: {
                root.step(-1);
                root.forceActiveFocus();
            }
        }
        SoftButton {
            x: info.x + info.width + 9
            y: info.y + 18
            text: "›"
            font.pixelSize: 24
            visible: info.visible
            onClicked: {
                root.step(1);
                root.forceActiveFocus();
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 508
            width: 720
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: backend.status
            color: "#f1b9a4"
            font.pixelSize: 12
        }
    }
    Settings {
        id: settings
        anchors.centerIn: parent
        scale: root.uiScale
        visible: root.settingsOpen
        onFinished: {
            root.settingsOpen = false;
            root.forceActiveFocus();
        }
    }
}
