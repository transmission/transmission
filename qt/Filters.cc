/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "Filters.h"

QString const FilterMode::names[NUM_MODES] =
{
    QLatin1String("show-all"),
    QLatin1String("show-active"),
    QLatin1String("show-downloading"),
    QLatin1String("show-seeding"),
    QLatin1String("show-paused"),
    QLatin1String("show-finished"),
    QLatin1String("show-verifying"),
    QLatin1String("show-error")
};

int FilterMode::modeFromName(QString const& name)
{
    for (int i = 0; i < NUM_MODES; ++i)
    {
        if (names[i] == name)
        {
            return i;
        }
    }

    return FilterMode().mode(); // use the default value
}

QString const SortMode::names[NUM_MODES] =
{
    QLatin1String("sort-by-activity"),
    QLatin1String("sort-by-age"),
    QLatin1String("sort-by-eta"),
    QLatin1String("sort-by-name"),
    QLatin1String("sort-by-progress"),
    QLatin1String("sort-by-queue"),
    QLatin1String("sort-by-ratio"),
    QLatin1String("sort-by-size"),
    QLatin1String("sort-by-state"),
    QLatin1String("sort-by-id")
};

int SortMode::modeFromName(QString const& name)
{
    for (int i = 0; i < NUM_MODES; ++i)
    {
        if (names[i] == name)
        {
            return i;
        }
    }

    return SortMode().mode(); // use the default value
}
