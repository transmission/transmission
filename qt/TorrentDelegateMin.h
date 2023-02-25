// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/tr-macros.h>

#include "TorrentDelegate.h"

class TorrentDelegateMin : public TorrentDelegate
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentDelegateMin)

public:
    explicit TorrentDelegateMin(QObject* parent = nullptr)
        : TorrentDelegate(parent)
    {
    }

protected:
    // TorrentDelegate
    QSize sizeHint(QStyleOptionViewItem const&, Torrent const&) const override;
    void drawTorrent(QPainter* painter, QStyleOptionViewItem const& option, Torrent const&) const override;
};
