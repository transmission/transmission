/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QItemDelegate>

class QAbstractItemModel;
class QComboBox;

class FilterBarComboBoxDelegate : public QItemDelegate
{
    Q_OBJECT

public:
    FilterBarComboBoxDelegate(QObject* parent, QComboBox* combo);

    static bool isSeparator(QModelIndex const& index);
    static void setSeparator(QAbstractItemModel* model, QModelIndex const& index);

protected:
    // QAbstractItemDelegate
    void paint(QPainter*, QStyleOptionViewItem const&, QModelIndex const&) const override;
    QSize sizeHint(QStyleOptionViewItem const&, QModelIndex const&) const override;

private:
    QComboBox* const myCombo;
};
