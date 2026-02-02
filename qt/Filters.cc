// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "Filters.h"

// NOTE: `ShowModeFields` is the set of all Torrent properties
// needed to correctly run these tests. If you change these tests,
// then update `ShowModeFields` accordingly.
bool should_show_torrent(Torrent const& tor, ShowMode const mode)
{
    switch (mode)
    {
    case ShowMode::ShowActive:
        return tor.peersWeAreUploadingTo() > 0 || tor.peersWeAreDownloadingFrom() > 0 || tor.isVerifying();

    case ShowMode::ShowDownloading:
        return tor.isDownloading() || tor.isWaitingToDownload();

    case ShowMode::ShowError:
        return tor.hasError();

    case ShowMode::ShowFinished:
        return tor.isFinished();

    case ShowMode::ShowPaused:
        return tor.isPaused();

    case ShowMode::ShowSeeding:
        return tor.isSeeding() || tor.isWaitingToSeed();

    case ShowMode::ShowVerifying:
        return tor.isVerifying() || tor.isWaitingToVerify();

    default: // SHOW_ALL
        return true;
    }
}

// The Torrent properties that can affect ShowMode filtering.
// When one of these changes, it's time to refilter.
// Update this as needed when ShowModeTests changes.
Torrent::fields_t const ShowModeFields{
    (uint64_t{ 1 } << Torrent::TORRENT_ERROR) | //
    (uint64_t{ 1 } << Torrent::IS_FINISHED) | //
    (uint64_t{ 1 } << Torrent::PEERS_GETTING_FROM_US) | //
    (uint64_t{ 1 } << Torrent::PEERS_SENDING_TO_US) | //
    (uint64_t{ 1 } << Torrent::STATUS) //
};
