/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_MODEL_H
#define QTR_TORRENT_MODEL_H

#include <QAbstractListModel>
#include <QMap>
#include <QSet>
#include <QVector>

class Prefs;
class Speed;
class Torrent;

extern "C"
{
  struct tr_variant;
}

class TorrentModel: public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Role
    {
      TorrentRole = Qt::UserRole
    };

  public:
    TorrentModel (const Prefs& prefs);
    virtual ~TorrentModel ();

    void clear ();
    bool hasTorrent (const QString& hashString) const;

    Torrent * getTorrentFromId (int id);
    const Torrent * getTorrentFromId (int id) const;

    void getTransferSpeed (Speed& uploadSpeed, size_t& uploadPeerCount,
                           Speed& downloadSpeed, size_t& downloadPeerCount);

    // QAbstractItemModel
    virtual int rowCount (const QModelIndex& parent = QModelIndex ()) const;
    virtual QVariant data (const QModelIndex& index, int role = Qt::DisplayRole) const;

  public slots:
    void updateTorrents (tr_variant * torrentList, bool isCompleteList);
    void removeTorrents (tr_variant * torrentList);
    void removeTorrent (int id);

  signals:
    void torrentsAdded (QSet<int>);

  private:
    typedef QMap<int, int> id_to_row_t;
    typedef QMap<int, Torrent*> id_to_torrent_t;
    typedef QVector<Torrent*> torrents_t;

  private:
    void addTorrent (Torrent *);
    QSet<int> getIds () const;

  private slots:
    void onTorrentChanged (int propertyId);

  private:
    const Prefs& myPrefs;

    id_to_row_t myIdToRow;
    id_to_torrent_t myIdToTorrent;
    torrents_t myTorrents;
};

#endif // QTR_TORRENT_MODEL_H
