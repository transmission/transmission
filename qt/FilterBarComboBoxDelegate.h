/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILTER_BAR_COMBO_BOX_DELEGATE_H
#define QTR_FILTER_BAR_COMBO_BOX_DELEGATE_H

#include <QItemDelegate>

class QAbstractItemModel;
class QComboBox;

class FilterBarComboBoxDelegate: public QItemDelegate
{
    Q_OBJECT

  public:
    FilterBarComboBoxDelegate (QObject * parent, QComboBox * combo);

    static bool isSeparator (const QModelIndex &index);
    static void setSeparator (QAbstractItemModel * model, const QModelIndex& index);

  protected:
    // QAbstractItemDelegate
    virtual void paint (QPainter *, const QStyleOptionViewItem&, const QModelIndex&) const;
    virtual QSize sizeHint (const QStyleOptionViewItem&, const QModelIndex&) const;

  private:
    QComboBox * const myCombo;
};

#endif // QTR_FILTER_BAR_COMBO_BOX_DELEGATE_H
