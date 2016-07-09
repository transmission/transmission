/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#pragma once

#include <QItemDelegate>

class FileTreeDelegate: public QItemDelegate
{
    Q_OBJECT

  public:
    FileTreeDelegate (QObject * parent = nullptr): QItemDelegate (parent) {}
    virtual ~FileTreeDelegate () {}

  public:
    // QAbstractItemDelegate
    virtual QSize sizeHint (const QStyleOptionViewItem&, const QModelIndex&) const;
    virtual void paint (QPainter *, const QStyleOptionViewItem&, const QModelIndex&) const;
};

