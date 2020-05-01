/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "TorrentDelegate.h"

class TorrentDelegateMin : public TorrentDelegate
{
    Q_OBJECT

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
