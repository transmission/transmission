// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <vector>

#include <QAbstractListModel>

#include <libtransmission/tr-macros.h>

#include "Torrent.h"
#include "Typedefs.h"

class TorrentModel;

struct TrackerInfo
{
    TrackerStat st;
    int torrent_id = {};
};

Q_DECLARE_METATYPE(TrackerInfo)

class TrackerModel : public QAbstractListModel
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TrackerModel)

public:
    enum Role
    {
        TrackerRole = Qt::UserRole
    };

    TrackerModel() = default;

    void refresh(TorrentModel const&, torrent_ids_t const& ids);
    int find(int torrent_id, QString const& url) const;

    // QAbstractItemModel
    int rowCount(QModelIndex const& parent = QModelIndex()) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

private:
    using rows_t = std::vector<TrackerInfo>;

    rows_t rows_;
};
