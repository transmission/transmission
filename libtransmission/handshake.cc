// This file Copyright © 2017-2022 Mnemosyne LLC.
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

#include "clients.h"
#include "crypto-utils.h"
#include "handshake.h"
#include "log.h"
#include "peer-io.h"
#include "timer.h"
#include "tr-assert.h"
#include "tr-buffer.h"
#include "utils.h"

using namespace std::literals;

/* enable LibTransmission extension protocol */
#define ENABLE_LTEP
/* fast extensions */
#define ENABLE_FAST
/* DHT */
#define ENABLE_DHT

/***
****
***/

static auto constexpr HandshakeName = std::array<std::byte, 20>{
    std::byte{ 19 },  std::byte{ 'B' }, std::byte{ 'i' }, std::byte{ 't' }, std::byte{ 'T' },
    std::byte{ 'o' }, std::byte{ 'r' }, std::byte{ 'r' }, std::byte{ 'e' }, std::byte{ 'n' },
    std::byte{ 't' }, std::byte{ ' ' }, std::byte{ 'p' }, std::byte{ 'r' }, std::byte{ 'o' },
    std::byte{ 't' }, std::byte{ 'o' }, std::byte{ 'c' }, std::byte{ 'o' }, std::byte{ 'l' }
};

// bittorrent handshake constants
static auto constexpr HandshakeFlagsLen = int{ 8 };
static auto constexpr HandshakeSize = int{ 68 };
static auto constexpr IncomingHandshakeLen = int{ 48 };

// encryption constants
static auto constexpr PadaMaxlen = int{ 512 };
static auto constexpr PadbMaxlen = int{ 512 };
static auto constexpr PadcMaxlen = int{ 512 };
static auto constexpr CryptoProvidePlaintext = int{ 1 };
static auto constexpr CryptoProvideCrypto = int{ 2 };

// "VC is a verification constant that is used to verify whether the
// other side knows S and SKEY and thus defeats replay attacks of the
// SKEY hash. As of this version VC is a String of 8 bytes set to 0x00."
// https://wiki.vuze.com/w/Message_Stream_Encryption
using vc_t = std::array<std::byte, 8>;
static auto constexpr VC = vc_t{};

#ifdef ENABLE_LTEP
#define HANDSHAKE_HAS_LTEP(bits) (((bits)[5] & 0x10) != 0)
#define HANDSHAKE_SET_LTEP(bits) ((bits)[5] |= 0x10)
#else
#define HANDSHAKE_HAS_LTEP(bits) (false)
#define HANDSHAKE_SET_LTEP(bits) ((void)0)
#endif

#ifdef ENABLE_FAST
#define HANDSHAKE_HAS_FASTEXT(bits) (((bits)[7] & 0x04) != 0)
#define HANDSHAKE_SET_FASTEXT(bits) ((bits)[7] |= 0x04)
#else
#define HANDSHAKE_HAS_FASTEXT(bits) (false)
#define HANDSHAKE_SET_FASTEXT(bits) ((void)0)
#endif

#ifdef ENABLE_DHT
#define HANDSHAKE_HAS_DHT(bits) (((bits)[7] & 0x01) != 0)
#define HANDSHAKE_SET_DHT(bits) ((bits)[7] |= 0x01)
#else
#define HANDSHAKE_HAS_DHT(bits) (false)
#define HANDSHAKE_SET_DHT(bits) ((void)0)
#endif

/**
***
**/

#define tr_logAddTraceHand(handshake, msg) tr_logAddTrace(msg, (handshake)->display_name())

using DH = tr_message_stream_encryption::DH;

class tr_handshake_impl final : public tr_handshake
{
public:
    tr_handshake_impl(Mediator* mediator, DoneFunc done_func, std::shared_ptr<tr_peerIo> const& io, tr_encryption_mode mode_in)
        : tr_handshake{ mediator, io, std::move(done_func) }
        , dh{ mediator->private_key() }
        , encryption_mode{ mode_in }
        , timeout_timer_{ mediator->timer_maker().create([this]() { fire_done(false); }) }
    {
        timeout_timer_->startSingleShot(HandshakeTimeoutSec);
    }

    tr_handshake_impl(tr_handshake_impl&&) = delete;
    tr_handshake_impl(tr_handshake_impl const&) = delete;
    tr_handshake_impl& operator=(tr_handshake_impl&&) = delete;
    tr_handshake_impl& operator=(tr_handshake_impl const&) = delete;
    ~tr_handshake_impl() override = default;

