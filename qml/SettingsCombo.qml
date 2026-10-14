// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitWidth: 220
    implicitHeight: 40
    leftPadding: 13
    rightPadding: 30
    hoverEnabled: true
    contentItem: Text {
        text: control.displayText
        color: "#e7edf4"
        font.pixelSize: 13
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 10
        color: control.hovered ? "#2b3743" : "#1a222c"
        border.color: control.activeFocus || control.popup.visible ? "#a4becf" : "#41505e"
        Behavior on color {
            ColorAnimation {
                duration: 130
            }
        }
    }
    indicator: Text {
        x: control.width - 25
        y: (control.height - height) / 2
        text: "⌄"
        color: "#a8bbcc"
        font.pixelSize: 16
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation {
            NumberAnimation {
                duration: 160
            }
        }
    }
    delegate: ItemDelegate {
        required property int index
        required property var modelData
        width: control.width - 8
        height: 38
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: modelData
            color: "#e7edf4"
            font.pixelSize: 13
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: parent.highlighted ? "#405668" : "transparent"
        }
    }
    popup: Popup {
        y: control.height + 6
        width: control.width
        implicitHeight: Math.min(240, list.contentHeight + 8)
        padding: 4
        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: "#222e39"
            radius: 12
            border.color: "#506373"
        }
        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 120
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: 90
            }
        }
    }
}
