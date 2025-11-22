// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>
#include <string_view>

#include <QApplication>
#include <QFont>
#include <QString>
#include <QStyle>

struct NativeIcon
{
public:
    struct Spec
    {
        Spec(
            QString sf_in,
            QString fluent_in,
            QString fdo_in,
            std::optional<QStyle::StandardPixmap> fallback_in,
            QFont::Weight weight_in = QFont::Normal);

        Spec(
            std::string_view const sf_in,
            std::string_view const fluent_in,
            std::string_view const fdo_in,
            std::optional<QStyle::StandardPixmap> fallback_in,
            QFont::Weight weight_in = QFont::Normal);

        // https://developer.apple.com/sf-symbols
        // https://hotpot.ai/free-icons
        QString sfSymbolName;

        // https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
        QString fluentCodepoint;

        // https://specifications.freedesktop.org/icon-naming/latest/#names
        QString fdoName;

        // https://doc.qt.io/qt-6/qstyle.html#StandardPixmap-enum
        std::optional<QStyle::StandardPixmap> fallback;

        QFont::Weight weight = QFont::Normal;
    };

    static QIcon get(Spec const& spec, QStyle* style = QApplication::style());

    static QIcon get(
        std::string_view const sf,
        std::string_view const fluent,
        std::string_view const fdo,
        std::optional<QStyle::StandardPixmap> qt = {},
        QStyle* style = QApplication::style());
};
