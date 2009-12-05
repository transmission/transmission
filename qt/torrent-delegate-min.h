/*
 * This file Copyright (C) 2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_DELEGATE_MIN_H
#define QTR_TORRENT_DELEGATE_MIN_H

#include <QSize>
#include "torrent-delegate.h"

class QStyleOptionViewItem;
class QStyle;
class Session;
class Torrent;

class TorrentDelegateMin: public TorrentDelegate
{
        Q_OBJECT

    protected:
        virtual QSize sizeHint( const QStyleOptionViewItem&, const Torrent& ) const;
        void drawTorrent( QPainter* painter, const QStyleOptionViewItem& option, const Torrent& ) const;

    public:
        explicit TorrentDelegateMin( QObject * parent=0 ): TorrentDelegate(parent) { }
        virtual ~TorrentDelegateMin( ) { }
};

#endif
