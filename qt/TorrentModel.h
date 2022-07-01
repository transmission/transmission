// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <bitset>
#include <map>
#include <optional>
#include <vector>
#include <unordered_map>

#include <QAbstractListModel>
#include <QTimer>

#include <libtransmission/tr-macros.h>

#include "Filters.h"
#include "Torrent.h"
#include "Typedefs.h"

class Prefs;
class Speed;
class QStandardItem;
class QStandardItemModel;

extern "C"
{
    struct tr_variant;
}

class TorrentModel : public QAbstractListModel
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentModel)

public:
    enum Role
    {
        TorrentRole = Qt::UserRole,
        CountRole,
        CountStringRole,
        ActivityRole,
        PathRole,
        TrackerRole,
    };

    explicit TorrentModel(Prefs const& prefs);
    ~TorrentModel() override;
    void clear();

    bool hasTorrent(TorrentHash const& hash) const;

    Torrent* getTorrentFromId(int id);
    Torrent const* getTorrentFromId(int id) const;

    using torrents_t = std::vector<Torrent*>;

    torrents_t const& torrents() const
    {
        return torrents_;
    }

    // QAbstractItemModel
    int rowCount(QModelIndex const& parent = QModelIndex()) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

    QStandardItemModel* activityFilterModel() const
    {
        return activity_model_;
    }
    QStandardItemModel* pathFilterModel() const
    {
        return path_model_;
    }
    QStandardItemModel* trackerFilterModel() const
    {
        return tracker_model_;
    }

public slots:
    void updateTorrents(tr_variant* torrent_list, bool is_complete_list);
    void removeTorrents(tr_variant* torrent_list);

signals:
    void torrentsAdded(torrent_ids_t const&);
    void torrentsChanged(torrent_ids_t const&, Torrent::fields_t const& fields);
    void torrentsCompleted(torrent_ids_t const&);
    void torrentsEdited(torrent_ids_t const&);
    void torrentsNeedInfo(torrent_ids_t const&);
    void filterChanged(int);

private:
    using Map = std::map<QString, int>;
    using MapIter = Map::const_iterator;
    using Counts = std::unordered_map<QString, int>;
    using MapUpdate = QStandardItem* (*)(QStandardItem* i, MapIter const& it);

    enum
    {
        ACTIVITY,
        PATHS,
        TRACKERS,

        NUM_FLAGS
    };

    using Pending = std::bitset<NUM_FLAGS>;

private:
    void rowsAdd(torrents_t const& torrents);
    void rowsRemove(torrents_t const& torrents);
    void rowsEmitChanged(torrent_ids_t const& ids);

    std::optional<int> getRow(int id) const;
    using span_t = std::pair<int, int>;
    std::vector<span_t> getSpans(torrent_ids_t const& ids) const;

    QStandardItemModel* createActivityModel();
    QStandardItemModel* createFilterModel(int role, int count);

    [[nodiscard]] std::array<int, FilterMode::NUM_MODES> countTorrentsPerMode() const;
    void refreshFilter(Map& map, QStandardItemModel* model, Counts& counts, MapUpdate update, int key);
    void refreshActivity();
    void refreshPaths();
    void refreshTrackers();

    Map path_counts_;
    Map sitename_counts_;
    Prefs const& prefs_;
    QStandardItemModel* activity_model_ = {};
    QStandardItemModel* path_model_ = {};
    QStandardItemModel* tracker_model_ = {};
    torrent_ids_t already_added_;
    torrents_t torrents_;
    Pending pending_ = {};
    QTimer recount_timer_;

private slots:
    void onTorrentsChanged(torrent_ids_t const&, Torrent::fields_t const& fields);
    void recount();
    void recountSoon(Pending const& fields);

    void recountActivitySoon()
    {
        recountSoon(Pending().set(ACTIVITY));
    }

    void recountPathsSoon()
    {
        recountSoon(Pending().set(PATHS));
    }

    void recountTrackersSoon()
    {
        recountSoon(Pending().set(TRACKERS));
    }

    void recountAllSoon()
    {
        recountSoon(Pending().set(ACTIVITY).set(PATHS).set(TRACKERS));
    }
};
