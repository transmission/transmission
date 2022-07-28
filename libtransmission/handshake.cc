// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>

#include <event2/buffer.h>
#include <event2/event.h>

#include <fmt/format.h>

#include "transmission.h"

#include "clients.h"
#include "crypto-utils.h"
#include "handshake.h"
#include "log.h"
#include "peer-io.h"
#include "tr-assert.h"
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

#define HANDSHAKE_NAME "\023BitTorrent protocol"

// bittorrent handshake constants
static auto constexpr HANDSHAKE_NAME_LEN = int{ 20 };
static auto constexpr HANDSHAKE_FLAGS_LEN = int{ 8 };
static auto constexpr HANDSHAKE_SIZE = int{ 68 };
static auto constexpr INCOMING_HANDSHAKE_LEN = int{ 48 };

// encryption constants
static auto constexpr PadA_MAXLEN = int{ 512 };
static auto constexpr PadB_MAXLEN = int{ 512 };
static auto constexpr PadC_MAXLEN = int{ 512 };
static auto constexpr CRYPTO_PROVIDE_PLAINTEXT = int{ 1 };
static auto constexpr CRYPTO_PROVIDE_CRYPTO = int{ 2 };

// "VC is a verification constant that is used to verify whether the
// other side knows S and SKEY and thus defeats replay attacks of the
// SKEY hash. As of this version VC is a String of 8 bytes set to 0x00.
using vc_t = std::array<std::byte, 8>;
static auto constexpr VC = vc_t{};

// how long to wait before giving up on a handshake
static auto constexpr HANDSHAKE_TIMEOUT_SEC = int{ 30 };

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
    tr_handshake(std::shared_ptr<tr_handshake_mediator> mediator_in)
        : mediator{ std::move(mediator_in) }
        , dh{ mediator->privateKey() }
    {
    }

    ~tr_handshake()
    {
        if (io != nullptr)
        {
            tr_peerIoUnref(io); /* balanced by the ref in tr_handshakeNew */
        }

        event_free(timeout_timer);
    }

    [[nodiscard]] auto constexpr isIncoming() const noexcept
    {
        return io->isIncoming();
    }

    std::shared_ptr<tr_handshake_mediator> const mediator;

    bool haveReadAnythingFromPeer = false;
    bool haveSentBitTorrentHandshake = false;
    tr_peerIo* io = nullptr;
    DH dh = {};
    handshake_state_t state;
    tr_encryption_mode encryptionMode;
    uint16_t pad_c_len = {};
    uint16_t pad_d_len = {};
    uint16_t ia_len = {};
    uint32_t crypto_select = {};
    uint32_t crypto_provide = {};
    struct event* timeout_timer = nullptr;

    std::optional<tr_peer_id_t> peer_id;

    tr_handshake_done_func done_func = nullptr;
    void* done_func_user_data = nullptr;
};

/**
***
**/

#define tr_logAddTraceHand(handshake, msg) tr_logAddTrace(msg, (handshake)->io->addrStr())

