Windows 10/11 x64 installer. Download the **Setup.exe** asset; dependencies are included.

- Resolve Windows monitors through Qt's native HMONITOR interface. A display's user-facing name is not necessarily its Win32 device name; comparing the two could prevent both images and videos from appearing.
- Use the same native monitor information for wallpaper placement and fullscreen pause detection, without guessing from DPI-scaled coordinates.
- Include display names, native device names, geometry, DPI and query errors in the saved diagnostic report.
- Preserve the smooth opening and low-memory hidden selector behavior from 0.3.2.

Validation: native monitor lookup against Windows enumeration, missing-screen handling, Explorer wallpaper attachment and the existing test suite. The reported failure on a remote desktop PC still needs confirmation on that machine; no RTX 5070 hardware test is claimed.

The installer is currently unsigned. Wallpapers and preferences are preserved when uninstalling.
Source and third-party notices are included with the installation.
