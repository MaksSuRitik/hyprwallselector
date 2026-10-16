# Hyprwallselector for Windows

Windows 10/11 x64. Install with Setup.exe; Qt and the FFmpeg video codecs are included.
No Python, MSYS2, administrator account or external hotkey tool is required to use it.

* Open from Start or press **Alt+T**. Escape hides the selector.
  The selector waits for its first transparent frame and visible thumbnails, then
  fades in over 180 ms. Closing resets the fade, including interrupted openings.
* Choose the wallpaper directory in Settings. The folder picker includes drive letters.
* Images and silent looping videos use the same carousel. Video changes show a cached poster
  immediately while the decoder starts, then fade into motion as soon as its first frame is ready.
* The tray menu opens the selector, pauses video or exits.
* The installer enables **Start at login** by default. The background process restores
  the last wallpaper and listens for Alt+T. Disable it in Settings if preferred.
  It uses a per-user Task Scheduler logon task with no delay or elevated privileges,
  so it does not wait in Explorer's delayed Run queue. If policy blocks task creation,
  it falls back to the Run key. The installer migrates previous Run entries.
* **Use wallpaper on the lock screen** syncs the primary monitor's image, or a still
  frame of its video, through Windows' personalization API. This is enabled by default
  and can be disabled independently. Video playback on the Windows lock screen is not supported.
* **High priority at startup** is an optional checkbox in Settings. Recommended if
  CPU load delays the selector. It applies immediately and persists across starts;
  it changes this app's CPU scheduling priority, not GPU priority or video quality.
* Videos pause on battery (optional), behind a foreground fullscreen app (optional),
  on session lock and when the display switches off. Pause is per monitor for fullscreen apps.
* Videos play directly from their original files; the app does not transcode them. Small cached
  poster images are used only to make wallpaper changes appear before video decoding finishes.
  Memory use still depends on the codec, video dimensions, displays and graphics driver.
  On Windows, supported SDR videos decode directly into D3D11 textures on the same GPU
  as the UI. One pending frame and a codec-sized reference pool replace the general
  media player's queues. Unsupported formats use the compatible Qt video backend.
  Neither a universal 50 MB ceiling nor a 500 ms launch on every disk/codec is guaranteed.
* Quitting restores visibility of the existing Windows wallpaper. The original wallpaper file
  and Windows wallpaper setting are never replaced.
* Uninstall from Windows Installed apps. This removes the program, shortcuts and autostart;
  your wallpaper files and preferences remain.

The wallpaper uses Explorer's desktop host, supporting both the classic WorkerW layout
and the newer Windows 11 raised desktop layout. This interface is not a public Windows API;
future Explorer changes may need an update. Failure to find the host hides the wallpaper and
retries instead of displaying a blocking top-level window. Pending selections survive
desktop reconnect attempts. Use one live wallpaper app at a time.
If a PC cannot apply wallpapers, use **Save diagnostic report** in the tray menu.
The JSON includes Windows version, Explorer layout, window geometry, decoder counters,
startup timing and errors. Display diagnostics include Qt labels, native Windows device
names, monitor handles, DPI scaling and physical bounds. Wallpaper placement uses the
native monitor handle, not a comparison of display names or scaled coordinates.
The report also contains your local wallpaper paths; it is saved
locally and is never uploaded automatically.

Windows does not have DMS, end-4 or Hyprland. Their shell/lock-screen palette integration is
available only in the Linux edition. Lock-screen image sync is separate from those integrations;
Windows policy can deny personalization, in which case the app reports the error.
The installer is currently unsigned.

## Updates and releases

The app checks GitHub Releases at most once a day (after a successful check) and shows a tray
notification for a newer stable `windows-vX.Y.Z` release. **Check for updates** is available
in Settings and the tray; the tray also lets users disable automatic checks. Downloads and
installation require the user's click. The updater verifies the installer size and SHA-256
against GitHub's release-asset digest over HTTPS before running it. It does not send local
file names, wallpaper paths or system inventory. Settings, library files and the current
autostart preference survive updates. Installation restarts the app hidden.

To publish from the Windows source branch after testing:

```sh
git push origin windows-port
git tag windows-v0.3.3
git push origin windows-v0.3.3
```

Use a new increasing version each time. `.github/workflows/windows-release.yml` builds and
tests the tagged source, embeds that version, creates the installer and SHA256SUMS, then
publishes the Windows release. It does not replace the repository's latest Linux release.
Recipients only need the Setup.exe once; subsequent versions appear in their app.
Do not replace an existing release asset; publish a new version instead.

## Commands

`hyprwallselector.exe --daemon` starts hidden and restores wallpapers.
`--toggle`, `--quit`, `--pause`, `--resume`, `--diagnose` control the current user's instance.
`--wallpaper "C:\Pictures\clip.mp4"` applies a local supported file.
`--check-updates` requests a background release check from a running instance.
`--preview` runs the UI without changing wallpapers or registering a global shortcut.

## Build

Install MSYS2 UCRT64, update it, then install:

```
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-declarative mingw-w64-ucrt-x86_64-qt6-shadertools \
  mingw-w64-ucrt-x86_64-qt6-imageformats mingw-w64-ucrt-x86_64-qt6-svg \
  mingw-w64-ucrt-x86_64-qt6-multimedia mingw-w64-ucrt-x86_64-ffmpeg
```

Run `packaging/windows/build.ps1` in PowerShell with `-QtRoot C:\msys64\ucrt64`,
`-BuildRoot C:\build\hws`, `-OutputRoot C:\build\release` and optionally
`-InnoCompiler "C:\path\to\ISCC.exe" -Version 0.3.3`. Each bundle is assembled in a new directory.
The script scans PE dependencies recursively and rejects missing libraries.
Keep the included licenses and source archive with redistributions.

The Linux build continues to use LayerShellQt, systemd/logind and its existing hotkey service.

For the opt-in GPU integration test, set `HWS_TEST_VIDEO_FOLDER` to a directory of MP4
videos and run `test-nativevideo.exe`. It checks moving frames, pause/resume, source
changes, concurrent preview, teardown and missing files. `HWS_TEST_DWELL` (milliseconds)
and `HWS_TEST_CYCLES` extend the run; `HWS_TEST_REPORT` sets its JSON measurement path.
`HWS_TEST_DESKTOP=1` enables an Explorer attachment test in `test-windows.exe`.
`HWS_TEST_LOCKSCREEN_IMAGE` enables a real personalization API test using the supplied
image (it changes the lock screen). Both are opt-in and are skipped in normal CI.
