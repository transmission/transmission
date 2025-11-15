// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>

#include <QApplication>
#include <QFont>
#include <QString>
#include <QStyle>

struct NativeIcon
{
public:
    struct Spec
    {
        // https://developer.apple.com/sf-symbols
        // https://github.com/andrewtavis/sf-symbols-online
        QString sfSymbolName;

        // https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
        QString fluentCodepoint;

        // https://specifications.freedesktop.org/icon-naming/latest/#names
        QString fdoName;

        // https://doc.qt.io/qt-6/qstyle.html#StandardPixmap-enum
        std::optional<QStyle::StandardPixmap> fallback;

        QFont::Weight weight = QFont::Normal; 
    };

    static QIcon get(const Spec& spec, QStyle* style = QApplication::style());
};
