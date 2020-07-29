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

    return parent.isValid() ? 0 : rows_.size();
}

QVariant TrackerModel::data(QModelIndex const& index, int role) const
{
    QVariant var;

    int const row = index.row();

    if (0 <= row && row < rows_.size())
    {
        TrackerInfo const& tracker_info = rows_.at(row);

        switch (role)
        {
        case Qt::DisplayRole:
            var = tracker_info.st.announce;
            break;

        case Qt::DecorationRole:
            var = QIcon(tracker_info.st.getFavicon());
            break;

        case TrackerRole:
            var = QVariant::fromValue(tracker_info);
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
        if (a.torrent_id != b.torrent_id)
        {
            return a.torrent_id < b.torrent_id;
        }

        if (a.st.tier != b.st.tier)
        {
            return a.st.tier < b.st.tier;
        }

        if (a.st.is_backup != b.st.is_backup)
        {
            return !a.st.is_backup;
        }

        return a.st.announce < b.st.announce;
    }
};

void TrackerModel::refresh(TorrentModel const& torrent_model, torrent_ids_t const& ids)
{
    // build a list of the TrackerInfos
    QVector<TrackerInfo> trackers;

    for (int const id : ids)
    {
        Torrent const* tor = torrent_model.getTorrentFromId(id);

        if (tor != nullptr)
        {
            TrackerStatsList const tracker_list = tor->trackerStats();

            for (TrackerStat const& st : tracker_list)
            {
                TrackerInfo tracker_info;
                tracker_info.st = st;
                tracker_info.torrent_id = id;
                trackers.append(tracker_info);
            }
        }
    }

    // sort 'em
    CompareTrackers comp;
    std::sort(trackers.begin(), trackers.end(), comp);

    // merge 'em with the existing list
    int old_index = 0;
    int new_index = 0;

    while (old_index < rows_.size() || new_index < trackers.size())
    {
        bool const is_end_of_old = old_index == rows_.size();
        bool const is_end_of_new = new_index == trackers.size();

        if (is_end_of_old || (!is_end_of_new && comp(trackers.at(new_index), rows_.at(old_index))))
        {
            // add this new row
            beginInsertRows(QModelIndex(), old_index, old_index);
            rows_.insert(old_index, trackers.at(new_index));
            endInsertRows();
            ++old_index;
            ++new_index;
        }
        else if (is_end_of_new || (!is_end_of_old && comp(rows_.at(old_index), trackers.at(new_index))))
        {
            // remove this old row
            beginRemoveRows(QModelIndex(), old_index, old_index);
            rows_.remove(old_index);
            endRemoveRows();
        }
        else // update existing row
        {
            rows_[old_index].st = trackers.at(new_index).st;
            emit dataChanged(index(old_index, 0), index(old_index, 0));
            ++old_index;
            ++new_index;
        }
    }
}

int TrackerModel::find(int torrent_id, QString const& url) const
{
    for (int i = 0, n = rows_.size(); i < n; ++i)
    {
        TrackerInfo const& inf = rows_.at(i);

        if (inf.torrent_id == torrent_id && url == inf.st.announce)
        {
            return i;
        }
    }

    return -1;
}
