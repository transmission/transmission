/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <iostream>

#include <libtransmission/transmission.h>
#include <libtransmission/variant.h>

#include "Speed.h"
#include "Torrent.h"
#include "TorrentDelegate.h"
#include "TorrentModel.h"

void
TorrentModel::clear ()
{
  beginResetModel ();

  myIdToRow.clear ();
  myIdToTorrent.clear ();
  qDeleteAll (myTorrents);
  myTorrents.clear ();

  endResetModel ();
}

int
TorrentModel::rowCount (const QModelIndex& parent) const
{
  Q_UNUSED (parent);

  return myTorrents.size ();
}

QVariant
TorrentModel::data (const QModelIndex& index, int role) const
{
  QVariant var;

  const Torrent * t = myTorrents.value (index.row(), 0);
  if (t != 0)
    {
      switch (role)
        {
          case Qt::DisplayRole:
            var.setValue (t->name());
            break;

          case Qt::DecorationRole:
            var.setValue (t->getMimeTypeIcon());
            break;

          case TorrentRole:
            var = qVariantFromValue(t);
            break;

          default:
            //std::cerr << "Unhandled role: " << role << std::endl;
            break;
        }
    }

  return var;
}

/***
****
***/

void
TorrentModel::addTorrent (Torrent * t)
{
  myIdToTorrent.insert (t->id (), t);
  myIdToRow.insert (t->id (), myTorrents.size ());
  myTorrents.append (t);
}

TorrentModel::TorrentModel (const Prefs& prefs):
  myPrefs (prefs)
{
}

TorrentModel::~TorrentModel ()
{
  clear ();
}

/***
****
***/

Torrent*
TorrentModel::getTorrentFromId (int id)
{
  id_to_torrent_t::iterator it (myIdToTorrent.find (id));
  return it == myIdToTorrent.end() ? 0 : it.value ();
}

const Torrent*
TorrentModel::getTorrentFromId (int id) const
{
  id_to_torrent_t::const_iterator it (myIdToTorrent.find (id));
  return it == myIdToTorrent.end() ? 0 : it.value ();
}

/***
****
***/

void
TorrentModel::onTorrentChanged (int torrentId)
{
  const int row (myIdToRow.value (torrentId, -1));
  if (row >= 0)
    {
      QModelIndex qmi (index (row, 0));
      emit dataChanged (qmi, qmi);
    }
}

void
TorrentModel::removeTorrents (tr_variant * torrents)
{
  int i = 0;
  tr_variant * child;
  while( (child = tr_variantListChild (torrents, i++)))
    {
      int64_t intVal;
      if (tr_variantGetInt (child, &intVal))
        removeTorrent (intVal);
    }
}

void
TorrentModel::updateTorrents (tr_variant * torrents, bool isCompleteList)
{
  QList<Torrent*> newTorrents;
  QSet<int> oldIds;
  QSet<int> addIds;
  QSet<int> newIds;

  if  (isCompleteList)
    oldIds = getIds ();

  if (tr_variantIsList (torrents))
    {
      size_t i (0);
      tr_variant * child;
      while( (child = tr_variantListChild (torrents, i++)))
        {
          int64_t id;
          if (tr_variantDictFindInt (child, TR_KEY_id, &id))
            {
              newIds.insert (id);

              Torrent * tor = getTorrentFromId (id);
              if (tor == 0)
                {
                  tor = new Torrent (myPrefs, id);
                  tor->update (child);
                  if (!tor->hasMetadata())
                    tor->setMagnet (true);
                  newTorrents.append (tor);
                  connect (tor, SIGNAL(torrentChanged(int)), this, SLOT(onTorrentChanged(int)));
                }
              else
                {
                  tor->update (child);
                  if (tor->isMagnet() && tor->hasMetadata())
                    {
                      addIds.insert (tor->id());
                      tor->setMagnet (false);
                    }
                }
            }
        }
    }

  if (!newTorrents.isEmpty ())
    {
      const int oldCount (rowCount ());
      const int newCount (oldCount + newTorrents.size ());
      QSet<int> ids;

      beginInsertRows (QModelIndex(), oldCount, newCount - 1);

      for (Torrent * const tor: newTorrents)
        {
          addTorrent (tor);
          addIds.insert (tor->id ());
        }

      endInsertRows ();
    }

  if (!addIds.isEmpty())
    emit torrentsAdded (addIds);

  if (isCompleteList)
    {
      QSet<int> removedIds (oldIds);
      removedIds -= newIds;
      for (const int id: removedIds)
        removeTorrent (id);
    }
}

void
TorrentModel::removeTorrent (int id)
{
  const int row = myIdToRow.value (id, -1);
  if (row >= 0)
    {
      Torrent * tor = myIdToTorrent.value (id, 0);

      beginRemoveRows (QModelIndex(), row, row);
      // make the myIdToRow map consistent with list view/model
      for (auto i = myIdToRow.begin(); i != myIdToRow.end(); ++i)
        if (i.value() > row)
          --i.value();
      myIdToRow.remove (id);
      myIdToTorrent.remove (id);
      myTorrents.remove (myTorrents.indexOf (tor));
      endRemoveRows ();

      delete tor;
    }
}

void
TorrentModel::getTransferSpeed (Speed   & uploadSpeed,
                                size_t  & uploadPeerCount,
                                Speed   & downloadSpeed,
                                size_t  & downloadPeerCount)
{
  Speed upSpeed, downSpeed;
  size_t upCount=0, downCount=0;

  for (const Torrent * const tor: myTorrents)
    {
      upSpeed += tor->uploadSpeed ();
      upCount += tor->peersWeAreUploadingTo ();
      downSpeed += tor->downloadSpeed ();
      downCount += tor->webseedsWeAreDownloadingFrom();
      downCount += tor->peersWeAreDownloadingFrom();
    }

  uploadSpeed = upSpeed;
  uploadPeerCount = upCount;
  downloadSpeed = downSpeed;
  downloadPeerCount = downCount;
}

QSet<int>
TorrentModel::getIds () const
{
  QSet<int> ids;

  ids.reserve (myTorrents.size());
  for (const Torrent * const tor: myTorrents)
    ids.insert (tor->id());

  return ids;
}

bool
TorrentModel::hasTorrent (const QString& hashString) const
{
  for (const Torrent * const tor: myTorrents)
    if (tor->hashString () == hashString)
      return true;

  return false;
}
