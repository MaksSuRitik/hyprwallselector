// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.folderlistmodel

Popup {
    id: picker
    objectName: "folderPicker"
    // An Item popup stays on the selector's layer-shell surface. A native
    // portal/dialog window would be below it and unable to receive keyboard input.
    popupType: Popup.Item
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: parent
    width: 680
    height: 480
    padding: 22
    property var translate: key => backend.text(key)
    property string currentPath: ""
    property string error: ""
    property bool showHidden: false
    signal chosen(string path)
    function navigate(path) {
        const url = backend.directoryUrl(path);
        if (!url.toString()) {
            error = translate("invalidFolder");
            return false;
        }
        error = "";
        currentPath = backend.filePath(url.toString());
        address.text = currentPath;
        directories.folder = url;
        folders.currentIndex = -1;
        return true;
    }
    function choose(path) {
        if (!navigate(path))
            navigate("~");
        open();
    }
    function acceptFolder() {
        // Validate typed paths as well, even when Enter was not pressed.
        if (!navigate(address.text))
            return;
        chosen(currentPath);
        close();
    }
    onOpened: folders.forceActiveFocus()
    background: Rectangle {
        radius: 20
        color: "#1b2631"
        border.color: "#718a9c"
    }
    Overlay.modal: Rectangle {
        color: "#88080e15"
    }
    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 150
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            to: 0
            duration: 100
        }
    }
    FolderListModel {
        id: directories
        objectName: "folderPickerModel"
        showFiles: false
        showDirs: true
        showDotAndDotDot: false
        showHidden: picker.showHidden
        showOnlyReadable: true
        sortCaseSensitive: false
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Text {
                text: picker.translate("folder")
                color: "#f1f5f9"
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            SoftButton {
                text: "×"
                font.pixelSize: 20
                Accessible.name: picker.translate("close")
                onClicked: picker.close()
            }
        }
        RowLayout {
            SoftButton {
                objectName: "folderUp"
                text: "↑"
                font.pixelSize: 20
                Accessible.name: picker.translate("parentFolder")
                enabled: picker.currentPath !== "/"
                onClicked: picker.navigate(directories.parentFolder.toString())
            }
            SoftButton {
                text: picker.translate("home")
                onClicked: picker.navigate("~")
            }
            SoftButton {
                text: picker.translate("disks")
                onClicked: picker.navigate("/mnt")
            }
            Item {
                Layout.fillWidth: true
            }
            CheckBox {
                text: picker.translate("hiddenFolders")
                checked: picker.showHidden
                onToggled: picker.showHidden = checked
                palette.windowText: "#c9d8e4"
                palette.highlight: "#aac6d6"
                font.pixelSize: 11
            }
        }
        TextField {
            id: address
            objectName: "folderAddress"
            Layout.fillWidth: true
            implicitHeight: 40
            color: "#edf4fa"
            font.pixelSize: 12
            selectByMouse: true
            leftPadding: 12
            rightPadding: 12
            onAccepted: if (picker.navigate(text))
                folders.forceActiveFocus()
            background: Rectangle {
                radius: 9
                color: "#111a23"
                border.color: address.activeFocus ? "#aac6d6" : "#425568"
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: "#131d27"
            border.color: "#334756"
            ListView {
                id: folders
                objectName: "folderList"
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: directories
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: true
                keyNavigationWraps: false
                ScrollBar.vertical: ScrollBar {}
                Keys.onReturnPressed: if (currentIndex >= 0)
                    picker.navigate(directories.get(currentIndex, "fileUrl").toString())
                Keys.onEnterPressed: if (currentIndex >= 0)
                    picker.navigate(directories.get(currentIndex, "fileUrl").toString())
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Backspace || (event.key === Qt.Key_Up && event.modifiers & Qt.AltModifier)) {
                        if (picker.currentPath !== "/")
                            picker.navigate(directories.parentFolder.toString());
                        event.accepted = true;
                    }
                }
                delegate: ItemDelegate {
                    required property int index
                    required property string fileName
                    required property url fileUrl
                    width: ListView.view.width
                    height: 42
                    highlighted: ListView.isCurrentItem
                    hoverEnabled: true
                    leftPadding: 13
                    rightPadding: 13
                    contentItem: RowLayout {
                        Text {
                            text: "▱"
                            color: "#a9c5d7"
                            font.pixelSize: 23
                            Layout.preferredWidth: 26
                        }
                        Text {
                            text: fileName
                            color: "#e2edf5"
                            font.pixelSize: 13
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "›"
                            color: "#93acbf"
                            font.pixelSize: 19
                        }
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.highlighted ? "#354c60" : parent.hovered ? "#273746" : "transparent"
                    }
                    onClicked: {
                        picker.navigate(fileUrl.toString());
                        folders.forceActiveFocus();
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: directories.count === 0
                    text: directories.status === FolderListModel.Loading ? picker.translate("loading") : picker.translate("noSubfolders")
                    color: "#91a8bb"
                    font.pixelSize: 12
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: picker.error || picker.translate("chooseFolderHint")
            color: picker.error ? "#efba9e" : "#97aebe"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Item {
                Layout.fillWidth: true
            }
            SoftButton {
                objectName: "cancelFolder"
                text: picker.translate("cancel")
                onClicked: picker.close()
            }
            SoftButton {
                objectName: "acceptFolder"
                text: picker.translate("chooseFolder")
                primary: true
                onClicked: picker.acceptFolder()
            }
        }
    }
}