    [[nodiscard]] auto is_incoming() const noexcept
    {
        return peer_io()->isIncoming();
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

    [[nodiscard]] constexpr auto state() const noexcept
    {
        return this->state_;
    }

    [[nodiscard]] constexpr auto is_state(State state) const noexcept
    {
        return this->state_ == state;
    }

    void set_state(tr_handshake::State state_in)
    {
        tr_logAddTraceHand(this, fmt::format("setting to state [{}]", state_string(state_in)));
        this->state_ = state_in;
    }

    [[nodiscard]] constexpr std::string_view state_string() const
    {
        return state_string(state_);
    }

    bool have_sent_bittorrent_handshake = false;
    DH dh = {};
    tr_encryption_mode encryption_mode;
    uint16_t pad_c_len = {};
    uint16_t pad_d_len = {};
    uint16_t ia_len = {};
    uint32_t crypto_select = {};
    uint32_t crypto_provide = {};

private:
    [[nodiscard]] static constexpr std::string_view state_string(State state)
    {
        using State = tr_handshake::State;

        switch (state)
        {
        case State::AwaitingHandshake:
            return "awaiting handshake"sv;
        case State::AwaitingPeerId:
            return "awaiting peer id"sv;
        case State::AwaitingYa:
            return "awaiting ya"sv;
        case State::AwaitingPadA:
            return "awaiting pad a"sv;
        case State::AwaitingCryptoProvide:
            return "awaiting crypto provide"sv;
        case State::AwaitingPadC:
            return "awaiting pad c"sv;
        case State::AwaitingIa:
            return "awaiting ia"sv;
        case State::AwaitingPayloadStream:
            return "awaiting payload stream"sv;

        // outgoing
        case State::AwaitingYb:
            return "awaiting yb"sv;
        case State::AwaitingVc:
            return "awaiting vc"sv;
        case State::AwaitingCryptoSelect:
            return "awaiting crypto select"sv;
        case State::AwaitingPadD:
            return "awaiting pad d"sv;
        }
    }

    // how long to wait before giving up on a handshake
    static auto constexpr HandshakeTimeoutSec = 30s;

    std::unique_ptr<libtransmission::Timer> const timeout_timer_;

    State state_ = State::AwaitingHandshake;
};

/**
***
**/

static void setReadState(tr_handshake_impl* handshake, tr_handshake::State state)
{
    handshake->set_state(state);
}

static bool buildHandshakeMessage(tr_handshake_impl const* const handshake, tr_peerIo* io, uint8_t* buf)
{
    auto const& info_hash = io->torrentHash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "buildHandshakeMessage requires an info_hash");

    auto const info = handshake->torrent_info(info_hash);
    if (!info)
    {
        return false;
    }

    uint8_t* walk = buf;

    walk = std::copy_n(reinterpret_cast<uint8_t const*>(std::data(HandshakeName)), std::size(HandshakeName), walk);

    std::fill_n(walk, HandshakeFlagsLen, 0);
    HANDSHAKE_SET_LTEP(walk);
    HANDSHAKE_SET_FASTEXT(walk);
    /* Note that this doesn't depend on whether the torrent is private.
     * We don't accept DHT peers for a private torrent,
     * but we participate in the DHT regardless. */
    if (handshake->allows_dht())
    {
        HANDSHAKE_SET_DHT(walk);
    }
    walk += HandshakeFlagsLen;

    walk = std::copy_n(reinterpret_cast<char const*>(std::data(info_hash)), std::size(info_hash), walk);
    [[maybe_unused]] auto const* const walk_end = std::copy(
        std::begin(info->client_peer_id),
        std::end(info->client_peer_id),
        walk);

    TR_ASSERT(walk_end - buf == HandshakeSize);
    return true;
}

enum class ParseResult
{
    Ok,
    EncryptionWrong,
    BadTorrent,
    PeerIsSelf,
};

