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

public:
    TorrentModel(Prefs const& prefs);
    virtual ~TorrentModel();

    void clear();
    bool hasTorrent(QString const& hashString) const;

    Torrent* getTorrentFromId(int id);
    Torrent const* getTorrentFromId(int id) const;

    void getTransferSpeed(Speed& uploadSpeed, size_t& uploadPeerCount, Speed& downloadSpeed, size_t& downloadPeerCount);

    // QAbstractItemModel
    virtual int rowCount(QModelIndex const& parent = QModelIndex()) const;
    virtual QVariant data(QModelIndex const& index, int role = Qt::DisplayRole) const;

public slots:
    void updateTorrents(tr_variant* torrentList, bool isCompleteList);
    void removeTorrents(tr_variant* torrentList);
    void removeTorrent(int id);

signals:
    void torrentsAdded(QSet<int>);

private:
    typedef QVector<Torrent*> torrents_t;

private:
    void addTorrent(Torrent*);
    void addTorrents(torrents_t&& torrents, QSet<int>& addIds);
    QSet<int> getIds() const;

private slots:
    void onTorrentChanged(int propertyId);

private:
    Prefs const& myPrefs;

    torrents_t myTorrents;
};
