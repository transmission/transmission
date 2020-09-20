/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "Filters.h"

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
