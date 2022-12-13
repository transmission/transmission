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

#include "net.h"
#include "peer-mse.h" // tr_message_stream_encryption::DH
#include "peer-io.h"
#include "timer.h"

// short-term class which manages the handshake phase of a tr_peerIo
class tr_handshake
{
public:
    using DH = tr_message_stream_encryption::DH;

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

private:
    enum class ParseResult
    {
        Ok,
        EncryptionWrong,
        BadTorrent,
        PeerIsSelf,
    };

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

    bool build_handshake_message(tr_peerIo* io, uint8_t* buf) const;

    ReadState read_crypto_provide(tr_peerIo*);
    ReadState read_crypto_select(tr_peerIo*);
    ReadState read_handshake(tr_peerIo*);
    ReadState read_ia(tr_peerIo*);
    ReadState read_pad_a(tr_peerIo*);
    ReadState read_pad_c(tr_peerIo*);
    ReadState read_pad_d(tr_peerIo*);
    ReadState read_payload_stream(tr_peerIo*);
    ReadState read_peer_id(tr_peerIo*);
    ReadState read_vc(tr_peerIo*);
    ReadState read_ya(tr_peerIo*);
    ReadState read_yb(tr_peerIo*);

    void send_ya(tr_peerIo*);

    ParseResult parse_handshake(tr_peerIo* peer_io);

    static ReadState can_read(tr_peerIo* peer_io, void* vhandshake, size_t* piece);
    static void on_error(tr_peerIo* io, short what, void* vhandshake);

    constexpr void set_peer_id(tr_peer_id_t const& id) noexcept
    {
        peer_id_ = id;
    }

    constexpr void set_have_read_anything_from_peer(bool val) noexcept
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

    [[nodiscard]] constexpr auto state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] constexpr auto is_state(State state) const noexcept
    {
        return state_ == state;
    }

    constexpr void set_state(State state) noexcept
    {
        state_ = state;
    }

    [[nodiscard]] constexpr std::string_view state_string() const
    {
        return state_string(state_);
    }

    [[nodiscard]] constexpr auto crypto_provide() const
    {
        auto provide = uint32_t{};

        switch (encryption_mode_)
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

    [[nodiscard]] static constexpr std::string_view state_string(State state) noexcept
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

    template<size_t PadMax>
    void send_public_key_and_pad(tr_peerIo* io)
    {
        auto const public_key = dh_.publicKey();
        auto outbuf = std::array<std::byte, std::size(public_key) + PadMax>{};
        auto const data = std::data(outbuf);
        auto walk = data;
        walk = std::copy(std::begin(public_key), std::end(public_key), walk);
        walk += mediator_->pad(walk, PadMax);
        io->writeBytes(data, walk - data, false);
    }

    static auto constexpr HandshakeTimeoutSec = std::chrono::seconds{ 30 };

    static auto constexpr CryptoProvidePlaintext = int{ 1 };
    static auto constexpr CryptoProvideCrypto = int{ 2 };

    DH dh_ = {};

    DoneFunc done_func_;

    std::optional<tr_peer_id_t> peer_id_;

    std::shared_ptr<tr_peerIo> peer_io_;

    std::unique_ptr<libtransmission::Timer> timeout_timer_;

    Mediator* mediator_ = nullptr;

    State state_ = State::AwaitingHandshake;

    tr_encryption_mode encryption_mode_;

    uint32_t crypto_select_ = {};
    uint32_t crypto_provide_ = {};
    uint16_t pad_c_len_ = {};
    uint16_t pad_d_len_ = {};
    uint16_t ia_len_ = {};

    bool have_read_anything_from_peer_ = false;

    bool have_sent_bittorrent_handshake_ = false;
};
