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

#include <iostream>

#include "prefs.h"
#include "torrent.h"
#include "torrent-filter.h"
#include "torrent-model.h"

TorrentFilter :: TorrentFilter( Prefs& prefs ):
    myPrefs( prefs ),
    myShowMode( getShowModeFromName( prefs.getString( Prefs::FILTER_MODE ) ) ),
    myTextMode( FILTER_BY_NAME ),
    mySortMode( getSortModeFromName( prefs.getString( Prefs::SORT_MODE ) ) ),
    myIsAscending( prefs.getBool( Prefs::SORT_REVERSED ) )
{
}

TorrentFilter :: ~TorrentFilter( )
{
}

/***
****
***/

void
TorrentFilter :: setShowMode( int showMode )
{
    if( myShowMode != showMode )
    {
        myPrefs.set( Prefs :: FILTER_MODE, getShowName( showMode ) );
        myShowMode = ShowMode( showMode );
        invalidateFilter( );
    }
}

void
TorrentFilter :: setTextMode( int textMode )
{
    if( myTextMode != textMode )
    {
        myTextMode = TextMode( textMode );
        invalidateFilter( );
    }
}

void
TorrentFilter :: setText( QString text )
{
    QString trimmed = text.trimmed( );

    if( myText != trimmed )
    {
        myText = trimmed;
        invalidateFilter( );
    }
}

bool
TorrentFilter :: filterAcceptsRow( int sourceRow, const QModelIndex& sourceParent ) const
{
    QModelIndex childIndex = sourceModel()->index( sourceRow, 0, sourceParent );
    const Torrent * tor = childIndex.model()->data( childIndex, TorrentModel::TorrentRole ).value<const Torrent*>();
    const tr_torrent_activity activity = tor->getActivity( );
    bool accepts;

    switch( myShowMode )
    {
        case SHOW_ALL:
            accepts = true;
            break;
        case SHOW_ACTIVE:
            accepts = tor->peersWeAreUploadingTo( ) > 0 || tor->peersWeAreDownloadingFrom( ) > 0 || tor->isVerifying( );
            break;
        case SHOW_DOWNLOADING:
            accepts = activity == TR_STATUS_DOWNLOAD;
            break;
        case SHOW_SEEDING:
            accepts = activity == TR_STATUS_SEED;
            break;
        case SHOW_PAUSED:
            accepts = activity == TR_STATUS_STOPPED;
            break;
    }

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

    return accepts;
}

/***
****
***/

namespace
{
    struct NameAndNum
    {
        const char * name;
        int num;
    };

    const struct NameAndNum showModes[] = {
        { "show-all",         TorrentFilter::SHOW_ALL },
        { "show-active",      TorrentFilter::SHOW_ACTIVE },
        { "show-downloading", TorrentFilter::SHOW_DOWNLOADING },
        { "show-seeding",     TorrentFilter::SHOW_SEEDING },
        { "show-paused",      TorrentFilter::SHOW_PAUSED }
    };

    const int showModeCount = sizeof(showModes) / sizeof(showModes[0]);

    const struct NameAndNum sortModes[] = {
        { "sort-by-name",     TorrentFilter::SORT_BY_NAME },
        { "sort-by-activity", TorrentFilter::SORT_BY_ACTIVITY },
        { "sort-by-age",      TorrentFilter::SORT_BY_AGE },
        { "sort-by-eta",      TorrentFilter::SORT_BY_ETA },
        { "sort-by-progress", TorrentFilter::SORT_BY_PROGRESS },
        { "sort-by-ratio",    TorrentFilter::SORT_BY_RATIO },
        { "sort-by-size",     TorrentFilter::SORT_BY_SIZE },
        { "sort-by-state",    TorrentFilter::SORT_BY_STATE },
        { "sort-by-tracker",  TorrentFilter::SORT_BY_TRACKER }
    };

    const int sortModeCount = sizeof(sortModes) / sizeof(sortModes[0]);

    int getNum( const struct NameAndNum * rows, int numRows, const QString& name )
    {
        for( int i=0; i<numRows; ++i )
            if( name == rows[i].name )
                return rows[i].num;
        return rows[0].num; // fallback value
    }

