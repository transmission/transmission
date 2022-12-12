// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <functional>
#include <optional>
#include <memory>

#include "transmission.h"

#include "net.h" // tr_address
#include "peer-mse.h" // tr_message_stream_encryption::DH

/** @addtogroup peers Peers
    @{ */

namespace libtransmission
{
class TimerMaker;
}

class tr_peerIo;

/** @brief opaque struct holding handshake state information.
           freed when the handshake is completed. */

class tr_handshake
{
public:
    enum class State
    {
        // incoming
        AwaitingHandshake,
        AwaitingPeerId,
        AwaitingYa,
        AwaitingPadA,
        AwaitingCryptoProvide,
        AwaitingPadC,
        AwaitingIa,
        AwaitingPayloadStream,

        // outgoing
        AwaitingYb,
        AwaitingVc,
        AwaitingCryptoSelect,
        AwaitingPadD
    };

    struct Result
    {
        std::shared_ptr<tr_peerIo> io;
        std::optional<tr_peer_id_t> peer_id;
        bool read_anything_from_peer;
        bool is_connected;
    };

    using DoneFunc = std::function<bool(Result const&)>;

    class Mediator
    {
    public:
        struct TorrentInfo
        {
            tr_sha1_digest_t info_hash;
            tr_peer_id_t client_peer_id;
            tr_torrent_id_t id;
            bool is_done;
        };

        virtual ~Mediator() = default;

        [[nodiscard]] virtual std::optional<TorrentInfo> torrent_info(tr_sha1_digest_t const& info_hash) const = 0;
        [[nodiscard]] virtual std::optional<TorrentInfo> torrent_info_from_obfuscated(
            tr_sha1_digest_t const& info_hash) const = 0;
        [[nodiscard]] virtual libtransmission::TimerMaker& timer_maker() = 0;
        [[nodiscard]] virtual bool allows_dht() const = 0;
        [[nodiscard]] virtual bool allows_tcp() const = 0;
        [[nodiscard]] virtual bool is_peer_known_seed(tr_torrent_id_t tor_id, tr_address addr) const = 0;
        [[nodiscard]] virtual size_t pad(void* setme, size_t max_bytes) const = 0;
        [[nodiscard]] virtual tr_message_stream_encryption::DH::private_key_bigend_t private_key() const
        {
            return tr_message_stream_encryption::DH::randomPrivateKey();
        }

        virtual void set_utp_failed(tr_sha1_digest_t const& info_hash, tr_address) = 0;
    };

    virtual ~tr_handshake() = default;

    static std::unique_ptr<tr_handshake> create(
        Mediator& mediator,
        std::shared_ptr<tr_peerIo> const& peer_io,
        tr_encryption_mode mode,
        DoneFunc done_func);
};

/** @} */
