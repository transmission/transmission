// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <optional>

#include "transmission.h"

/** @addtogroup peers Peers
    @{ */

class tr_peerIo;

/** @brief opaque struct holding hanshake state information.
           freed when the handshake is completed. */
struct tr_handshake;

struct tr_handshake_result
{
    struct tr_handshake* handshake;
    tr_peerIo* io;
    bool readAnythingFromPeer;
    bool isConnected;
    void* userData;
    std::optional<tr_peer_id_t> peer_id;
};

/* returns true on success, false on error */
using tr_handshake_done_func = bool (*)(tr_handshake_result const& result);

/** @brief create a new handshake */
tr_handshake* tr_handshakeNew(
    tr_peerIo* io,
    tr_encryption_mode encryption_mode,
    tr_handshake_done_func when_done,
    void* when_done_user_data);

void tr_handshakeAbort(tr_handshake* handshake);

tr_peerIo* tr_handshakeStealIO(tr_handshake* handshake);

/** @} */
