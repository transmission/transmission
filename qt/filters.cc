/*
 * This file Copyright (C) 2009 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include "filters.h"

const QString FilterMode::names[NUM_MODES] = {
    "show-all",
    "show-active",
    "show-downloading",
    "show-seeding",
    "show-paused"
};

int
FilterMode :: modeFromName( const QString& name )
{
    for( int i=0; i<NUM_MODES; ++i )
        if( names[i] == name )
            return i;
    return FilterMode().mode(); // use the default value
}

const QString SortMode::names[NUM_MODES] = {
    "sort-by-activity",
    "sort-by-age",
    "sort-by-eta",
    "sort-by-name",
    "sort-by-progress",
    "sort-by-ratio",
    "sort-by-size",
    "sort-by-state",
    "sort-by-tracker",
    "sort-by-id"
};

int
SortMode :: modeFromName( const QString& name )
{
    for( int i=0; i<NUM_MODES; ++i )
        if( names[i] == name )
            return i;
    return SortMode().mode(); // use the default value
}
