/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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
