/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "tracker-model.h"
#include "tracker-model-filter.h"

TrackerModelFilter::TrackerModelFilter (QObject * parent):
  QSortFilterProxyModel (parent),
  myShowBackups (false)
{
}

void
TrackerModelFilter::setShowBackupTrackers (bool b)
{
  myShowBackups = b;
  invalidateFilter ();
}

bool
TrackerModelFilter::filterAcceptsRow (int                 sourceRow,
                                      const QModelIndex & sourceParent) const
{
  QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
  const TrackerInfo trackerInfo = index.data(TrackerModel::TrackerRole).value<TrackerInfo>();
  return myShowBackups || !trackerInfo.st.isBackup;
}
