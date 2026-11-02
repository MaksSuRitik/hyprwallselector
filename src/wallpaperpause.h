// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>

inline bool wallpaperFullscreen(const QJsonObject &monitor, const QJsonArray &clients) {
    const auto active = monitor["activeWorkspace"].toObject()["name"].toString();
    const auto special = monitor["specialWorkspace"].toObject()["name"].toString();
    for (const auto &value : clients) {
        const auto client = value.toObject();
        if (client["fullscreen"].toInt() < 2 || client["hidden"].toBool() ||
            !client["mapped"].toBool(true))
            continue;
        const auto workspace = client["workspace"].toObject()["name"].toString();
        if ((!active.isEmpty() && workspace == active) ||
            (!special.isEmpty() && workspace == special))
            return true;
    }
    return false;
}
