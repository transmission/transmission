// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QApplication>
#include <QPainter>

#include "FileTreeDelegate.h"
#include "FileTreeModel.h"
#include "StyleHelper.h"

QSize FileTreeDelegate::sizeHint(QStyleOptionViewItem const& item, QModelIndex const& index) const
{
    QSize size;

    switch (index.column())
    {
    case FileTreeModel::COL_PROGRESS:
    case FileTreeModel::COL_WANTED:
        size = QSize(20, 1);
        break;

    default:
        size = QItemDelegate::sizeHint(item, index);
    }

    size.rheight() += 8; // make the spacing a little nicer
    return size;
}

void FileTreeDelegate::paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const
{
    int const column(index.column());

    if (column != FileTreeModel::COL_PROGRESS && column != FileTreeModel::COL_WANTED)
    {
        QItemDelegate::paint(painter, option, index);
        return;
    }

    QStyle const* style = QApplication::style();

    painter->save();
    QItemDelegate::drawBackground(painter, option, index);

    if (column == FileTreeModel::COL_PROGRESS)
    {
        QStyleOptionProgressBar p;
        p.state = option.state | QStyle::State_Horizontal | QStyle::State_Small;
        p.direction = QApplication::layoutDirection();
        p.rect = option.rect;
        p.rect.setSize(QSize(option.rect.width() - 4, option.rect.height() - 8));
        p.rect.moveCenter(option.rect.center());
        p.fontMetrics = QFontMetrics{ QApplication::font() };
        p.minimum = 0;
        p.maximum = 100;
        p.textAlignment = Qt::AlignCenter;
        p.textVisible = true;
        p.progress = static_cast<int>(100.0 * index.data().toDouble());
        p.text = QStringLiteral("%1%").arg(p.progress);
        StyleHelper::drawProgressBar(*style, *painter, p);
    }
    else if (column == FileTreeModel::COL_WANTED)
    {
        QStyleOptionViewItem vi(option);
        vi.features |= QStyleOptionViewItem::HasCheckIndicator;
        QRect check_rect = style->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &vi, nullptr);
        check_rect.moveCenter(option.rect.center());
        drawCheck(painter, vi, check_rect, static_cast<Qt::CheckState>(index.data().toInt()));
    }

    QItemDelegate::drawFocus(painter, option, option.rect);
    painter->restore();
}
