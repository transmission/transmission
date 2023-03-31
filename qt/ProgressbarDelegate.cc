// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QStyleOptionProgressBar>

#include <libtransmission/transmission.h>

#include <libtransmission/utils.h>

#include "StyleHelper.h"
#include "Torrent.h"
#include "ProgressbarDelegate.h"
#include "Utils.h"

enum
{
    GUI_PAD = 6,
    BAR_HEIGHT = 16
};

namespace
{

class ItemLayout
{
public:
    QRect bar_rect;

    ItemLayout(Qt::LayoutDirection direction, QPoint const& top_left, int width);

    [[nodiscard]] QSize size() const
    {
        return bar_rect.size();
    }
};

ItemLayout::ItemLayout(Qt::LayoutDirection direction, QPoint const& top_left, int width)
{
    auto const* style = QApplication::style();

    QStyleOptionProgressBar bar_style;
    bar_style.rect = QRect(0, 0, width, BAR_HEIGHT);
    bar_style.maximum = 100;
    bar_style.progress = 100;
    bar_style.textVisible = true;
    QSize const bar_size(bar_style.rect.width(), bar_style.rect.height());

    QRect base_rect(top_left, QSize(width, bar_size.height()));

    bar_rect = QStyle::alignedRect(direction, Qt::AlignCenter, bar_size, base_rect);
    Utils::narrowRect(base_rect, GUI_PAD, bar_rect.width() + GUI_PAD, direction);
    Utils::narrowRect(base_rect, 0, GUI_PAD, direction);
}

} // namespace

QSize ProgressbarDelegate::sizeHint(QStyleOptionViewItem const& option, Torrent const& /*tor*/) const
{
    auto const m = margin(*QApplication::style());
    auto const layout = ItemLayout(option.direction, QPoint(0, 0), option.rect.width() - m.width() * 2);
    return layout.size() + m * 2;
}

void ProgressbarDelegate::drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const& tor) const
{
    auto const* style = QApplication::style();

    bool const is_paused(tor.isPaused());

    bool const is_item_selected((option.state & QStyle::State_Selected) != 0);
    bool const is_item_enabled((option.state & QStyle::State_Enabled) != 0);
    bool const is_item_active((option.state & QStyle::State_Active) != 0);

    painter->save();

    if (is_item_selected)
    {
        auto color_group = is_item_enabled ? QPalette::Normal : QPalette::Disabled;

        if (color_group == QPalette::Normal && !is_item_active)
        {
            color_group = QPalette::Inactive;
        }

        painter->fillRect(option.rect, option.palette.brush(color_group, QPalette::Highlight));
    }

    QPalette::ColorGroup color_group = QPalette::Normal;

    if (is_paused || !is_item_enabled)
    {
        color_group = QPalette::Disabled;
    }

    if (color_group == QPalette::Normal && !is_item_active)
    {
        color_group = QPalette::Inactive;
    }

    auto const color_role = is_item_selected ? QPalette::HighlightedText : QPalette::Text;

    QStyle::State progress_bar_state(option.state);

    if (is_paused)
    {
        progress_bar_state = QStyle::State_None;
    }

    progress_bar_state |= QStyle::State_Small | QStyle::State_Horizontal;

    // layout
    QSize const m(margin(*style));
    QRect const content_rect(option.rect.adjusted(m.width(), m.height(), -m.width(), -m.height()));
    ItemLayout const layout(option.direction, content_rect.topLeft(), content_rect.width());

    // render
    if (tor.hasError() && !is_item_selected)
    {
        painter->setPen(QColor("red"));
    }
    else
    {
        painter->setPen(option.palette.color(color_group, color_role));
    }

    progress_bar_style_.rect = layout.bar_rect;

    if (tor.isDownloading())
    {
        progress_bar_style_.palette.setBrush(QPalette::Highlight, BlueBrush);
        progress_bar_style_.palette.setColor(QPalette::Base, BlueBack);
        progress_bar_style_.palette.setColor(QPalette::Window, BlueBack);
    }
    else if (tor.isSeeding())
    {
        progress_bar_style_.palette.setBrush(QPalette::Highlight, GreenBrush);
        progress_bar_style_.palette.setColor(QPalette::Base, GreenBack);
        progress_bar_style_.palette.setColor(QPalette::Window, GreenBack);
    }
    else
    {
        progress_bar_style_.palette.setBrush(QPalette::Highlight, SilverBrush);
        progress_bar_style_.palette.setColor(QPalette::Base, SilverBack);
        progress_bar_style_.palette.setColor(QPalette::Window, SilverBack);
    }

    progress_bar_style_.state = progress_bar_state;
    progress_bar_style_.text = QStringLiteral("%1%").arg(static_cast<int>(tr_truncd(100.0 * tor.percentDone(), 0)));
    progress_bar_style_.textVisible = true;
    progress_bar_style_.textAlignment = Qt::AlignCenter;
    setProgressBarPercentDone(option, tor);
    StyleHelper::drawProgressBar(*style, *painter, progress_bar_style_);

    painter->restore();
}
