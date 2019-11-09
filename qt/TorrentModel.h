/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <optional>

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

class Prefs;
class Speed;
class Torrent;

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
    virtual ~TorrentModel();
    void clear();

    bool hasTorrent(QString const& hashString) const;

    Torrent* getTorrentFromId(int id);
    Torrent const* getTorrentFromId(int id) const;

    void getTransferSpeed(Speed& uploadSpeed, size_t& uploadPeerCount, Speed& downloadSpeed, size_t& downloadPeerCount) const;

    // QAbstractItemModel
    int rowCount(QModelIndex const& parent = QModelIndex()) const override;
    QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const override;

public slots:
    void updateTorrents(tr_variant* torrentList, bool isCompleteList);
    void removeTorrents(tr_variant* torrentList);

signals:
    void torrentsAdded(QSet<int>);
    void torrentsChanged(QSet<int>);
    void torrentsCompleted(QSet<int>);
    void torrentsNeedInfo(QSet<int>);

private:
    using torrents_t = QVector<Torrent*>;
    void rowsAdd(torrents_t const& torrents);
    void rowsRemove(QSet<Torrent*> const& torrents);
    void rowsEmitChanged(QSet<int> const& ids);

    std::optional<int> getRow(int id) const;
    std::optional<int> getRow(Torrent const* tor) const;
    using span_t = std::pair<int, int>;
    std::vector<span_t> getSpans(QSet<int> const& ids) const;

    Prefs const& myPrefs;
    torrents_t myTorrents;
    QSet<int> myAlreadyAdded;
};
