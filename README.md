# Hyprwallselector

A standalone image and video wallpaper selector for Hyprland, with a slanted
parallelogram carousel. Written in C++20 and Qt Quick. Early development release.

## Features

- Alt+T opens the selector; Escape or a second Alt+T closes it.
- Left/right or the mouse wheel browse; Enter applies; P previews a video.
- Animated settings with General, Playback and Shell & colors sections.
  Save and Cancel return to the carousel without closing it.
- Caelestia/MiDnight renderer handover with native effects, theme updates, GIF/video
  playback and Wallpaper Engine projects where supported by the installed shell.
- Optional DMS palette and lock-screen sync, including still frames for video wallpapers.
- Folder browsing stays inside the selector, without an external portal/file-manager
  window competing with the layer-shell overlay. Supports typed paths and mounted disks.
- English, Українська and Русский interface languages.
- Independent layer-shell UI: no tiled window, dock entry or fullscreen window animation.
- Images and silent looping videos, crossfades, persistent selections per display.
- Hardware video decoding through libmpv when supported by the graphics driver.
- Pause on session lock/inactivity, battery, screen off and fullscreen applications.
- A passive global shortcut service: no compositor binding or input-group membership.

## Installation

The source is at <https://github.com/MaksSuRitik/hyprwallselector>.
Install [hyprwallselector-git from AUR](https://aur.archlinux.org/packages/hyprwallselector-git):

```sh
yay -S hyprwallselector-git
```

The recipe is also provided in `packaging/aur`.

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
  and saving the old value. DMS reloads its settings file automatically; its object-valued IPC setter is not used. `hyprwallselector --restore-dms` restores it if it
  has not subsequently been changed. No DMS source files are patched.
  Enable **DMS colors & lock screen** to synchronize the selected image through
  DMS wallpaper IPC. Videos send a cached still frame, so DMS does not start
  another video player. Choose the **Dynamic** theme in DMS for adaptive colors;
  keep its custom lock-screen wallpaper unset to follow the desktop. Existing
  global/per-monitor mode and the DMS palette target monitor are respected.
  Requests start as soon as the new image/video has loaded, alongside the crossfade,
  rather than after it. They run asynchronously, coalesce queued changes and retry
  briefly at login. DMS still controls the actual color extraction and theme update.
  If DMS is unavailable, native wallpaper playback continues normally.
- **Caelestia / MiDnight:** choose **Caelestia / MiDnight** as the renderer and
  enable desktop wallpaper management. Saving backs up and enables the supported
  `background.enabled` / `background.wallpaperEnabled` user settings in
  `$XDG_CONFIG_HOME/caelestia/shell.json`, then removes this app's native wallpaper
  surfaces. Selections use `caelestia shell wallpaper set`; a read-back confirms
  the selected path. Shell sources are never patched.
  The shell owns transitions, recoloring, dynamic palettes, lock-screen appearance,
  desktop widgets, fitting and per-display behavior. The selector's Playback tab
  controls manual pause, battery pause, fullscreen pause and MiDnight's separate
  pause-behind-windows rule. A Pause/Resume button is also available above the
  carousel, and `hyprwallselector --pause` / `--resume` control the running instance.
  Battery/overlap settings use MiDnight's watched `wallpaper/pauser.json`; existing
  decoder preferences are preserved. Manual/automatic requests pause the actual
  shell player. Fullscreen detection follows visible clients on active normal and
  special workspaces (including current Hyprland JSON); keep the selector daemon
  running for this fallback on MiDnight versions whose new video player does not
  implement the shell's fullscreen setting. Other automatic pause conditions still
  apply after a manual resume. **Open Caelestia / MiDnight
  settings** opens its native controls, including the effects, video sound,
  hardware decoder and automatic pause options available in that shell version.
  Existing choices are preserved; dynamic colors require the shell's Dynamic scheme.
  Ordinary Caelestia supports its own image/GIF formats; compatible MiDnight builds
  also support MP4, MKV and WebM. Video is checked through the CLI's color-preview
  interface before sending it to the shell. Unsupported formats leave the current
  wallpaper selected. For Wallpaper Engine, install `linux-wallpaperengine` and
  use a compatible MiDnight shell/CLI. Browse a project directory, or a parent
  containing project directories; `project.json` entries use their local preview
  in the carousel and are handed to the native shell as projects.
  Switching back to another renderer or disabling background management turns
  off the shell wallpaper surface, retaining its widgets and other preferences.
  Explicit recovery: stop the other wallpaper renderer, then run
  `hyprwallselector --restore-caelestia` to restore the original background flags.
  Changes made independently to those flags are checked before writing.
  Some current MiDnight IPC implementations strip `#`/`?` and interpolate single
  quotes/control characters in filenames. Such paths are rejected before IPC;
  rename them or use the Hyprwallselector renderer.
- **end-4 / illogical-impulse:** choose **end-4 (ii)** as the renderer. Selections
  are sent to its `qs -c ii ipc call wallpapers apply` interface. The shell owns
  rendering, transitions, video resource use and monitor behavior in this mode;
  Hyprwallselector's per-display/pause controls only apply to its own renderer.
- **Other dotfiles:** disable their wallpaper daemon and use the native renderer.
  Do not solve competing renderers by moving wallpaper above application windows.

DMS integration is tested with 1.6.3. Caelestia/MiDnight integration has
configuration-preservation, IPC, project-catalogue and GUI regression tests.
The real-session renderer handover is verified on MiDnight commit `f4ade958c1`
with matching CLI `e6b4900fd`. Wallpaper Engine project dispatch is covered with
an isolated protocol fixture; a real scene renderer is an optional dependency.
End-4's interface is checked against upstream source. Shell APIs can change.
External shell renderers control their own monitor settings. MiDnight playback
controls are available in the selector; end-4 controls remain in its own shell.

## Configuration and recovery

- Settings: `~/.config/hyprwallselector/config.json`
- Selections: `~/.local/state/hyprwallselector/state.json`
- Thumbnails: `~/.cache/hyprwallselector/thumbs/`
- DMS backup: `~/.local/state/hyprwallselector/dms-backup.json`
- Caelestia original/managed background flags: `~/.local/state/hyprwallselector/caelestia-backup.json`

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
navigation, settings, video motion/pause and image/video transitions. An optional
memory stress test switches between eight generated 4K images and videos 48 times,
checks that obsolete players/renderers are destroyed and reports RSS/PSS, private
memory and allocator usage on Linux:

```sh
HWS_MEMORY_TEST=1 sh tests/run-ui.sh /tmp/hyprwallselector-build/test-ui wallpaperMemory
```

Xvfb uses software rendering and a small generated video. It does **not** validate
real GPU/decoder memory use. For that, install `weston` and use the isolated
headless GPU runner with a directory containing representative MP4 files:

```sh
HWS_MEMORY_TEST=1 HWS_MEMORY_MEDIA_DIR=/path/to/test-videos \
  HWS_MEMORY_CYCLES=119 HWS_MEMORY_DWELL_MS=1000 \
  HWS_MEMORY_SHOW_SELECTOR=1 HWS_MEMORY_SOAK_SECONDS=120 \
  sh tests/run-gpu-ui.sh /tmp/hyprwallselector-build/test-ui wallpaperMemory
```

This reads the video files without changing them, alternates videos/images,
opens/closes the selector, then keeps the final wallpaper playing. Check the
reported GL renderer, decoder, dropped frames and memory trend after warm-up;
a low startup measurement alone is not evidence of stable memory use.
Set `HWS_MEMORY_MAX_PRIVATE_KB` to enforce a settled private-memory ceiling for a
specific test machine/library (including the soak phase). RSS also counts shared
libraries; GPU allocations and brief transition peaks are separate from this check.

Native images are decoded to the output's physical pixel size and are not retained
in the shared image cache. Hidden carousel cards unload, and local video packet
buffers are bounded. `--diagnose` includes live player/renderer counts; transitions
briefly need both the old and new wallpaper. Memory figures include the graphics
and video drivers and will vary with resolution, codec and monitor count.
On glibc, large allocations use a fixed 32 KiB mmap threshold so freed frame
buffers can return to the OS instead of accumulating in allocator arenas.
Explicit allocator threshold overrides are respected. Unused mpv UI scripts are
disabled; Intel/AMD GL contexts use native VA-API interop without probing CUDA.
These changes do not lower video resolution or frame rate.

Current limits: no wallpaper spanning, still frames on the DMS lock screen for
video wallpapers, no native end-4 renderer takeover. The broker requires Linux, udev, systemd-logind and a
local active graphical session. Qt 6.10 or newer is required.

Licensed under GPL-3.0-or-later.
