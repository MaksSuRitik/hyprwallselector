// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFileInfo>
#include <QStringList>

inline bool videoFile(const QString &path) {
    return QStringList{"mp4", "mkv", "webm", "mov", "avi", "m4v",
                       "mpg", "mpeg", "wmv", "flv", "ogv"}
        .contains(QFileInfo(path).suffix().toLower());
}