static ParseResult parseHandshake(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    tr_logAddTraceHand(handshake, fmt::format("payload: need {}, got {}", HandshakeSize, peer_io->readBufferSize()));

    if (peer_io->readBufferSize() < HandshakeSize)
    {
        return ParseResult::EncryptionWrong;
    }

    /* confirm the protocol */
    auto name = decltype(HandshakeName){};
    peer_io->readBytes(std::data(name), std::size(name));
    if (name != HandshakeName)
    {
        return ParseResult::EncryptionWrong;
    }

    /* read the reserved bytes */
    auto reserved = std::array<uint8_t, HandshakeFlagsLen>{};
    peer_io->readBytes(std::data(reserved), std::size(reserved));

    // torrent hash
    auto info_hash = tr_sha1_digest_t{};
    peer_io->readBytes(std::data(info_hash), std::size(info_hash));
    if (info_hash == tr_sha1_digest_t{} || info_hash != peer_io->torrentHash())
    {
        tr_logAddTraceHand(handshake, "peer returned the wrong hash. wtf?");
        return ParseResult::BadTorrent;
    }

    // peer_id
    auto peer_id = tr_peer_id_t{};
    peer_io->readBytes(std::data(peer_id), std::size(peer_id));
    handshake->set_peer_id(peer_id);

    /* peer id */
    auto const peer_id_sv = std::string_view{ std::data(peer_id), std::size(peer_id) };
    tr_logAddTraceHand(handshake, fmt::format("peer-id is '{}'", peer_id_sv));

    if (auto const info = handshake->torrent_info(info_hash); info && info->client_peer_id == peer_id)
    {
        tr_logAddTraceHand(handshake, "streuth!  we've connected to ourselves.");
        return ParseResult::PeerIsSelf;
    }

    /**
    *** Extensions
    **/

    peer_io->enableDHT(HANDSHAKE_HAS_DHT(reserved));
    peer_io->enableLTEP(HANDSHAKE_HAS_LTEP(reserved));
    peer_io->enableFEXT(HANDSHAKE_HAS_FASTEXT(reserved));

    return ParseResult::Ok;
}

/***
****
****  OUTGOING CONNECTIONS
****
***/

template<size_t PadMax>
static void sendPublicKeyAndPad(tr_handshake_impl* handshake, tr_peerIo* io)
{
    auto const public_key = handshake->dh.publicKey();
    auto outbuf = std::array<std::byte, std::size(public_key) + PadMax>{};
    auto const data = std::data(outbuf);
    auto walk = data;
    walk = std::copy(std::begin(public_key), std::end(public_key), walk);
    walk += handshake->pad(walk, PadMax);
    io->writeBytes(data, walk - data, false);
}

// 1 A->B: our public key (Ya) and some padding (PadA)
static void sendYa(tr_handshake_impl* handshake, tr_peerIo* io)
{
    sendPublicKeyAndPad<PadaMaxlen>(handshake, io);
    setReadState(handshake, tr_handshake::State::AwaitingYb);
}

static constexpr uint32_t getCryptoSelect(tr_encryption_mode encryption_mode, uint32_t crypto_provide)
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

