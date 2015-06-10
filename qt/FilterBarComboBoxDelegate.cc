/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <QAbstractItemView>
#include <QComboBox>
#include <QStandardItemModel>
#include <QStyle>

#include "FilterBarComboBox.h"
#include "FilterBarComboBoxDelegate.h"
#include "Utils.h"

namespace
{
  int
  getHSpacing (const QWidget * w)
  {
    return qMax (3, w->style ()->pixelMetric (QStyle::PM_LayoutHorizontalSpacing, 0, w));
  }
}

FilterBarComboBoxDelegate::FilterBarComboBoxDelegate (QObject * parent, QComboBox * combo):
  QItemDelegate (parent),
  myCombo (combo)
{
}

bool
FilterBarComboBoxDelegate::isSeparator (const QModelIndex& index)
{
  return index.data (Qt::AccessibleDescriptionRole).toString () == QLatin1String ("separator");
}
void
FilterBarComboBoxDelegate::setSeparator (QAbstractItemModel * model, const QModelIndex& index)
{
  model->setData (index, QString::fromLatin1 ("separator"), Qt::AccessibleDescriptionRole);

  if (QStandardItemModel *m = qobject_cast<QStandardItemModel*> (model))
    if (QStandardItem *item = m->itemFromIndex (index))
      item->setFlags (item->flags () & ~ (Qt::ItemIsSelectable|Qt::ItemIsEnabled));
}

void
FilterBarComboBoxDelegate::paint (QPainter                    * painter,
                                  const QStyleOptionViewItem  & option,
                                  const QModelIndex           & index) const
{
  if (isSeparator (index))
    {
      QRect rect = option.rect;
      if (const QStyleOptionViewItemV3 *v3 = qstyleoption_cast<const QStyleOptionViewItemV3*> (&option))
        if (const QAbstractItemView *view = qobject_cast<const QAbstractItemView*> (v3->widget))
          rect.setWidth (view->viewport ()->width ());
      QStyleOption opt;
      opt.rect = rect;
      myCombo->style ()->drawPrimitive (QStyle::PE_IndicatorToolBarSeparator, &opt, painter, myCombo);
    }
  else
    {
      QStyleOptionViewItem disabledOption = option;
      const QPalette::ColorRole disabledColorRole = (disabledOption.state & QStyle::State_Selected) ?
                                                     QPalette::HighlightedText : QPalette::Text;
      disabledOption.palette.setColor (disabledColorRole, Utils::getFadedColor (disabledOption.palette.color (disabledColorRole)));

      QRect boundingBox = option.rect;

      const int hmargin = getHSpacing (myCombo);
      boundingBox.adjust (hmargin, 0, -hmargin, 0);

      QRect decorationRect = rect (option, index, Qt::DecorationRole);
      decorationRect.setSize (myCombo->iconSize ());
      decorationRect = QStyle::alignedRect (option.direction,
                                            Qt::AlignLeft|Qt::AlignVCenter,
                                            decorationRect.size (), boundingBox);
      Utils::narrowRect (boundingBox, decorationRect.width () + hmargin, 0, option.direction);

      QRect countRect  = rect (option, index, FilterBarComboBox::CountStringRole);
      countRect = QStyle::alignedRect (option.direction,
                                       Qt::AlignRight|Qt::AlignVCenter,
                                       countRect.size (), boundingBox);
      Utils::narrowRect (boundingBox, 0, countRect.width () + hmargin, option.direction);
      const QRect displayRect = boundingBox;

      drawBackground (painter, option, index);
      QStyleOptionViewItem option2 = option;
      option2.decorationSize = myCombo->iconSize ();
      drawDecoration (painter, option, decorationRect, decoration (option2,index.data (Qt::DecorationRole)));
      drawDisplay (painter, option, displayRect, index.data (Qt::DisplayRole).toString ());
      drawDisplay (painter, disabledOption, countRect, index.data (FilterBarComboBox::CountStringRole).toString ());
      drawFocus (painter, option, displayRect|countRect);
    }
}

QSize
FilterBarComboBoxDelegate::sizeHint (const QStyleOptionViewItem & option,
                                     const QModelIndex          & index) const
{
  if (isSeparator (index))
    {
      const int pm = myCombo->style ()->pixelMetric (QStyle::PM_DefaultFrameWidth, 0, myCombo);
      return QSize (pm, pm + 10);
    }
  else
    {
      QStyle * s = myCombo->style ();
      const int hmargin = getHSpacing (myCombo);

      QSize size = QItemDelegate::sizeHint (option, index);
      size.setHeight (qMax (size.height (), myCombo->iconSize ().height () + 6));
      size.rwidth () += s->pixelMetric (QStyle::PM_FocusFrameHMargin, 0, myCombo);
      size.rwidth () += rect (option,index,FilterBarComboBox::CountStringRole).width ();
      size.rwidth () += hmargin * 4;
      return size;
    }
}
