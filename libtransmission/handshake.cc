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
#include "peer-mgr.h"
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-dht.h"
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
static auto constexpr VC_LENGTH = int{ 8 };
static auto constexpr CRYPTO_PROVIDE_PLAINTEXT = int{ 1 };
static auto constexpr CRYPTO_PROVIDE_CRYPTO = int{ 2 };

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
    bool haveReadAnythingFromPeer;
    bool haveSentBitTorrentHandshake;
    tr_peerIo* io;
    tr_crypto* crypto;
    tr_session* session;
    handshake_state_t state;
    tr_encryption_mode encryptionMode;
    uint16_t pad_c_len;
    uint16_t pad_d_len;
    uint16_t ia_len;
    uint32_t crypto_select;
    uint32_t crypto_provide;
    tr_sha1_digest_t myReq1;
    struct event* timeout_timer;

    std::optional<tr_peer_id_t> peer_id;

    tr_handshake_done_func done_func;
    void* done_func_user_data;
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
    auto const torrent_hash = tr_cryptoGetTorrentHash(handshake->crypto);
    auto* const tor = torrent_hash ? handshake->session->torrents().get(*torrent_hash) : nullptr;
    bool const success = tor != nullptr;

    if (success)
    {
        uint8_t* walk = buf;

        walk = std::copy_n(HANDSHAKE_NAME, HANDSHAKE_NAME_LEN, walk);

        memset(walk, 0, HANDSHAKE_FLAGS_LEN);
        HANDSHAKE_SET_LTEP(walk);
        HANDSHAKE_SET_FASTEXT(walk);
        /* Note that this doesn't depend on whether the torrent is private.
         * We don't accept DHT peers for a private torrent,
         * but we participate in the DHT regardless. */
        if (tr_dhtEnabled(handshake->session))
        {
            HANDSHAKE_SET_DHT(walk);
        }
        walk += HANDSHAKE_FLAGS_LEN;

        walk = std::copy_n(reinterpret_cast<char const*>(std::data(tor->infoHash())), std::size(tor->infoHash()), walk);

        auto const& peer_id = tr_torrentGetPeerId(tor);
        std::copy_n(std::data(peer_id), std::size(peer_id), walk);

        TR_ASSERT(walk + std::size(peer_id) - buf == HANDSHAKE_SIZE);
    }

    return success;
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
    if (auto const torrent_hash = tr_peerIoGetTorrentHash(handshake->io); !torrent_hash || *torrent_hash != hash)
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

    if (auto* const tor = handshake->session->torrents().get(hash); peer_id == tr_torrentGetPeerId(tor))
    {
        tr_logAddTraceHand(handshake, "streuth!  we've connected to ourselves.");
        return HANDSHAKE_PEER_IS_SELF;
    }

    /**
    *** Extensions
    **/

    tr_peerIoEnableDHT(handshake->io, HANDSHAKE_HAS_DHT(reserved));
    tr_peerIoEnableLTEP(handshake->io, HANDSHAKE_HAS_LTEP(reserved));
    tr_peerIoEnableFEXT(handshake->io, HANDSHAKE_HAS_FASTEXT(reserved));

    return HANDSHAKE_OK;
}

/***
****
****  OUTGOING CONNECTIONS
****
***/