static ReadState readYb(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    if (peer_io->readBufferSize() < std::size(HandshakeName))
    {
        return READ_LATER;
    }

    bool const is_encrypted = !peer_io->readBufferStartsWith(HandshakeName);
    auto peer_public_key = DH::key_bigend_t{};
    if (is_encrypted && (peer_io->readBufferSize() < std::size(peer_public_key)))
    {
        return READ_LATER;
    }

    tr_logAddTraceHand(handshake, is_encrypted ? "got an encrypted handshake" : "got a plain handshake");

    if (!is_encrypted)
    {
        handshake->set_state(tr_handshake::State::AwaitingHandshake);
        return READ_NOW;
    }

    handshake->set_have_read_anything_from_peer(true);

    // get the peer's public key
    peer_io->readBytes(std::data(peer_public_key), std::size(peer_public_key));
    handshake->dh.setPeerPublicKey(peer_public_key);

    /* now send these: HASH('req1', S), HASH('req2', SKEY) xor HASH('req3', S),
     * ENCRYPT(VC, crypto_provide, len(PadC), PadC, len(IA)), ENCRYPT(IA) */
    auto outbuf = libtransmission::Buffer{};

    /* HASH('req1', S) */
    outbuf.add(tr_sha1::digest("req1"sv, handshake->dh.secret()));

    auto const& info_hash = peer_io->torrentHash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readYb requires an info_hash");

    /* HASH('req2', SKEY) xor HASH('req3', S) */
    {
        auto const req2 = tr_sha1::digest("req2"sv, info_hash);
        auto const req3 = tr_sha1::digest("req3"sv, handshake->dh.secret());
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
    peer_io->encryptInit(peer_io->isIncoming(), handshake->dh, info_hash);
    outbuf.add(VC);
    outbuf.addUint32(handshake->cryptoProvide());
    outbuf.addUint16(0);

    /* ENCRYPT len(IA)), ENCRYPT(IA) */
    if (auto msg = std::array<uint8_t, HandshakeSize>{}; buildHandshakeMessage(handshake, peer_io, std::data(msg)))
    {
        outbuf.addUint16(std::size(msg));
        outbuf.add(msg);
        handshake->have_sent_bittorrent_handshake = true;
    }
    else
    {
        return handshake->done(false);
    }

    /* send it */
    setReadState(handshake, tr_handshake::State::AwaitingVc);
    peer_io->write(outbuf, false);
    return READ_NOW;
}

// MSE spec: "Since the length of [PadB is] unknown,
// A will be able to resynchronize on ENCRYPT(VC)"
static ReadState readVC(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    auto const info_hash = peer_io->torrentHash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readVC requires an info_hash");

    // find the end of PadB by looking for `ENCRYPT(VC)`
    auto needle = VC;
    auto filter = tr_message_stream_encryption::Filter{};
    filter.encryptInit(true, handshake->dh, info_hash);
    filter.encrypt(std::size(needle), std::data(needle));

    for (size_t i = 0; i < PadbMaxlen; ++i)
    {
        if (peer_io->readBufferSize() < std::size(needle))
        {
            tr_logAddTraceHand(handshake, "not enough bytes... returning read_more");
            return READ_LATER;
        }

        if (peer_io->readBufferStartsWith(needle))
        {
            tr_logAddTraceHand(handshake, "got it!");
            // We already know it's a match; now we just need to
            // consume it from the read buffer.
            peer_io->decryptInit(peer_io->isIncoming(), handshake->dh, info_hash);
            peer_io->readBytes(std::data(needle), std::size(needle));
            handshake->set_state(tr_handshake::State::AwaitingCryptoSelect);
            return READ_NOW;
        }

        peer_io->readBufferDrain(1);
    }

    tr_logAddTraceHand(handshake, "couldn't find ENCRYPT(VC)");
    return handshake->done(false);
}

static ReadState readCryptoSelect(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    if (static size_t constexpr NeedLen = sizeof(uint32_t) + sizeof(uint16_t); peer_io->readBufferSize() < NeedLen)
    {
        return READ_LATER;
    }

    uint32_t crypto_select = 0;
    peer_io->readUint32(&crypto_select);
    handshake->crypto_select = crypto_select;
    tr_logAddTraceHand(handshake, fmt::format("crypto select is {}", crypto_select));

    if ((crypto_select & handshake->cryptoProvide()) == 0)
    {
        tr_logAddTraceHand(handshake, "peer selected an encryption option we didn't offer");
        return handshake->done(false);
    }

    uint16_t pad_d_len = 0;
    peer_io->readUint16(&pad_d_len);
    tr_logAddTraceHand(handshake, fmt::format("pad_d_len is {}", pad_d_len));

    if (pad_d_len > 512)
    {
        tr_logAddTraceHand(handshake, "encryption handshake: pad_d_len is too long");
        return handshake->done(false);
    }

    handshake->pad_d_len = pad_d_len;

    handshake->set_state(tr_handshake::State::AwaitingPadD);
    return READ_NOW;
}

static ReadState readPadD(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    size_t const needlen = handshake->pad_d_len;

    tr_logAddTraceHand(handshake, fmt::format("pad d: need {}, got {}", needlen, peer_io->readBufferSize()));

    if (peer_io->readBufferSize() < needlen)
    {
        return READ_LATER;
    }

    peer_io->readBufferDrain(needlen);

    handshake->set_state(tr_handshake::State::AwaitingHandshake);
    return READ_NOW;
}

/***
****
****  INCOMING CONNECTIONS
****
***/

static ReadState readHandshake(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    tr_logAddTraceHand(handshake, fmt::format("payload: need {}, got {}", IncomingHandshakeLen, peer_io->readBufferSize()));

    if (peer_io->readBufferSize() < IncomingHandshakeLen)
    {
        return READ_LATER;
    }

    handshake->set_have_read_anything_from_peer(true);

    if (peer_io->readBufferStartsWith(HandshakeName)) // unencrypted
    {
        if (handshake->encryption_mode == TR_ENCRYPTION_REQUIRED)
        {
            tr_logAddTraceHand(handshake, "peer is unencrypted, and we're disallowing that");
            return handshake->done(false);
        }
    }
    else // either encrypted or corrupt
    {
        if (handshake->is_incoming())
        {
            tr_logAddTraceHand(handshake, "I think peer is sending us an encrypted handshake...");
            handshake->set_state(tr_handshake::State::AwaitingYa);
            return READ_NOW;
        }
    }

    auto name = decltype(HandshakeName){};
    peer_io->readBytes(std::data(name), std::size(name));
    if (name != HandshakeName)
    {
        return handshake->done(false);
    }

    /* reserved bytes */
    auto reserved = std::array<uint8_t, HandshakeFlagsLen>{};
    peer_io->readBytes(std::data(reserved), std::size(reserved));

    /**
    *** Extensions
    **/

    peer_io->enableDHT(HANDSHAKE_HAS_DHT(reserved));
    peer_io->enableLTEP(HANDSHAKE_HAS_LTEP(reserved));
    peer_io->enableFEXT(HANDSHAKE_HAS_FASTEXT(reserved));

    /* torrent hash */
    auto hash = tr_sha1_digest_t{};
    peer_io->readBytes(std::data(hash), std::size(hash));

    if (handshake->is_incoming())
    {
        if (!handshake->torrent_info(hash))
        {
            tr_logAddTraceHand(handshake, "peer is trying to connect to us for a torrent we don't have.");
            return handshake->done(false);
        }

        peer_io->setTorrentHash(hash);
    }
    else // outgoing
    {
        if (peer_io->torrentHash() != hash)
        {
            tr_logAddTraceHand(handshake, "peer returned the wrong hash. wtf?");
            return handshake->done(false);
        }
    }

    /**
    ***  If it's an incoming message, we need to send a response handshake
    **/

    if (!handshake->have_sent_bittorrent_handshake)
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};

        if (!buildHandshakeMessage(handshake, peer_io, std::data(msg)))
        {
            return handshake->done(false);
        }

        peer_io->writeBytes(std::data(msg), std::size(msg), false);
        handshake->have_sent_bittorrent_handshake = true;
    }

    setReadState(handshake, tr_handshake::State::AwaitingPeerId);
    return READ_NOW;
}

