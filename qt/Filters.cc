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
        return tor.peers_we_are_uploading_to() > 0 || tor.peers_we_are_downloading_from() > 0 || tor.is_verifying();

    case ShowMode::ShowDownloading:
        return tor.is_downloading() || tor.is_waiting_to_download();

    case ShowMode::ShowError:
        return tor.has_error();

    case ShowMode::ShowFinished:
        return tor.is_finished();

    case ShowMode::ShowPaused:
        return tor.is_paused();

    case ShowMode::ShowSeeding:
        return tor.is_seeding() || tor.is_waiting_to_seed();

    case ShowMode::ShowVerifying:
        return tor.is_verifying() || tor.is_waiting_to_verify();

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
