// This file Copyright © 2017-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "transmission.h"

#include "bitfield.h"
#include "clients.h"
#include "crypto-utils.h"
#include "handshake.h"
#include "log.h"
#include "peer-io.h"
#include "timer.h"
#include "tr-assert.h"
#include "tr-buffer.h"
#include "utils.h"

#define tr_logAddTraceHand(handshake, msg) tr_logAddTrace(msg, (handshake)->peer_io_->display_name())

using namespace std::literals;
using DH = tr_message_stream_encryption::DH;

bool tr_handshake::build_handshake_message(tr_peerIo* io, uint8_t* buf) const
{
    auto const& info_hash = io->torrent_hash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "build_handshake_message requires an info_hash");

    auto const info = mediator_->torrent(info_hash);
    if (!info)
    {
        return false;
    }

    auto flags = tr_bitfield{ HandshakeFlagsBits };
    flags.set(LtepFlag);
    flags.set(FextFlag);
    if (mediator_->allows_dht())
    {
        flags.set(DhtFlag);
    }
    auto const flag_bytes = flags.raw();

    [[maybe_unused]] auto* walk = buf;
    walk = std::copy_n(reinterpret_cast<uint8_t const*>(std::data(HandshakeName)), std::size(HandshakeName), walk);
    walk = std::copy(std::begin(flag_bytes), std::end(flag_bytes), walk);
    walk = std::copy_n(reinterpret_cast<char const*>(std::data(info_hash)), std::size(info_hash), walk);
    walk = std::copy(std::begin(info->client_peer_id), std::end(info->client_peer_id), walk);
    TR_ASSERT(walk - buf == HandshakeSize);

    return true;
}

tr_handshake::ParseResult tr_handshake::parse_handshake(tr_peerIo* peer_io)
{
    tr_logAddTraceHand(this, fmt::format("payload: need {}, got {}", HandshakeSize, peer_io->read_buffer_size()));

    if (peer_io->read_buffer_size() < HandshakeSize)
    {
        return ParseResult::EncryptionWrong;
    }

    /* confirm the protocol */
    auto name = decltype(HandshakeName){};
    peer_io->read_bytes(std::data(name), std::size(name));
    if (name != HandshakeName)
    {
        return ParseResult::EncryptionWrong;
    }

    /* read the reserved bytes */
    auto flags = tr_bitfield{ HandshakeFlagsBits };
    auto reserved = std::array<uint8_t, HandshakeFlagsBytes>{};
    peer_io->read_bytes(std::data(reserved), std::size(reserved));
    flags.setRaw(std::data(reserved), std::size(reserved));
    peer_io->set_supports_dht(flags.test(DhtFlag));
    peer_io->set_supports_ltep(flags.test(LtepFlag));
    peer_io->set_supports_fext(flags.test(FextFlag));

    // torrent hash
    auto info_hash = tr_sha1_digest_t{};
    peer_io->read_bytes(std::data(info_hash), std::size(info_hash));
    if (info_hash == tr_sha1_digest_t{} || info_hash != peer_io->torrent_hash())
    {
        tr_logAddTraceHand(this, "peer returned the wrong hash. wtf?");
        return ParseResult::BadTorrent;
    }

    // peer_id
    auto peer_id = tr_peer_id_t{};
    peer_io->read_bytes(std::data(peer_id), std::size(peer_id));
    set_peer_id(peer_id);

    /* peer id */
    auto const peer_id_sv = std::string_view{ std::data(peer_id), std::size(peer_id) };
    tr_logAddTraceHand(this, fmt::format("peer-id is '{}'", peer_id_sv));

    if (auto const info = mediator_->torrent(info_hash); info && info->client_peer_id == peer_id)
    {
        tr_logAddTraceHand(this, "streuth!  we've connected to ourselves.");
        return ParseResult::PeerIsSelf;
    }

    return ParseResult::Ok;
}

// --- Outgoing Connections

// 1 A->B: our public key (Ya) and some padding (PadA)
void tr_handshake::send_ya(tr_peerIo* io)
{
    send_public_key_and_pad<PadaMaxlen>(io);
    set_state(tr_handshake::State::AwaitingYb);
}

