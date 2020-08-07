/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <optional>
#include <vector>

#include <QAbstractListModel>
#include <QVector>

#include "Macros.h"
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
    TR_DISABLE_COPY_MOVE(TorrentModel)

public:
    enum Role
    {
        TorrentRole = Qt::UserRole
    };

    explicit TorrentModel(Prefs const& prefs);
    virtual ~TorrentModel() override;
    void clear();

    bool hasTorrent(QString const& hash_string) const;

    Torrent* getTorrentFromId(int id);
    Torrent const* getTorrentFromId(int id) const;

    using torrents_t = QVector<Torrent*>;
    torrents_t const& torrents() const { return torrents_; }

    // QAbstractItemModel
    int rowCount(QModelIndex const& parent = QModelIndex()) const override;
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
