// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <chrono>
#include <cstddef> // for size_t
#include <functional>
#include <memory>
#include <optional>

#include "transmission.h"

#include "net.h" // tr_address
#include "peer-mse.h" // tr_message_stream_encryption::DH
#include "peer-io.h"
#include "timer.h"

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
    using DH = tr_message_stream_encryption::DH;

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
        [[nodiscard]] virtual bool is_peer_known_seed(tr_torrent_id_t tor_id, tr_address const& addr) const = 0;
        [[nodiscard]] virtual size_t pad(void* setme, size_t max_bytes) const = 0;
        [[nodiscard]] virtual DH::private_key_bigend_t private_key() const
        {
            return DH::randomPrivateKey();
        }

        virtual void set_utp_failed(tr_sha1_digest_t const& info_hash, tr_address const&) = 0;
    };

    tr_handshake(Mediator* mediator, std::shared_ptr<tr_peerIo> peer_io, tr_encryption_mode mode_in, DoneFunc done_func);

    virtual ~tr_handshake() = default;

    void set_peer_id(tr_peer_id_t const& id) noexcept
    {
        peer_id_ = id;
    }

    void set_have_read_anything_from_peer(bool val) noexcept
    {
        have_read_anything_from_peer_ = val;
    }

    ReadState done(bool is_connected)
    {
        peer_io_->clearCallbacks();
        return fire_done(is_connected) ? READ_LATER : READ_ERR;
    }

    [[nodiscard]] auto is_incoming() const noexcept
    {
        return peer_io_->isIncoming();
    }

    [[nodiscard]] auto* peer_id() noexcept
    {
        return peer_io_.get();
    }

    [[nodiscard]] auto torrent_info(tr_sha1_digest_t const& info_hash) const
    {
        return mediator_->torrent_info(info_hash);
    }

    [[nodiscard]] auto torrent_info_from_obfuscated(tr_sha1_digest_t const& info_hash) const
    {
        return mediator_->torrent_info_from_obfuscated(info_hash);
    }

    [[nodiscard]] auto allows_dht() const
    {
        return mediator_->allows_dht();
    }

    [[nodiscard]] auto allows_tcp() const
    {
        return mediator_->allows_tcp();
    }

    [[nodiscard]] auto is_peer_known_seed(tr_torrent_id_t tor_id, tr_address const& addr) const
    {
        return mediator_->is_peer_known_seed(tor_id, addr);
    }

    [[nodiscard]] auto pad(void* setme, size_t max_bytes) const
    {
        return mediator_->pad(setme, max_bytes);
    }

    [[nodiscard]] auto* peer_io() noexcept
    {
        return peer_io_.get();
    }

    [[nodiscard]] auto const* peer_io() const noexcept
    {
        return peer_io_.get();
    }

    [[nodiscard]] auto display_name() const
    {
        return peer_io_->display_name();
    }

    void set_utp_failed(tr_sha1_digest_t const& info_hash, tr_address const& addr)
    {
        mediator_->set_utp_failed(info_hash, addr);
    }

    [[nodiscard]] constexpr auto state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] constexpr auto is_state(State state) const noexcept
    {
        return state_ == state;
    }

    constexpr void set_state(State state)
    {
        state_ = state;
    }

    [[nodiscard]] constexpr std::string_view state_string() const
    {
        return state_string(state_);
    }

    [[nodiscard]] constexpr uint32_t cryptoProvide() const
    {
        uint32_t provide = 0;

        switch (encryption_mode)
        {
        case TR_ENCRYPTION_REQUIRED:
        case TR_ENCRYPTION_PREFERRED:
            provide |= CryptoProvideCrypto;
            break;

        case TR_CLEAR_PREFERRED:
            provide |= CryptoProvideCrypto | CryptoProvidePlaintext;
            break;
        }

        return provide;
    }

    static auto constexpr CryptoProvidePlaintext = int{ 1 };
    static auto constexpr CryptoProvideCrypto = int{ 2 };

    bool have_sent_bittorrent_handshake = false;
    DH dh = {};
    tr_encryption_mode encryption_mode;
    uint16_t pad_c_len = {};
    uint16_t pad_d_len = {};
    uint16_t ia_len = {};
    uint32_t crypto_select = {};
    uint32_t crypto_provide = {};

protected:
    bool fire_done(bool is_connected)
    {
        if (!done_func_)
        {
            return false;
        }

        auto cb = DoneFunc{};
        std::swap(cb, done_func_);

        auto peer_io = std::shared_ptr<tr_peerIo>{};
        std::swap(peer_io, peer_io_);

        bool const success = (cb)(Result{ std::move(peer_io), peer_id_, have_read_anything_from_peer_, is_connected });
        return success;
    }

private:
    static auto constexpr HandshakeTimeoutSec = std::chrono::seconds{ 30 };

    [[nodiscard]] static constexpr std::string_view state_string(State state)
    {
        using State = tr_handshake::State;

        switch (state)
        {
        case State::AwaitingHandshake:
            return "awaiting handshake";
        case State::AwaitingPeerId:
            return "awaiting peer id";
        case State::AwaitingYa:
            return "awaiting ya";
        case State::AwaitingPadA:
            return "awaiting pad a";
        case State::AwaitingCryptoProvide:
            return "awaiting crypto provide";
        case State::AwaitingPadC:
            return "awaiting pad c";
        case State::AwaitingIa:
            return "awaiting ia";
        case State::AwaitingPayloadStream:
            return "awaiting payload stream";

        // outgoing
        case State::AwaitingYb:
            return "awaiting yb";
        case State::AwaitingVc:
            return "awaiting vc";
        case State::AwaitingCryptoSelect:
            return "awaiting crypto select";
        case State::AwaitingPadD:
            return "awaiting pad d";
        }
    }

    Mediator* mediator_ = nullptr;

    std::shared_ptr<tr_peerIo> peer_io_;

    std::optional<tr_peer_id_t> peer_id_;
    bool have_read_anything_from_peer_ = false;
    DoneFunc done_func_;

    State state_ = State::AwaitingHandshake;

    std::unique_ptr<libtransmission::Timer> timeout_timer_;
};
