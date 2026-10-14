// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Switch {
    id: control
    property string detail: ""
    implicitWidth: 430
    implicitHeight: Math.max(48, copy.implicitHeight + 16)
    hoverEnabled: true
    spacing: 16
    padding: 8
    leftPadding: 12
    rightPadding: 66
    background: Rectangle {
        radius: 12
        color: control.hovered ? "#27313c" : "transparent"
        Behavior on color {
            ColorAnimation {
                duration: 140
            }
        }
    }
    contentItem: Column {
        id: copy
        spacing: 5
        Text {
            width: parent.width
            text: control.text
            color: control.enabled ? "#e7edf4" : "#788491"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: control.detail
            color: "#95a5b5"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            lineHeight: 1.2
        }
    }
    indicator: Rectangle {
        x: control.width - width - 12
        y: (control.height - height) / 2
        width: 38
        height: 22
        radius: 11
        opacity: control.enabled ? 1 : 0.4
        color: control.checked ? "#aac6d6" : "#414d5a"
        Behavior on color {
            ColorAnimation {
                duration: 160
            }
        }
        Rectangle {
            width: 16
            height: 16
            radius: 8
            y: 3
            x: control.checked ? 19 : 3
            color: control.checked ? "#1a2731" : "#c3ced8"
            Behavior on x {
                NumberAnimation {
                    duration: 180
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
}