static ReadState readPeerId(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    // read the peer_id
    auto peer_id = tr_peer_id_t{};
    if (peer_io->readBufferSize() < std::size(peer_id))
    {
        return READ_LATER;
    }
    peer_io->readBytes(std::data(peer_id), std::size(peer_id));
    handshake->set_peer_id(peer_id);

    auto client = std::array<char, 128>{};
    tr_clientForId(std::data(client), std::size(client), peer_id);
    tr_logAddTraceHand(
        handshake,
        fmt::format("peer-id is '{}' ... isIncoming is {}", std::data(client), handshake->is_incoming()));

    // if we've somehow connected to ourselves, don't keep the connection
    auto const info_hash = peer_io->torrentHash();
    auto const info = handshake->torrent_info(info_hash);
    auto const connected_to_self = info && info->client_peer_id == peer_id;

    return handshake->done(!connected_to_self);
}

static ReadState readYa(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    auto peer_public_key = DH::key_bigend_t{};
    tr_logAddTraceHand(
        handshake,
        fmt::format("in readYa... need {}, have {}", std::size(peer_public_key), peer_io->readBufferSize()));

    if (peer_io->readBufferSize() < std::size(peer_public_key))
    {
        return READ_LATER;
    }

    /* read the incoming peer's public key */
    peer_io->readBytes(std::data(peer_public_key), std::size(peer_public_key));
    handshake->dh.setPeerPublicKey(peer_public_key);

    // send our public key to the peer
    tr_logAddTraceHand(handshake, "sending B->A: Diffie Hellman Yb, PadB");
    sendPublicKeyAndPad<PadbMaxlen>(handshake, peer_io);

    setReadState(handshake, tr_handshake::State::AwaitingPadA);
    return READ_NOW;
}

