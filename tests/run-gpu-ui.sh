#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# An isolated Wayland display using the real GPU. Never uses the user's desktop.
set -eu
command -v weston >/dev/null || { echo 'Install weston for GPU tests.' >&2; exit 1; }
runtime=$(mktemp -d /tmp/hws-gpu-test.XXXXXX)
weston_pid=
cleanup() {
    if [ -n "$weston_pid" ]; then
        kill "$weston_pid" 2>/dev/null || true
        wait "$weston_pid" 2>/dev/null || true
    fi
    rm -rf "$runtime"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
unset DISPLAY WAYLAND_DISPLAY LIBGL_ALWAYS_SOFTWARE QSG_RENDER_LOOP
export XDG_RUNTIME_DIR="$runtime" QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME=
export DBUS_SESSION_BUS_ADDRESS="unix:path=$runtime/no-session-bus"
weston --backend=headless --renderer=gl --width=1920 --height=1080 \
    --socket=hws-test --idle-time=0 --no-config --fake-seat \
    --log="$runtime/weston.log" >"$runtime/weston.stderr" 2>&1 &
weston_pid=$!
attempt=0
while [ ! -S "$runtime/hws-test" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -gt 100 ] || ! kill -0 "$weston_pid" 2>/dev/null; then
        cat "$runtime/weston.log" "$runtime/weston.stderr" >&2
        exit 1
    fi
    sleep 0.05
done
sed -n '/GL renderer:/p' "$runtime/weston.log"
export WAYLAND_DISPLAY=hws-test HWS_TEST_DISPLAY=isolated
# The optional switch-and-soak test exceeds QtTest's default five-minute watchdog.
export QTEST_FUNCTION_TIMEOUT="${QTEST_FUNCTION_TIMEOUT:-900000}"
"$@"
