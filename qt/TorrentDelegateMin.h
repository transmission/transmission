/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_DELEGATE_MIN_H
#define QTR_TORRENT_DELEGATE_MIN_H

#include <QSize>

#include "TorrentDelegate.h"

class QStyleOptionViewItem;
class QStyle;
class Session;
class Torrent;

class TorrentDelegateMin: public TorrentDelegate
{
    Q_OBJECT

  protected:
    virtual QSize sizeHint (const QStyleOptionViewItem&, const Torrent&) const;
    void drawTorrent (QPainter* painter, const QStyleOptionViewItem& option, const Torrent&) const;

  public:
    explicit TorrentDelegateMin (QObject * parent=0): TorrentDelegate(parent) {}
    virtual ~TorrentDelegateMin () {}
};

#endif // QTR_TORRENT_DELEGATE_MIN_H
