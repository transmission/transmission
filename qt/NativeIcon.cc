// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "NativeIcon.h"

#include <iostream>

#include <QtGui/QIcon>
#include <QtGui/QPixmap>
#include <QtGui/QPainter>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>

namespace
{

#if defined(Q_OS_WIN)
QChar parseCodepoint(QString const& code)
{
    // Supports "E710" or "\uE710"
    QString s = code.trimmed();
    if (s.startsWith("\\u", Qt::CaseInsensitive))
        s = s.mid(2);
    bool ok = false;
    uint u = s.toUInt(&ok, 16);
    return ok ? QChar(u) : QChar();
}

QPixmap makeFluentPixmap(QString const& codepointHex, int const pointSize)
{
    if (codepointHex.isEmpty())
        return {};

    const QChar glyph = parseCodepoint(codepointHex);
    if (glyph.isNull())
        return {};

    // Prefer Segoe Fluent Icons (Win11), fall back to Segoe MDL2 Assets (Win10)
    QFont font;
    font.setPointSize(pointSize);
    font.setStyleStrategy(QFont::PreferDefault);

    // Try Fluent first
    font.setFamily("Segoe Fluent Icons");
    if (!QFontInfo(font).exactMatch()) {
        font.setFamily("Segoe MDL2 Assets");
    }

    auto const dpr = qApp->primaryScreen()->devicePixelRatio();
    auto const px = qRound(pointSize * dpr) + 8; // padding
    QPixmap pm{px, px};
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    auto const fg = qApp->palette().color(QPalette::ButtonText);
    QPainter p{&pm};
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fg);
    p.setFont(font);

    // Center the glyph
    QRectF r(0, 0, pm.width() / dpr, pm.height() / dpr);
    p.drawText(r, Qt::AlignCenter, QString(glyph));
    p.end();

    return QIcon{pm};
}
# endif  // Q_OS_WIN

}  // namespace

QIcon NativeIcon::get(Spec const& spec, QStyle* style)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    auto constexpr auto IconMetrics = std::array<QStyle::PixelMetric, 7U>{{
        QStyle::PM_ButtonIconSize
        QStyle::PM_LargeIconSize,
        QStyle::PM_ListViewIconSize,
        QStyle::PM_MessageBoxIconSize,
        QStyle::PM_SmallIconSize,
        QStyle::PM_TabBarIconSize,
        QStyle::PM_ToolBarIconSize,
    }};

    auto dipSizes = std::set<QSize>{};
    for (auto const pm : IconMetrics) {
        auto const dip = style->pixelMetric(pm);
        dipSizes.emplace(dip, dip);
    }
#endif

#if defined(Q_OS_MAC)
    // TODO: try sfSymbolName if on macOS
    // https://stackoverflow.com/questions/74747658/how-to-convert-a-cgimageref-to-a-qpixmap-in-qt-6
#endif

#if defined(Q_OS_WIN)
    if (!spec.fluentCodepoint.isEmpty()) {
        auto icon = QIcon{};
        for (auto const dipSize : dipSizes)
            if (auto pixmap = makeFluentIcon(spec.fluentCodepoint, dipSize); !pixmap.isNull())
                icon.addPixmap(pixmap);
        if (!icon.isNull())
            return icon;
    }
#endif

    if (!spec.fdoName.isEmpty()) {
        if (auto icon = QIcon::fromTheme(spec.fdoName + QStringLiteral("-symbolic")); !icon.isNull()) {
            std::cerr << "using fdo" << std::endl;
            return icon;
        } else {
            std::cerr << "QIcon::fromTheme(" << qPrintable(spec.fdoName) << ") returned empty" << std::endl;
        }
    }

    if (spec.fallback) {
        if (auto icon = style->standardIcon(*spec.fallback); !icon.isNull()) {
            std::cerr << "Qt Standard Icon" << std::endl;
            return icon;
        }
    }

    std::cerr << "no match" << std::endl;
    return {};
}
