Windows 10/11 x64 — download **Hyprwallselector-0.3.7-Setup.exe**. Dependencies are included.

- Fix Steam imports copying unrelated game cutscenes and Steam interface videos. The search is now strictly limited to Wallpaper Engine's `steamapps/workshop/content/431960` directory and its subfolders.
- Select Steam, SteamLibrary, or the `431960` folder directly. Settings displays the resolved Workshop path. A missing Workshop directory stops the import rather than scanning other Steam folders.
- Skip identical existing videos by their contents, including copies with generated names from 0.3.6, when changing between a library root and the Workshop directory.
- Imports MP4, WebM, AVI, M4V, MOV, WMV, MKV, MPG, MPEG, FLV and OGV files, including uppercase extensions and hidden subfolders.
- Search and copying run in the background with progress and cancellation. Completed copies remain available after cancellation; unfinished temporary files are removed.
- Existing files are preserved. Different videos sharing a filename get distinct names; repeating an import skips matching copies. Source videos remain unchanged.
- Refreshes the selected wallpaper library after import. Steam folder choices are remembered when settings are saved.
- Includes regression tests for unrelated game/Steam/other-app videos, missing Workshop folders, supported folder choices, legacy copies and update detection from 0.3.6.

Existing Windows users with update notifications enabled will be offered this version on their next scheduled check (at most once a day). **Settings → Check for updates** checks immediately. Installation starts after the user accepts; wallpaper selections and preferences are preserved.

Only actual video files are copied. Wallpaper Engine scene archives (`scene.pkg`) are not videos and cannot be converted by this import. Directory links and junctions are skipped, and a destination inside Steam is excluded from the search.

This update does not automatically delete files copied by older versions; existing personal libraries are preserved.

The installer is unsigned. Source and third-party notices are included with the installation.
