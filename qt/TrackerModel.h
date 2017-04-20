/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

#include "Torrent.h"

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
    TrackerModel()
    {
    }

    virtual ~TrackerModel()
    {
    }

    void refresh(TorrentModel const&, QSet<int> const& ids);
    int find(int torrentId, QString const& url) const;

    // QAbstractItemModel
    virtual int rowCount(QModelIndex const& parent = QModelIndex()) const;
    virtual QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const;

private:
    typedef QVector<TrackerInfo> rows_t;

private:
    rows_t myRows;
};
