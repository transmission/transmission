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
