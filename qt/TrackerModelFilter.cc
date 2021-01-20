/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "TrackerModel.h"
#include "TrackerModelFilter.h"

TrackerModelFilter::TrackerModelFilter(QObject* parent) :
    QSortFilterProxyModel(parent)
{
}

void TrackerModelFilter::setShowBackupTrackers(bool b)
{
    show_backups_ = b;
    invalidateFilter();
}

bool TrackerModelFilter::filterAcceptsRow(int source_row, QModelIndex const& source_parent) const
{
    QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
    auto const tracker_info = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
    return show_backups_ || !tracker_info.st.is_backup;
}
