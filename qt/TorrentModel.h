// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <QAbstractListModel>

#include "Torrent.h"
#include "Typedefs.h"

class Prefs;
class Speed;

extern "C"
{
    struct tr_variant;
}

class TorrentModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        TorrentRole = Qt::UserRole
    };

    explicit TorrentModel(Prefs const& prefs);
    TorrentModel& operator=(TorrentModel&&) = delete;
    TorrentModel& operator=(TorrentModel const&) = delete;
    TorrentModel(TorrentModel&&) = delete;
    TorrentModel(TorrentModel const&) = delete;
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
    int rowCount(QModelIndex const& parent = QModelIndex{}) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

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
