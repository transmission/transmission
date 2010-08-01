/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
 */

#include "tracker-model.h"
#include "tracker-model-filter.h"

TrackerModelFilter :: TrackerModelFilter( QObject * parent ):
    QSortFilterProxyModel( parent ),
    myShowBackups( false )
{
}

void
TrackerModelFilter :: setShowBackupTrackers( bool b )
{
    myShowBackups = b;
    invalidateFilter( );
}

bool
TrackerModelFilter :: filterAcceptsRow( int                 sourceRow,
                                        const QModelIndex & sourceParent ) const
{
    QModelIndex index = sourceModel()->index( sourceRow, 0, sourceParent );
    const TrackerInfo trackerInfo = index.data( TrackerModel::TrackerRole ).value<TrackerInfo>();
    return myShowBackups || !trackerInfo.st.isBackup;
}
