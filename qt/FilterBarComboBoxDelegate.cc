/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QAbstractItemView>
#include <QComboBox>
#include <QStandardItemModel>
#include <QStyle>

#include "FilterBarComboBox.h"
#include "FilterBarComboBoxDelegate.h"
#include "StyleHelper.h"
#include "Utils.h"

namespace
{

int getHSpacing(QWidget const* w)
{
    return qMax(3, w->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing, nullptr, w));
}

} // namespace

FilterBarComboBoxDelegate::FilterBarComboBoxDelegate(QObject* parent, QComboBox* combo) :
    QItemDelegate(parent),
    myCombo(combo)
{
}

bool FilterBarComboBoxDelegate::isSeparator(QModelIndex const& index)
{
    return index.data(Qt::AccessibleDescriptionRole).toString() == QLatin1String("separator");
}

void FilterBarComboBoxDelegate::setSeparator(QAbstractItemModel* model, QModelIndex const& index)
{
    model->setData(index, QString::fromLatin1("separator"), Qt::AccessibleDescriptionRole);

    if (QStandardItemModel* m = qobject_cast<QStandardItemModel*>(model))
    {
        if (QStandardItem* item = m->itemFromIndex(index))
        {
            item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
        }
    }
}

void FilterBarComboBoxDelegate::paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    if (isSeparator(index))
    {
        QRect rect = option.rect;

        if (QAbstractItemView const* view = qobject_cast<QAbstractItemView const*>(option.widget))
        {
            rect.setWidth(view->viewport()->width());
        }

        QStyleOption opt;
        opt.rect = rect;
        myCombo->style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator, &opt, painter, myCombo);
    }
    else
    {
        QStyleOptionViewItem disabledOption = option;
        QPalette::ColorRole const disabledColorRole = (disabledOption.state & QStyle::State_Selected) != 0 ?
            QPalette::HighlightedText : QPalette::Text;
        disabledOption.palette.setColor(disabledColorRole, Utils::getFadedColor(disabledOption.palette.color(
            disabledColorRole)));

        QRect boundingBox = option.rect;

        int const hmargin = getHSpacing(myCombo);
        boundingBox.adjust(hmargin, 0, -hmargin, 0);

        QRect decorationRect = rect(option, index, Qt::DecorationRole);
        decorationRect.setSize(myCombo->iconSize());
        decorationRect = QStyle::alignedRect(option.direction, Qt::AlignLeft | Qt::AlignVCenter, decorationRect.size(),
            boundingBox);
        Utils::narrowRect(boundingBox, decorationRect.width() + hmargin, 0, option.direction);

        QRect countRect = rect(option, index, FilterBarComboBox::CountStringRole);
        countRect = QStyle::alignedRect(option.direction, Qt::AlignRight | Qt::AlignVCenter, countRect.size(), boundingBox);
        Utils::narrowRect(boundingBox, 0, countRect.width() + hmargin, option.direction);
        QRect const displayRect = boundingBox;

        QIcon const icon = Utils::getIconFromIndex(index);

        drawBackground(painter, option, index);
        icon.paint(painter, decorationRect, Qt::AlignCenter, StyleHelper::getIconMode(option.state), QIcon::Off);
        drawDisplay(painter, option, displayRect, index.data(Qt::DisplayRole).toString());
        drawDisplay(painter, disabledOption, countRect, index.data(FilterBarComboBox::CountStringRole).toString());
        drawFocus(painter, option, displayRect | countRect);
    }
}

QSize FilterBarComboBoxDelegate::sizeHint(QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    if (isSeparator(index))
    {
        int const pm = myCombo->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, myCombo);
        return QSize(pm, pm + 10);
    }
    else
    {
        QStyle* s = myCombo->style();
        int const hmargin = getHSpacing(myCombo);

        QSize size = QItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), myCombo->iconSize().height() + 6));
        size.rwidth() += s->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, myCombo);
        size.rwidth() += rect(option, index, FilterBarComboBox::CountStringRole).width();
        size.rwidth() += hmargin * 4;
        return size;
    }
}
