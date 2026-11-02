// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

// Empty result means success. Only supported user settings are changed.
QString configureCaelestia(bool enabled, bool restore = false);
QString applyCaelestiaWallpaper(const QString &path);
QString openCaelestiaSettings();
QString configureCaelestiaPlayback(bool battery, bool fullscreen, bool overlap);
QString pauseCaelestia(bool paused);
