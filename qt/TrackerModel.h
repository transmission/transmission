/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "Torrent.h"
#include "Typedefs.h"

class TorrentModel;

struct TrackerInfo
{
    TrackerStat st;
    int torrentId;
};

Q_DECLARE_METATYPE(TrackerInfo)

class TrackerModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        TrackerRole = Qt::UserRole
    };

public:
    TrackerModel() = default;

    void refresh(TorrentModel const&, torrent_ids_t const& ids);
    int find(int torrentId, QString const& url) const;

    // QAbstractItemModel
    int rowCount(QModelIndex const& parent = QModelIndex()) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

private:
    typedef QVector<TrackerInfo> rows_t;

private:
    rows_t myRows;
};
