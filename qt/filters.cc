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

#include "filters.h"

const QString FilterMode::names[NUM_MODES] =
{
    "show-all",
    "show-active",
    "show-downloading",
    "show-seeding",
    "show-paused",
    "show-finished",
    "show-verifying",
    "show-error",
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
