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
struct tr_variant;

class TorrentModel : public QAbstractListModel
{
    Q_OBJECT

public:
    // NOLINTNEXTLINE(performance-enum-size)
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

    bool has_torrent(TorrentHash const& hash) const;

    Torrent* get_torrent_from_id(int id);
    Torrent const* get_torrent_from_id(int id) const;

    using torrents_t = std::vector<Torrent*>;

    torrents_t const& torrents() const
    {
        return torrents_;
    }

    // QAbstractItemModel
    int rowCount(QModelIndex const& parent = QModelIndex{}) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

public slots:
    void update_torrents(tr_variant* torrent_list, bool is_complete_list);
    void remove_torrents(tr_variant* torrent_list);

signals:
    void torrents_added(torrent_ids_t const&);
    void torrents_changed(torrent_ids_t const&, Torrent::fields_t const& fields);
    void torrents_completed(torrent_ids_t const&);
    void torrents_edited(torrent_ids_t const&);
    void torrents_need_info(torrent_ids_t const&);

private:
    void rows_add(torrents_t const& torrents);
    void rows_remove(torrents_t const& torrents);
    void rows_emit_changed(torrent_ids_t const& ids);

    std::optional<int> get_row(int id) const;
    using span_t = std::pair<int, int>;
    std::vector<span_t> get_spans(torrent_ids_t const& ids) const;

    Prefs const& prefs_;
    torrent_ids_t already_added_;
    torrents_t torrents_;
};
