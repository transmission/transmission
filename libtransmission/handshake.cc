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

// how long to wait before giving up on a handshake
static auto constexpr HandshakeTimeoutSec = 30s;

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

using DH = tr_message_stream_encryption::DH;

enum handshake_state_t
{
    /* incoming */
    AWAITING_HANDSHAKE,
    AWAITING_PEER_ID,
    AWAITING_YA,
    AWAITING_PAD_A,
    AWAITING_CRYPTO_PROVIDE,
    AWAITING_PAD_C,
    AWAITING_IA,
    AWAITING_PAYLOAD_STREAM,
    /* outgoing */
    AWAITING_YB,
    AWAITING_VC,
    AWAITING_CRYPTO_SELECT,
    AWAITING_PAD_D,
    /* */
    N_STATES
};

struct tr_handshake
{
    tr_handshake(
        std::unique_ptr<tr_handshake_mediator> mediator_in,
        std::shared_ptr<tr_peerIo> io_in,
        tr_encryption_mode encryption_mode_in)
        : mediator{ std::move(mediator_in) }
        , io{ std::move(io_in) }
        , dh{ mediator->privateKey() }
        , encryption_mode{ encryption_mode_in }
    {
    }

    tr_handshake(tr_handshake&&) = delete;
    tr_handshake(tr_handshake const&) = delete;
    tr_handshake& operator=(tr_handshake&&) = delete;
    tr_handshake& operator=(tr_handshake const&) = delete;
    ~tr_handshake() = default;

