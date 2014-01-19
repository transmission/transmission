/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU Public License v2 or v3 licenses,
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TRACKER_MODEL_FILTER_H
#define QTR_TRACKER_MODEL_FILTER_H

#include <QSortFilterProxyModel>

class TrackerModelFilter : public QSortFilterProxyModel
{
    Q_OBJECT

  public:
    TrackerModelFilter (QObject *parent = 0);

  public:
    void setShowBackupTrackers (bool);
    bool showBackupTrackers () const { return myShowBackups; }

  protected:
    bool filterAcceptsRow (int sourceRow, const QModelIndex&sourceParent) const;

  private:
    bool myShowBackups;
};

#endif