[[nodiscard]] uint32_t tr_handshake::get_crypto_select(tr_encryption_mode encryption_mode, uint32_t crypto_provide) noexcept
{
    auto choices = std::array<uint32_t, 2>{};
    int n_choices = 0;

    switch (encryption_mode)
    {
    case TR_ENCRYPTION_REQUIRED:
        choices[n_choices++] = CryptoProvideCrypto;
        break;

    case TR_ENCRYPTION_PREFERRED:
        choices[n_choices++] = CryptoProvideCrypto;
        choices[n_choices++] = CryptoProvidePlaintext;
        break;

    case TR_CLEAR_PREFERRED:
        choices[n_choices++] = CryptoProvidePlaintext;
        choices[n_choices++] = CryptoProvideCrypto;
        break;
    }

    for (auto const& choice : choices)
    {
        if ((crypto_provide & choice) != 0)
        {
            return choice;
        }
    }

    return 0;
}

ReadState tr_handshake::read_yb(tr_peerIo* peer_io)
{
    if (peer_io->read_buffer_size() < std::size(HandshakeName))
    {
        return READ_LATER;
    }

    bool const is_encrypted = !peer_io->read_buffer_starts_with(HandshakeName);
    auto peer_public_key = DH::key_bigend_t{};
    if (is_encrypted && (peer_io->read_buffer_size() < std::size(peer_public_key)))
    {
        return READ_LATER;
    }

    tr_logAddTraceHand(this, is_encrypted ? "got an encrypted handshake" : "got a plain handshake");

    if (!is_encrypted)
    {
        set_state(tr_handshake::State::AwaitingHandshake);
        return READ_NOW;
    }

    set_have_read_anything_from_peer(true);

    // get the peer's public key
    peer_io->read_bytes(std::data(peer_public_key), std::size(peer_public_key));
    dh_.setPeerPublicKey(peer_public_key);

    /* now send these: HASH('req1', S), HASH('req2', SKEY) xor HASH('req3', S),
     * ENCRYPT(VC, crypto_provide, len(PadC), PadC, len(IA)), ENCRYPT(IA) */
    auto outbuf = libtransmission::Buffer{};

    /* HASH('req1', S) */
    outbuf.add(tr_sha1::digest("req1"sv, dh_.secret()));

    auto const& info_hash = peer_io->torrent_hash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readYb requires an info_hash");

    /* HASH('req2', SKEY) xor HASH('req3', S) */
    {
        auto const req2 = tr_sha1::digest("req2"sv, info_hash);
        auto const req3 = tr_sha1::digest("req3"sv, dh_.secret());
        auto x_or = tr_sha1_digest_t{};
        for (size_t i = 0, n = std::size(x_or); i < n; ++i)
        {
            x_or[i] = req2[i] ^ req3[i];
        }

        outbuf.add(x_or);
    }

    /* ENCRYPT(VC, crypto_provide, len(PadC), PadC
     * PadC is reserved for future extensions to the handshake...
     * standard practice at this time is for it to be zero-length */
    peer_io->write(outbuf, false);
    peer_io->encrypt_init(peer_io->is_incoming(), dh_, info_hash);
    outbuf.add(VC);
    outbuf.add_uint32(crypto_provide());
    outbuf.add_uint16(0);

    /* ENCRYPT len(IA)), ENCRYPT(IA) */
    if (auto msg = std::array<uint8_t, HandshakeSize>{}; build_handshake_message(peer_io, std::data(msg)))
    {
        outbuf.add_uint16(std::size(msg));
        outbuf.add(msg);
        have_sent_bittorrent_handshake_ = true;
    }
    else
    {
        return done(false);
    }

    /* send it */
    set_state(State::AwaitingVc);
    peer_io->write(outbuf, false);
    return READ_NOW;
}