static char const* getStateName(handshake_state_t const state)
{
    static char const* const state_strings[N_STATES] = {
        "awaiting handshake", /* AWAITING_HANDSHAKE */
        "awaiting peer id", /* AWAITING_PEER_ID */
        "awaiting ya", /* AWAITING_YA */
        "awaiting pad a", /* AWAITING_PAD_A */
        "awaiting crypto_provide", /* AWAITING_CRYPTO_PROVIDE */
        "awaiting pad c", /* AWAITING_PAD_C */
        "awaiting ia", /* AWAITING_IA */
        "awaiting payload stream", /* AWAITING_PAYLOAD_STREAM */
        "awaiting yb", /* AWAITING_YB */
        "awaiting vc", /* AWAITING_VC */
        "awaiting crypto select", /* AWAITING_CRYPTO_SELECT */
        "awaiting pad d" /* AWAITING_PAD_D */
    };

    return state < N_STATES ? state_strings[state] : "unknown state";
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

static bool buildHandshakeMessage(tr_handshake* handshake, uint8_t* buf)
{
    auto const info_hash = handshake->io->torrentHash();
    auto const info = info_hash ? handshake->mediator->torrentInfo(*info_hash) : std::nullopt;
    if (!info)
    {
        return false;
    }

    uint8_t* walk = buf;

    walk = std::copy_n(HANDSHAKE_NAME, HANDSHAKE_NAME_LEN, walk);

    memset(walk, 0, HANDSHAKE_FLAGS_LEN);
    HANDSHAKE_SET_LTEP(walk);
    HANDSHAKE_SET_FASTEXT(walk);
    /* Note that this doesn't depend on whether the torrent is private.
     * We don't accept DHT peers for a private torrent,
     * but we participate in the DHT regardless. */
    if (handshake->mediator->isDHTEnabled())
    {
        HANDSHAKE_SET_DHT(walk);
    }
    walk += HANDSHAKE_FLAGS_LEN;

    walk = std::copy_n(reinterpret_cast<char const*>(std::data(*info_hash)), std::size(*info_hash), walk);
    walk = std::copy(std::begin(info->client_peer_id), std::end(info->client_peer_id), walk);

    TR_ASSERT(walk - buf == HANDSHAKE_SIZE);
    return true;
}

static ReadState tr_handshakeDone(tr_handshake* handshake, bool isConnected);

enum handshake_parse_err_t
{
    HANDSHAKE_OK,
    HANDSHAKE_ENCRYPTION_WRONG,
    HANDSHAKE_BAD_TORRENT,
    HANDSHAKE_PEER_IS_SELF,
};

static handshake_parse_err_t parseHandshake(tr_handshake* handshake, struct evbuffer* inbuf)
{
    uint8_t name[HANDSHAKE_NAME_LEN];
    uint8_t reserved[HANDSHAKE_FLAGS_LEN];

    tr_logAddTraceHand(handshake, fmt::format("payload: need {}, got {}", HANDSHAKE_SIZE, evbuffer_get_length(inbuf)));

    if (evbuffer_get_length(inbuf) < HANDSHAKE_SIZE)
    {
        return HANDSHAKE_ENCRYPTION_WRONG;
    }

    /* confirm the protocol */
    tr_peerIoReadBytes(handshake->io, inbuf, name, HANDSHAKE_NAME_LEN);

    if (memcmp(name, HANDSHAKE_NAME, HANDSHAKE_NAME_LEN) != 0)
    {
        return HANDSHAKE_ENCRYPTION_WRONG;
    }

    /* read the reserved bytes */
    tr_peerIoReadBytes(handshake->io, inbuf, reserved, HANDSHAKE_FLAGS_LEN);

    /* torrent hash */
    auto hash = tr_sha1_digest_t{};
    tr_peerIoReadBytes(handshake->io, inbuf, std::data(hash), std::size(hash));
    if (auto const torrent_hash = handshake->io->torrentHash(); !torrent_hash || *torrent_hash != hash)
    {
        tr_logAddTraceHand(handshake, "peer returned the wrong hash. wtf?");
        return HANDSHAKE_BAD_TORRENT;
    }

    // peer_id
    auto peer_id = tr_peer_id_t{};
    tr_peerIoReadBytes(handshake->io, inbuf, std::data(peer_id), std::size(peer_id));
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

    handshake->io->enableDHT(HANDSHAKE_HAS_DHT(reserved));
    handshake->io->enableLTEP(HANDSHAKE_HAS_LTEP(reserved));
    handshake->io->enableFEXT(HANDSHAKE_HAS_FASTEXT(reserved));

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
    tr_peerIoWriteBytes(handshake->io, data, walk - data, false);
}

// 1 A->B: our public key (Ya) and some padding (PadA)
static void sendYa(tr_handshake* handshake)
{
    sendPublicKeyAndPad<PadA_MAXLEN>(handshake);
    setReadState(handshake, AWAITING_YB);
}

static uint32_t getCryptoProvide(tr_handshake const* handshake)
{
    uint32_t provide = 0;

    switch (handshake->encryptionMode)
    {
    case TR_ENCRYPTION_REQUIRED:
    case TR_ENCRYPTION_PREFERRED:
        provide |= CRYPTO_PROVIDE_CRYPTO;
        break;

    case TR_CLEAR_PREFERRED:
        provide |= CRYPTO_PROVIDE_CRYPTO | CRYPTO_PROVIDE_PLAINTEXT;
        break;
    }

    return provide;
}

static uint32_t getCryptoSelect(tr_handshake const* handshake, uint32_t crypto_provide)
{
    uint32_t choices[2];
    int nChoices = 0;

    switch (handshake->encryptionMode)
    {
    case TR_ENCRYPTION_REQUIRED:
        choices[nChoices++] = CRYPTO_PROVIDE_CRYPTO;
        break;

    case TR_ENCRYPTION_PREFERRED:
        choices[nChoices++] = CRYPTO_PROVIDE_CRYPTO;
        choices[nChoices++] = CRYPTO_PROVIDE_PLAINTEXT;
        break;

    case TR_CLEAR_PREFERRED:
        choices[nChoices++] = CRYPTO_PROVIDE_PLAINTEXT;
        choices[nChoices++] = CRYPTO_PROVIDE_CRYPTO;
        break;
    }

    for (int i = 0; i < nChoices; ++i)
    {
        if ((crypto_provide & choices[i]) != 0)
        {
            return choices[i];
        }
    }

    return 0;
}

static ReadState readYb(tr_handshake* handshake, struct evbuffer* inbuf)
{
    size_t needlen = HANDSHAKE_NAME_LEN;

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    bool const isEncrypted = memcmp(evbuffer_pullup(inbuf, HANDSHAKE_NAME_LEN), HANDSHAKE_NAME, HANDSHAKE_NAME_LEN) != 0;

    auto peer_public_key = DH::key_bigend_t{};
    if (isEncrypted)
    {
        needlen = std::size(peer_public_key);

        if (evbuffer_get_length(inbuf) < needlen)
        {
            return READ_LATER;
        }
    }

    tr_logAddTraceHand(handshake, isEncrypted ? "got an encrypted handshake" : "got a plain handshake");

    if (!isEncrypted)
    {
        setState(handshake, AWAITING_HANDSHAKE);
        return READ_NOW;
    }

    handshake->haveReadAnythingFromPeer = true;

    // get the peer's public key
    evbuffer_remove(inbuf, std::data(peer_public_key), std::size(peer_public_key));
    handshake->dh.setPeerPublicKey(peer_public_key);

    /* now send these: HASH('req1', S), HASH('req2', SKEY) xor HASH('req3', S),
     * ENCRYPT(VC, crypto_provide, len(PadC), PadC, len(IA)), ENCRYPT(IA) */
    evbuffer* const outbuf = evbuffer_new();

    /* HASH('req1', S) */
    if (auto const req1 = tr_sha1("req1"sv, handshake->dh.secret()); req1)
    {
        evbuffer_add(outbuf, std::data(*req1), std::size(*req1));
    }
    else
    {
        tr_logAddTraceHand(handshake, "error while computing req1 hash after Yb");
        return tr_handshakeDone(handshake, false);
    }

    auto const info_hash = handshake->io->torrentHash();
    if (!info_hash)
    {
        tr_logAddTraceHand(handshake, "error while computing req2/req3 hash after Yb");
        return tr_handshakeDone(handshake, false);
    }

    /* HASH('req2', SKEY) xor HASH('req3', S) */
    {
        auto const req2 = tr_sha1("req2"sv, *info_hash);
        auto const req3 = tr_sha1("req3"sv, handshake->dh.secret());
        if (!req2 || !req3)
        {
            tr_logAddTraceHand(handshake, "error while computing req2/req3 hash after Yb");
            return tr_handshakeDone(handshake, false);
        }

        auto buf = tr_sha1_digest_t{};
        for (size_t i = 0, n = std::size(buf); i < n; ++i)
        {
            buf[i] = (*req2)[i] ^ (*req3)[i];
        }

        evbuffer_add(outbuf, std::data(buf), std::size(buf));
    }

    /* ENCRYPT(VC, crypto_provide, len(PadC), PadC
     * PadC is reserved for future extensions to the handshake...
     * standard practice at this time is for it to be zero-length */
    tr_peerIoWriteBuf(handshake->io, outbuf, false);
    handshake->io->encryptInit(handshake->io->isIncoming(), handshake->dh, *info_hash);
    evbuffer_add(outbuf, std::data(VC), std::size(VC));
    evbuffer_add_uint32(outbuf, getCryptoProvide(handshake));
    evbuffer_add_uint16(outbuf, 0);

    /* ENCRYPT len(IA)), ENCRYPT(IA) */
    if (uint8_t msg[HANDSHAKE_SIZE]; buildHandshakeMessage(handshake, msg))
    {
        evbuffer_add_uint16(outbuf, sizeof(msg));
        evbuffer_add(outbuf, msg, sizeof(msg));
        handshake->haveSentBitTorrentHandshake = true;
    }
    else
    {
        return tr_handshakeDone(handshake, false);
    }

    /* send it */
    handshake->io->decryptInit(handshake->io->isIncoming(), handshake->dh, *info_hash);
    setReadState(handshake, AWAITING_VC);
    tr_peerIoWriteBuf(handshake->io, outbuf, false);

    /* cleanup */
    evbuffer_free(outbuf);
    return READ_NOW;
}

// MSE spec: "Since the length of [PadB is] unknown,
// A will be able to resynchronize on ENCRYPT(VC)"
static ReadState readVC(tr_handshake* handshake, struct evbuffer* inbuf)
{
    // find the end of PadB by looking for `ENCRYPT(VC)`
    auto needle = VC;
    auto filter = tr_message_stream_encryption::Filter{};
    filter.encryptInit(true, handshake->dh, *handshake->io->torrentHash());
    filter.encrypt(std::size(needle), std::data(needle));

    for (size_t i = 0; i < PadB_MAXLEN; ++i)
    {
        if (evbuffer_get_length(inbuf) < std::size(needle))
        {
            tr_logAddTraceHand(handshake, "not enough bytes... returning read_more");
            return READ_LATER;
        }

        auto const* peek = reinterpret_cast<std::byte const*>(evbuffer_pullup(inbuf, std::size(needle)));
        if (std::equal(std::begin(needle), std::end(needle), peek))
        {
            tr_logAddTraceHand(handshake, "got it!");
            // We already know it's a match; now we just need to
            // consume it from the read buffer.
            tr_peerIoReadBytes(handshake->io, inbuf, std::data(needle), std::size(needle));
            setState(handshake, AWAITING_CRYPTO_SELECT);
            return READ_NOW;
        }

        evbuffer_drain(inbuf, 1);
    }

    tr_logAddTraceHand(handshake, "couldn't find ENCRYPT(VC)");
    return tr_handshakeDone(handshake, false);
}

static ReadState readCryptoSelect(tr_handshake* handshake, struct evbuffer* inbuf)
{
    static size_t const needlen = sizeof(uint32_t) + sizeof(uint16_t);

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    uint32_t crypto_select = 0;
    tr_peerIoReadUint32(handshake->io, inbuf, &crypto_select);
    handshake->crypto_select = crypto_select;
    tr_logAddTraceHand(handshake, fmt::format("crypto select is {}", crypto_select));

    if ((crypto_select & getCryptoProvide(handshake)) == 0)
    {
        tr_logAddTraceHand(handshake, "peer selected an encryption option we didn't offer");
        return tr_handshakeDone(handshake, false);
    }

    uint16_t pad_d_len = 0;
    tr_peerIoReadUint16(handshake->io, inbuf, &pad_d_len);
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

static ReadState readPadD(tr_handshake* handshake, struct evbuffer* inbuf)
{
    size_t const needlen = handshake->pad_d_len;

    tr_logAddTraceHand(handshake, fmt::format("pad d: need {}, got {}", needlen, evbuffer_get_length(inbuf)));

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    tr_peerIoDrain(handshake->io, inbuf, needlen);

    setState(handshake, AWAITING_HANDSHAKE);
    return READ_NOW;
}

/***
****
****  INCOMING CONNECTIONS
****
***/

static ReadState readHandshake(tr_handshake* handshake, struct evbuffer* inbuf)
{
    tr_logAddTraceHand(handshake, fmt::format("payload: need {}, got {}", INCOMING_HANDSHAKE_LEN, evbuffer_get_length(inbuf)));

    if (evbuffer_get_length(inbuf) < INCOMING_HANDSHAKE_LEN)
    {
        return READ_LATER;
    }

    handshake->haveReadAnythingFromPeer = true;

    uint8_t pstrlen = evbuffer_pullup(inbuf, 1)[0]; /* peek, don't read. We may be handing inbuf to AWAITING_YA */

    if (pstrlen == 19) /* unencrypted */
    {
        if (handshake->encryptionMode == TR_ENCRYPTION_REQUIRED)
        {
            tr_logAddTraceHand(handshake, "peer is unencrypted, and we're disallowing that");
            return tr_handshakeDone(handshake, false);
        }
    }
    else /* encrypted or corrupt */
    {
        if (handshake->isIncoming())
        {
            tr_logAddTraceHand(handshake, "I think peer is sending us an encrypted handshake...");
            setState(handshake, AWAITING_YA);
            return READ_NOW;
        }

        handshake->io->decrypt(1, &pstrlen);

        if (pstrlen != 19)
        {
            tr_logAddTraceHand(handshake, "I think peer has sent us a corrupt handshake...");
            return tr_handshakeDone(handshake, false);
        }
    }

    evbuffer_drain(inbuf, 1);

    /* pstr (BitTorrent) */
    TR_ASSERT(pstrlen == 19);
    uint8_t pstr[20];
    tr_peerIoReadBytes(handshake->io, inbuf, pstr, pstrlen);
    pstr[pstrlen] = '\0';

    if (strncmp((char const*)pstr, "BitTorrent protocol", 19) != 0)
    {
        return tr_handshakeDone(handshake, false);
    }

    /* reserved bytes */
    uint8_t reserved[HANDSHAKE_FLAGS_LEN];
    tr_peerIoReadBytes(handshake->io, inbuf, reserved, sizeof(reserved));

    /**
    *** Extensions
    **/

    handshake->io->enableDHT(HANDSHAKE_HAS_DHT(reserved));
    handshake->io->enableLTEP(HANDSHAKE_HAS_LTEP(reserved));
    handshake->io->enableFEXT(HANDSHAKE_HAS_FASTEXT(reserved));

    /* torrent hash */
    auto hash = tr_sha1_digest_t{};
    tr_peerIoReadBytes(handshake->io, inbuf, std::data(hash), std::size(hash));

    if (handshake->isIncoming())
    {
        if (!handshake->mediator->torrentInfo(hash))
        {
            tr_logAddTraceHand(handshake, "peer is trying to connect to us for a torrent we don't have.");
            return tr_handshakeDone(handshake, false);
        }

        handshake->io->setTorrentHash(hash);
    }
    else /* outgoing */
    {
        auto const torrent_hash = handshake->io->torrentHash();

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
        uint8_t msg[HANDSHAKE_SIZE];

        if (!buildHandshakeMessage(handshake, msg))
        {
            return tr_handshakeDone(handshake, false);
        }

        tr_peerIoWriteBytes(handshake->io, msg, sizeof(msg), false);
        handshake->haveSentBitTorrentHandshake = true;
    }

    setReadState(handshake, AWAITING_PEER_ID);
    return READ_NOW;
}

static ReadState readPeerId(tr_handshake* handshake, struct evbuffer* inbuf)
{
    // read the peer_id
    auto peer_id = tr_peer_id_t{};
    if (evbuffer_get_length(inbuf) < std::size(peer_id))
    {
        return READ_LATER;
    }
    tr_peerIoReadBytes(handshake->io, inbuf, std::data(peer_id), std::size(peer_id));
    handshake->peer_id = peer_id;

    char client[128] = {};
    tr_clientForId(client, sizeof(client), peer_id);
    tr_logAddTraceHand(handshake, fmt::format("peer-id is '{}' ... isIncoming is {}", client, handshake->isIncoming()));

    // if we've somehow connected to ourselves, don't keep the connection
    auto const hash = handshake->io->torrentHash();
    auto const info = hash ? handshake->mediator->torrentInfo(*hash) : std::nullopt;
    auto const connected_to_self = info && info->client_peer_id == peer_id;

    return tr_handshakeDone(handshake, !connected_to_self);
}

static ReadState readYa(tr_handshake* handshake, struct evbuffer* inbuf)
{
    auto peer_public_key = DH::key_bigend_t{};
    tr_logAddTraceHand(
        handshake,
        fmt::format("in readYa... need {}, have {}", std::size(peer_public_key), evbuffer_get_length(inbuf)));

    if (evbuffer_get_length(inbuf) < std::size(peer_public_key))
    {
        return READ_LATER;
    }

    /* read the incoming peer's public key */
    evbuffer_remove(inbuf, std::data(peer_public_key), std::size(peer_public_key));
    handshake->dh.setPeerPublicKey(peer_public_key);

    // send our public key to the peer
    tr_logAddTraceHand(handshake, "sending B->A: Diffie Hellman Yb, PadB");
    sendPublicKeyAndPad<PadB_MAXLEN>(handshake);

    setReadState(handshake, AWAITING_PAD_A);
    return READ_NOW;
}

static ReadState readPadA(tr_handshake* handshake, struct evbuffer* inbuf)
{
    // find the end of PadA by looking for HASH('req1', S)
    auto const needle = *tr_sha1("req1"sv, handshake->dh.secret());

    for (size_t i = 0; i < PadA_MAXLEN; ++i)
    {
        if (evbuffer_get_length(inbuf) < std::size(needle))
        {
            tr_logAddTraceHand(handshake, "not enough bytes... returning read_more");
            return READ_LATER;
        }

        auto const* peek = reinterpret_cast<std::byte const*>(evbuffer_pullup(inbuf, std::size(needle)));
        if (std::equal(std::begin(needle), std::end(needle), peek))
        {
            tr_logAddTraceHand(handshake, "found it... looking setting to awaiting_crypto_provide");
            evbuffer_drain(inbuf, std::size(needle));
            setState(handshake, AWAITING_CRYPTO_PROVIDE);
            return READ_NOW;
        }

        evbuffer_drain(inbuf, 1);
    }

    tr_logAddTraceHand(handshake, "couldn't find HASH('req', S)");
    return tr_handshakeDone(handshake, false);
}

static ReadState readCryptoProvide(tr_handshake* handshake, struct evbuffer* inbuf)
{
    /* HASH('req2', SKEY) xor HASH('req3', S), ENCRYPT(VC, crypto_provide, len(PadC)) */

    uint16_t padc_len = 0;
    uint32_t crypto_provide = 0;
    size_t const needlen = SHA_DIGEST_LENGTH + /* HASH('req2', SKEY) xor HASH('req3', S) */
        std::size(VC) + sizeof(crypto_provide) + sizeof(padc_len);

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    /* This next piece is HASH('req2', SKEY) xor HASH('req3', S) ...
     * we can get the first half of that (the obfuscatedTorrentHash)
     * by building the latter and xor'ing it with what the peer sent us */
    tr_logAddTraceHand(handshake, "reading obfuscated torrent hash...");
    auto req2 = tr_sha1_digest_t{};
    evbuffer_remove(inbuf, std::data(req2), std::size(req2));

    auto const req3 = tr_sha1("req3"sv, handshake->dh.secret());
    if (!req3)
    {
        tr_logAddTraceHand(handshake, "error while computing req3 hash after req2");
        return tr_handshakeDone(handshake, false);
    }

    auto obfuscated_hash = tr_sha1_digest_t{};
    for (size_t i = 0; i < std::size(obfuscated_hash); ++i)
    {
        obfuscated_hash[i] = req2[i] ^ (*req3)[i];
    }

    if (auto const info = handshake->mediator->torrentInfoFromObfuscated(obfuscated_hash); info)
    {
        bool const client_is_seed = info->is_done;
        bool const peer_is_seed = handshake->mediator->isPeerKnownSeed(info->id, handshake->io->address());
        tr_logAddTraceHand(handshake, fmt::format("got INCOMING connection's encrypted handshake for torrent [{}]", info->id));
        handshake->io->setTorrentHash(info->info_hash);

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

    handshake->io->decryptInit(handshake->io->isIncoming(), handshake->dh, *handshake->io->torrentHash());

    auto vc_in = vc_t{};
    tr_peerIoReadBytes(handshake->io, inbuf, std::data(vc_in), std::size(vc_in));

    tr_peerIoReadUint32(handshake->io, inbuf, &crypto_provide);
    handshake->crypto_provide = crypto_provide;
    tr_logAddTraceHand(handshake, fmt::format("crypto_provide is {}", crypto_provide));

    tr_peerIoReadUint16(handshake->io, inbuf, &padc_len);
    tr_logAddTraceHand(handshake, fmt::format("padc is {}", padc_len));
    if (padc_len > PadC_MAXLEN)
    {
        tr_logAddTraceHand(handshake, "peer's PadC is too big");
        return tr_handshakeDone(handshake, false);
    }

    handshake->pad_c_len = padc_len;
    setState(handshake, AWAITING_PAD_C);
    return READ_NOW;
}

static ReadState readPadC(tr_handshake* handshake, struct evbuffer* inbuf)
{
    uint16_t ia_len = 0;

    if (auto const needlen = handshake->pad_c_len + sizeof(uint16_t); evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    // read the throwaway padc
    auto pad_c = std::array<char, PadC_MAXLEN>{};
    tr_peerIoReadBytes(handshake->io, inbuf, std::data(pad_c), handshake->pad_c_len);

    /* read ia_len */
    tr_peerIoReadUint16(handshake->io, inbuf, &ia_len);
    tr_logAddTraceHand(handshake, fmt::format("ia_len is {}", ia_len));
    handshake->ia_len = ia_len;
    setState(handshake, AWAITING_IA);
    return READ_NOW;
}

static ReadState readIA(tr_handshake* handshake, struct evbuffer const* inbuf)
{
    size_t const needlen = handshake->ia_len;

    tr_logAddTraceHand(handshake, fmt::format("reading IA... have {}, need {}", evbuffer_get_length(inbuf), needlen));

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    /**
    ***  B->A: ENCRYPT(VC, crypto_select, len(padD), padD), ENCRYPT2(Payload Stream)
    **/

    handshake->io->encryptInit(handshake->io->isIncoming(), handshake->dh, *handshake->io->torrentHash());
    evbuffer* const outbuf = evbuffer_new();

    // send VC
    tr_logAddTraceHand(handshake, "sending vc");
    evbuffer_add(outbuf, std::data(VC), std::size(VC));

    /* send crypto_select */
    uint32_t const crypto_select = getCryptoSelect(handshake, handshake->crypto_provide);

    if (crypto_select != 0)
    {
        tr_logAddTraceHand(handshake, fmt::format("selecting crypto mode '{}'", crypto_select));
        evbuffer_add_uint32(outbuf, crypto_select);
    }
    else
    {
        tr_logAddTraceHand(handshake, "peer didn't offer an encryption mode we like.");
        evbuffer_free(outbuf);
        return tr_handshakeDone(handshake, false);
    }

    tr_logAddTraceHand(handshake, "sending pad d");

    /* ENCRYPT(VC, crypto_provide, len(PadD), PadD
     * PadD is reserved for future extensions to the handshake...
     * standard practice at this time is for it to be zero-length */
    {
        uint16_t const len = 0;
        evbuffer_add_uint16(outbuf, len);
    }

    /* maybe de-encrypt our connection */
    if (crypto_select == CRYPTO_PROVIDE_PLAINTEXT)
    {
        tr_peerIoWriteBuf(handshake->io, outbuf, false);
    }

    tr_logAddTraceHand(handshake, "sending handshake");

    /* send our handshake */
    if (uint8_t msg[HANDSHAKE_SIZE]; buildHandshakeMessage(handshake, msg))
    {
        evbuffer_add(outbuf, msg, sizeof(msg));
        handshake->haveSentBitTorrentHandshake = true;
    }
    else
    {
        return tr_handshakeDone(handshake, false);
    }

    /* send it out */
    tr_peerIoWriteBuf(handshake->io, outbuf, false);
    evbuffer_free(outbuf);

    /* now await the handshake */
    setState(handshake, AWAITING_PAYLOAD_STREAM);
    return READ_NOW;
}

static ReadState readPayloadStream(tr_handshake* handshake, struct evbuffer* inbuf)
{
    size_t const needlen = HANDSHAKE_SIZE;

    tr_logAddTraceHand(
        handshake,
        fmt::format("reading payload stream... have {}, need {}", evbuffer_get_length(inbuf), needlen));

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    /* parse the handshake ... */
    handshake_parse_err_t const i = parseHandshake(handshake, inbuf);
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

static ReadState canRead(tr_peerIo* io, void* vhandshake, size_t* piece)
{
    TR_ASSERT(tr_isPeerIo(io));

    auto* handshake = static_cast<tr_handshake*>(vhandshake);

    evbuffer* const inbuf = io->getReadBuffer();
    bool readyForMore = true;

    /* no piece data in handshake */
    *piece = 0;

    tr_logAddTraceHand(handshake, fmt::format("handling canRead; state is [{}]", getStateName(handshake->state)));

    ReadState ret = READ_NOW;
    while (readyForMore)
    {
        switch (handshake->state)
        {
        case AWAITING_HANDSHAKE:
            ret = readHandshake(handshake, inbuf);
            break;

        case AWAITING_PEER_ID:
            ret = readPeerId(handshake, inbuf);
            break;

        case AWAITING_YA:
            ret = readYa(handshake, inbuf);
            break;

        case AWAITING_PAD_A:
            ret = readPadA(handshake, inbuf);
            break;

        case AWAITING_CRYPTO_PROVIDE:
            ret = readCryptoProvide(handshake, inbuf);
            break;

        case AWAITING_PAD_C:
            ret = readPadC(handshake, inbuf);
            break;

        case AWAITING_IA:
            ret = readIA(handshake, inbuf);
            break;

        case AWAITING_PAYLOAD_STREAM:
            ret = readPayloadStream(handshake, inbuf);
            break;

        case AWAITING_YB:
            ret = readYb(handshake, inbuf);
            break;

        case AWAITING_VC:
            ret = readVC(handshake, inbuf);
            break;

        case AWAITING_CRYPTO_SELECT:
            ret = readCryptoSelect(handshake, inbuf);
            break;

        case AWAITING_PAD_D:
            ret = readPadD(handshake, inbuf);
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
            readyForMore = false;
        }
        else if (handshake->state == AWAITING_PAD_C)
        {
            readyForMore = evbuffer_get_length(inbuf) >= handshake->pad_c_len;
        }
        else if (handshake->state == AWAITING_PAD_D)
        {
            readyForMore = evbuffer_get_length(inbuf) >= handshake->pad_d_len;
        }
        else if (handshake->state == AWAITING_IA)
        {
            readyForMore = evbuffer_get_length(inbuf) >= handshake->ia_len;
        }
    }

    return ret;
}

static bool fireDoneFunc(tr_handshake* handshake, bool isConnected)
{
    auto result = tr_handshake_result{};
    result.handshake = handshake;
    result.io = handshake->io;
    result.readAnythingFromPeer = handshake->haveReadAnythingFromPeer;
    result.isConnected = isConnected;
    result.userData = handshake->done_func_user_data;
    result.peer_id = handshake->peer_id;
    bool const success = (*handshake->done_func)(result);
    return success;
}

static ReadState tr_handshakeDone(tr_handshake* handshake, bool isOK)
{
    tr_logAddTraceHand(handshake, isOK ? "handshakeDone: connected" : "handshakeDone: aborting");
    tr_peerIoSetIOFuncs(handshake->io, nullptr, nullptr, nullptr, nullptr);

    bool const success = fireDoneFunc(handshake, isOK);
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
        // the peer probably doesn't speak uTP.

        auto const hash = io->torrentHash();
        auto const info = hash ? handshake->mediator->torrentInfo(*hash) : std::nullopt;

        /* Don't mark a peer as non-uTP unless it's really a connect failure. */
        if ((errcode == ETIMEDOUT || errcode == ECONNREFUSED) && info)
        {
            handshake->mediator->setUTPFailed(*hash, io->address());
        }

        if (tr_peerIoReconnect(handshake->io) == 0)
        {
            uint8_t msg[HANDSHAKE_SIZE];
            buildHandshakeMessage(handshake, msg);
            handshake->haveSentBitTorrentHandshake = true;
            setReadState(handshake, AWAITING_HANDSHAKE);
            tr_peerIoWriteBytes(handshake->io, msg, sizeof(msg), false);
        }
    }

    /* if the error happened while we were sending a public key, we might
     * have encountered a peer that doesn't do encryption... reconnect and
     * try a plaintext handshake */
    if ((handshake->state == AWAITING_YB || handshake->state == AWAITING_VC) &&
        handshake->encryptionMode != TR_ENCRYPTION_REQUIRED && tr_peerIoReconnect(handshake->io) == 0)
    {
        uint8_t msg[HANDSHAKE_SIZE];

        tr_logAddTraceHand(handshake, "handshake failed, trying plaintext...");
        buildHandshakeMessage(handshake, msg);
        handshake->haveSentBitTorrentHandshake = true;
        setReadState(handshake, AWAITING_HANDSHAKE);
        tr_peerIoWriteBytes(handshake->io, msg, sizeof(msg), false);
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

static void handshakeTimeout(evutil_socket_t /*s*/, short /*type*/, void* handshake)
{
    tr_handshakeAbort(static_cast<tr_handshake*>(handshake));
}

tr_handshake* tr_handshakeNew(
    std::shared_ptr<tr_handshake_mediator> mediator,
    tr_peerIo* io,
    tr_encryption_mode encryptionMode,
    tr_handshake_done_func done_func,
    void* done_func_user_data)
{
    auto* const handshake = new tr_handshake{ std::move(mediator) };
    handshake->io = io;
    handshake->encryptionMode = encryptionMode;
    handshake->done_func = done_func;
    handshake->done_func_user_data = done_func_user_data;
    handshake->timeout_timer = evtimer_new(handshake->mediator->eventBase(), handshakeTimeout, handshake);
    tr_timerAdd(*handshake->timeout_timer, HANDSHAKE_TIMEOUT_SEC, 0);

    tr_peerIoRef(io); /* balanced by the unref in ~tr_handshake() */
    tr_peerIoSetIOFuncs(handshake->io, canRead, nullptr, gotError, handshake);

    if (handshake->isIncoming())
    {
        setReadState(handshake, AWAITING_HANDSHAKE);
    }
    else if (encryptionMode != TR_CLEAR_PREFERRED)
    {
        sendYa(handshake);
    }
    else
    {
        uint8_t msg[HANDSHAKE_SIZE];
        buildHandshakeMessage(handshake, msg);

        handshake->haveSentBitTorrentHandshake = true;
        setReadState(handshake, AWAITING_HANDSHAKE);
        tr_peerIoWriteBytes(handshake->io, msg, sizeof(msg), false);
    }

    return handshake;
}

tr_peerIo* tr_handshakeStealIO(tr_handshake* handshake)
{
    TR_ASSERT(handshake != nullptr);
    TR_ASSERT(handshake->io != nullptr);

    tr_peerIo* io = handshake->io;
    handshake->io = nullptr;
    return io;
}
