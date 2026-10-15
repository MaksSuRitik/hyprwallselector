Windows 10/11 x64 installer. Download the **Setup.exe** asset; dependencies are included.

- Fix the flash when opening the selector for the first time or reopening it.
- Keep the native window transparent until Qt presents a clean frame, then fade the prepared carousel in over 180 ms.
- Wait for visible thumbnails before revealing the carousel and reset interrupted animations when closing it.
- Disable the extra Windows window transition only for the selector overlay.
- Keep lazy UI loading and release the hidden scene graph as before.

Tested on Windows 11 with rendered first-frame, monotonic fade, repeated opening and interrupted opening checks, plus the existing test suite.

The installer is currently unsigned. Wallpapers and preferences are preserved when uninstalling.
Source and third-party notices are included with the installation.