// MSE spec: "Since the length of [PadB is] unknown,
// A will be able to resynchronize on ENCRYPT(VC)"
ReadState tr_handshake::read_vc(tr_peerIo* peer_io)
{
    auto const info_hash = peer_io->torrent_hash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readVC requires an info_hash");

    // We need to find the end of PadB by looking for `ENCRYPT(VC)`,
    // so calculate and cache the value of `ENCRYPT(VC)`.
    if (!encrypted_vc_)
    {
        auto needle = VC;
        auto filter = tr_message_stream_encryption::Filter{};
        filter.encryptInit(true, dh_, info_hash);
        filter.encrypt(std::size(needle), std::data(needle));
        encrypted_vc_ = needle;
    }

    for (size_t i = 0; i < PadbMaxlen; ++i)
    {
        if (peer_io->read_buffer_size() < std::size(*encrypted_vc_))
        {
            tr_logAddTraceHand(this, "not enough bytes... returning read_more");
            return READ_LATER;
        }

        if (peer_io->read_buffer_starts_with(*encrypted_vc_))
        {
            tr_logAddTraceHand(this, "got it!");
            // We already know it's a match; now we just need to
            // consume it from the read buffer.
            peer_io->decrypt_init(peer_io->is_incoming(), dh_, info_hash);
            peer_io->read_bytes(std::data(*encrypted_vc_), std::size(*encrypted_vc_));
            set_state(tr_handshake::State::AwaitingCryptoSelect);
            return READ_NOW;
        }

        peer_io->read_buffer_drain(1);
    }

    tr_logAddTraceHand(this, "couldn't find ENCRYPT(VC)");
    return done(false);
}

ReadState tr_handshake::read_crypto_select(tr_peerIo* peer_io)
{
    if (static size_t constexpr NeedLen = sizeof(uint32_t) + sizeof(uint16_t); peer_io->read_buffer_size() < NeedLen)
    {
        return READ_LATER;
    }

    auto crypto_select = uint32_t{};
    peer_io->read_uint32(&crypto_select);
    crypto_select_ = crypto_select;
    tr_logAddTraceHand(this, fmt::format("crypto select is {}", crypto_select));

    if ((crypto_select & crypto_provide()) == 0)
    {
        tr_logAddTraceHand(this, "peer selected an encryption option we didn't offer");
        return done(false);
    }

    uint16_t pad_d_len = 0;
    peer_io->read_uint16(&pad_d_len);
    tr_logAddTraceHand(this, fmt::format("pad_d_len is {}", pad_d_len));

    if (pad_d_len > 512)
    {
        tr_logAddTraceHand(this, "encryption handshake: pad_d_len is too long");
        return done(false);
    }

    pad_d_len_ = pad_d_len;

    set_state(tr_handshake::State::AwaitingPadD);
    return READ_NOW;
}

ReadState tr_handshake::read_pad_d(tr_peerIo* peer_io)
{
    size_t const needlen = pad_d_len_;
    tr_logAddTraceHand(this, fmt::format("pad d: need {}, got {}", needlen, peer_io->read_buffer_size()));
    if (peer_io->read_buffer_size() < needlen)
    {
        return READ_LATER;
    }

    peer_io->read_buffer_drain(needlen);

    set_state(tr_handshake::State::AwaitingHandshake);
    return READ_NOW;
}

// --- Incoming Connections

ReadState tr_handshake::read_handshake(tr_peerIo* peer_io)
{
    static auto constexpr Needlen = IncomingHandshakeLen;
    tr_logAddTraceHand(this, fmt::format("payload: need {}, got {}", Needlen, peer_io->read_buffer_size()));
    if (peer_io->read_buffer_size() < Needlen)
    {
        return READ_LATER;
    }

    set_have_read_anything_from_peer(true);

    if (peer_io->read_buffer_starts_with(HandshakeName)) // unencrypted
    {
        if (encryption_mode_ == TR_ENCRYPTION_REQUIRED)
        {
            tr_logAddTraceHand(this, "peer is unencrypted, and we're disallowing that");
            return done(false);
        }
    }
    else // either encrypted or corrupt
    {
        if (is_incoming())
        {
            tr_logAddTraceHand(this, "I think peer is sending us an encrypted handshake...");
            set_state(tr_handshake::State::AwaitingYa);
            return READ_NOW;
        }
    }

    auto name = decltype(HandshakeName){};
    peer_io->read_bytes(std::data(name), std::size(name));
    if (name != HandshakeName)
    {
        return done(false);
    }

    // reserved bytes / flags
    auto reserved = std::array<uint8_t, HandshakeFlagsBytes>{};
    auto flags = tr_bitfield{ HandshakeFlagsBits };
    peer_io->read_bytes(std::data(reserved), std::size(reserved));
    flags.setRaw(std::data(reserved), std::size(reserved));
    peer_io->set_supports_dht(flags.test(DhtFlag));
    peer_io->set_supports_ltep(flags.test(LtepFlag));
    peer_io->set_supports_fext(flags.test(FextFlag));

    /* torrent hash */
    auto hash = tr_sha1_digest_t{};
    peer_io->read_bytes(std::data(hash), std::size(hash));

    if (is_incoming())
    {
        if (!mediator_->torrent(hash))
        {
            tr_logAddTraceHand(this, "peer is trying to connect to us for a torrent we don't have.");
            return done(false);
        }

        peer_io->set_torrent_hash(hash);
    }
    else // outgoing
    {
        if (peer_io->torrent_hash() != hash)
        {
            tr_logAddTraceHand(this, "peer returned the wrong hash. wtf?");
            return done(false);
        }
    }

    // If it's an incoming message, we need to send a response handshake

    if (!have_sent_bittorrent_handshake_)
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};

        if (!build_handshake_message(peer_io, std::data(msg)))
        {
            return done(false);
        }

        peer_io->write_bytes(std::data(msg), std::size(msg), false);
        have_sent_bittorrent_handshake_ = true;
    }

    set_state(State::AwaitingPeerId);
    return READ_NOW;
}

