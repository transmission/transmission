// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QItemDelegate>

#include <libtransmission/tr-macros.h>

class QAbstractItemModel;
class QComboBox;

class FilterBarComboBoxDelegate : public QItemDelegate
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(FilterBarComboBoxDelegate)

public:
    FilterBarComboBoxDelegate(QObject* parent, QComboBox* combo);

    static bool isSeparator(QModelIndex const& index);
    static void setSeparator(QAbstractItemModel* model, QModelIndex const& index);

protected:
    // QAbstractItemDelegate
    void paint(QPainter*, QStyleOptionViewItem const&, QModelIndex const&) const override;
    QSize sizeHint(QStyleOptionViewItem const&, QModelIndex const&) const override;

private:
    QComboBox* const combo_ = {};
};
