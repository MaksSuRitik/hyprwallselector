// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>

namespace WindowsDisplays {
struct Display { QString id, label; };

// Old versions stored user-facing labels. Preserve unambiguous selections and
// copy old wallpaper state to each matching output without overwriting new IDs.
inline QString migrateTarget(const QString &target, const QList<Display> &displays) {
    if (target == "ALL") return target;
    for (const auto &display : displays)
        if (display.id == target) return target;
    QString match;
    for (const auto &display : displays) {
        if (display.label != target || display.id.isEmpty()) continue;
        if (!match.isEmpty()) return "ALL"; // Legacy labels cannot distinguish twins.
        match = display.id;
    }
    return match.isEmpty() ? target : match; // Keep disconnected selections.
}
inline QJsonObject migrateState(QJsonObject state, const QList<Display> &displays) {
    for (const auto &display : displays)
        if (!display.id.isEmpty() && !state.contains(display.id) && state.value(display.label).isObject())
            state[display.id] = state.value(display.label);
    return state;
}
}
