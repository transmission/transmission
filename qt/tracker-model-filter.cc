/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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
    const TrackerInfo trackerInfo = index.model()->data( index, TrackerModel::TrackerRole ).value<TrackerInfo>();
    return myShowBackups || !trackerInfo.st.isBackup;
}