    [[nodiscard]] auto isIncoming() const noexcept
    {
        return io->isIncoming();
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

    std::unique_ptr<tr_handshake_mediator> const mediator;

    bool haveReadAnythingFromPeer = false;
    bool haveSentBitTorrentHandshake = false;
    std::shared_ptr<tr_peerIo> const io;
    DH dh = {};
    handshake_state_t state = AWAITING_HANDSHAKE;
    tr_encryption_mode encryption_mode;
    uint16_t pad_c_len = {};
    uint16_t pad_d_len = {};
    uint16_t ia_len = {};
    uint32_t crypto_select = {};
    uint32_t crypto_provide = {};
    std::unique_ptr<libtransmission::Timer> timeout_timer;

    std::optional<tr_peer_id_t> peer_id;

    tr_handshake_done_func done_func = nullptr;
    void* done_func_user_data = nullptr;
};

/**
***
**/

#define tr_logAddTraceHand(handshake, msg) tr_logAddTrace(msg, (handshake)->io->addrStr())

static constexpr std::string_view getStateName(handshake_state_t const state)
{
    auto state_strings = std::array<std::string_view, N_STATES>{
        "awaiting handshake"sv, /* AWAITING_HANDSHAKE */
        "awaiting peer id"sv, /* AWAITING_PEER_ID */
        "awaiting ya"sv, /* AWAITING_YA */
        "awaiting pad a"sv, /* AWAITING_PAD_A */
        "awaiting crypto_provide"sv, /* AWAITING_CRYPTO_PROVIDE */
        "awaiting pad c"sv, /* AWAITING_PAD_C */
        "awaiting ia"sv, /* AWAITING_IA */
        "awaiting payload stream"sv, /* AWAITING_PAYLOAD_STREAM */
        "awaiting yb"sv, /* AWAITING_YB */
        "awaiting vc"sv, /* AWAITING_VC */
        "awaiting crypto select"sv, /* AWAITING_CRYPTO_SELECT */
        "awaiting pad d"sv /* AWAITING_PAD_D */
    };

    return state < N_STATES ? state_strings[state] : "unknown state"sv;
}

static void setState(tr_handshake* handshake, handshake_state_t state)
{
    tr_logAddTraceHand(handshake, fmt::format("setting to state [{}]", getStateName(state)));
    handshake->state = state;
}

static void setReadState(tr_handshake* handshake, handshake_state_t state)
{
    setState(handshake, state);
}

static bool buildHandshakeMessage(tr_handshake const* const handshake, uint8_t* buf)
{
    auto const info_hash = handshake->io->torrentHash();
    auto const info = info_hash ? handshake->mediator->torrentInfo(*info_hash) : std::nullopt;
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
    if (handshake->mediator->allowsDHT())
    {
        HANDSHAKE_SET_DHT(walk);
    }
    walk += HandshakeFlagsLen;

    walk = std::copy_n(reinterpret_cast<char const*>(std::data(*info_hash)), std::size(*info_hash), walk);
    walk = std::copy(std::begin(info->client_peer_id), std::end(info->client_peer_id), walk);

    TR_ASSERT(walk - buf == HandshakeSize);
    return true;
}

static ReadState tr_handshakeDone(tr_handshake* handshake, bool is_connected);

enum handshake_parse_err_t
{
    HANDSHAKE_OK,
    HANDSHAKE_ENCRYPTION_WRONG,
    HANDSHAKE_BAD_TORRENT,
    HANDSHAKE_PEER_IS_SELF,
};

static handshake_parse_err_t parseHandshake(tr_handshake* handshake, tr_peerIo* peer_io)
{
    tr_logAddTraceHand(handshake, fmt::format("payload: need {}, got {}", HandshakeSize, peer_io->readBufferSize()));

    if (peer_io->readBufferSize() < HandshakeSize)
    {
        return HANDSHAKE_ENCRYPTION_WRONG;
    }

    /* confirm the protocol */
    auto name = decltype(HandshakeName){};
    peer_io->readBytes(std::data(name), std::size(name));
    if (name != HandshakeName)
    {
        return HANDSHAKE_ENCRYPTION_WRONG;
    }

    /* read the reserved bytes */
    auto reserved = std::array<uint8_t, HandshakeFlagsLen>{};
    peer_io->readBytes(std::data(reserved), std::size(reserved));

    /* torrent hash */
    auto hash = tr_sha1_digest_t{};
    peer_io->readBytes(std::data(hash), std::size(hash));
    if (auto const torrent_hash = peer_io->torrentHash(); !torrent_hash || *torrent_hash != hash)
    {
        tr_logAddTraceHand(handshake, "peer returned the wrong hash. wtf?");
        return HANDSHAKE_BAD_TORRENT;
    }

    // peer_id
    auto peer_id = tr_peer_id_t{};
    peer_io->readBytes(std::data(peer_id), std::size(peer_id));
    handshake->peer_id = peer_id;

    /* peer id */
    auto const peer_id_sv = std::string_view{ std::data(peer_id), std::size(peer_id) };
    tr_logAddTraceHand(handshake, fmt::format("peer-id is '{}'", peer_id_sv));

    if (auto const info = handshake->mediator->torrentInfo(hash); info && info->client_peer_id == peer_id)
    {
        tr_logAddTraceHand(handshake, "streuth!  we've connected to ourselves.");
        return HANDSHAKE_PEER_IS_SELF;
    }

    /**
    *** Extensions
    **/

    peer_io->enableDHT(HANDSHAKE_HAS_DHT(reserved));
    peer_io->enableLTEP(HANDSHAKE_HAS_LTEP(reserved));
    peer_io->enableFEXT(HANDSHAKE_HAS_FASTEXT(reserved));

    return HANDSHAKE_OK;
}

/***
****
****  OUTGOING CONNECTIONS
****
***/

template<size_t PadMax>
static void sendPublicKeyAndPad(tr_handshake* handshake)
{
    auto const public_key = handshake->dh.publicKey();
    auto outbuf = std::array<std::byte, std::size(public_key) + PadMax>{};
    auto const data = std::data(outbuf);
    auto walk = data;
    walk = std::copy(std::begin(public_key), std::end(public_key), walk);
    walk += handshake->mediator->pad(walk, PadMax);
    handshake->io->writeBytes(data, walk - data, false);
}

// 1 A->B: our public key (Ya) and some padding (PadA)
static void sendYa(tr_handshake* handshake)
{
    sendPublicKeyAndPad<PadaMaxlen>(handshake);
    setReadState(handshake, AWAITING_YB);
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

static ReadState readYb(tr_handshake* handshake, tr_peerIo* peer_io)
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
        setState(handshake, AWAITING_HANDSHAKE);
        return READ_NOW;
    }

    handshake->haveReadAnythingFromPeer = true;

    // get the peer's public key
    peer_io->readBytes(std::data(peer_public_key), std::size(peer_public_key));
    handshake->dh.setPeerPublicKey(peer_public_key);

    /* now send these: HASH('req1', S), HASH('req2', SKEY) xor HASH('req3', S),
     * ENCRYPT(VC, crypto_provide, len(PadC), PadC, len(IA)), ENCRYPT(IA) */
    auto outbuf = libtransmission::Buffer{};

