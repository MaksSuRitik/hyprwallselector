Windows 10/11 x64 installer. Download the **Setup.exe** asset; dependencies are included.

- Uninstall now removes generated caches and downloaded updates, and asks whether to also remove preferences and saved wallpaper selections. Original images/videos and unrelated files are preserved. Silent uninstall keeps preferences unless `/PURGEUSERDATA=1` is supplied.
- Run the actual uninstall-cleanup helper against an isolated fixture during each installer build, checking both choices and path boundaries.
- Retain the native monitor lookup fix from 0.3.3, confirmed by a diagnostic report from 0.3.2 where monitor model labels did not match Win32 device names.
- Separate per-monitor wallpaper and pause state by Windows device key, keeping readable model names in Settings. Identical monitor models no longer share one settings key.
- Migrate existing wallpaper selections automatically, preserving current device-specific choices.
- Track the intended output independently of Qt window parenting and recover after a temporary wallpaper positioning failure.
- Add clean-profile, malformed-config, legacy-selection and duplicate-model tests to the normal test suite, plus opt-in first-use and repeated desktop recovery checks.

The user who supplied the Windows 11 / RTX 5070 diagnostic confirmed that 0.3.3 restored wallpaper application. Additional validation covers local desktop integration and simulated state/recovery cases; multi-monitor hardware and every Windows build have not been exhaustively tested.

The installer is currently unsigned. Original wallpapers are preserved; keeping preferences is a choice during uninstall.
Source and third-party notices are included with the installation.
