/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "Filters.h"

std::array<QString, FilterMode::NUM_MODES> const FilterMode::Names =
{
    QStringLiteral("show-all"),
    QStringLiteral("show-active"),
    QStringLiteral("show-downloading"),
    QStringLiteral("show-seeding"),
    QStringLiteral("show-paused"),
    QStringLiteral("show-finished"),
    QStringLiteral("show-verifying"),
    QStringLiteral("show-error")
};

int FilterMode::modeFromName(QString const& name)
{
    for (int i = 0; i < NUM_MODES; ++i)
    {
        if (Names[i] == name)
        {
            return i;
        }
    }

    return FilterMode().mode(); // use the default value
}

std::array<QString, SortMode::NUM_MODES> const SortMode::Names =
{
    QStringLiteral("sort-by-activity"),
    QStringLiteral("sort-by-age"),
    QStringLiteral("sort-by-eta"),
    QStringLiteral("sort-by-name"),
    QStringLiteral("sort-by-progress"),
    QStringLiteral("sort-by-queue"),
    QStringLiteral("sort-by-ratio"),
    QStringLiteral("sort-by-size"),
    QStringLiteral("sort-by-state"),
    QStringLiteral("sort-by-id")
};

int SortMode::modeFromName(QString const& name)
{
    for (int i = 0; i < NUM_MODES; ++i)
    {
        if (Names[i] == name)
        {
            return i;
        }
    }

    return SortMode().mode(); // use the default value
}
