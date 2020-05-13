/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QApplication>
#include <QStyle>
#include <QStylePainter>

#include "FilterBarComboBox.h"
#include "StyleHelper.h"
#include "Utils.h"

namespace
{

int getHSpacing(QWidget const* w)
{
    return qMax(3, w->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, w));
}

} // namespace

FilterBarComboBox::FilterBarComboBox(QWidget* parent) :
    QComboBox(parent)
{
    setSizeAdjustPolicy(QComboBox::AdjustToContents);
}

QSize FilterBarComboBox::minimumSizeHint() const
{
    QFontMetrics fm(fontMetrics());
    QSize const textSize = fm.boundingRect(itemText(0)).size();
    QSize const countSize = fm.boundingRect(itemData(0, CountStringRole).toString()).size();
    return calculateSize(textSize, countSize);
}

QSize FilterBarComboBox::sizeHint() const
{
    QFontMetrics fm(fontMetrics());
    QSize maxTextSize(0, 0);
    QSize maxCountSize(0, 0);

    for (int i = 0, n = count(); i < n; ++i)
    {
        QSize const textSize = fm.boundingRect(itemText(i)).size();
        maxTextSize.setHeight(qMax(maxTextSize.height(), textSize.height()));
        maxTextSize.setWidth(qMax(maxTextSize.width(), textSize.width()));

        QSize const countSize = fm.boundingRect(itemData(i, CountStringRole).toString()).size();
        maxCountSize.setHeight(qMax(maxCountSize.height(), countSize.height()));
        maxCountSize.setWidth(qMax(maxCountSize.width(), countSize.width()));
    }

    return calculateSize(maxTextSize, maxCountSize);
}

QSize FilterBarComboBox::calculateSize(QSize const& textSize, QSize const& countSize) const
{
    int const hmargin = getHSpacing(this);

    QStyleOptionComboBox option;
    initStyleOption(&option);

    QSize contentSize = iconSize() + QSize(4, 2);
    contentSize.setHeight(qMax(contentSize.height(), textSize.height()));
    contentSize.rwidth() += hmargin + textSize.width();
    contentSize.rwidth() += hmargin + countSize.width();

    return style()->sizeFromContents(QStyle::CT_ComboBox, &option, contentSize, this).expandedTo(qApp->globalStrut());
}

void FilterBarComboBox::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e)

    QStylePainter painter(this);
    painter.setPen(palette().color(QPalette::Text));

    // draw the combobox frame, focusrect and selected etc.
    QStyleOptionComboBox opt;
    initStyleOption(&opt);
    painter.drawComplexControl(QStyle::CC_ComboBox, opt);

    // draw the icon and text
    QModelIndex const modelIndex = model()->index(currentIndex(), 0, rootModelIndex());

    if (modelIndex.isValid())
    {
        QStyle* s = style();
        int const hmargin = getHSpacing(this);

        QRect rect = s->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        rect.adjust(2, 1, -2, -1);

        // draw the icon
        QIcon const icon = Utils::getIconFromIndex(modelIndex);

        if (!icon.isNull())
        {
            QRect const iconRect = QStyle::alignedRect(opt.direction, Qt::AlignLeft | Qt::AlignVCenter, opt.iconSize, rect);
            icon.paint(&painter, iconRect, Qt::AlignCenter, StyleHelper::getIconMode(opt.state), QIcon::Off);
            Utils::narrowRect(rect, iconRect.width() + hmargin, 0, opt.direction);
        }

        // draw the count
        QString text = modelIndex.data(CountStringRole).toString();

        if (!text.isEmpty())
        {
            QPen const pen = painter.pen();
            painter.setPen(Utils::getFadedColor(pen.color()));
            QRect const textRect = QStyle::alignedRect(opt.direction, Qt::AlignRight | Qt::AlignVCenter,
                QSize(opt.fontMetrics.width(text), rect.height()), rect);
            painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, text);
            Utils::narrowRect(rect, 0, textRect.width() + hmargin, opt.direction);
            painter.setPen(pen);
        }

        // draw the text
        text = modelIndex.data(Qt::DisplayRole).toString();
        text = painter.fontMetrics().elidedText(text, Qt::ElideRight, rect.width());
        painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }
}
