Windows 10/11 x64 installer. Download the **Setup.exe** asset; dependencies are included.

- Restore wallpapers at sign-in using a per-user logon task, without the delayed Run queue or administrator privileges. The installer migrates existing autostart entries.
- Retry the Explorer attachment promptly while the desktop starts; restore wallpapers before generating library thumbnails.
- Load the hidden carousel only on first activation, reducing startup time and idle memory before opening it.
- Keep wallpaper selections pending while Explorer is unavailable. Maintain the Windows 11 desktop layer order so the system wallpaper cannot cover the application's layer.
- Synchronize the primary monitor's wallpaper to the Windows lock screen (a still frame for videos). Enable or disable this in Settings → Desktop.
- Fall back to the compatible video backend when a driver never produces the first native frame.
- Save a diagnostic JSON report from the tray, including the Windows version, Explorer layout, startup timing and errors.

Tested on Windows 11 with actual Explorer attachment, lock-screen API and rendered settings checks.

The installer is currently unsigned. Wallpapers and preferences are preserved when uninstalling.
Source and third-party notices are included with the installation.
