/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm> // std::sort()

#include <QUrl>

#include "Application.h" // Application
#include "TorrentModel.h"
#include "TrackerModel.h"

int TrackerModel::rowCount(QModelIndex const& parent) const
{
    Q_UNUSED(parent)

    return parent.isValid() ? 0 : myRows.size();
}

QVariant TrackerModel::data(QModelIndex const& index, int role) const
{
    QVariant var;

    int const row = index.row();

    if (0 <= row && row < myRows.size())
    {
        TrackerInfo const& trackerInfo = myRows.at(row);

        switch (role)
        {
        case Qt::DisplayRole:
            var = trackerInfo.st.announce;
            break;

        case Qt::DecorationRole:
            var = QIcon(trackerInfo.st.getFavicon());
            break;

        case TrackerRole:
            var = qVariantFromValue(trackerInfo);
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
    bool operator ()(TrackerInfo const& a, TrackerInfo const& b) const
    {
        if (a.torrentId != b.torrentId)
        {
            return a.torrentId < b.torrentId;
        }

        if (a.st.tier != b.st.tier)
        {
            return a.st.tier < b.st.tier;
        }

        if (a.st.isBackup != b.st.isBackup)
        {
            return !a.st.isBackup;
        }

        return a.st.announce < b.st.announce;
    }
};

void TrackerModel::refresh(TorrentModel const& torrentModel, torrent_ids_t const& ids)
{
    // build a list of the TrackerInfos
    QVector<TrackerInfo> trackers;

    for (int const id : ids)
    {
        Torrent const* tor = torrentModel.getTorrentFromId(id);

        if (tor != nullptr)
        {
            TrackerStatsList const trackerList = tor->trackerStats();

            for (TrackerStat const& st : trackerList)
            {
                TrackerInfo trackerInfo;
                trackerInfo.st = st;
                trackerInfo.torrentId = id;
                trackers.append(trackerInfo);
            }
        }
    }

    // sort 'em
    CompareTrackers comp;
    std::sort(trackers.begin(), trackers.end(), comp);

    // merge 'em with the existing list
    int oldIndex = 0;
    int newIndex = 0;

    while (oldIndex < myRows.size() || newIndex < trackers.size())
    {
        bool const isEndOfOld = oldIndex == myRows.size();
        bool const isEndOfNew = newIndex == trackers.size();

        if (isEndOfOld || (!isEndOfNew && comp(trackers.at(newIndex), myRows.at(oldIndex))))
        {
            // add this new row
            beginInsertRows(QModelIndex(), oldIndex, oldIndex);
            myRows.insert(oldIndex, trackers.at(newIndex));
            endInsertRows();
            ++oldIndex;
            ++newIndex;
        }
        else if (isEndOfNew || (!isEndOfOld && comp(myRows.at(oldIndex), trackers.at(newIndex))))
        {
            // remove this old row
            beginRemoveRows(QModelIndex(), oldIndex, oldIndex);
            myRows.remove(oldIndex);
            endRemoveRows();
        }
        else // update existing row
        {
            myRows[oldIndex].st = trackers.at(newIndex).st;
            emit dataChanged(index(oldIndex, 0), index(oldIndex, 0));
            ++oldIndex;
            ++newIndex;
        }
    }
}

int TrackerModel::find(int torrentId, QString const& url) const
{
    for (int i = 0, n = myRows.size(); i < n; ++i)
    {
        TrackerInfo const& inf = myRows.at(i);

        if (inf.torrentId == torrentId && url == inf.st.announce)
        {
            return i;
        }
    }

    return -1;
}
