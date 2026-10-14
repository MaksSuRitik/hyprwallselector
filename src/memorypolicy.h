// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <string_view>
#ifdef __GLIBC__
#include <malloc.h>
#endif

inline void configureWallpaperMemory() {
#ifdef __GLIBC__
    // A long-lived wallpaper process repeatedly allocates large decoded frames.
    // glibc's adaptive mmap threshold otherwise promotes these into per-thread
    // heaps, where freed frames can remain resident for the lifetime of the app.
    // Keep large buffers independently releasable. This affects only this process;
    // it neither changes system settings nor periodically trims active heaps.
    // Preserve an explicitly configured allocator threshold.
    const char *tunables = std::getenv("GLIBC_TUNABLES");
    const bool customThreshold =
        tunables &&
        std::string_view(tunables).find("glibc.malloc.mmap_threshold=") != std::string_view::npos;
    if (!std::getenv("MALLOC_MMAP_THRESHOLD_") && !customThreshold)
        mallopt(M_MMAP_THRESHOLD, 32 * 1024);
#endif
}