ReadState tr_handshake::read_peer_id(tr_peerIo* peer_io)
{
    // read the peer_id
    auto peer_id = tr_peer_id_t{};
    if (peer_io->read_buffer_size() < std::size(peer_id))
    {
        return READ_LATER;
    }
    peer_io->read_bytes(std::data(peer_id), std::size(peer_id));
    set_peer_id(peer_id);

    auto client = std::array<char, 128>{};
    tr_clientForId(std::data(client), std::size(client), peer_id);
    tr_logAddTraceHand(this, fmt::format("peer-id is '{}' ... isIncoming is {}", std::data(client), is_incoming()));

    // if we've somehow connected to ourselves, don't keep the connection
    auto const info_hash = peer_io_->torrent_hash();
    auto const info = mediator_->torrent(info_hash);
    auto const connected_to_self = info && info->client_peer_id == peer_id;

    return done(!connected_to_self);
}

ReadState tr_handshake::read_ya(tr_peerIo* peer_io)
{
    auto peer_public_key = DH::key_bigend_t{};
    tr_logAddTraceHand(
        this,
        fmt::format("in readYa... need {}, have {}", std::size(peer_public_key), peer_io->read_buffer_size()));

    if (peer_io->read_buffer_size() < std::size(peer_public_key))
    {
        return READ_LATER;
    }

    /* read the incoming peer's public key */
    peer_io->read_bytes(std::data(peer_public_key), std::size(peer_public_key));
    dh_.setPeerPublicKey(peer_public_key);

    // send our public key to the peer
    tr_logAddTraceHand(this, "sending B->A: Diffie Hellman Yb, PadB");
    send_public_key_and_pad<PadbMaxlen>(peer_io);

    set_state(State::AwaitingPadA);
    return READ_NOW;
}

ReadState tr_handshake::read_pad_a(tr_peerIo* peer_io)
{
    // find the end of PadA by looking for HASH('req1', S)
    auto const needle = tr_sha1::digest("req1"sv, dh_.secret());

    for (size_t i = 0; i < PadaMaxlen; ++i)
    {
        if (peer_io->read_buffer_size() < std::size(needle))
        {
            tr_logAddTraceHand(this, "not enough bytes... returning read_more");
            return READ_LATER;
        }

        if (peer_io->read_buffer_starts_with(needle))
        {
            tr_logAddTraceHand(this, "found it... looking setting to awaiting_crypto_provide");
            peer_io->read_buffer_drain(std::size(needle));
            set_state(State::AwaitingCryptoProvide);
            return READ_NOW;
        }

        peer_io->read_buffer_drain(1U);
    }

    tr_logAddTraceHand(this, "couldn't find HASH('req', S)");
    return done(false);
}

