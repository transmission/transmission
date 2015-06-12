/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TRACKER_MODEL_H
#define QTR_TRACKER_MODEL_H

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

#include "Torrent.h"

class TorrentModel;

struct TrackerInfo
{
  TrackerStat st;
  int torrentId;
};

Q_DECLARE_METATYPE(TrackerInfo)

class TrackerModel: public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Role
    {
      TrackerRole = Qt::UserRole
    };

  public:
    TrackerModel () {}
    virtual ~TrackerModel () {}

    void refresh (const TorrentModel&, const QSet<int>& ids);
    int find (int torrentId, const QString& url) const;

    // QAbstractItemModel
    virtual int rowCount (const QModelIndex& parent = QModelIndex ()) const;
    virtual QVariant data (const QModelIndex& index, int role = Qt::DisplayRole) const;

  private:
    typedef QVector<TrackerInfo> rows_t;

  private:
    rows_t myRows;
};

#endif // QTR_TRACKER_MODEL_H
