# Hyprwallselector

A standalone image and video wallpaper selector for Hyprland, with a slanted
parallelogram carousel. Written in C++20 and Qt Quick. Early development release.

## Features

- Alt+T opens the selector; Escape or a second Alt+T closes it.
- Left/right or the mouse wheel browse; Enter applies; P previews a video.
- Settings stay inside the selector. Save and Cancel return to the carousel.
- English, Українська and Русский interface languages.
- Independent layer-shell UI: no tiled window, dock entry or fullscreen window animation.
- Images and silent looping videos, crossfades, persistent selections per display.
- Hardware video decoding through libmpv when supported by the graphics driver.
- Pause on session lock/inactivity, battery, screen off and fullscreen applications.
- A passive global shortcut service: no compositor binding or input-group membership.

## Installation

The source is at <https://github.com/MaksSuRitik/hyprwallselector>.
An AUR recipe is provided in `packaging/aur`. Its presence here does **not** mean
that the package has already been submitted to AUR.

On Arch, install dependencies and build on a Linux filesystem:

```sh
sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules \
  qt6-base qt6-declarative qt6-wayland qt6-shadertools layer-shell-qt mpv ffmpeg
cmake -S . -B /tmp/hyprwallselector-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBEXECDIR=lib/hyprwallselector
cmake --build /tmp/hyprwallselector-build
sudo cmake --install /tmp/hyprwallselector-build
sudo systemctl daemon-reload
```

Prefer the package recipe over a manual install. It tracks all installed files.
The system shortcut service activates automatically over D-Bus; do not enable it
manually. The graphical application must run as your ordinary desktop user.

## Start once per session

Launch `hyprwallselector`, select your wallpaper folder, and enable desktop
wallpaper management after disabling the other wallpaper renderer (see below).
Enable **Start at login** if your session launches XDG autostart applications.
UWSM sessions normally provide this. Otherwise put just this command into your
dotfiles' autostart mechanism:

```sh
hyprwallselector --daemon
```

For traditional Hyprland configuration this is `exec-once = hyprwallselector --daemon`.
No `bind` line is required. Avoid assigning Alt+T to another program. The shortcut
is passive, so it can also reach the focused application; see [security notes](docs/SECURITY.md).

Useful commands:

```sh
hyprwallselector --toggle
hyprwallselector --diagnose
hyprwallselector --quit
```

## Shell compatibility

The selector and its shortcut are independent of your bar/shell. Only one
wallpaper renderer should own each display.

- **DMS:** use the DMS external-wallpaper button in Settings, or run
  `hyprwallselector --integrate-dms`. This changes the supported
  `screenPreferences.wallpaper` setting to `[]`, preserving other preferences
  and saving the old value. `hyprwallselector --restore-dms` restores it if it
  has not subsequently been changed. No DMS source files are patched.
- **Caelestia:** disable the wallpaper in its Wallpaper and Style settings
  (`background.wallpaperEnabled: false`), then use the Hyprwallselector renderer.
  Keep its desktop widgets enabled if desired.
- **end-4 / illogical-impulse:** choose **end-4 (ii)** as the renderer. Selections
  are sent to its `qs -c ii ipc call wallpapers apply` interface. The shell owns
  rendering, transitions, video resource use and monitor behavior in this mode;
  Hyprwallselector's per-display/pause controls only apply to its own renderer.
- **Other dotfiles:** disable their wallpaper daemon and use the native renderer.
  Do not solve competing renderers by moving wallpaper above application windows.

The DMS integration is tested with 1.6.3. Caelestia and end-4 interfaces were
checked against upstream source; complete desktop sessions were not installed
for testing. Shell APIs can change. Per-monitor selection uses each display's
own crop, rather than spanning a single image across outputs.

## Configuration and recovery

- Settings: `~/.config/hyprwallselector/config.json`
- Selections: `~/.local/state/hyprwallselector/state.json`
- Thumbnails: `~/.cache/hyprwallselector/thumbs/`
- DMS backup: `~/.local/state/hyprwallselector/dms-backup.json`

XDG overrides are supported. Legacy Python-selector settings and selection are
imported when native settings do not yet exist. The program does not silently
remove old services or shortcuts: disable duplicate startup entries when migrating.

The compositor session must report lock state to logind (DMS does). A lock surface
still prevents the overlay from receiving focus while locked. The application
never uses an overlay layer to display desktop wallpaper.

## Development and tests

```sh
sudo pacman -S --needed xorg-server-xvfb xorg-xauth
ctest --test-dir /tmp/hyprwallselector-build --output-on-failure
sh tests/run-ui.sh /tmp/hyprwallselector-build/test-ui
```

UI tests generate their own image/video files and use a private Xvfb display.
They do not overlay the user's desktop or modify shell preferences. They cover
navigation, settings, video motion/pause and image/video transitions.

Current limits: no wallpaper spanning, no DMS palette synchronization, no native
end-4 renderer takeover. The broker requires Linux, udev, systemd-logind and a
local active graphical session. Qt 6.10 or newer is required.

Licensed under GPL-3.0-or-later.