ReadState tr_handshake::read_crypto_provide(tr_peerIo* peer_io)
{
    /* HASH('req2', SKEY) xor HASH('req3', S), ENCRYPT(VC, crypto_provide, len(PadC)) */

    uint16_t padc_len = 0;
    uint32_t crypto_provide = 0;
    auto obfuscated_hash = tr_sha1_digest_t{};
    size_t const needlen = sizeof(obfuscated_hash) + /* HASH('req2', SKEY) xor HASH('req3', S) */
        std::size(VC) + sizeof(crypto_provide) + sizeof(padc_len);

    if (peer_io->read_buffer_size() < needlen)
    {
        return READ_LATER;
    }

    /* This next piece is HASH('req2', SKEY) xor HASH('req3', S) ...
     * we can get the first half of that (the obfuscatedTorrentHash)
     * by building the latter and xor'ing it with what the peer sent us */
    tr_logAddTraceHand(this, "reading obfuscated torrent hash...");
    auto req2 = tr_sha1_digest_t{};
    peer_io->read_bytes(std::data(req2), std::size(req2));

    auto const req3 = tr_sha1::digest("req3"sv, dh_.secret());
    for (size_t i = 0; i < std::size(obfuscated_hash); ++i)
    {
        obfuscated_hash[i] = req2[i] ^ req3[i];
    }

    if (auto const info = mediator_->torrent_from_obfuscated(obfuscated_hash); info)
    {
        bool const client_is_seed = info->is_done;
        bool const peer_is_seed = mediator_->is_peer_known_seed(info->id, peer_io->address());
        tr_logAddTraceHand(this, fmt::format("got INCOMING connection's encrypted handshake for torrent [{}]", info->id));
        peer_io->set_torrent_hash(info->info_hash);

        if (client_is_seed && peer_is_seed)
        {
            tr_logAddTraceHand(this, "another seed tried to reconnect to us!");
            return done(false);
        }
    }
    else
    {
        tr_logAddTraceHand(this, "can't find that torrent...");
        return done(false);
    }

    /* next part: ENCRYPT(VC, crypto_provide, len(PadC), */

    auto const& info_hash = peer_io->torrent_hash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readCryptoProvide requires an info_hash");
    peer_io->decrypt_init(peer_io->is_incoming(), dh_, info_hash);

    auto vc_in = vc_t{};
    peer_io->read_bytes(std::data(vc_in), std::size(vc_in));

    peer_io->read_uint32(&crypto_provide);
    crypto_provide_ = crypto_provide;
    tr_logAddTraceHand(this, fmt::format("crypto_provide is {}", crypto_provide));

    peer_io->read_uint16(&padc_len);
    tr_logAddTraceHand(this, fmt::format("padc is {}", padc_len));
    if (padc_len > PadcMaxlen)
    {
        tr_logAddTraceHand(this, "peer's PadC is too big");
        return done(false);
    }

    pad_c_len_ = padc_len;
    set_state(State::AwaitingPadC);
    return READ_NOW;
}

ReadState tr_handshake::read_pad_c(tr_peerIo* peer_io)
{
    if (auto const needlen = pad_c_len_ + sizeof(uint16_t); peer_io->read_buffer_size() < needlen)
    {
        return READ_LATER;
    }

    // read the throwaway padc
    auto pad_c = std::array<char, PadcMaxlen>{};
    peer_io->read_bytes(std::data(pad_c), pad_c_len_);

    /* read ia_len */
    uint16_t ia_len = 0;
    peer_io->read_uint16(&ia_len);
    tr_logAddTraceHand(this, fmt::format("ia_len is {}", ia_len));
    ia_len_ = ia_len;
    set_state(State::AwaitingIa);
    return READ_NOW;
}

