/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "Macros.h"
#include "TorrentDelegate.h"

class TorrentDelegateMin : public TorrentDelegate
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentDelegateMin)

public:
    explicit TorrentDelegateMin(QObject* parent = nullptr) :
        TorrentDelegate(parent)
    {
    }

protected:
    // TorrentDelegate
    QSize sizeHint(QStyleOptionViewItem const&, Torrent const&) const override;
    void drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const&) const override;
};
