// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <cstddef> // for size_t
#include <memory>
#include <utility> // for std::pair<>

#include "peer-common.h" // for tr_peer

class tr_peer;
class tr_peerIo;
struct tr_address;
struct tr_torrent;

/**
 * @addtogroup peers Peers
 * @{
 */

class tr_peerMsgs : public tr_peer
{
public:
    tr_peerMsgs(
        tr_torrent const* tor,
        peer_atom* atom_in,
        tr_interned_string user_agent,
        bool connection_is_encrypted,
        bool connection_is_incoming,
        bool connection_is_utp)
        : tr_peer{ tor, atom_in }
        , user_agent_{ user_agent }
        , connection_is_encrypted_{ connection_is_encrypted }
        , connection_is_incoming_{ connection_is_incoming }
        , connection_is_utp_{ connection_is_utp }
    {
        ++n_peers;
    }

    virtual ~tr_peerMsgs() override;

    [[nodiscard]] static auto size() noexcept
    {
        return n_peers.load();
    }

    [[nodiscard]] constexpr auto client_is_choked() const noexcept
    {
        return client_is_choked_;
    }

    [[nodiscard]] constexpr auto client_is_interested() const noexcept
    {
        return client_is_interested_;
    }

    [[nodiscard]] constexpr auto peer_is_choked() const noexcept
    {
        return peer_is_choked_;
    }

    [[nodiscard]] constexpr auto peer_is_interested() const noexcept
    {
        return peer_is_interested_;
    }

    [[nodiscard]] constexpr auto is_encrypted() const noexcept
    {
        return connection_is_encrypted_;
    }

    [[nodiscard]] constexpr auto is_incoming_connection() const noexcept
    {
        return connection_is_incoming_;
    }

    [[nodiscard]] constexpr auto is_utp_connection() const noexcept
    {
        return connection_is_utp_;
    }

    [[nodiscard]] constexpr auto const& user_agent() const noexcept
    {
        return user_agent_;
    }

    [[nodiscard]] constexpr auto is_active(tr_direction direction) const noexcept
    {
        return is_active_[direction];
    }

    [[nodiscard]] virtual std::pair<tr_address, tr_port> socketAddress() const = 0;

    virtual void cancel_block_request(tr_block_index_t block) = 0;

    virtual void set_choke(bool peer_is_choked) = 0;
    virtual void set_interested(bool client_is_interested) = 0;

    virtual void pulse() = 0;

    virtual void onTorrentGotMetainfo() = 0;

    virtual void on_piece_completed(tr_piece_index_t) = 0;

protected:
    constexpr void set_client_choked(bool val) noexcept
    {
        client_is_choked_ = val;
    }

    constexpr void set_client_interested(bool val) noexcept
    {
        client_is_interested_ = val;
    }

    constexpr void set_peer_choked(bool val) noexcept
    {
        peer_is_choked_ = val;
    }

    constexpr void set_peer_interested(bool val) noexcept
    {
        peer_is_interested_ = val;
    }

    constexpr void set_active(tr_direction direction, bool active) noexcept
    {
        is_active_[direction] = active;
    }

    constexpr void set_user_agent(tr_interned_string val) noexcept
    {
        user_agent_ = val;
    }

private:
    static inline auto n_peers = std::atomic<size_t>{};

    // What software the peer is running.
    // Derived from the `v` string in LTEP's handshake dictionary, when available.
    tr_interned_string user_agent_;

    bool const connection_is_encrypted_;
    bool const connection_is_incoming_;
    bool const connection_is_utp_;

    std::array<bool, 2> is_active_ = {};

    // whether or not the peer is choking us.
    bool client_is_choked_ = true;

    // whether or not we've indicated to the peer that we would download from them if unchoked
    bool client_is_interested_ = false;

    // whether or not we've choked this peer
    bool peer_is_choked_ = true;

    // whether or not the peer has indicated it will download from us
    bool peer_is_interested_ = false;
};

tr_peerMsgs* tr_peerMsgsNew(
    tr_torrent* torrent,
    peer_atom* atom,
    std::shared_ptr<tr_peerIo> io,
    tr_interned_string user_agent,
    tr_peer_callback callback,
    void* callback_data);

/* @} */