ReadState tr_handshake::read_ia(tr_peerIo* peer_io)
{
    size_t const needlen = ia_len_;

    tr_logAddTraceHand(this, fmt::format("reading IA... have {}, need {}", peer_io->read_buffer_size(), needlen));

    if (peer_io->read_buffer_size() < needlen)
    {
        return READ_LATER;
    }

    // B->A: ENCRYPT(VC, crypto_select, len(padD), padD), ENCRYPT2(Payload Stream)

    auto const& info_hash = peer_io->torrent_hash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readIA requires an info_hash");
    peer_io->encrypt_init(peer_io->is_incoming(), dh_, info_hash);
    auto outbuf = libtransmission::Buffer{};

    // send VC
    tr_logAddTraceHand(this, "sending vc");
    outbuf.add(VC);

    /* send crypto_select */
    uint32_t const crypto_select = get_crypto_select(encryption_mode_, crypto_provide_);

    if (crypto_select != 0)
    {
        tr_logAddTraceHand(this, fmt::format("selecting crypto mode '{}'", crypto_select));
        outbuf.add_uint32(crypto_select);
    }
    else
    {
        tr_logAddTraceHand(this, "peer didn't offer an encryption mode we like.");
        return done(false);
    }

    tr_logAddTraceHand(this, "sending pad d");

    /* ENCRYPT(VC, crypto_provide, len(PadD), PadD
     * PadD is reserved for future extensions to the handshake...
     * standard practice at this time is for it to be zero-length */
    outbuf.add_uint16(0);

    /* maybe de-encrypt our connection */
    if (crypto_select == CryptoProvidePlaintext)
    {
        peer_io->write(outbuf, false);
        TR_ASSERT(std::empty(outbuf));
    }

    tr_logAddTraceHand(this, "sending handshake");

    /* send our handshake */
    if (auto msg = std::array<uint8_t, HandshakeSize>{}; build_handshake_message(peer_io, std::data(msg)))
    {
        outbuf.add(msg);
        have_sent_bittorrent_handshake_ = true;
    }
    else
    {
        return done(false);
    }

    /* send it out */
    peer_io->write(outbuf, false);

    /* now await the handshake */
    set_state(State::AwaitingPayloadStream);
    return READ_NOW;
}

ReadState tr_handshake::read_payload_stream(tr_peerIo* peer_io)
{
    static auto constexpr Needlen = HandshakeSize;
    tr_logAddTraceHand(this, fmt::format("reading payload stream... have {}, need {}", peer_io->read_buffer_size(), Needlen));
    if (peer_io->read_buffer_size() < Needlen)
    {
        return READ_LATER;
    }

    /* parse the handshake ... */
    auto const i = parse_handshake(peer_io);
    tr_logAddTraceHand(this, fmt::format("parseHandshake returned {}", static_cast<int>(i)));
    if (i != ParseResult::Ok)
    {
        return done(false);
    }

    /* we've completed the BT handshake... pass the work on to peer-msgs */
    return done(true);
}

// ---

ReadState tr_handshake::can_read(tr_peerIo* peer_io, void* vhandshake, size_t* piece)
{
    auto* handshake = static_cast<tr_handshake*>(vhandshake);

    bool ready_for_more = true;

    /* no piece data in handshake */
    *piece = 0;

    tr_logAddTraceHand(handshake, fmt::format("handling canRead; state is [{}]", handshake->state_string()));

    ReadState ret = READ_NOW;
    while (ready_for_more)
    {
        switch (handshake->state())
        {
        case State::AwaitingHandshake:
            ret = handshake->read_handshake(peer_io);
            break;

        case State::AwaitingPeerId:
            ret = handshake->read_peer_id(peer_io);
            break;

        case State::AwaitingYa:
            ret = handshake->read_ya(peer_io);
            break;

        case State::AwaitingPadA:
            ret = handshake->read_pad_a(peer_io);
            break;

        case State::AwaitingCryptoProvide:
            ret = handshake->read_crypto_provide(peer_io);
            break;

        case State::AwaitingPadC:
            ret = handshake->read_pad_c(peer_io);
            break;

        case State::AwaitingIa:
            ret = handshake->read_ia(peer_io);
            break;

        case State::AwaitingPayloadStream:
            ret = handshake->read_payload_stream(peer_io);
            break;

        case State::AwaitingYb:
            ret = handshake->read_yb(peer_io);
            break;

        case State::AwaitingVc:
            ret = handshake->read_vc(peer_io);
            break;

        case State::AwaitingCryptoSelect:
            ret = handshake->read_crypto_select(peer_io);
            break;

        case State::AwaitingPadD:
            ret = handshake->read_pad_d(peer_io);
            break;

        default:
#ifdef TR_ENABLE_ASSERTS
            TR_ASSERT_MSG(
                false,
                fmt::format(FMT_STRING("unhandled handshake state {:d}"), static_cast<int>(handshake->state())));
#else
            ret = READ_ERR;
            break;
#endif
        }

        if (ret != READ_NOW)
        {
            ready_for_more = false;
        }
        else if (handshake->is_state(State::AwaitingPadC))
        {
            ready_for_more = peer_io->read_buffer_size() >= handshake->pad_c_len_;
        }
        else if (handshake->is_state(State::AwaitingPadD))
        {
            ready_for_more = peer_io->read_buffer_size() >= handshake->pad_d_len_;
        }
        else if (handshake->is_state(State::AwaitingIa))
        {
            ready_for_more = peer_io->read_buffer_size() >= handshake->ia_len_;
        }
    }

    return ret;
}

