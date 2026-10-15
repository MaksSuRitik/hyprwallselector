// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtMultimedia
import Hyprwallselector 1.0

Item {
    id: surface
    property string source: ""
    property bool paused: false
    property bool ready: player.item ? player.item.ready : false
    property bool fallback: false
    signal failed(string message)
    onSourceChanged: fallback = false
    Timer {
        // Some drivers accept a decoder but never deliver its first frame.
        // Move to Qt Multimedia instead of leaving a permanent poster.
        interval: 3000
        running: surface.source !== "" && !surface.ready && !surface.fallback
        onTriggered: {
            console.warn("Native decoder first frame timed out; trying compatible decoder");
            surface.fallback = true;
        }
    }
    Loader {
        id: player
        anchors.fill: parent
        sourceComponent: surface.fallback ? compatible : native
    }
    Component {
        id: native
        NativeVideo {
            source: surface.source
            paused: surface.paused
            contain: backend.settings.fit === "contain"
            onFailed: message => {
                console.warn(message + "; trying compatible decoder");
                surface.fallback = true;
            }
        }
    }
    Component {
        id: compatible
        Item {
            property alias ready: session.ready
            VideoOutput {
                id: output
                anchors.fill: parent
                endOfStreamPolicy: VideoOutput.KeepLastFrame
                fillMode: backend.settings.fit === "contain" ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
            }
            VideoSession {
                id: session
                source: surface.source
                paused: surface.paused
                sink: output.videoSink
                onFailed: message => surface.failed(message)
            }
        }
    }
}
