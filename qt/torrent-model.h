/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
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

#include "speed.h"
#include "torrent.h"

class Prefs;

extern "C"
{
  struct tr_variant;
}

class TorrentModel: public QAbstractListModel
{
    Q_OBJECT

  private:
    typedef QMap<int,int> id_to_row_t;
    typedef QMap<int,Torrent*> id_to_torrent_t;
    typedef QVector<Torrent*> torrents_t;
    id_to_row_t myIdToRow;
    id_to_torrent_t myIdToTorrent;
    torrents_t myTorrents;
    const Prefs& myPrefs;

  public:
    void clear ();
    bool hasTorrent (const QString& hashString) const;
    virtual int rowCount (const QModelIndex& parent = QModelIndex()) const;
    virtual QVariant data (const QModelIndex& index, int role = Qt::DisplayRole) const;
    enum Role { TorrentRole = Qt::UserRole };

  public:
    Torrent* getTorrentFromId (int id);
    const Torrent* getTorrentFromId (int id) const;

  private:
    void addTorrent (Torrent *);
    QSet<int> getIds () const;

  public:
    void getTransferSpeed (Speed  & uploadSpeed,
                           size_t & uploadPeerCount,
                           Speed  & downloadSpeed,
                           size_t & downloadPeerCount);

  signals:
    void torrentsAdded (QSet<int>);

  public slots:
    void updateTorrents (tr_variant * torrentList, bool isCompleteList);
    void removeTorrents (tr_variant * torrentList);
    void removeTorrent (int id);

  private slots:
    void onTorrentChanged (int propertyId);

  public:
    TorrentModel (const Prefs& prefs);
    virtual ~TorrentModel ();
};

#endif
