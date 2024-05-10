// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>
#include <string_view>

#include "libtransmission/transmission.h"

#include "libtransmission/peer-common.h"

using tr_peer_callback_webseed = tr_peer_callback_generic;

class tr_webseed : public tr_peer
{
protected:
    explicit tr_webseed(tr_torrent& tor_in)
        : tr_peer{ tor_in }
    {
    }

public:
    [[nodiscard]] static std::unique_ptr<tr_webseed> create(
        tr_torrent& torrent,
        std::string_view,
        tr_peer_callback_webseed callback,
        void* callback_data);

    [[nodiscard]] virtual tr_webseed_view get_view() const = 0;
};
