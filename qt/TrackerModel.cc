// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

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

    if (index.isValid() && index.row() < rowCount())
    {
        TrackerInfo const& tracker_info = rows_.at(index.row());

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
    bool operator()(TrackerInfo const& a, TrackerInfo const& b) const
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
    std::vector<TrackerInfo> trackers;

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
                trackers.push_back(tracker_info);
            }
        }
    }

    // sort 'em
    CompareTrackers const comp;
    std::sort(trackers.begin(), trackers.end(), comp);

    // merge 'em with the existing list
    unsigned int old_index = 0;
    unsigned int new_index = 0;

    while (old_index < rows_.size() || new_index < trackers.size())
    {
        bool const is_end_of_old = old_index == rows_.size();
        bool const is_end_of_new = new_index == trackers.size();

        if (is_end_of_old || (!is_end_of_new && comp(trackers.at(new_index), rows_.at(old_index))))
        {
            // add this new row
            beginInsertRows(QModelIndex(), old_index, old_index);
            rows_.insert(rows_.begin() + old_index, trackers.at(new_index));
            endInsertRows();
            ++old_index;
            ++new_index;
        }
        else if (is_end_of_new || (!is_end_of_old && comp(rows_.at(old_index), trackers.at(new_index))))
        {
            // remove this old row
            beginRemoveRows(QModelIndex(), old_index, old_index);
            rows_.erase(rows_.begin() + old_index);
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
