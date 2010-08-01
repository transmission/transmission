/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id: torrent-delegate.h 9868 2010-01-04 21:00:47Z charles $
 */

#ifndef QTR_TORRENT_DELEGATE_H
#define QTR_TORRENT_DELEGATE_H

#include <QItemDelegate>
#include <QSize>

class QPainter;
class QStyleOptionViewItem;
class QStyle;
class Session;
class TrackerInfo;

class TrackerDelegate: public QItemDelegate
{
        Q_OBJECT

    public:
        TrackerDelegate( QObject * parent=0 ): QItemDelegate(parent), myShowMore(false) { }
        virtual ~TrackerDelegate( ) { }

    public:
        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;

    public:
        void setShowMore( bool b );

    protected:
        QString getText( const TrackerInfo& ) const; 
        QSize margin( const QStyle& style ) const;
        virtual QSize sizeHint( const QStyleOptionViewItem&, const TrackerInfo& ) const;
        void drawTracker( QPainter*, const QStyleOptionViewItem&, const TrackerInfo& ) const;

    private:
        bool myShowMore;
};

#endif