static ReadState readPadA(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    // find the end of PadA by looking for HASH('req1', S)
    auto const needle = tr_sha1::digest("req1"sv, handshake->dh.secret());

    for (size_t i = 0; i < PadaMaxlen; ++i)
    {
        if (peer_io->readBufferSize() < std::size(needle))
        {
            tr_logAddTraceHand(handshake, "not enough bytes... returning read_more");
            return READ_LATER;
        }

        if (peer_io->readBufferStartsWith(needle))
        {
            tr_logAddTraceHand(handshake, "found it... looking setting to awaiting_crypto_provide");
            peer_io->readBufferDrain(std::size(needle));
            handshake->set_state(tr_handshake::State::AwaitingCryptoProvide);
            return READ_NOW;
        }

        peer_io->readBufferDrain(1U);
    }

    tr_logAddTraceHand(handshake, "couldn't find HASH('req', S)");
    return handshake->done(false);
}

static ReadState readCryptoProvide(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    /* HASH('req2', SKEY) xor HASH('req3', S), ENCRYPT(VC, crypto_provide, len(PadC)) */

    uint16_t padc_len = 0;
    uint32_t crypto_provide = 0;
    auto obfuscated_hash = tr_sha1_digest_t{};
    size_t const needlen = sizeof(obfuscated_hash) + /* HASH('req2', SKEY) xor HASH('req3', S) */
        std::size(VC) + sizeof(crypto_provide) + sizeof(padc_len);

    if (peer_io->readBufferSize() < needlen)
    {
        return READ_LATER;
    }

    /* This next piece is HASH('req2', SKEY) xor HASH('req3', S) ...
     * we can get the first half of that (the obfuscatedTorrentHash)
     * by building the latter and xor'ing it with what the peer sent us */
    tr_logAddTraceHand(handshake, "reading obfuscated torrent hash...");
    auto req2 = tr_sha1_digest_t{};
    peer_io->readBytes(std::data(req2), std::size(req2));

    auto const req3 = tr_sha1::digest("req3"sv, handshake->dh.secret());
    for (size_t i = 0; i < std::size(obfuscated_hash); ++i)
    {
        obfuscated_hash[i] = req2[i] ^ req3[i];
    }

    if (auto const info = handshake->torrent_info_from_obfuscated(obfuscated_hash); info)
    {
        bool const client_is_seed = info->is_done;
        bool const peer_is_seed = handshake->is_peer_known_seed(info->id, peer_io->address());
        tr_logAddTraceHand(handshake, fmt::format("got INCOMING connection's encrypted handshake for torrent [{}]", info->id));
        peer_io->setTorrentHash(info->info_hash);

        if (client_is_seed && peer_is_seed)
        {
            tr_logAddTraceHand(handshake, "another seed tried to reconnect to us!");
            return handshake->done(false);
        }
    }
    else
    {
        tr_logAddTraceHand(handshake, "can't find that torrent...");
        return handshake->done(false);
    }

    /* next part: ENCRYPT(VC, crypto_provide, len(PadC), */

    auto const& info_hash = peer_io->torrentHash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readCryptoProvide requires an info_hash");
    peer_io->decryptInit(peer_io->isIncoming(), handshake->dh, info_hash);

    auto vc_in = vc_t{};
    peer_io->readBytes(std::data(vc_in), std::size(vc_in));

    peer_io->readUint32(&crypto_provide);
    handshake->crypto_provide = crypto_provide;
    tr_logAddTraceHand(handshake, fmt::format("crypto_provide is {}", crypto_provide));

    peer_io->readUint16(&padc_len);
    tr_logAddTraceHand(handshake, fmt::format("padc is {}", padc_len));
    if (padc_len > PadcMaxlen)
    {
        tr_logAddTraceHand(handshake, "peer's PadC is too big");
        return handshake->done(false);
    }

    handshake->pad_c_len = padc_len;
    handshake->set_state(tr_handshake::State::AwaitingPadC);
    return READ_NOW;
}

static ReadState readPadC(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    if (auto const needlen = handshake->pad_c_len + sizeof(uint16_t); peer_io->readBufferSize() < needlen)
    {
        return READ_LATER;
    }

    // read the throwaway padc
    auto pad_c = std::array<char, PadcMaxlen>{};
    peer_io->readBytes(std::data(pad_c), handshake->pad_c_len);

    /* read ia_len */
    uint16_t ia_len = 0;
    peer_io->readUint16(&ia_len);
    tr_logAddTraceHand(handshake, fmt::format("ia_len is {}", ia_len));
    handshake->ia_len = ia_len;
    handshake->set_state(tr_handshake::State::AwaitingIa);
    return READ_NOW;
}

