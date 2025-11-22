// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "NativeIcon.h"

#include <iostream>
#include <set>

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

namespace
{

#if defined(Q_OS_WIN)
QPixmap makeFluentPixmap(QChar const codepoint, int const point_size)
{
    static auto const is_win11 = QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows11;
    auto const fontName = is_win11 ? QStringLiteral("Segoe Fluent Icons") : QStringLiteral("Segoe MDL2 Assets");
    auto const font = QFont{fontName, point_size};

    if (!QFontMetrics{font}.inFont(codepoint))
	    return {};

    // TODO: HDPI, pixel size vs point size?
    auto const rect = QRect{ QPoint{}, QSize{point_size, point_size}};
    auto pixmap = QPixmap{ rect.size() };
    pixmap.fill(Qt::red);
    auto painter = QPainter{ &pixmap };
    painter.setFont(font);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{Qt::green});
    auto br = QRect{};
    painter.drawText(rect, Qt::AlignCenter, QString{codepoint}, &br);
    painter.end();

    std::cerr << "br x " << br.x() << " y " << br.y() << " w " << br.width() << " h " << br.height() << std::endl;
    // FIXME: segoe icon not rendering -- br.width() is zero??
    // This is a blocker

    return pixmap;
}
#endif // Q_OS_WIN

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
    auto constexpr IconMetrics = std::array<QStyle::PixelMetric, 7U>{
        QStyle::PM_LargeIconSize, QStyle::PM_ButtonIconSize, QStyle::PM_ListViewIconSize, QStyle::PM_MessageBoxIconSize,
        QStyle::PM_SmallIconSize, QStyle::PM_TabBarIconSize, QStyle::PM_ToolBarIconSize,
    };

    auto point_sizes = std::set<int>{};
    for (auto const pm : IconMetrics)
        point_sizes.emplace(style->pixelMetric(pm));

#if 0
    if (auto const todo = QStringLiteral("TODO"); spec.sfSymbolName == todo || spec.fluentCodePoint == todo || spec.fdoName == todo)
      abort();
#endif

#if defined(Q_OS_MAC)
        // TODO: try sfSymbolName if on macOS
        // https://stackoverflow.com/questions/74747658/how-to-convert-a-cgimageref-to-a-qpixmap-in-qt-6
#endif

#if defined(Q_OS_WIN)
    if (!spec.fluentCodepoint.isNull())
    {
#if 0
        auto icon = QIcon{};
        for (int const point_size : point_sizes)
            if (QPixmap const pixmap = makeFluentPixmap(spec.fluentCodepoint, point_size); !pixmap.isNull())
                icon.addPixmap(pixmap);
        if (!icon.isNull())
            return icon;
#else
	return makeFluentPixmap(spec.fluentCodepoint, 24);
#endif
    }
#endif

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
