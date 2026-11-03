// SPDX-License-Identifier: GPL-3.0-or-later
// Load this component in the same Quickshell configuration as qs.services.
import QtQuick
import Quickshell
import Quickshell.Io
import qs.services

Scope {
    id: root
    property string owner: ""
    property int ownerPid: 0
    property int sequence: 0
    property string original: ""
    property string lastPreview: ""
    property bool writing: false
    property var closed: []

    function result(ok, error) {
        return JSON.stringify({ok: ok, error: error || "", actual: Wallpapers.actualCurrent,
            preview: Wallpapers.showPreview, path: Wallpapers.current});
    }
    function release(token) {
        if (token && !closed.includes(token)) closed = closed.concat([token]).slice(-32);
        owner = "";ownerPid = 0;sequence = 0;original = "";lastPreview = "";
    }
    function owns(token, order) {
        if (owner !== token || closed.includes(token) || order <= sequence) return false;
        sequence = order;
        return true;
    }
    function supported(path) {
        return !/\.(mp4|mkv|webm)$/i.test(path) || typeof Wallpapers.isVideo === "function";
    }
    function stop(token) {
        if (owner === token) {
            writing = true;
            // Another program's confirmed choice takes priority over our old
            // rollback. MiDnight tracks this explicitly; vanilla has no flag.
            if (Wallpapers.actualCurrent !== original && Wallpapers.isTrackingRollback !== undefined)
                Wallpapers.isTrackingRollback = false;
            try { Wallpapers.stopPreview(); }
            finally { writing = false;release(token); }
        } else if (!closed.includes(token)) closed = closed.concat([token]).slice(-32);
    }
    IpcHandler {
        target: "hws-preview"
        function begin(token: string, pid: int): string {
            if (closed.includes(token)) return root.result(false, "stale");
            if (root.owner || Wallpapers.showPreview || !Wallpapers.actualCurrent)
                return root.result(false, "busy");
            if (!/^[a-fA-F0-9-]{36}$/.test(token) || pid <= 0)
                return root.result(false, "invalid");
            root.owner = token;root.ownerPid = pid;root.sequence = 0;
            root.original = Wallpapers.actualCurrent;
            root.lastPreview = Wallpapers.previewPath;
            return root.result(true);
        }
        function preview(token: string, order: int, path: string): string {
            if (!root.owns(token, order)) return root.result(false, "stale");
            if (path[0] !== "/" || /['\n\r#?]/.test(path)) return root.result(false, "invalid");
            if (!root.supported(path)) return root.result(false, "unsupported");
            root.writing = true;
            try { Wallpapers.preview(path);root.lastPreview = Wallpapers.previewPath; }
            finally { root.writing = false; }
            return root.result(true);
        }
        function commit(token: string, order: int, path: string): string {
            if (!root.owns(token, order)) return root.result(false, "stale");
            if (path[0] !== "/" || /['\n\r#?]/.test(path)) return root.result(false, "invalid");
            if (!root.supported(path)) return root.result(false, "unsupported");
            root.writing = true;
            try {
                if (Wallpapers.actualCurrent !== path || Wallpapers.showPreview)
                    Wallpapers.setWallpaper(path);
            }
            finally { root.writing = false; }
            root.release(token);
            return root.result(true);
        }
        function cancel(token: string, order: int): string {
            root.stop(token);
            return root.result(true);
        }
        function status(): string { return root.result(true); }
    }
    Connections {
        target: Wallpapers
        function onActualCurrentChanged() {
            if (root.owner && !root.writing && Wallpapers.actualCurrent !== root.original)
                root.stop(root.owner);
        }
        function onPreviewPathChanged() {
            if (root.owner && !root.writing && Wallpapers.previewPath !== root.lastPreview)
                root.release(root.owner);
        }
    }
    // Only active during a preview. A crashed/killed selector cannot strand
    // its unconfirmed wallpaper or preview palette on the desktop.
    Timer {
        interval: 1000
        repeat: true
        running: root.owner !== ""
        onTriggered: if (!alive.running) {
            alive.checked = root.owner;
            alive.command = ["kill", "-0", String(root.ownerPid)];
            alive.running = true;
        }
    }
    Process {
        id: alive
        property string checked
        onExited: (code, status) => {
            if (code !== 0 && checked === root.owner) root.stop(checked);
        }
    }
}
