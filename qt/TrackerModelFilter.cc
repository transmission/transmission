// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "TrackerModel.h"
#include "TrackerModelFilter.h"

TrackerModelFilter::TrackerModelFilter(QObject* parent)
    : QSortFilterProxyModel{ parent }
{
}

void TrackerModelFilter::set_show_backup_trackers(bool b)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 10, 0)

    show_backups_ = b;
    invalidateFilter(); // deprecated in Qt 6.13

#else

    beginFilterChange();
    show_backups_ = b;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);

#endif
}

bool TrackerModelFilter::filterAcceptsRow(int source_row, QModelIndex const& source_parent) const
{
    QModelIndex const index = sourceModel()->index(source_row, 0, source_parent);
    auto const tracker_info = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
    return show_backups_ || !tracker_info.st.is_backup;
}
