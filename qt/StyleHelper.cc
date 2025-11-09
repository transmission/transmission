// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>

#include <QPainter>
#include <QStaticText>
#include <QStyleOptionProgressBar>

#include "StyleHelper.h"

QIcon::Mode StyleHelper::getIconMode(QStyle::State const& state)
{
    if (!state.testFlag(QStyle::State_Enabled))
    {
        return QIcon::Disabled;
    }

    if (state.testFlag(QStyle::State_Selected))
    {
        return QIcon::Selected;
    }

    return QIcon::Normal;
}

void StyleHelper::drawProgressBar(QPainter& painter, QStyleOptionProgressBar const& option)
{
    painter.save();

    auto rect = option.rect.adjusted(0, 0, -1, -1);

    painter.setPen(option.palette.color(QPalette::Base));
    painter.drawRect(rect);

    rect.adjust(1, 1, 0, 0);

    auto const max_pos = std::max(option.maximum - option.minimum, 1);
    auto const pos = std::min(std::max(option.progress - option.minimum, 0), max_pos);
    auto const bar_width = pos * rect.width() / max_pos;
    auto const is_inverted = option.invertedAppearance != (option.direction == Qt::RightToLeft);

    if (pos < max_pos)
    {
        auto back_rect = rect;
        if (pos > 0)
        {
            back_rect.setWidth(back_rect.width() - bar_width);
            if (!is_inverted)
            {
                back_rect.moveRight(rect.right());
            }
        }

        painter.fillRect(back_rect, option.palette.brush(QPalette::Window));
    }

    if (pos > 0)
    {
        auto bar_rect = rect;
        if (pos < max_pos)
        {
            bar_rect.setWidth(bar_width);
            if (is_inverted)
            {
                bar_rect.moveRight(rect.right());
            }
        }

        painter.fillRect(bar_rect, option.palette.brush(QPalette::Highlight));
    }

    if (option.textVisible)
    {
        rect.adjust(1, 0, -1, 0);

        auto text = QStaticText(option.text);
        text.setTextFormat(Qt::PlainText);
        text.prepare({}, painter.font());
        auto const text_pos = QStyle::alignedRect(option.direction, option.textAlignment, text.size().toSize(), rect).topLeft();

        auto const text_color = option.palette.color(QPalette::WindowText);
        auto const shadow_color = text_color.value() <= 128 ? QColor(255, 255, 255, 160) : QColor(0, 0, 0, 160);

        painter.setPen(shadow_color);
        for (int i = -1; i <= 1; ++i)
        {
            for (int j = -1; j <= 1; ++j)
            {
                if (i != 0 && j != 0)
                {
                    painter.drawStaticText(text_pos.x() + i, text_pos.y() + j, text);
                }
            }
        }

        painter.setPen(text_color);
        painter.drawStaticText(text_pos, text);
    }

    painter.restore();
}