    const char* getName( const struct NameAndNum * rows, int numRows, int num )
    {
        for( int i=0; i<numRows; ++i )
            if( num == rows[i].num )
                return rows[i].name;
        return rows[0].name; // fallback value
    }
}

TorrentFilter :: ShowMode
TorrentFilter :: getShowModeFromName( const QString& key ) const
{
    return ShowMode( getNum( showModes, showModeCount, key ) );
}

const char*
TorrentFilter :: getShowName( int mode ) const
{
    if( mode < 0 ) mode = getShowMode( );
    return getName( showModes, showModeCount, mode );
}

TorrentFilter :: SortMode
TorrentFilter :: getSortModeFromName( const QString& key ) const
{
    return SortMode( getNum( sortModes, sortModeCount, key ) );
}

const char*
TorrentFilter :: getSortName( int mode ) const
{
    if( mode < 0 ) mode = getSortMode( );
    return getName( sortModes, sortModeCount, mode );
}

/***
****
***/

void
TorrentFilter :: resort( )
{
    invalidate( );
    sort( 0, myIsAscending ? Qt::AscendingOrder : Qt::DescendingOrder );
}

void
TorrentFilter :: setAscending( bool b )
{
    if( myIsAscending != b )
    {
        myIsAscending = b;
        resort( );
    }
}

void
TorrentFilter :: setSortMode( int sortMode )
{
    if( mySortMode != sortMode )
    {
        myPrefs.set( Prefs :: SORT_MODE, getSortName( sortMode ) );
        mySortMode = SortMode( sortMode );
        setDynamicSortFilter ( true );
        resort( );
    }
}

void TorrentFilter :: sortByActivity ( ) { setSortMode( SORT_BY_ACTIVITY ); }
void TorrentFilter :: sortByAge      ( ) { setSortMode( SORT_BY_AGE ); }
void TorrentFilter :: sortByETA      ( ) { setSortMode( SORT_BY_ETA ); }
void TorrentFilter :: sortById       ( ) { setSortMode( SORT_BY_ID ); }
void TorrentFilter :: sortByName     ( ) { setSortMode( SORT_BY_NAME ); }
void TorrentFilter :: sortByProgress ( ) { setSortMode( SORT_BY_PROGRESS ); }
void TorrentFilter :: sortByRatio    ( ) { setSortMode( SORT_BY_RATIO ); }
void TorrentFilter :: sortBySize     ( ) { setSortMode( SORT_BY_SIZE ); }
void TorrentFilter :: sortByState    ( ) { setSortMode( SORT_BY_STATE ); }
void TorrentFilter :: sortByTracker  ( ) { setSortMode( SORT_BY_TRACKER ); }

bool
TorrentFilter :: lessThan( const QModelIndex& left, const QModelIndex& right ) const
{
    const Torrent * a = sourceModel()->data( left, TorrentModel::TorrentRole ).value<const Torrent*>();
    const Torrent * b = sourceModel()->data( right, TorrentModel::TorrentRole ).value<const Torrent*>();
    bool less;

    switch( getSortMode( ) )
    {
        case SORT_BY_SIZE:
            less = a->sizeWhenDone() < b->sizeWhenDone();
            break;
        case SORT_BY_ACTIVITY:
            less = a->downloadSpeed() + a->uploadSpeed() < b->downloadSpeed() + b->uploadSpeed();
            break;
        case SORT_BY_AGE:
            less = a->dateAdded() < b->dateAdded();
            break;
        case SORT_BY_ID:
            less = a->id() < b->id();
            break;
        case SORT_BY_RATIO:
            less = a->compareRatio( *b ) < 0;
            break;
        case SORT_BY_PROGRESS:
            less = a->percentDone() < b->percentDone();
            break;
        case SORT_BY_ETA:
            less = a->compareETA( *b ) < 0;
            break;
        case SORT_BY_STATE:
            if( a->hasError() != b->hasError() )
                less = a->hasError();
            else
                less = a->getActivity() < b->getActivity();
            break;
        case SORT_BY_TRACKER:
            less = a->compareTracker( *b ) < 0;
            break;
        default:
            less = a->name().compare( b->name(), Qt::CaseInsensitive ) > 0;
            break;
    }

    return less;
}

int
TorrentFilter :: hiddenRowCount( ) const
{
    return sourceModel()->rowCount( ) - rowCount( );
}