    /* HASH('req1', S) */
    outbuf.add(tr_sha1::digest("req1"sv, handshake->dh.secret()));

    auto const info_hash = peer_io->torrentHash();
    if (!info_hash)
    {
        tr_logAddTraceHand(handshake, "error while computing req2/req3 hash after Yb");
        return tr_handshakeDone(handshake, false);
    }

    /* HASH('req2', SKEY) xor HASH('req3', S) */
    {
        auto const req2 = tr_sha1::digest("req2"sv, *info_hash);
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
    peer_io->encryptInit(peer_io->isIncoming(), handshake->dh, *info_hash);
    outbuf.add(VC);
    outbuf.addUint32(handshake->cryptoProvide());
    outbuf.addUint16(0);

    /* ENCRYPT len(IA)), ENCRYPT(IA) */
    if (auto msg = std::array<uint8_t, HandshakeSize>{}; buildHandshakeMessage(handshake, std::data(msg)))
    {
        outbuf.addUint16(std::size(msg));
        outbuf.add(msg);
        handshake->haveSentBitTorrentHandshake = true;
    }
    else
    {
        return tr_handshakeDone(handshake, false);
    }

    /* send it */
    setReadState(handshake, AWAITING_VC);
    peer_io->write(outbuf, false);
    return READ_NOW;
}

// MSE spec: "Since the length of [PadB is] unknown,
// A will be able to resynchronize on ENCRYPT(VC)"
static ReadState readVC(tr_handshake* handshake, tr_peerIo* peer_io)
{
    // find the end of PadB by looking for `ENCRYPT(VC)`
    auto needle = VC;
    auto filter = tr_message_stream_encryption::Filter{};
    filter.encryptInit(true, handshake->dh, *peer_io->torrentHash());
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
            peer_io->decryptInit(peer_io->isIncoming(), handshake->dh, *peer_io->torrentHash());
            peer_io->readBytes(std::data(needle), std::size(needle));
            setState(handshake, AWAITING_CRYPTO_SELECT);
            return READ_NOW;
        }

        peer_io->readBufferDrain(1);
    }

    tr_logAddTraceHand(handshake, "couldn't find ENCRYPT(VC)");
    return tr_handshakeDone(handshake, false);
}

static ReadState readCryptoSelect(tr_handshake* handshake, tr_peerIo* peer_io)
{
    static size_t const needlen = sizeof(uint32_t) + sizeof(uint16_t);

    if (peer_io->readBufferSize() < needlen)
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
        return tr_handshakeDone(handshake, false);
    }

    uint16_t pad_d_len = 0;
    peer_io->readUint16(&pad_d_len);
    tr_logAddTraceHand(handshake, fmt::format("pad_d_len is {}", pad_d_len));

    if (pad_d_len > 512)
    {
        tr_logAddTraceHand(handshake, "encryption handshake: pad_d_len is too long");
        return tr_handshakeDone(handshake, false);
    }

    handshake->pad_d_len = pad_d_len;

    setState(handshake, AWAITING_PAD_D);
    return READ_NOW;
}

static ReadState readPadD(tr_handshake* handshake, tr_peerIo* peer_io)
{
    size_t const needlen = handshake->pad_d_len;

    tr_logAddTraceHand(handshake, fmt::format("pad d: need {}, got {}", needlen, peer_io->readBufferSize()));

    if (peer_io->readBufferSize() < needlen)
    {
        return READ_LATER;
    }

    peer_io->readBufferDrain(needlen);

    setState(handshake, AWAITING_HANDSHAKE);
    return READ_NOW;
}

/***
****
****  INCOMING CONNECTIONS
****
***/

