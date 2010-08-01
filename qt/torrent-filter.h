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

#ifndef QTR_TORRENT_FILTER_H
#define QTR_TORRENT_FILTER_H

#include <QSortFilterProxyModel>
#include <QMetaType>
#include <QVariant>

class QString;
class QWidget;

class FilterMode;
class Prefs;
class Torrent;

class TorrentFilter: public QSortFilterProxyModel
{
        Q_OBJECT

    public:
        TorrentFilter( Prefs& prefs );
        virtual ~TorrentFilter( );

    public:
        enum TextMode { FILTER_BY_NAME, FILTER_BY_FILES, FILTER_BY_TRACKER };
        int hiddenRowCount( ) const;

    private slots:
        void refreshPref( int key );

    protected:
        virtual bool filterAcceptsRow( int, const QModelIndex& ) const;
        virtual bool lessThan( const QModelIndex&, const QModelIndex& ) const;

    private:
        bool activityFilterAcceptsTorrent( const Torrent * tor, const FilterMode& mode ) const;
        bool trackerFilterAcceptsTorrent( const Torrent * tor, const QString& tracker ) const;

    public:
        int count( const FilterMode& ) const;

    private:
        Prefs& myPrefs;
};

#endif
