// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property color tint: "#bac9d9"
    implicitHeight: 38
    implicitWidth: Math.max(40, contentItem.implicitWidth + 28)
    hoverEnabled: true
    font.pixelSize: 13
    font.weight: Font.Medium
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? (control.primary ? "#12191f" : "#e5e9ef") : "#68727c"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 11
        color: control.primary ? (control.down ? "#95a7b8" : control.tint) : (control.down ? "#404a56" : control.hovered ? "#35404c" : "#e0232930")
        border.width: 1
        border.color: control.primary ? "transparent" : (control.hovered ? "#8798aa" : "#48515a")
        Behavior on color {
            ColorAnimation {
                duration: 120
            }
        }
    }
}
