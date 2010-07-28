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

#ifndef QTR_TRACKER_MODEL_FILTER_H
#define QTR_TRACKER_MODEL_FILTER_H

#include <QSortFilterProxyModel>

class TrackerModelFilter : public QSortFilterProxyModel
{
        Q_OBJECT

    public:
        TrackerModelFilter( QObject *parent = 0 );

    public:
        void setShowBackupTrackers( bool );
        bool showBackupTrackers( ) const { return myShowBackups; }

    protected:
        bool filterAcceptsRow( int sourceRow, const QModelIndex&sourceParent ) const;

    private:
        bool myShowBackups;
};

#endif
