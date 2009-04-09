/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef QTR_TORRENT_FILTER_H
#define QTR_TORRENT_FILTER_H

#include <QSortFilterProxyModel>

struct Prefs;

class TorrentFilter: public QSortFilterProxyModel
{
        Q_OBJECT

    public:
        TorrentFilter( Prefs& prefs );
        virtual ~TorrentFilter( );

    public:
        enum ShowMode { SHOW_ALL, SHOW_ACTIVE, SHOW_DOWNLOADING, SHOW_SEEDING, SHOW_PAUSED };
        ShowMode getShowMode( ) const { return myShowMode; }

        enum TextMode { FILTER_BY_NAME, FILTER_BY_FILES, FILTER_BY_TRACKER };
        TextMode getTextMode( ) const { return myTextMode; }

        enum SortMode{ SORT_BY_ACTIVITY, SORT_BY_AGE, SORT_BY_ETA, SORT_BY_NAME,
                       SORT_BY_PROGRESS, SORT_BY_RATIO, SORT_BY_SIZE,
                       SORT_BY_STATE, SORT_BY_TRACKER, SORT_BY_ID };
        const char * getSortKey( int mode=-1 );
        SortMode getSortMode( ) const { return mySortMode; }

        bool isAscending( ) const { return myIsAscending; }

        int hiddenRowCount( ) const;


    public slots:
        void setShowMode( int showMode );
        void setTextMode( int textMode );
        void setSortMode( int sortMode );
        void setText( QString );
        void sortByActivity( );
        void sortByAge( );
        void sortByETA( );
        void sortById( );
        void sortByName( );
        void sortByProgress( );
        void sortByRatio( );
        void sortBySize( );
        void sortByState( );
        void sortByTracker( );
        void setAscending( bool );
        void resort( );

    protected:
        virtual bool filterAcceptsRow( int, const QModelIndex& ) const;
        virtual bool lessThan( const QModelIndex&, const QModelIndex& ) const;

    private:
        Prefs& myPrefs;
        ShowMode myShowMode;
        TextMode myTextMode;
        SortMode mySortMode;
        bool myIsAscending;
        QString myText;
};

#endif
