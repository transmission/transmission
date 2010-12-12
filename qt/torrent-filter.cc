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

#include <iostream>

#include "filters.h"
#include "hig.h"
#include "prefs.h"
#include "torrent.h"
#include "torrent-filter.h"
#include "torrent-model.h"
#include "utils.h"

TorrentFilter :: TorrentFilter( Prefs& prefs ):
    myPrefs( prefs )
{
    // listen for changes to the preferences to know when to refilter / resort
    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(int)));

    setDynamicSortFilter( true );

    // initialize our state from the current prefs
    QList<int> initKeys;
    initKeys << Prefs :: SORT_MODE
             << Prefs :: FILTER_MODE
             << Prefs :: FILTER_TRACKERS
             << Prefs :: FILTER_TEXT;
    foreach( int key, initKeys )
        refreshPref( key );
}

TorrentFilter :: ~TorrentFilter( )
{
}

void
TorrentFilter :: refreshPref( int key )
{
    switch( key )
    {
        case Prefs :: FILTER_TEXT:
        case Prefs :: FILTER_MODE:
        case Prefs :: FILTER_TRACKERS:
            invalidateFilter( );
            /* force a re-sort */
            sort( 0, !myPrefs.getBool(Prefs::SORT_REVERSED) ? Qt::AscendingOrder : Qt::DescendingOrder );

        case Prefs :: SORT_MODE:
        case Prefs :: SORT_REVERSED:
            sort( 0, myPrefs.getBool(Prefs::SORT_REVERSED) ? Qt::AscendingOrder : Qt::DescendingOrder );
            invalidate( );
            break;
    }
}

/***
****
***/

namespace
{
    template <typename T> int compare( const T a, const T b )
    {
        if( a < b ) return -1;
        if( b < a ) return 1;
        return 0;
    }
}

bool
TorrentFilter :: lessThan( const QModelIndex& left, const QModelIndex& right ) const
{
    int val = 0;
    const Torrent * a = sourceModel()->data( left, TorrentModel::TorrentRole ).value<const Torrent*>();
    const Torrent * b = sourceModel()->data( right, TorrentModel::TorrentRole ).value<const Torrent*>();

    switch( myPrefs.get<SortMode>(Prefs::SORT_MODE).mode() )
    {
        case SortMode :: SORT_BY_SIZE:
            if( !val ) val = compare( a->sizeWhenDone(), b->sizeWhenDone() );
            break;
        case SortMode :: SORT_BY_ACTIVITY:
            if( !val ) val = compare( a->downloadSpeed() + a->uploadSpeed(), b->downloadSpeed() + b->uploadSpeed() );
            if( !val ) val = compare( a->uploadedEver(), b->uploadedEver() );
            break;
        case SortMode :: SORT_BY_AGE:
            val = compare( a->dateAdded().toTime_t(), b->dateAdded().toTime_t() );
            break;
        case SortMode :: SORT_BY_ID:
            if( !val ) val = compare( a->id(), b->id() );
            break;
        case SortMode :: SORT_BY_STATE:
            if( !val ) val = compare( a->hasError(), b->hasError() );
            if( !val ) val = compare( a->getActivity(), b->getActivity() );
            // fall through
        case SortMode :: SORT_BY_PROGRESS:
            if( !val ) val = compare( a->percentComplete(), b->percentComplete() );
            if( !val ) val = a->compareSeedRatio( *b );
            // fall through
        case SortMode :: SORT_BY_RATIO:
            if( !val ) val = a->compareRatio( *b );
            break;
        case SortMode :: SORT_BY_ETA:
            if( !val ) val = a->compareETA( *b );
            break;
        default:
            break;
    }
    if( val == 0 )
        val = -a->name().compare( b->name(), Qt::CaseInsensitive );
    if( val == 0 )
        val = compare( a->hashString(), b->hashString() );
    return val < 0;
}


/***
****
***/

bool
TorrentFilter :: trackerFilterAcceptsTorrent( const Torrent * tor, const QString& tracker ) const
{
    return tracker.isEmpty() || tor->hasTrackerSubstring( tracker );
}

bool
TorrentFilter :: activityFilterAcceptsTorrent( const Torrent * tor, const FilterMode& m ) const
{
    bool accepts;

    switch( m.mode( ) )
    {
        case FilterMode::SHOW_ACTIVE:
            accepts = tor->peersWeAreUploadingTo( ) > 0 || tor->peersWeAreDownloadingFrom( ) > 0 || tor->isVerifying( );
            break;
        case FilterMode::SHOW_DOWNLOADING:
            accepts = tor->isDownloading( );
            break;
        case FilterMode::SHOW_SEEDING:
            accepts = tor->isSeeding( );
            break;
        case FilterMode::SHOW_PAUSED:
            accepts = tor->isPaused( );
            break;
        case FilterMode::SHOW_FINISHED:
            accepts = tor->isFinished( );
            break;
        case FilterMode::SHOW_QUEUED:
            accepts = tor->isWaitingToVerify( );
            break;
        case FilterMode::SHOW_VERIFYING:
            accepts = tor->isVerifying( ) || tor->isWaitingToVerify( );
            break;
        case FilterMode::SHOW_ERROR:
            accepts = tor->hasError( );
            break;
        default: // FilterMode::SHOW_ALL
            accepts = true;
            break;
    }

    return accepts;
}

bool
TorrentFilter :: filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const
{
    QModelIndex childIndex = sourceModel()->index( sourceRow, 0, sourceParent );
    const Torrent * tor = childIndex.model()->data( childIndex, TorrentModel::TorrentRole ).value<const Torrent*>();
    bool accepts = true;

    if( accepts ) {
        const FilterMode m = myPrefs.get<FilterMode>(Prefs::FILTER_MODE);
        accepts = activityFilterAcceptsTorrent( tor, m );
    }

    if( accepts ) {
        const QString trackers = myPrefs.getString(Prefs::FILTER_TRACKERS);
        accepts = trackerFilterAcceptsTorrent( tor, trackers );
    }

    if( accepts ) {
        const QString text = myPrefs.getString( Prefs::FILTER_TEXT );
        if( !text.isEmpty( ) )
            accepts = tor->name().contains( text, Qt::CaseInsensitive );
    }

#if 0
    if( accepts && !myText.isEmpty( ) ) switch( myTextMode )
    {
        case FILTER_BY_NAME:
            accepts = tor->name().contains( myText, Qt::CaseInsensitive );
            break;
        case FILTER_BY_FILES:
            accepts = tor->hasFileSubstring( myText );
            break;
        case FILTER_BY_TRACKER:
            accepts = tor->hasTrackerSubstring( myText );
            break;
    }
#endif

    return accepts;
}

int
TorrentFilter :: hiddenRowCount( ) const
{
    return sourceModel()->rowCount( ) - rowCount( );
}

int
TorrentFilter :: count( const FilterMode& mode ) const
{
    int count = 0;

    for( int row=0; ; ++row ) {
        QModelIndex index = sourceModel()->index( row, 0 );
        if( !index.isValid( ) )
            break;
        const Torrent * tor = index.data( TorrentModel::TorrentRole ).value<const Torrent*>();
        if( activityFilterAcceptsTorrent( tor, mode ) )
            ++count;
    }

    return count;
}
