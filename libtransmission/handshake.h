// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <optional>
#include <memory>

#include "transmission.h"

#include "net.h" // tr_address
#include "peer-mse.h" // tr_message_stream_encryption::DH
#include "timer.h"

/** @addtogroup peers Peers
    @{ */

class tr_peerIo;

/** @brief opaque struct holding handshake state information.
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

class tr_handshake_mediator
{
public:
    struct torrent_info
    {
        tr_sha1_digest_t info_hash;
        tr_peer_id_t client_peer_id;
        tr_torrent_id_t id;
        bool is_done;
    };

    [[nodiscard]] virtual std::optional<torrent_info> torrentInfo(tr_sha1_digest_t const& info_hash) const = 0;

    [[nodiscard]] virtual std::optional<torrent_info> torrentInfoFromObfuscated(tr_sha1_digest_t const& info_hash) const = 0;

    [[nodiscard]] virtual std::unique_ptr<libtransmission::Timer> createTimer() = 0;

    [[nodiscard]] virtual bool isDHTEnabled() const = 0;

    [[nodiscard]] virtual bool allowsTCP() const = 0;

    [[nodiscard]] virtual bool isPeerKnownSeed(tr_torrent_id_t tor_id, tr_address addr) const = 0;

    [[nodiscard]] virtual size_t pad(void* setme, size_t max_bytes) const = 0;

    [[nodiscard]] virtual tr_message_stream_encryption::DH::private_key_bigend_t privateKey() const
    {
        return tr_message_stream_encryption::DH::randomPrivateKey();
    }

    virtual void setUTPFailed(tr_sha1_digest_t const& info_hash, tr_address) = 0;
};

/* returns true on success, false on error */
using tr_handshake_done_func = bool (*)(tr_handshake_result const& result);

/** @brief create a new handshake */
tr_handshake* tr_handshakeNew(
    std::shared_ptr<tr_handshake_mediator> mediator,
    tr_peerIo* io,
    tr_encryption_mode encryption_mode,
    tr_handshake_done_func done_func,
    void* done_func_user_data);

void tr_handshakeAbort(tr_handshake* handshake);

tr_peerIo* tr_handshakeStealIO(tr_handshake* handshake);

/** @} */
