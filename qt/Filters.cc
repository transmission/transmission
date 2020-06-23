/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstdint> // uint64_t

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

// NB: if you change this function, update TorrentFields too
bool FilterMode::test(Torrent const& tor, int mode)
{
    switch (mode)
    {
    case SHOW_ACTIVE:
        return tor.peersWeAreUploadingTo() > 0 || tor.peersWeAreDownloadingFrom() > 0 || tor.isVerifying();

    case SHOW_DOWNLOADING:
        return tor.isDownloading() || tor.isWaitingToDownload();

    case SHOW_ERROR:
        return tor.hasError();

    case SHOW_FINISHED:
        return tor.isFinished();

    case SHOW_PAUSED:
        return tor.isPaused();

    case SHOW_SEEDING:
        return tor.isSeeding() || tor.isWaitingToSeed();

    case SHOW_VERIFYING:
        return tor.isVerifying() || tor.isWaitingToVerify();

    default: // SHOW_ALL
        return true;
    }
}

/***
****
***/

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
