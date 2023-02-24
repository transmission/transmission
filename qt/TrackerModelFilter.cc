// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "TrackerModel.h"
#include "TrackerModelFilter.h"

TrackerModelFilter::TrackerModelFilter(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

void TrackerModelFilter::setShowBackupTrackers(bool b)
{
    show_backups_ = b;
    invalidateFilter();
}

bool TrackerModelFilter::filterAcceptsRow(int source_row, QModelIndex const& source_parent) const
{
    QModelIndex const index = sourceModel()->index(source_row, 0, source_parent);
    auto const tracker_info = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
    return show_backups_ || !tracker_info.st.is_backup;
}