/* 1 A->B: Diffie Hellman Ya, PadA */
static void sendYa(tr_handshake* handshake)
{
    /* add our public key (Ya) */

    int len = 0;
    uint8_t const* const public_key = tr_cryptoGetMyPublicKey(handshake->crypto, &len);
    TR_ASSERT(len == KEY_LEN);
    TR_ASSERT(public_key != nullptr);

    char outbuf[KEY_LEN + PadA_MAXLEN];
    char* walk = outbuf;
    walk = std::copy_n(public_key, len, walk);

    /* add some bullshit padding */
    len = tr_rand_int(PadA_MAXLEN);
    tr_rand_buffer(walk, len);
    walk += len;

    /* send it */
    setReadState(handshake, AWAITING_YB);
    tr_peerIoWriteBytes(handshake->io, outbuf, walk - outbuf, false);
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

static auto computeRequestHash(tr_handshake const* handshake, std::string_view name)
{
    return tr_cryptoSecretKeySha1(handshake->crypto, std::data(name), std::size(name), "", 0);
}

static ReadState readYb(tr_handshake* handshake, struct evbuffer* inbuf)
{
    uint8_t yb[KEY_LEN];
    size_t needlen = HANDSHAKE_NAME_LEN;

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    bool const isEncrypted = memcmp(evbuffer_pullup(inbuf, HANDSHAKE_NAME_LEN), HANDSHAKE_NAME, HANDSHAKE_NAME_LEN) != 0;

    if (isEncrypted)
    {
        needlen = KEY_LEN;

        if (evbuffer_get_length(inbuf) < needlen)
        {
            return READ_LATER;
        }
    }

    tr_logAddTraceHand(handshake, isEncrypted ? "got an encrypted handshake" : "got a plain handshake");

    tr_peerIoSetEncryption(handshake->io, isEncrypted ? PEER_ENCRYPTION_RC4 : PEER_ENCRYPTION_NONE);

    if (!isEncrypted)
    {
        setState(handshake, AWAITING_HANDSHAKE);
        return READ_NOW;
    }

    handshake->haveReadAnythingFromPeer = true;

    /* compute the secret */
    evbuffer_remove(inbuf, yb, KEY_LEN);

    if (!tr_cryptoComputeSecret(handshake->crypto, yb))
    {
        return tr_handshakeDone(handshake, false);
    }

    /* now send these: HASH('req1', S), HASH('req2', SKEY) xor HASH('req3', S),
     * ENCRYPT(VC, crypto_provide, len(PadC), PadC, len(IA)), ENCRYPT(IA) */
    evbuffer* const outbuf = evbuffer_new();

    /* HASH('req1', S) */
    {
        auto const req1 = computeRequestHash(handshake, "req1"sv);
        if (!req1)
        {
            tr_logAddTraceHand(handshake, "error while computing req1 hash after Yb");
            return tr_handshakeDone(handshake, false);
        }
        evbuffer_add(outbuf, std::data(*req1), std::size(*req1));
    }

    /* HASH('req2', SKEY) xor HASH('req3', S) */
    {
        auto const req2 = tr_sha1("req2"sv, *tr_cryptoGetTorrentHash(handshake->crypto));
        auto const req3 = computeRequestHash(handshake, "req3"sv);
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
    {
        uint8_t vc[VC_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        tr_peerIoWriteBuf(handshake->io, outbuf, false);
        tr_cryptoEncryptInit(handshake->crypto);
        tr_peerIoSetEncryption(handshake->io, PEER_ENCRYPTION_RC4);

        evbuffer_add(outbuf, vc, VC_LENGTH);
        evbuffer_add_uint32(outbuf, getCryptoProvide(handshake));
        evbuffer_add_uint16(outbuf, 0);
    }

    /* ENCRYPT len(IA)), ENCRYPT(IA) */
    {
        uint8_t msg[HANDSHAKE_SIZE];

        if (!buildHandshakeMessage(handshake, msg))
        {
            return tr_handshakeDone(handshake, false);
        }

        evbuffer_add_uint16(outbuf, sizeof(msg));
        evbuffer_add(outbuf, msg, sizeof(msg));

        handshake->haveSentBitTorrentHandshake = true;
    }

    /* send it */
    tr_cryptoDecryptInit(handshake->crypto);
    setReadState(handshake, AWAITING_VC);
    tr_peerIoWriteBuf(handshake->io, outbuf, false);

    /* cleanup */
    evbuffer_free(outbuf);
    return READ_LATER;
}

static ReadState readVC(tr_handshake* handshake, struct evbuffer* inbuf)
{
    uint8_t tmp[VC_LENGTH];
    int const key_len = VC_LENGTH;
    uint8_t const key[VC_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    /* note: this works w/o having to `unwind' the buffer if
     * we read too much, but it is pretty brute-force.
     * it would be nice to make this cleaner. */
    for (;;)
    {
        if (evbuffer_get_length(inbuf) < VC_LENGTH)
        {
            tr_logAddTraceHand(handshake, "not enough bytes... returning read_more");
            return READ_LATER;
        }

        memcpy(tmp, evbuffer_pullup(inbuf, key_len), key_len);
        tr_cryptoDecryptInit(handshake->crypto);
        tr_cryptoDecrypt(handshake->crypto, key_len, tmp, tmp);

        if (memcmp(tmp, key, key_len) == 0)
        {
            break;
        }

        evbuffer_drain(inbuf, 1);
    }

    tr_logAddTraceHand(handshake, "got it!");
    evbuffer_drain(inbuf, key_len);
    setState(handshake, AWAITING_CRYPTO_SELECT);
    return READ_NOW;
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

    tr_peerIoSetEncryption(handshake->io, static_cast<tr_encryption_type>(handshake->crypto_select));

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
        tr_peerIoSetEncryption(handshake->io, PEER_ENCRYPTION_NONE);

        if (handshake->encryptionMode == TR_ENCRYPTION_REQUIRED)
        {
            tr_logAddTraceHand(handshake, "peer is unencrypted, and we're disallowing that");
            return tr_handshakeDone(handshake, false);
        }
    }
    else /* encrypted or corrupt */
    {
        tr_peerIoSetEncryption(handshake->io, PEER_ENCRYPTION_RC4);

        if (tr_peerIoIsIncoming(handshake->io))
        {
            tr_logAddTraceHand(handshake, "I think peer is sending us an encrypted handshake...");
            setState(handshake, AWAITING_YA);
            return READ_NOW;
        }

        tr_cryptoDecrypt(handshake->crypto, 1, &pstrlen, &pstrlen);

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

    tr_peerIoEnableDHT(handshake->io, HANDSHAKE_HAS_DHT(reserved));
    tr_peerIoEnableLTEP(handshake->io, HANDSHAKE_HAS_LTEP(reserved));
    tr_peerIoEnableFEXT(handshake->io, HANDSHAKE_HAS_FASTEXT(reserved));

    /* torrent hash */
    auto hash = tr_sha1_digest_t{};
    tr_peerIoReadBytes(handshake->io, inbuf, std::data(hash), std::size(hash));

    if (tr_peerIoIsIncoming(handshake->io))
    {
        if (!handshake->session->torrents().contains(hash))
        {
            tr_logAddTraceHand(handshake, "peer is trying to connect to us for a torrent we don't have.");
            return tr_handshakeDone(handshake, false);
        }

        tr_peerIoSetTorrentHash(handshake->io, hash);
    }
    else /* outgoing */
    {
        auto const torrent_hash = tr_peerIoGetTorrentHash(handshake->io);
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
    tr_logAddTraceHand(
        handshake,
        fmt::format("peer-id is '{}' ... isIncoming is {}", client, tr_peerIoIsIncoming(handshake->io)));

    // if we've somehow connected to ourselves, don't keep the connection
    auto const hash = tr_peerIoGetTorrentHash(handshake->io);
    auto* const tor = hash ? handshake->session->torrents().get(*hash) : nullptr;
    bool const connected_to_self = peer_id == tr_torrentGetPeerId(tor);

    return tr_handshakeDone(handshake, !connected_to_self);
}

static ReadState readYa(tr_handshake* handshake, struct evbuffer* inbuf)
{
    tr_logAddTraceHand(handshake, fmt::format("in readYa... need {}, have {}", KEY_LEN, evbuffer_get_length(inbuf)));

    if (evbuffer_get_length(inbuf) < KEY_LEN)
    {
        return READ_LATER;
    }

    /* read the incoming peer's public key */
    uint8_t ya[KEY_LEN];
    evbuffer_remove(inbuf, ya, KEY_LEN);

    if (!tr_cryptoComputeSecret(handshake->crypto, ya))
    {
        return tr_handshakeDone(handshake, false);
    }

    auto req1 = computeRequestHash(handshake, "req1"sv);
    if (!req1)
    {
        tr_logAddTraceHand(handshake, "error while computing req1 hash after Ya");
        return tr_handshakeDone(handshake, false);
    }
    handshake->myReq1 = *req1;

    /* send our public key to the peer */
    tr_logAddTraceHand(handshake, "sending B->A: Diffie Hellman Yb, PadB");
    uint8_t outbuf[KEY_LEN + PadB_MAXLEN];
    uint8_t* walk = outbuf;
    int len = 0;
    uint8_t const* const myKey = tr_cryptoGetMyPublicKey(handshake->crypto, &len);
    walk = std::copy_n(myKey, len, walk);
    len = tr_rand_int(PadB_MAXLEN);
    tr_rand_buffer(walk, len);
    walk += len;

    setReadState(handshake, AWAITING_PAD_A);
    tr_peerIoWriteBytes(handshake->io, outbuf, walk - outbuf, false);
    return READ_NOW;
}

static ReadState readPadA(tr_handshake* handshake, struct evbuffer* inbuf)
{
    /* resynchronizing on HASH('req1', S) */
    struct evbuffer_ptr ptr = evbuffer_search(
        inbuf,
        reinterpret_cast<char const*>(std::data(handshake->myReq1)),
        std::size(handshake->myReq1),
        nullptr);

    if (ptr.pos != -1) /* match */
    {
        evbuffer_drain(inbuf, ptr.pos);
        tr_logAddTraceHand(handshake, "found it... looking setting to awaiting_crypto_provide");
        setState(handshake, AWAITING_CRYPTO_PROVIDE);
        return READ_NOW;
    }

    if (size_t const len = evbuffer_get_length(inbuf); len > SHA_DIGEST_LENGTH)
    {
        evbuffer_drain(inbuf, len - SHA_DIGEST_LENGTH);
    }

    return READ_LATER;
}

static ReadState readCryptoProvide(tr_handshake* handshake, struct evbuffer* inbuf)
{
    /* HASH('req2', SKEY) xor HASH('req3', S), ENCRYPT(VC, crypto_provide, len(PadC)) */

    uint8_t vc_in[VC_LENGTH];
    uint16_t padc_len = 0;
    uint32_t crypto_provide = 0;
    size_t const needlen = SHA_DIGEST_LENGTH + /* HASH('req1', s) */
        SHA_DIGEST_LENGTH + /* HASH('req2', SKEY) xor HASH('req3', S) */
        VC_LENGTH + sizeof(crypto_provide) + sizeof(padc_len);

    if (evbuffer_get_length(inbuf) < needlen)
    {
        return READ_LATER;
    }

    /* TODO: confirm they sent HASH('req1',S) here? */
    evbuffer_drain(inbuf, SHA_DIGEST_LENGTH);

    /* This next piece is HASH('req2', SKEY) xor HASH('req3', S) ...
     * we can get the first half of that (the obufscatedTorrentHash)
     * by building the latter and xor'ing it with what the peer sent us */
    tr_logAddTraceHand(handshake, "reading obfuscated torrent hash...");
    auto req2 = tr_sha1_digest_t{};
    evbuffer_remove(inbuf, std::data(req2), std::size(req2));

    auto const req3 = computeRequestHash(handshake, "req3"sv);
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

    if (auto const* const tor = tr_torrentFindFromObfuscatedHash(handshake->session, obfuscated_hash); tor != nullptr)
    {
        bool const clientIsSeed = tor->isDone();
        bool const peerIsSeed = tr_peerMgrPeerIsSeed(tor, tr_peerIoGetAddress(handshake->io, nullptr));
        tr_logAddTraceHand(
            handshake,
            fmt::format("got INCOMING connection's encrypted handshake for torrent [{}]", tor->name()));
        tr_peerIoSetTorrentHash(handshake->io, tor->infoHash());

        if (clientIsSeed && peerIsSeed)
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

    tr_cryptoDecryptInit(handshake->crypto);

    tr_peerIoReadBytes(handshake->io, inbuf, vc_in, VC_LENGTH);

    tr_peerIoReadUint32(handshake->io, inbuf, &crypto_provide);
    handshake->crypto_provide = crypto_provide;
    tr_logAddTraceHand(handshake, fmt::format("crypto_provide is {}", crypto_provide));

    tr_peerIoReadUint16(handshake->io, inbuf, &padc_len);
    tr_logAddTraceHand(handshake, fmt::format("padc is {}", padc_len));
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

    /* read the throwaway padc */
    auto* const padc = tr_new(char, handshake->pad_c_len);
    tr_peerIoReadBytes(handshake->io, inbuf, padc, handshake->pad_c_len);
    tr_free(padc);

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

    tr_cryptoEncryptInit(handshake->crypto);
    evbuffer* const outbuf = evbuffer_new();

    {
        /* send VC */
        uint8_t vc[VC_LENGTH];
        memset(vc, 0, VC_LENGTH);
        evbuffer_add(outbuf, vc, VC_LENGTH);
        tr_logAddTraceHand(handshake, "sending vc");
    }

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
        tr_peerIoSetEncryption(handshake->io, PEER_ENCRYPTION_NONE);
    }

    tr_logAddTraceHand(handshake, "sending handshake");

    /* send our handshake */
    {
        uint8_t msg[HANDSHAKE_SIZE];

        if (!buildHandshakeMessage(handshake, msg))
        {
            return tr_handshakeDone(handshake, false);
        }

        evbuffer_add(outbuf, msg, sizeof(msg));
        handshake->haveSentBitTorrentHandshake = true;
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

static void tr_handshakeFree(tr_handshake* handshake)
{
    if (handshake->io != nullptr)
    {
        tr_peerIoUnref(handshake->io); /* balanced by the ref in tr_handshakeNew */
    }

    event_free(handshake->timeout_timer);
    tr_free(handshake);
}

static ReadState tr_handshakeDone(tr_handshake* handshake, bool isOK)
{
    tr_logAddTraceHand(handshake, isOK ? "handshakeDone: connected" : "handshakeDone: aborting");
    tr_peerIoSetIOFuncs(handshake->io, nullptr, nullptr, nullptr, nullptr);

    bool const success = fireDoneFunc(handshake, isOK);
    tr_handshakeFree(handshake);
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
    int errcode = errno;
    auto* handshake = static_cast<tr_handshake*>(vhandshake);

    if (io->socket.type == TR_PEER_SOCKET_TYPE_UTP && !tr_peerIoIsIncoming(io) && handshake->state == AWAITING_YB)
    {
        /* This peer probably doesn't speak uTP. */

        auto const hash = tr_peerIoGetTorrentHash(io);
        auto* const tor = hash ? handshake->session->torrents().get(*hash) : nullptr;

        /* Don't mark a peer as non-uTP unless it's really a connect failure. */
        if ((errcode == ETIMEDOUT || errcode == ECONNREFUSED) && tr_isTorrent(tor))
        {
            tr_peerMgrSetUtpFailed(tor, tr_peerIoGetAddress(io, nullptr), true);
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
    tr_peerIo* io,
    tr_encryption_mode encryptionMode,
    tr_handshake_done_func done_func,
    void* done_func_user_data)
{
    tr_session* session = tr_peerIoGetSession(io);

    auto* const handshake = tr_new0(tr_handshake, 1);
    handshake->io = io;
    handshake->crypto = tr_peerIoGetCrypto(io);
    handshake->encryptionMode = encryptionMode;
    handshake->done_func = done_func;
    handshake->done_func_user_data = done_func_user_data;
    handshake->session = session;
    handshake->timeout_timer = evtimer_new(session->event_base, handshakeTimeout, handshake);
    tr_timerAdd(*handshake->timeout_timer, HANDSHAKE_TIMEOUT_SEC, 0);

    tr_peerIoRef(io); /* balanced by the unref in tr_handshakeFree */
    tr_peerIoSetIOFuncs(handshake->io, canRead, nullptr, gotError, handshake);
    tr_peerIoSetEncryption(io, PEER_ENCRYPTION_NONE);

    if (tr_peerIoIsIncoming(handshake->io))
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