static ReadState readIA(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    size_t const needlen = handshake->ia_len;

    tr_logAddTraceHand(handshake, fmt::format("reading IA... have {}, need {}", peer_io->readBufferSize(), needlen));

    if (peer_io->readBufferSize() < needlen)
    {
        return READ_LATER;
    }

    /**
    ***  B->A: ENCRYPT(VC, crypto_select, len(padD), padD), ENCRYPT2(Payload Stream)
    **/

    auto const& info_hash = peer_io->torrentHash();
    TR_ASSERT_MSG(info_hash != tr_sha1_digest_t{}, "readIA requires an info_hash");
    peer_io->encryptInit(peer_io->isIncoming(), handshake->dh, info_hash);
    auto outbuf = libtransmission::Buffer{};

    // send VC
    tr_logAddTraceHand(handshake, "sending vc");
    outbuf.add(VC);

    /* send crypto_select */
    uint32_t const crypto_select = getCryptoSelect(handshake->encryption_mode, handshake->crypto_provide);

    if (crypto_select != 0)
    {
        tr_logAddTraceHand(handshake, fmt::format("selecting crypto mode '{}'", crypto_select));
        outbuf.addUint32(crypto_select);
    }
    else
    {
        tr_logAddTraceHand(handshake, "peer didn't offer an encryption mode we like.");
        return handshake->done(false);
    }

    tr_logAddTraceHand(handshake, "sending pad d");

    /* ENCRYPT(VC, crypto_provide, len(PadD), PadD
     * PadD is reserved for future extensions to the handshake...
     * standard practice at this time is for it to be zero-length */
    outbuf.addUint16(0);

    /* maybe de-encrypt our connection */
    if (crypto_select == CryptoProvidePlaintext)
    {
        peer_io->write(outbuf, false);
        TR_ASSERT(std::empty(outbuf));
    }

    tr_logAddTraceHand(handshake, "sending handshake");

    /* send our handshake */
    if (auto msg = std::array<uint8_t, HandshakeSize>{}; buildHandshakeMessage(handshake, peer_io, std::data(msg)))
    {
        outbuf.add(msg);
        handshake->have_sent_bittorrent_handshake = true;
    }
    else
    {
        return handshake->done(false);
    }

    /* send it out */
    peer_io->write(outbuf, false);

    /* now await the handshake */
    handshake->set_state(tr_handshake::State::AwaitingPayloadStream);
    return READ_NOW;
}

static ReadState readPayloadStream(tr_handshake_impl* handshake, tr_peerIo* peer_io)
{
    size_t const needlen = HandshakeSize;

    tr_logAddTraceHand(
        handshake,
        fmt::format("reading payload stream... have {}, need {}", peer_io->readBufferSize(), needlen));

    if (peer_io->readBufferSize() < needlen)
    {
        return READ_LATER;
    }

    /* parse the handshake ... */
    auto const i = parseHandshake(handshake, peer_io);
    tr_logAddTraceHand(handshake, fmt::format("parseHandshake returned {}", static_cast<int>(i)));

    if (i != ParseResult::Ok)
    {
        return handshake->done(false);
    }

    /* we've completed the BT handshake... pass the work on to peer-msgs */
    return handshake->done(true);
}

/***
****
****
****
***/

