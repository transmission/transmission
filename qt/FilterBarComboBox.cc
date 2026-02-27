// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>

#include <QApplication>
#include <QComboBox>
#include <QPen>
#include <QRect>
#include <QStyle>
#include <QStylePainter>

#include "FilterBarComboBox.h"
#include "StyleHelper.h"
#include "Utils.h"

namespace
{

int get_h_spacing(QWidget const* w)
{
    return std::max(3, w->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, w));
}

} // namespace

FilterBarComboBox::FilterBarComboBox(QWidget* parent)
    : QComboBox{ parent }
{
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

QSize FilterBarComboBox::minimumSizeHint() const
{
    auto const fm = fontMetrics();
    auto const text_size = fm.size(0, itemText(0));
    auto const count_size = fm.size(0, itemData(0, CountStringRole).toString());
    return calculate_size(text_size, count_size);
}

QSize FilterBarComboBox::sizeHint() const
{
    auto const fm = fontMetrics();
    auto max_text_size = QSize{ 0, 0 };
    auto max_count_size = QSize{ 0, 0 };

    for (int i = 0, n = count(); i < n; ++i)
    {
        auto const text_size = fm.size(0, itemText(i));
        max_text_size.setHeight(qMax(max_text_size.height(), text_size.height()));
        max_text_size.setWidth(qMax(max_text_size.width(), text_size.width()));

        auto const count_size = fm.size(0, itemData(i, CountStringRole).toString());
        max_count_size.setHeight(qMax(max_count_size.height(), count_size.height()));
        max_count_size.setWidth(qMax(max_count_size.width(), count_size.width()));
    }

    return calculate_size(max_text_size, max_count_size);
}

QSize FilterBarComboBox::calculate_size(QSize const& text_size, QSize const& count_size) const
{
    auto const hmargin = get_h_spacing(this);

    QStyleOptionComboBox option;
    initStyleOption(&option);

    auto content_size = iconSize() + QSize{ 4, 2 };
    content_size.setHeight(qMax(content_size.height(), text_size.height()));
    content_size.rwidth() += hmargin + text_size.width();
    content_size.rwidth() += hmargin + count_size.width();

    return style()->sizeFromContents(QStyle::CT_ComboBox, &option, content_size, this);
}

void FilterBarComboBox::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e)

    auto painter = QStylePainter{ this };
    painter.setPen(palette().color(QPalette::Text));

    // draw the combobox frame, focusrect and selected etc.
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // draw the icon and text
    QModelIndex const model_index = model()->index(currentIndex(), 0, rootModelIndex());

    if (model_index.isValid())
    {
        auto const* const s = style();
        int const hmargin = get_h_spacing(this);

        QRect rect = s->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        rect.adjust(2, 1, -2, -1);

        // draw the icon
        if (auto const icon = Utils::get_icon_from_index(model_index); !icon.isNull())
        {
            auto const icon_rect = QStyle::alignedRect(opt.direction, Qt::AlignLeft | Qt::AlignVCenter, opt.iconSize, rect);
            icon.paint(&painter, icon_rect, Qt::AlignCenter, StyleHelper::get_icon_mode(opt.state), QIcon::Off);
            Utils::narrow_rect(rect, icon_rect.width() + hmargin, 0, opt.direction);
        }

        // draw the count
        auto text = model_index.data(CountStringRole).toString();

        if (!text.isEmpty())
        {
            QPen const pen = painter.pen();
            painter.setPen(Utils::get_faded_color(pen.color()));
            QRect const text_rect = QStyle::alignedRect(
                opt.direction,
                Qt::AlignRight | Qt::AlignVCenter,
                QSize{ opt.fontMetrics.size(0, text).width(), rect.height() },
                rect);
            painter.drawText(text_rect, Qt::AlignRight | Qt::AlignVCenter, text);
            Utils::narrow_rect(rect, 0, text_rect.width() + hmargin, opt.direction);
            painter.setPen(pen);
        }

        // draw the text
        text = model_index.data(Qt::DisplayRole).toString();
        text = painter.fontMetrics().elidedText(text, Qt::ElideRight, rect.width());
        painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}