static ReadState readHandshake(tr_handshake* handshake, tr_peerIo* peer_io)
{
    tr_logAddTraceHand(handshake, fmt::format("payload: need {}, got {}", IncomingHandshakeLen, peer_io->readBufferSize()));

    if (peer_io->readBufferSize() < IncomingHandshakeLen)
    {
        return READ_LATER;
    }

    handshake->haveReadAnythingFromPeer = true;

    if (peer_io->readBufferStartsWith(HandshakeName)) // unencrypted
    {
        if (handshake->encryption_mode == TR_ENCRYPTION_REQUIRED)
        {
            tr_logAddTraceHand(handshake, "peer is unencrypted, and we're disallowing that");
            return tr_handshakeDone(handshake, false);
        }
    }
    else // either encrypted or corrupt
    {
        if (handshake->isIncoming())
        {
            tr_logAddTraceHand(handshake, "I think peer is sending us an encrypted handshake...");
            setState(handshake, AWAITING_YA);
            return READ_NOW;
        }
    }

    auto name = decltype(HandshakeName){};
    peer_io->readBytes(std::data(name), std::size(name));
    if (name != HandshakeName)
    {
        return tr_handshakeDone(handshake, false);
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

    if (handshake->isIncoming())
    {
        if (!handshake->mediator->torrentInfo(hash))
        {
            tr_logAddTraceHand(handshake, "peer is trying to connect to us for a torrent we don't have.");
            return tr_handshakeDone(handshake, false);
        }

        peer_io->setTorrentHash(hash);
    }
    else /* outgoing */
    {
        auto const torrent_hash = peer_io->torrentHash();

        if (!torrent_hash || *torrent_hash != hash)
        {
            tr_logAddTraceHand(handshake, "peer returned the wrong hash. wtf?");
            return tr_handshakeDone(handshake, false);
        }
    }

    /**
    ***  If it's an incoming message, we need to send a response handshake
    **/

    if (!handshake->haveSentBitTorrentHandshake)
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};

        if (!buildHandshakeMessage(handshake, std::data(msg)))
        {
            return tr_handshakeDone(handshake, false);
        }

        peer_io->writeBytes(std::data(msg), std::size(msg), false);
        handshake->haveSentBitTorrentHandshake = true;
    }

    setReadState(handshake, AWAITING_PEER_ID);
    return READ_NOW;
}

static ReadState readPeerId(tr_handshake* handshake, tr_peerIo* peer_io)
{
    // read the peer_id
    auto peer_id = tr_peer_id_t{};
    if (peer_io->readBufferSize() < std::size(peer_id))
    {
        return READ_LATER;
    }
    peer_io->readBytes(std::data(peer_id), std::size(peer_id));
    handshake->peer_id = peer_id;

    auto client = std::array<char, 128>{};
    tr_clientForId(std::data(client), std::size(client), peer_id);
    tr_logAddTraceHand(
        handshake,
        fmt::format("peer-id is '{}' ... isIncoming is {}", std::data(client), handshake->isIncoming()));

    // if we've somehow connected to ourselves, don't keep the connection
    auto const hash = peer_io->torrentHash();
    auto const info = hash ? handshake->mediator->torrentInfo(*hash) : std::nullopt;
    auto const connected_to_self = info && info->client_peer_id == peer_id;

    return tr_handshakeDone(handshake, !connected_to_self);
}

static ReadState readYa(tr_handshake* handshake, tr_peerIo* peer_io)
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
    sendPublicKeyAndPad<PadbMaxlen>(handshake);

    setReadState(handshake, AWAITING_PAD_A);
    return READ_NOW;
}

static ReadState readPadA(tr_handshake* handshake, tr_peerIo* peer_io)
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
            setState(handshake, AWAITING_CRYPTO_PROVIDE);
            return READ_NOW;
        }

        peer_io->readBufferDrain(1U);
    }

    tr_logAddTraceHand(handshake, "couldn't find HASH('req', S)");
    return tr_handshakeDone(handshake, false);
}

static ReadState readCryptoProvide(tr_handshake* handshake, tr_peerIo* peer_io)
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

    if (auto const info = handshake->mediator->torrentInfoFromObfuscated(obfuscated_hash); info)
    {
        bool const client_is_seed = info->is_done;
        bool const peer_is_seed = handshake->mediator->isPeerKnownSeed(info->id, peer_io->address());
        tr_logAddTraceHand(handshake, fmt::format("got INCOMING connection's encrypted handshake for torrent [{}]", info->id));
        peer_io->setTorrentHash(info->info_hash);

        if (client_is_seed && peer_is_seed)
        {
            tr_logAddTraceHand(handshake, "another seed tried to reconnect to us!");
            return tr_handshakeDone(handshake, false);
        }
    }
    else
    {
        tr_logAddTraceHand(handshake, "can't find that torrent...");
        return tr_handshakeDone(handshake, false);
    }

    /* next part: ENCRYPT(VC, crypto_provide, len(PadC), */

    peer_io->decryptInit(peer_io->isIncoming(), handshake->dh, *peer_io->torrentHash());

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
        return tr_handshakeDone(handshake, false);
    }

    handshake->pad_c_len = padc_len;
    setState(handshake, AWAITING_PAD_C);
    return READ_NOW;
}

