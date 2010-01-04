/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_DELEGATE_H
#define QTR_TORRENT_DELEGATE_H

#include <QItemDelegate>
#include <QSize>

class QStyleOptionProgressBarV2;
class QStyleOptionViewItem;
class QStyle;
class Session;
class Torrent;

class TorrentDelegate: public QItemDelegate
{
        Q_OBJECT

    protected:
        QStyleOptionProgressBarV2 * myProgressBarStyle;

    protected:
        QString statusString( const Torrent& tor ) const;
        QString progressString( const Torrent& tor ) const;
        QString shortStatusString( const Torrent& tor ) const;
        QString shortTransferString( const Torrent& tor ) const;

    protected:
        QSize margin( const QStyle& style ) const;
        virtual QSize sizeHint( const QStyleOptionViewItem&, const Torrent& ) const;
        virtual void drawTorrent( QPainter* painter, const QStyleOptionViewItem& option, const Torrent& ) const;

    public:
        explicit TorrentDelegate( QObject * parent=0 );
        virtual ~TorrentDelegate( );

        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const;
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const;
};

#endif
