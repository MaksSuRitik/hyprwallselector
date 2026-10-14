// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window
import Hyprwallselector 1.0

Item {
    id: root
    property Item activeDeck: null
    property Item pendingDeck: null
    property int generation: 0
    property bool paused: backend.pausedFor(outputName)
    Connections {
        target: backend
        function onPausedChanged() {
            root.paused = backend.pausedFor(outputName);
        }
    }
    function failed(item, message) {
        if (item !== pendingDeck)
            return;
        fade.stop();
        pendingDeck = null;
        item.destroy();
        backend.reportError(message);
    }
    function request(path, video) {
        if (pendingDeck && pendingDeck.path === path && pendingDeck.video === video)
            return;
        if (!pendingDeck && activeDeck && activeDeck.path === path && activeDeck.video === video) {
            backend.wallpaperPrepared(outputName, path);
            backend.wallpaperReady(outputName, path);
            return;
        }
        generation++;
        fade.stop();
        if (pendingDeck) {
            pendingDeck.destroy();
            pendingDeck = null;
        }
        pendingDeck = deck.createObject(root, {
            "path": path,
            "video": video,
            "generation": generation
        });
    }
    function prepared(item) {
        if (item !== pendingDeck || item.generation !== generation || item.frameReady)
            return;
        item.frameReady = true;
        // Start the shell's palette work alongside the crossfade, after loading succeeds.
        backend.wallpaperPrepared(outputName, item.path);
        fade.target = item;
        fade.start();
    }
    Connections {
        target: backend
        function onWallpaperRequested(screen, path, video) {
            if (screen === "ALL" || screen === outputName)
                root.request(path, video);
        }
    }
    Component {
        id: deck
        Item {
            id: page
            anchors.fill: parent
            property string path: ""
            property bool video: false
            property int generation: 0
            property bool frameReady: false
            opacity: 0.001
            Loader {
                anchors.fill: parent
                sourceComponent: page.video ? videoContent : imageContent
            }
            Component {
                id: imageContent
                Image {
                    source: backend.fileUrl(page.path)
                    fillMode: backend.settings.fit === "contain" ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                    asynchronous: true
                    // Decode for physical output pixels, not the full 4K/8K source.
                    // Wallpaper decks already own the current/transition images;
                    // retaining them again in Qt's global cache wastes memory.
                    sourceSize: Qt.size(Math.ceil(width * Screen.devicePixelRatio), Math.ceil(height * Screen.devicePixelRatio))
                    cache: false
                    onStatusChanged: {
                        if (status === Image.Ready)
                            Qt.callLater(() => {
                                if (root && page)
                                    root.prepared(page);
                            });
                        else if (status === Image.Error)
                            Qt.callLater(() => {
                                if (root && page)
                                    root.failed(page, "Cannot load: " + page.path);
                            });
                    }
                }
            }
            Component {
                id: videoContent
                VideoSurface {
                    source: page.path
                    paused: ready && root.paused
                    onReadyChanged: {
                        if (ready)
                            Qt.callLater(() => {
                                if (root && page)
                                    root.prepared(page);
                            });
                    }
                    onFailed: message => root.failed(page, message)
                }
            }
        }
    }
    NumberAnimation {
        id: fade
        property: "opacity"
        from: 0
        to: 1
        duration: 450
        easing.type: Easing.InOutCubic
        onFinished: {
            if (root.activeDeck)
                root.activeDeck.destroy();
            root.activeDeck = root.pendingDeck;
            root.pendingDeck = null;
            if (root.activeDeck)
                backend.wallpaperReady(outputName, root.activeDeck.path);
        }
    }
}