static ReadState readPadC(tr_handshake* handshake, tr_peerIo* peer_io)
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
    setState(handshake, AWAITING_IA);
    return READ_NOW;
}

static ReadState readIA(tr_handshake* handshake, tr_peerIo* peer_io)
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

    peer_io->encryptInit(peer_io->isIncoming(), handshake->dh, *peer_io->torrentHash());
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
        return tr_handshakeDone(handshake, false);
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
    if (auto msg = std::array<uint8_t, HandshakeSize>{}; buildHandshakeMessage(handshake, std::data(msg)))
    {
        outbuf.add(msg);
        handshake->haveSentBitTorrentHandshake = true;
    }
    else
    {
        return tr_handshakeDone(handshake, false);
    }

    /* send it out */
    peer_io->write(outbuf, false);

    /* now await the handshake */
    setState(handshake, AWAITING_PAYLOAD_STREAM);
    return READ_NOW;
}

static ReadState readPayloadStream(tr_handshake* handshake, tr_peerIo* peer_io)
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
    handshake_parse_err_t const i = parseHandshake(handshake, peer_io);
    tr_logAddTraceHand(handshake, fmt::format("parseHandshake returned {}", i));

    if (i != HANDSHAKE_OK)
    {
        return tr_handshakeDone(handshake, false);
    }

    /* we've completed the BT handshake... pass the work on to peer-msgs */
    return tr_handshakeDone(handshake, true);
}

/***
****
****
****
***/

static ReadState canRead(tr_peerIo* peer_io, void* vhandshake, size_t* piece)
{
    TR_ASSERT(tr_isPeerIo(peer_io));

    auto* handshake = static_cast<tr_handshake*>(vhandshake);

    bool ready_for_more = true;

    /* no piece data in handshake */
    *piece = 0;

    tr_logAddTraceHand(handshake, fmt::format("handling canRead; state is [{}]", getStateName(handshake->state)));

    ReadState ret = READ_NOW;
    while (ready_for_more)
    {
        switch (handshake->state)
        {
        case AWAITING_HANDSHAKE:
            ret = readHandshake(handshake, peer_io);
            break;

        case AWAITING_PEER_ID:
            ret = readPeerId(handshake, peer_io);
            break;

        case AWAITING_YA:
            ret = readYa(handshake, peer_io);
            break;

        case AWAITING_PAD_A:
            ret = readPadA(handshake, peer_io);
            break;

        case AWAITING_CRYPTO_PROVIDE:
            ret = readCryptoProvide(handshake, peer_io);
            break;

        case AWAITING_PAD_C:
            ret = readPadC(handshake, peer_io);
            break;

        case AWAITING_IA:
            ret = readIA(handshake, peer_io);
            break;

        case AWAITING_PAYLOAD_STREAM:
            ret = readPayloadStream(handshake, peer_io);
            break;

        case AWAITING_YB:
            ret = readYb(handshake, peer_io);
            break;

        case AWAITING_VC:
            ret = readVC(handshake, peer_io);
            break;

        case AWAITING_CRYPTO_SELECT:
            ret = readCryptoSelect(handshake, peer_io);
            break;

        case AWAITING_PAD_D:
            ret = readPadD(handshake, peer_io);
            break;

        default:
#ifdef TR_ENABLE_ASSERTS
            TR_ASSERT_MSG(false, fmt::format(FMT_STRING("unhandled handshake state {:d}"), handshake->state));
#else
            ret = READ_ERR;
            break;
#endif
        }

        if (ret != READ_NOW)
        {
            ready_for_more = false;
        }
        else if (handshake->state == AWAITING_PAD_C)
        {
            ready_for_more = peer_io->readBufferSize() >= handshake->pad_c_len;
        }
        else if (handshake->state == AWAITING_PAD_D)
        {
            ready_for_more = peer_io->readBufferSize() >= handshake->pad_d_len;
        }
        else if (handshake->state == AWAITING_IA)
        {
            ready_for_more = peer_io->readBufferSize() >= handshake->ia_len;
        }
    }

    return ret;
}

static bool fireDoneFunc(tr_handshake* handshake, bool is_connected)
{
    auto result = tr_handshake_result{};
    result.handshake = handshake;
    result.io = handshake->io;
    result.readAnythingFromPeer = handshake->haveReadAnythingFromPeer;
    result.isConnected = is_connected;
    result.userData = handshake->done_func_user_data;
    result.peer_id = handshake->peer_id;
    bool const success = (*handshake->done_func)(result);
    return success;
}

