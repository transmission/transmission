/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TRACKER_MODEL_FILTER_H
#define QTR_TRACKER_MODEL_FILTER_H

#include <QSortFilterProxyModel>

class TrackerModelFilter: public QSortFilterProxyModel
{
    Q_OBJECT

  public:
    TrackerModelFilter (QObject * parent = nullptr);

    void setShowBackupTrackers (bool);
    bool showBackupTrackers () const { return myShowBackups; }

  protected:
    // QSortFilterProxyModel
    virtual bool filterAcceptsRow (int sourceRow, const QModelIndex& sourceParent) const;

  private:
    bool myShowBackups;
};

#endif // QTR_TRACKER_MODEL_FILTER_H
