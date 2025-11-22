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

namespace segoe
{

// https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-ui-symbol-font
// https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
inline constexpr auto Add = QChar{ 0xE710 };
inline constexpr auto CaretDown8 = QChar{ 0xEDD8 };
inline constexpr auto CaretDownSolid8 = QChar{ 0xEDDC };
inline constexpr auto CaretUp8 = QChar{ 0xEDD7 };
inline constexpr auto CaretUpSolid8 = QChar{ 0xEDDB };
inline constexpr auto Checkbox = QChar{ 0xE739 };
inline constexpr auto ChevronDown = QChar{ 0xE70D };
inline constexpr auto ChevronUp = QChar{ 0xE70E };
inline constexpr auto Contact = QChar{ 0xE77B };
inline constexpr auto Copy = QChar{ 0xE8C8 };
inline constexpr auto Delete = QChar{ 0xE74D };
inline constexpr auto Down = QChar{ 0xE74B };
inline constexpr auto Download = QChar{ 0xE896 };
inline constexpr auto Edit = QChar{ 0xE70F };
inline constexpr auto Error = QChar{ 0xE783 };
inline constexpr auto FastForward = QChar{ 0xEB9D };
inline constexpr auto Globe = QChar{ 0xE774 };
inline constexpr auto Heart = QChar{ 0xEB51 };
inline constexpr auto Help = QChar{ 0xE897 };
inline constexpr auto Info = QChar{ 0xE946 };
inline constexpr auto Move = QChar{ 0xE7C2 };
inline constexpr auto OpenFile = QChar{ 0xE8E5 };
inline constexpr auto OpenFolder = QChar{ 0xED25 };
inline constexpr auto Pause = QChar{ 0xE769 };
inline constexpr auto Play = QChar{ 0xE768 };
inline constexpr auto PowerButton = QChar{ 0xE7E8 };
inline constexpr auto Refresh = QChar{ 0xE72C };
inline constexpr auto Remove = QChar{ 0xE738 };
inline constexpr auto Ruler = QChar{ 0xED5E };
inline constexpr auto SelectAll = QChar{ 0xE8B3 };
inline constexpr auto Settings = QChar{ 0xE713 };
inline constexpr auto StatusErrorFull = QChar{ 0xEB90 };
inline constexpr auto Sync = QChar{ 0xE895 };
inline constexpr auto Upload = QChar{ 0xE898 };
inline constexpr auto UploadDownload = QChar{ 0xE174 };
inline constexpr auto Warning = QChar{ 0xE7BA };

} // namespace segoe

struct NativeIcon
{
public:
    struct Spec
    {
        Spec(
            QString sf_in,
            QChar fluent_in,
            QString fdo_in,
            std::optional<QStyle::StandardPixmap> fallback_in,
            QFont::Weight weight_in = QFont::Normal);

        Spec(
            std::string_view const sf_in,
            QChar const fluent_in,
            std::string_view const fdo_in,
            std::optional<QStyle::StandardPixmap> fallback_in,
            QFont::Weight weight_in = QFont::Normal);

        // https://developer.apple.com/sf-symbols
        // https://hotpot.ai/free-icons
        QString sfSymbolName;

        // https://learn.microsoft.com/en-us/windows/apps/design/style/segoe-fluent-icons-font
        QChar fluentCodepoint;

        // https://specifications.freedesktop.org/icon-naming/latest/#names
        QString fdoName;

        // https://doc.qt.io/qt-6/qstyle.html#StandardPixmap-enum
        std::optional<QStyle::StandardPixmap> fallback;

        QFont::Weight weight = QFont::Normal;
    };

    static QIcon get(Spec const& spec, QStyle* style = QApplication::style());

    static QIcon get(
        std::string_view const sf,
        QChar const fluent,
        std::string_view const fdo,
        std::optional<QStyle::StandardPixmap> qt = {},
        QStyle* style = QApplication::style());
};