static ReadState tr_handshakeDone(tr_handshake* handshake, bool is_connected)
{
    tr_logAddTraceHand(handshake, is_connected ? "handshakeDone: connected" : "handshakeDone: aborting");
    handshake->io->setCallbacks(nullptr, nullptr, nullptr, nullptr);

    bool const success = fireDoneFunc(handshake, is_connected);
    delete handshake;
    return success ? READ_LATER : READ_ERR;
}

void tr_handshakeAbort(tr_handshake* handshake)
{
    if (handshake != nullptr)
    {
        tr_handshakeDone(handshake, false);
    }
}

static void gotError(tr_peerIo* io, short what, void* vhandshake)
{
    int const errcode = errno;
    auto* handshake = static_cast<tr_handshake*>(vhandshake);

    if (io->socket.type == TR_PEER_SOCKET_TYPE_UTP && !io->isIncoming() && handshake->state == AWAITING_YB)
    {
        // the peer probably doesn't speak µTP.

        auto const hash = io->torrentHash();
        auto const info = hash ? handshake->mediator->torrentInfo(*hash) : std::nullopt;

        /* Don't mark a peer as non-µTP unless it's really a connect failure. */
        if ((errcode == ETIMEDOUT || errcode == ECONNREFUSED) && info)
        {
            handshake->mediator->setUTPFailed(*hash, io->address());
        }

        if (handshake->mediator->allowsTCP() && handshake->io->reconnect() == 0)
        {
            auto msg = std::array<uint8_t, HandshakeSize>{};
            buildHandshakeMessage(handshake, std::data(msg));
            handshake->haveSentBitTorrentHandshake = true;
            setReadState(handshake, AWAITING_HANDSHAKE);
            handshake->io->writeBytes(std::data(msg), std::size(msg), false);
        }
    }

    /* if the error happened while we were sending a public key, we might
     * have encountered a peer that doesn't do encryption... reconnect and
     * try a plaintext handshake */
    if ((handshake->state == AWAITING_YB || handshake->state == AWAITING_VC) &&
        handshake->encryption_mode != TR_ENCRYPTION_REQUIRED && handshake->mediator->allowsTCP() &&
        handshake->io->reconnect() == 0)
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};
        tr_logAddTraceHand(handshake, "handshake failed, trying plaintext...");
        buildHandshakeMessage(handshake, std::data(msg));
        handshake->haveSentBitTorrentHandshake = true;
        setReadState(handshake, AWAITING_HANDSHAKE);
        handshake->io->writeBytes(std::data(msg), std::size(msg), false);
    }
    else
    {
        tr_logAddTraceHand(
            handshake,
            fmt::format("libevent got an error what=={}, errno={} ({})", what, errcode, tr_strerror(errcode)));
        tr_handshakeDone(handshake, false);
    }
}

/**
***
**/

tr_handshake* tr_handshakeNew(
    std::unique_ptr<tr_handshake_mediator> mediator,
    std::shared_ptr<tr_peerIo> io,
    tr_encryption_mode encryption_mode,
    tr_handshake_done_func done_func,
    void* done_func_user_data)
{
    auto* const handshake = new tr_handshake{ std::move(mediator), std::move(io), encryption_mode };
    handshake->done_func = done_func;
    handshake->done_func_user_data = done_func_user_data;
    handshake->timeout_timer = handshake->mediator->timerMaker().create([handshake]() { tr_handshakeAbort(handshake); });
    handshake->timeout_timer->startSingleShot(HandshakeTimeoutSec);

    handshake->io->setCallbacks(canRead, nullptr, gotError, handshake);

    if (handshake->isIncoming())
    {
        setReadState(handshake, AWAITING_HANDSHAKE);
    }
    else if (encryption_mode != TR_CLEAR_PREFERRED)
    {
        sendYa(handshake);
    }
    else
    {
        auto msg = std::array<uint8_t, HandshakeSize>{};
        buildHandshakeMessage(handshake, std::data(msg));

        handshake->haveSentBitTorrentHandshake = true;
        setReadState(handshake, AWAITING_HANDSHAKE);
        handshake->io->writeBytes(std::data(msg), std::size(msg), false);
    }

    return handshake;
}
