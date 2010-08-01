/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
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
