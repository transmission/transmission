// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "NativeIcon.h"

#include <iostream>

#include <QFontInfo>
#include <QOperatingSystemVersion>
#include <QPainterPath>
#include <QRawFont>
#include <QScreen>
#include <QTextLayout>
#include <QtGui/QIcon>
#include <QtGui/QPixmap>
#include <QtGui/QPainter>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>

#include <small/set.hpp>

#if defined(Q_OS_MAC)
extern QPixmap loadSFSymbol(QString symbol_name, int pixel_size);
#endif

namespace
{
auto const Win10IconFamily = QStringLiteral("Segoe MDL2 Assets");
auto const Win11IconFamily = QStringLiteral("Segoe Fluent Icons");

// Define these two macros to force a specific icon during development.
// Their EULA doesn't allow redistribution but does allow using them
// during design/develop/testing.
// 1. Snag the ttf you want to use (Win 10 uses https://aka.ms/SegoeFonts,
//    Win 11 uses https://aka.ms/SegoeFluentIcons).
// 2. Add it to application.qrc
// 3. Set these two macros accordingly
#define DEV_FORCE_FONT_FAMILY Win11IconFamily
#define DEV_FORCE_FONT_RESOURCE QStringLiteral(":devonly/segoe_fluent_icons.ttf")

QString getWindowsFontFamily()
{
#ifdef DEV_FORCE_FONT_FAMILY
    return DEV_FORCE_FONT_FAMILY;
#else
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 11))
        return Win11IconFamily;

    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10))
        return Win10IconFamily;

    return {};
#endif
}

void ensureFontsLoaded()
{
#ifdef DEV_FORCE_FONT_RESOURCE
    [[maybe_unused]] static auto const font_id = QFontDatabase::addApplicationFont(DEV_FORCE_FONT_RESOURCE);
#endif
}

QPixmap makeIconFromCodepoint(QString const family, QChar const codepoint, int const point_size)
{
    auto const font = QFont{ family, point_size - 8 };
    if (!QFontMetrics{ font }.inFont(codepoint))
        return {};

    // FIXME: HDPI, pixel size vs point size?
    // FIXME: light mode vs. dark mode?
    auto const rect = QRect{ 0, 0, point_size, point_size };
    auto pixmap = QPixmap{ rect.size() };
    pixmap.fill(Qt::transparent);
    auto painter = QPainter{ &pixmap };
    painter.setFont(font);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(qApp->palette().color(QPalette::ButtonText));
    painter.setRenderHint(QPainter::TextAntialiasing);
    auto br = QRect{};
    painter.drawText(rect, Qt::AlignCenter, QString{ codepoint }, &br);
    painter.end();

    //std::cerr << "br x " << br.x() << " y " << br.y() << " w " << br.width() << " h " << br.height() << std::endl;

    return pixmap;
}

QString qstrFromUtf8(std::string_view const sv)
{
    return QString::fromUtf8(std::data(sv), std::size(sv));
}

} // namespace

NativeIcon::Spec::Spec(
    QString sf_in,
    QChar const fluent_in,
    QString fdo_in,
    std::optional<QStyle::StandardPixmap> fallback_in,
    QFont::Weight weight_in)
    : sfSymbolName{ sf_in }
    , fluentCodepoint{ fluent_in }
    , fdoName{ fdo_in }
    , fallback{ fallback_in }
    , weight{ weight_in }
{
}

NativeIcon::Spec::Spec(
    std::string_view const sf_in,
    QChar const fluent_in,
    std::string_view const fdo_in,
    std::optional<QStyle::StandardPixmap> fallback_in,
    QFont::Weight weight_in)
    : Spec{ qstrFromUtf8(sf_in), fluent_in, qstrFromUtf8(fdo_in), fallback_in, weight_in }
{
}

// static
QIcon NativeIcon::get(
    std::string_view const sf,
    QChar const fluent,
    std::string_view const fdo,
    std::optional<QStyle::StandardPixmap> qt,
    QStyle* style)
{
    return get({ sf, fluent, fdo, qt }, style);
}

QIcon NativeIcon::get(Spec const& spec, QStyle* style)
{
    ensureFontsLoaded();

    static auto const point_sizes = small::set<int>{
        style->pixelMetric(QStyle::PM_ButtonIconSize),   style->pixelMetric(QStyle::PM_LargeIconSize),
        style->pixelMetric(QStyle::PM_ListViewIconSize), style->pixelMetric(QStyle::PM_MessageBoxIconSize),
        style->pixelMetric(QStyle::PM_SmallIconSize),    style->pixelMetric(QStyle::PM_TabBarIconSize),
        style->pixelMetric(QStyle::PM_ToolBarIconSize)
    };

#if defined(Q_OS_MAC)
    if (!spec.sfSymbolName.isEmpty())
    {
        auto icon = QIcon{};
        for (int const point_size : point_sizes)
            if (auto const pixmap = loadSFSymbol(spec.sfSymbolName, point_size); !pixmap.isNull())
                icon.addPixmap(pixmap);
        if (!icon.isNull())
            return icon;
    }
#endif

    if (!spec.fluentCodepoint.isNull())
    {
        if (auto const font_family = getWindowsFontFamily(); !font_family.isEmpty())
        {
            auto icon = QIcon{};
            for (int const point_size : point_sizes)
                if (auto const pixmap = makeIconFromCodepoint(font_family, spec.fluentCodepoint, point_size); !pixmap.isNull())
                    icon.addPixmap(pixmap);
            if (!icon.isNull())
                return icon;
        }
    }

    if (!spec.fdoName.isEmpty())
    {
        if (auto icon = QIcon::fromTheme(spec.fdoName); !icon.isNull())
            return icon;
        if (auto icon = QIcon::fromTheme(spec.fdoName + QStringLiteral("-symbolic")); !icon.isNull())
            return icon;
    }

    if (spec.fallback)
        return style->standardIcon(*spec.fallback);

    return {};
}
