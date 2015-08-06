/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <algorithm> // std::sort()

#include <QUrl>

#include "Application.h" // Application
#include "TorrentModel.h"
#include "TrackerModel.h"

int
TrackerModel::rowCount (const QModelIndex& parent) const
{
  Q_UNUSED (parent);

  return parent.isValid() ? 0 : myRows.size();
}

QVariant
TrackerModel::data (const QModelIndex& index, int role) const
{
  QVariant var;

  const int row = index.row ();

  if ((0<=row) && (row<myRows.size()))
    {
      const TrackerInfo& trackerInfo = myRows.at (row);

      switch (role)
        {
          case Qt::DisplayRole:
            var = trackerInfo.st.announce;
            break;

          case Qt::DecorationRole:
            var = trackerInfo.st.getFavicon ();
            break;

          case TrackerRole:
            var = qVariantFromValue (trackerInfo);
            break;

          default:
            break;
        }
    }

  return var;
}

/***
****
***/

struct CompareTrackers
{
  bool operator() (const TrackerInfo& a, const TrackerInfo& b) const
  {
    if (a.torrentId != b.torrentId )
      return a.torrentId < b.torrentId;

    if (a.st.tier != b.st.tier)
      return a.st.tier < b.st.tier;

    if (a.st.isBackup != b.st.isBackup)
      return !a.st.isBackup;

    return a.st.announce < b.st.announce;
  }
};

void
TrackerModel::refresh (const TorrentModel& torrentModel, const QSet<int>& ids)
{
  // build a list of the TrackerInfos
  QVector<TrackerInfo> trackers;
  for (const int id: ids)
    {
      const Torrent * tor = torrentModel.getTorrentFromId (id);
      if (tor != 0)
        {
          const TrackerStatsList trackerList = tor->trackerStats ();
          for (const TrackerStat& st: trackerList)
            {
              TrackerInfo trackerInfo;
              trackerInfo.st = st;
              trackerInfo.torrentId = id;
              trackers.append (trackerInfo);
            }
        }
    }

  // sort 'em
  CompareTrackers comp;
  std::sort (trackers.begin(), trackers.end(), comp);

  // merge 'em with the existing list
  int oldIndex = 0;
  int newIndex = 0;

  while (oldIndex < myRows.size () || newIndex < trackers.size ())
    {
      const bool isEndOfOld = oldIndex == myRows.size ();
      const bool isEndOfNew = newIndex == trackers.size ();

      if (isEndOfOld || (!isEndOfNew && comp (trackers.at (newIndex), myRows.at (oldIndex))))
        {
          // add this new row
          beginInsertRows (QModelIndex (), oldIndex, oldIndex);
          myRows.insert (oldIndex, trackers.at (newIndex));
          endInsertRows ();
          ++oldIndex;
          ++newIndex;
        }
      else if (isEndOfNew || (!isEndOfOld && comp (myRows.at (oldIndex), trackers.at (newIndex))))
        {
          // remove this old row
          beginRemoveRows (QModelIndex (), oldIndex, oldIndex);
          myRows.remove (oldIndex);
          endRemoveRows ();
        }
      else // update existing row
        {
          myRows[oldIndex].st = trackers.at (newIndex).st;
          emit dataChanged (index (oldIndex, 0), index (oldIndex, 0));
          ++oldIndex;
          ++newIndex;
        }
    }
}

int
TrackerModel::find (int torrentId, const QString& url) const
{
  for (int i=0, n=myRows.size(); i<n; ++i)
    {
      const TrackerInfo& inf = myRows.at(i);

      if ((inf.torrentId == torrentId) &&  (url == inf.st.announce))
        return i;
    }

  return -1;
}
