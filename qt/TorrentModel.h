// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <QAbstractTableModel>

#include <libtransmission/tr-macros.h>

#include "Torrent.h"
#include "Typedefs.h"

class Prefs;
class Speed;

extern "C"
{
    struct tr_variant;
}

class TorrentModel : public QAbstractTableModel
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(TorrentModel)

public:
    enum Columns
    {
        COL_NAME,
        COL_PRIVATE,
        COL_STATUS,
        COL_SIZE,
        COL_SEEDING_TIME,
        COL_SEEDS,
        COL_PEERS,
        COL_ACTIVITY,
        COL_ETA,
        COL_RATIO,
        COL_DOWNLOADED,
        COL_UPLOADED,
        COL_PROGRESS,
        COL_TRACKER_STATUS,
        COL_ADDED_ON,
        COL_LAST_ACTIVE,
        COL_PATH,
        COL_PRIORITY,
        COL_QUEUE_POSITION,
        COL_SIZE_LEFT,
        COL_ID,
        //
        NUM_COLUMNS
    };

    enum Role
    {
        TorrentRole = Qt::UserRole
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

    // QAbstractTableModel
    int rowCount(QModelIndex const& parent = QModelIndex{}) const override;
    int columnCount(QModelIndex const& parent = QModelIndex{}) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

public slots:
    void updateTorrents(tr_variant* torrent_list, bool is_complete_list);
    void removeTorrents(tr_variant* torrent_list);

signals:
    void torrentsAdded(torrent_ids_t const&);
    void torrentsChanged(torrent_ids_t const&, Torrent::fields_t const& fields);
    void torrentsCompleted(torrent_ids_t const&);
    void torrentsEdited(torrent_ids_t const&);
    void torrentsNeedInfo(torrent_ids_t const&);

private:
    void rowsAdd(torrents_t const& torrents);
    void rowsRemove(torrents_t const& torrents);
    void rowsEmitChanged(torrent_ids_t const& ids);

    std::optional<int> getRow(int id) const;
    using span_t = std::pair<int, int>;
    std::vector<span_t> getSpans(torrent_ids_t const& ids) const;

    Prefs const& prefs_;
    torrent_ids_t already_added_;
    torrents_t torrents_;
};