static ReadState canRead(tr_peerIo* peer_io, void* vhandshake, size_t* piece)
{
    TR_ASSERT(tr_isPeerIo(peer_io));

    auto* handshake = static_cast<tr_handshake_impl*>(vhandshake);

    bool ready_for_more = true;

    /* no piece data in handshake */
    *piece = 0;

    tr_logAddTraceHand(handshake, fmt::format("handling canRead; state is [{}]", handshake->state_string()));

    ReadState ret = READ_NOW;
    while (ready_for_more)
    {
        switch (handshake->state())
        {
        case tr_handshake::State::AwaitingHandshake:
            ret = readHandshake(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingPeerId:
            ret = readPeerId(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingYa:
            ret = readYa(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingPadA:
            ret = readPadA(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingCryptoProvide:
            ret = readCryptoProvide(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingPadC:
            ret = readPadC(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingIa:
            ret = readIA(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingPayloadStream:
            ret = readPayloadStream(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingYb:
            ret = readYb(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingVc:
            ret = readVC(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingCryptoSelect:
            ret = readCryptoSelect(handshake, peer_io);
            break;

        case tr_handshake::State::AwaitingPadD:
            ret = readPadD(handshake, peer_io);
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
        else if (handshake->is_state(tr_handshake::State::AwaitingPadC))
        {
            ready_for_more = peer_io->readBufferSize() >= handshake->pad_c_len;
        }
        else if (handshake->is_state(tr_handshake::State::AwaitingPadD))
        {
            ready_for_more = peer_io->readBufferSize() >= handshake->pad_d_len;
        }
        else if (handshake->is_state(tr_handshake::State::AwaitingIa))
        {
            ready_for_more = peer_io->readBufferSize() >= handshake->ia_len;
        }
    }

    return ret;
}

static void gotError(tr_peerIo* io, short what, void* vhandshake)
{
    int const errcode = errno;
    auto* handshake = static_cast<tr_handshake_impl*>(vhandshake);

    if (io->socket.is_utp() && !io->isIncoming() && handshake->is_state(tr_handshake::State::AwaitingYb))
    {
        // the peer probably doesn't speak µTP.

        auto const info_hash = io->torrentHash();
        auto const info = handshake->torrent_info(info_hash);

        /* Don't mark a peer as non-µTP unless it's really a connect failure. */
        if ((errcode == ETIMEDOUT || errcode == ECONNREFUSED) && info)
        {
            handshake->set_utp_failed(info_hash, io->address());
        }

        if (handshake->allows_tcp() && io->reconnect() == 0)
        {
            auto msg = std::array<uint8_t, HandshakeSize>{};
            buildHandshakeMessage(handshake, io, std::data(msg));
            handshake->have_sent_bittorrent_handshake = true;
            setReadState(handshake, tr_handshake::State::AwaitingHandshake);
            io->writeBytes(std::data(msg), std::size(msg), false);
        }
    }

    /* if the error happened while we were sending a public key, we might
     * have encountered a peer that doesn't do encryption... reconnect and
     * try a plaintext handshake */
    if ((handshake->is_state(tr_handshake::State::AwaitingYb) || handshake->is_state(tr_handshake::State::AwaitingVc)) &&
        handshake->encryption_mode != TR_ENCRYPTION_REQUIRED && handshake->allows_tcp() && io->reconnect() == 0)
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};
        tr_logAddTraceHand(handshake, "handshake failed, trying plaintext...");
        buildHandshakeMessage(handshake, io, std::data(msg));
        handshake->have_sent_bittorrent_handshake = true;
        setReadState(handshake, tr_handshake::State::AwaitingHandshake);
        io->writeBytes(std::data(msg), std::size(msg), false);
    }
    else
    {
        tr_logAddTraceHand(
            handshake,
            fmt::format("libevent got an error: what={:d}, errno={:d} ({:s})", what, errcode, tr_strerror(errcode)));
        handshake->done(false);
    }
}

/**
***
**/

std::unique_ptr<tr_handshake> tr_handshake::create(
    Mediator* mediator,
    std::shared_ptr<tr_peerIo> const& peer_io,
    tr_encryption_mode encryption_mode,
    DoneFunc done_func)
{
    auto handshake = std::make_unique<tr_handshake_impl>(mediator, std::move(done_func), peer_io, encryption_mode);

    peer_io->setCallbacks(canRead, nullptr, gotError, handshake.get());

    if (handshake->is_incoming())
    {
        setReadState(handshake.get(), tr_handshake::State::AwaitingHandshake);
    }
    else if (encryption_mode != TR_CLEAR_PREFERRED)
    {
        sendYa(handshake.get(), peer_io.get());
    }
    else
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};
        buildHandshakeMessage(handshake.get(), peer_io.get(), std::data(msg));

        handshake->have_sent_bittorrent_handshake = true;
        setReadState(handshake.get(), tr_handshake::State::AwaitingHandshake);
        peer_io->writeBytes(std::data(msg), std::size(msg), false);
    }

    return handshake;
}