void tr_handshake::on_error(tr_peerIo* io, tr_error const& error, void* vhandshake)
{
    auto* handshake = static_cast<tr_handshake*>(vhandshake);

    if (io->is_utp() && !io->is_incoming() && handshake->is_state(State::AwaitingYb))
    {
        // the peer probably doesn't speak µTP.

        auto const info_hash = io->torrent_hash();
        auto const info = handshake->mediator_->torrent(info_hash);

        /* Don't mark a peer as non-µTP unless it's really a connect failure. */
        if ((error.code == ETIMEDOUT || error.code == ECONNREFUSED) && info)
        {
            handshake->mediator_->set_utp_failed(info_hash, io->address());
        }

        if (handshake->mediator_->allows_tcp() && io->reconnect())
        {
            auto msg = std::array<uint8_t, HandshakeSize>{};
            handshake->build_handshake_message(io, std::data(msg));
            handshake->have_sent_bittorrent_handshake_ = true;
            handshake->set_state(State::AwaitingHandshake);
            io->write_bytes(std::data(msg), std::size(msg), false);
            return;
        }
    }

    /* if the error happened while we were sending a public key, we might
     * have encountered a peer that doesn't do encryption... reconnect and
     * try a plaintext handshake */
    if ((handshake->is_state(State::AwaitingYb) || handshake->is_state(State::AwaitingVc)) &&
        handshake->encryption_mode_ != TR_ENCRYPTION_REQUIRED && handshake->mediator_->allows_tcp() && io->reconnect())
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};
        tr_logAddTraceHand(handshake, "handshake failed, trying plaintext...");
        handshake->build_handshake_message(io, std::data(msg));
        handshake->have_sent_bittorrent_handshake_ = true;
        handshake->set_state(State::AwaitingHandshake);
        io->write_bytes(std::data(msg), std::size(msg), false);
        return;
    }

    tr_logAddTraceHand(handshake, fmt::format("handshake socket err: {:s} ({:d})", error.message, error.code));
    handshake->done(false);
}

bool tr_handshake::fire_done(bool is_connected)
{
    maybe_recycle_dh();

    if (!on_done_)
    {
        return false;
    }

    // handshake could get destroyed inside on_done,
    // so handle all our housekeeping *before* calling it

    auto cb = DoneFunc{};
    std::swap(cb, on_done_);

    auto peer_io = std::shared_ptr<tr_peerIo>{};
    std::swap(peer_io, peer_io_);

    bool const success = (cb)(Result{ std::move(peer_io), peer_id_, have_read_anything_from_peer_, is_connected });
    return success;
}

std::string_view tr_handshake::state_string(State state) noexcept
{
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

    return "unknown state";
}

uint32_t tr_handshake::crypto_provide() const noexcept
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

// ---

tr_handshake::tr_handshake(Mediator* mediator, std::shared_ptr<tr_peerIo> peer_io, tr_encryption_mode mode, DoneFunc on_done)
    : dh_{ tr_handshake::get_dh(mediator) }
    , on_done_{ std::move(on_done) }
    , peer_io_{ std::move(peer_io) }
    , timeout_timer_{ mediator->timer_maker().create([this]() { fire_done(false); }) }
    , mediator_{ mediator }
    , encryption_mode_{ mode }
{
    timeout_timer_->startSingleShot(HandshakeTimeoutSec);

    peer_io_->set_callbacks(&tr_handshake::can_read, nullptr, &tr_handshake::on_error, this);

    if (is_incoming())
    {
        set_state(State::AwaitingHandshake);
    }
    else if (encryption_mode_ != TR_CLEAR_PREFERRED)
    {
        send_ya(peer_io_.get());
    }
    else
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};
        build_handshake_message(peer_io_.get(), std::data(msg));

        have_sent_bittorrent_handshake_ = true;
        set_state(State::AwaitingHandshake);
        peer_io_->write_bytes(std::data(msg), std::size(msg), false);
    }
}
