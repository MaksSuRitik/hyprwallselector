// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Hyprwallselector 1.0

Item {
    id: card
    property var entry: ({})
    property bool focused: false
    property bool current: false
    property bool previewing: false
    property int distance: 0
    property real hover: mouse.containsMouse ? 1 : 0
    property real emphasis: focused ? 1 : 0
    property real skew: Math.min(42, width * 0.18)
    property string videoLabel: "VIDEO"
    property string currentLabel: "Current"
    signal chosen
    Behavior on hover {
        NumberAnimation {
            duration: 140
        }
    }
    Behavior on emphasis {
        NumberAnimation {
            duration: 230
            easing.type: Easing.OutCubic
        }
    }
    Item {
        id: content
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            color: "#252c35"
        }
        Image {
            anchors.fill: parent
            source: card.entry.thumb || ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
        }
        Loader {
            anchors.fill: parent
            active: card.previewing && card.focused && card.entry.video
            sourceComponent: VideoSurface {
                source: card.entry.url || ""
                opacity: ready ? 1 : 0.001
                Behavior on opacity {
                    NumberAnimation {
                        duration: backend.windows ? 60 : 180
                    }
                }
                onFailed: message => backend.reportError(message)
            }
        }
    }
    ShaderEffectSource {
        id: texture
        sourceItem: content
        hideSource: true
        live: true
        visible: false
    }
    ShaderEffect {
        anchors.fill: parent
        property variant source: texture
        property vector2d itemSize: Qt.vector2d(width, height)
        property real skew: card.skew
        property real dimming: card.focused ? 0 : Math.min(0.55, 0.16 + Math.abs(card.distance) * 0.065) - card.hover * 0.14
        property real focused: Math.max(card.emphasis, card.hover * 0.65)
        property color accent: "#b6c7d7"
        fragmentShader: "qrc:/shaders/card.frag.qsb"
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        function inside(x, y) {
            return x >= card.skew * (1 - y / height) && x <= width - card.skew * y / height;
        }
        onPressed: mouse => {
            if (!inside(mouse.x, mouse.y))
                mouse.accepted = false;
        }
        onClicked: card.chosen()
    }
    Rectangle {
        x: card.skew + 15
        y: 14
        width: badge.implicitWidth + 18
        height: 25
        radius: 8
        color: "#d11a2027"
        border.color: "#576678"
        visible: card.entry.video || false
        Text {
            id: badge
            anchors.centerIn: parent
            text: "▷" + ((card.focused || mouse.containsMouse) ? "  " + card.videoLabel : "")
            color: "#eef3f8"
            font.pixelSize: 10
            font.weight: Font.DemiBold
            font.letterSpacing: 0.6
        }
    }
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: 17
        y: 14
        width: tick.implicitWidth + 18
        height: 25
        radius: 8
        color: "#df23362f"
        visible: card.current
        Text {
            id: tick
            anchors.centerIn: parent
            text: "✓" + (card.focused ? "  " + card.currentLabel : "")
            color: "#bfe8cf"
            font.pixelSize: 11
        }
    }
    Text {
        x: 15
        y: parent.height - 43
        width: parent.width - card.skew - 28
        text: card.entry.title || ""
        color: "#f2f5fa"
        font.pixelSize: 11
        font.weight: Font.Medium
        elide: Text.ElideRight
        opacity: card.focused ? 0 : card.hover
    }
}
