/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <string.h> /* strcmp (), strlen () */

#include <event2/buffer.h>
#include <event2/event.h>

#include "transmission.h"
#include "clients.h"
#include "crypto-utils.h"
#include "handshake.h"
#include "log.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "session.h"
#include "torrent.h"
#include "tr-dht.h"
#include "utils.h"

/* enable LibTransmission extension protocol */
#define ENABLE_LTEP * /
/* fast extensions */
#define ENABLE_FAST * /
/* DHT */
#define ENABLE_DHT * /

/***
****
***/

#define HANDSHAKE_NAME "\023BitTorrent protocol"

enum
{
  /* BitTorrent Handshake Constants */
  HANDSHAKE_NAME_LEN             = 20,
  HANDSHAKE_FLAGS_LEN            = 8,
  HANDSHAKE_SIZE                 = 68,
  INCOMING_HANDSHAKE_LEN         = 48,

  /* Encryption Constants */
  PadA_MAXLEN                    = 512,
  PadB_MAXLEN                    = 512,
  PadC_MAXLEN                    = 512,
  PadD_MAXLEN                    = 512,
  VC_LENGTH                      = 8,
  CRYPTO_PROVIDE_PLAINTEXT       = 1,
  CRYPTO_PROVIDE_CRYPTO          = 2,

  /* how long to wait before giving up on a handshake */
  HANDSHAKE_TIMEOUT_SEC          = 30
};


#ifdef ENABLE_LTEP
 #define HANDSHAKE_HAS_LTEP(bits)(((bits)[5] & 0x10) != 0)
 #define HANDSHAKE_SET_LTEP(bits)((bits)[5] |= 0x10)
#else
 #define HANDSHAKE_HAS_LTEP(bits)(false)
 #define HANDSHAKE_SET_LTEP(bits)((void)0)
#endif

#ifdef ENABLE_FAST
 #define HANDSHAKE_HAS_FASTEXT(bits)(((bits)[7] & 0x04) != 0)
 #define HANDSHAKE_SET_FASTEXT(bits)((bits)[7] |= 0x04)
#else
 #define HANDSHAKE_HAS_FASTEXT(bits)(false)
 #define HANDSHAKE_SET_FASTEXT(bits)((void)0)
#endif

#ifdef ENABLE_DHT
 #define HANDSHAKE_HAS_DHT(bits)(((bits)[7] & 0x01) != 0)
 #define HANDSHAKE_SET_DHT(bits)((bits)[7] |= 0x01)
#else
 #define HANDSHAKE_HAS_DHT(bits)(false)
 #define HANDSHAKE_SET_DHT(bits)((void)0)
#endif

/* http://www.azureuswiki.com/index.php/Extension_negotiation_protocol
   these macros are to be used if both extended messaging and the
   azureus protocol is supported, they indicate which protocol is preferred */
#define HANDSHAKE_GET_EXTPREF(reserved)    ((reserved)[5] & 0x03)
#define HANDSHAKE_SET_EXTPREF(reserved, val)((reserved)[5] |= 0x03 & (val))

/**
***
**/

typedef enum
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

  N_STATES
}
handshake_state_t;

struct tr_handshake
{
  bool                  haveReadAnythingFromPeer;
  bool                  havePeerID;
  bool                  haveSentBitTorrentHandshake;
  tr_peerIo *           io;
  tr_crypto *           crypto;
  tr_session *          session;
  handshake_state_t     state;
  tr_encryption_mode    encryptionMode;
  uint16_t              pad_c_len;
  uint16_t              pad_d_len;
  uint16_t              ia_len;
  uint32_t              crypto_select;
  uint32_t              crypto_provide;
  uint8_t               myReq1[SHA_DIGEST_LENGTH];
  handshakeDoneCB       doneCB;
  void *                doneUserData;
  struct event        * timeout_timer;
};

/**
***
**/

#define dbgmsg(handshake, ...) \
  do { \
    if (tr_logGetDeepEnabled ()) \
      tr_logAddDeep (__FILE__, __LINE__, tr_peerIoGetAddrStr (handshake->io), __VA_ARGS__); \
  } while (0)

static const char*
getStateName (const handshake_state_t state)
{
  static const char * const state_strings[N_STATES] =
    {
      /* AWAITING_HANDSHAKE      */ "awaiting handshake",
      /* AWAITING_PEER_ID        */ "awaiting peer id",
      /* AWAITING_YA             */ "awaiting ya",
      /* AWAITING_PAD_A          */ "awaiting pad a",
      /* AWAITING_CRYPTO_PROVIDE */ "awaiting crypto_provide",
      /* AWAITING_PAD_C          */ "awaiting pad c",
      /* AWAITING_IA             */ "awaiting ia",
      /* AWAITING_PAYLOAD_STREAM */ "awaiting payload stream",
      /* AWAITING_YB             */ "awaiting yb",
      /* AWAITING_VC             */ "awaiting vc",
      /* AWAITING_CRYPTO_SELECT  */ "awaiting crypto select",
      /* AWAITING_PAD_D          */ "awaiting pad d"
  };

  return state<N_STATES ? state_strings[state] : "unknown state";
}

static void
setState (tr_handshake * handshake, handshake_state_t state)
{
  dbgmsg (handshake, "setting to state [%s]", getStateName (state));
  handshake->state = state;
}

static void
setReadState (tr_handshake * handshake, handshake_state_t state)
{
  setState (handshake, state);
}

static bool
buildHandshakeMessage (tr_handshake * handshake, uint8_t * buf)
{
  const unsigned char * peer_id = NULL;
  const uint8_t * torrentHash;
  tr_torrent * tor;
  bool success;

  if ((torrentHash = tr_cryptoGetTorrentHash (handshake->crypto)))
    if ((tor = tr_torrentFindFromHash (handshake->session, torrentHash)))
      peer_id = tr_torrentGetPeerId (tor);

  if (peer_id == NULL)
    {
      success = false;
    }
  else
    {
      uint8_t * walk = buf;

      memcpy (walk, HANDSHAKE_NAME, HANDSHAKE_NAME_LEN);
      walk += HANDSHAKE_NAME_LEN;
      memset (walk, 0, HANDSHAKE_FLAGS_LEN);
      HANDSHAKE_SET_LTEP (walk);
      HANDSHAKE_SET_FASTEXT (walk);

      /* Note that this doesn't depend on whether the torrent is private.
       * We don't accept DHT peers for a private torrent,
       * but we participate in the DHT regardless. */
      if (tr_dhtEnabled (handshake->session))
        HANDSHAKE_SET_DHT (walk);

      walk += HANDSHAKE_FLAGS_LEN;
      memcpy (walk, torrentHash, SHA_DIGEST_LENGTH);
      walk += SHA_DIGEST_LENGTH;
      memcpy (walk, peer_id, PEER_ID_LEN);
      walk += PEER_ID_LEN;

      assert (walk - buf == HANDSHAKE_SIZE);
      success = true;
    }

  return success;
}

static ReadState tr_handshakeDone (tr_handshake * handshake,
                                   bool           isConnected);

typedef enum
{
  HANDSHAKE_OK,
  HANDSHAKE_ENCRYPTION_WRONG,
  HANDSHAKE_BAD_TORRENT,
  HANDSHAKE_PEER_IS_SELF,
}
handshake_parse_err_t;

static handshake_parse_err_t
parseHandshake (tr_handshake *    handshake,
                struct evbuffer * inbuf)
{
  uint8_t name[HANDSHAKE_NAME_LEN];
  uint8_t reserved[HANDSHAKE_FLAGS_LEN];
  uint8_t hash[SHA_DIGEST_LENGTH];
  tr_torrent * tor;
  uint8_t peer_id[PEER_ID_LEN];

  dbgmsg (handshake, "payload: need %d, got %zu",
          HANDSHAKE_SIZE, evbuffer_get_length (inbuf));

  if (evbuffer_get_length (inbuf) < HANDSHAKE_SIZE)
    return HANDSHAKE_ENCRYPTION_WRONG;

  /* confirm the protocol */
  tr_peerIoReadBytes (handshake->io, inbuf, name, HANDSHAKE_NAME_LEN);
  if (memcmp (name, HANDSHAKE_NAME, HANDSHAKE_NAME_LEN))
    return HANDSHAKE_ENCRYPTION_WRONG;

  /* read the reserved bytes */
  tr_peerIoReadBytes (handshake->io, inbuf, reserved, HANDSHAKE_FLAGS_LEN);

  /* torrent hash */
  tr_peerIoReadBytes (handshake->io, inbuf, hash, sizeof (hash));
  assert (tr_peerIoHasTorrentHash (handshake->io));
  if (!tr_torrentExists (handshake->session, hash)
      || memcmp (hash, tr_peerIoGetTorrentHash (handshake->io), SHA_DIGEST_LENGTH))
    {
      dbgmsg (handshake, "peer returned the wrong hash. wtf?");
      return HANDSHAKE_BAD_TORRENT;
    }

  /* peer_id */
  tr_peerIoReadBytes (handshake->io, inbuf, peer_id, sizeof (peer_id));
  tr_peerIoSetPeersId (handshake->io, peer_id);

  /* peer id */
  handshake->havePeerID = true;
  dbgmsg (handshake, "peer-id is [%*.*s]", PEER_ID_LEN, PEER_ID_LEN, peer_id);

  tor = tr_torrentFindFromHash (handshake->session, hash);
  if (!memcmp (peer_id, tr_torrentGetPeerId(tor), PEER_ID_LEN))
    {
      dbgmsg (handshake, "streuth!  we've connected to ourselves.");
      return HANDSHAKE_PEER_IS_SELF;
    }

  /**
  *** Extensions
  **/

  tr_peerIoEnableDHT  (handshake->io, HANDSHAKE_HAS_DHT    (reserved));
  tr_peerIoEnableLTEP (handshake->io, HANDSHAKE_HAS_LTEP    (reserved));
  tr_peerIoEnableFEXT (handshake->io, HANDSHAKE_HAS_FASTEXT (reserved));

  return HANDSHAKE_OK;
}

/***
****
****  OUTGOING CONNECTIONS
****
***/

/* 1 A->B: Diffie Hellman Ya, PadA */
static void
sendYa (tr_handshake * handshake)
{
  int len;
  const uint8_t * public_key;
  char outbuf[ KEY_LEN + PadA_MAXLEN ];
  char *walk = outbuf;

  /* add our public key (Ya) */
  public_key = tr_cryptoGetMyPublicKey (handshake->crypto, &len);
  assert (len == KEY_LEN);
  assert (public_key);
  memcpy (walk, public_key, len);
  walk += len;

  /* add some bullshit padding */
  len = tr_rand_int (PadA_MAXLEN);
  tr_rand_buffer (walk, len);
  walk += len;

  /* send it */
  setReadState (handshake, AWAITING_YB);
  tr_peerIoWriteBytes (handshake->io, outbuf, walk - outbuf, false);
}

static uint32_t
getCryptoProvide (const tr_handshake * handshake)
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

static uint32_t
getCryptoSelect (const tr_handshake * handshake,
                 uint32_t             crypto_provide)
{
  uint32_t choices[2];
  int i;
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

  for (i=0; i<nChoices; ++i)
    if (crypto_provide & choices[i])
      return choices[i];

  return 0;
}

static void
computeRequestHash (const tr_handshake * handshake,
                    const char         * name,
                    uint8_t            * hash)
{
  tr_cryptoSecretKeySha1 (handshake->crypto, name, 4, NULL, 0, hash);
}

static ReadState
readYb (tr_handshake * handshake, struct evbuffer * inbuf)
{
  bool isEncrypted;
  uint8_t yb[KEY_LEN];
  struct evbuffer * outbuf;
  size_t needlen = HANDSHAKE_NAME_LEN;

  if (evbuffer_get_length (inbuf) < needlen)
    return READ_LATER;

  isEncrypted = memcmp (evbuffer_pullup (inbuf, HANDSHAKE_NAME_LEN), HANDSHAKE_NAME, HANDSHAKE_NAME_LEN) != 0;
  if (isEncrypted)
    {
      needlen = KEY_LEN;

      if (evbuffer_get_length (inbuf) < needlen)
        return READ_LATER;
    }

  dbgmsg (handshake, "got an %s handshake", (isEncrypted ? "encrypted" : "plain"));

  tr_peerIoSetEncryption (handshake->io, isEncrypted ? PEER_ENCRYPTION_RC4
                                                     : PEER_ENCRYPTION_NONE);
  if (!isEncrypted)
    {
      setState (handshake, AWAITING_HANDSHAKE);
      return READ_NOW;
    }

  handshake->haveReadAnythingFromPeer = true;

  /* compute the secret */
  evbuffer_remove (inbuf, yb, KEY_LEN);
  if (!tr_cryptoComputeSecret (handshake->crypto, yb))
    return tr_handshakeDone (handshake, false);

  /* now send these: HASH ('req1', S), HASH ('req2', SKEY) xor HASH ('req3', S),
   * ENCRYPT (VC, crypto_provide, len (PadC), PadC, len (IA)), ENCRYPT (IA) */
  outbuf = evbuffer_new ();

  /* HASH ('req1', S) */
  {
    uint8_t req1[SHA_DIGEST_LENGTH];
    computeRequestHash (handshake, "req1", req1);
    evbuffer_add (outbuf, req1, SHA_DIGEST_LENGTH);
  }

  /* HASH ('req2', SKEY) xor HASH ('req3', S) */
  {
    int i;
    uint8_t req2[SHA_DIGEST_LENGTH];
    uint8_t req3[SHA_DIGEST_LENGTH];
    uint8_t buf[SHA_DIGEST_LENGTH];

    tr_sha1 (req2, "req2", 4, tr_cryptoGetTorrentHash (handshake->crypto), SHA_DIGEST_LENGTH, NULL);
    computeRequestHash (handshake, "req3", req3);

    for (i=0; i<SHA_DIGEST_LENGTH; ++i)
      buf[i] = req2[i] ^ req3[i];

    evbuffer_add (outbuf, buf, SHA_DIGEST_LENGTH);
  }

  /* ENCRYPT (VC, crypto_provide, len (PadC), PadC
   * PadC is reserved for future extensions to the handshake...
   * standard practice at this time is for it to be zero-length */
  {
    uint8_t vc[VC_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    tr_peerIoWriteBuf (handshake->io, outbuf, false);
    tr_cryptoEncryptInit (handshake->crypto);
    tr_peerIoSetEncryption (handshake->io, PEER_ENCRYPTION_RC4);

    evbuffer_add        (outbuf, vc, VC_LENGTH);
    evbuffer_add_uint32 (outbuf, getCryptoProvide (handshake));
    evbuffer_add_uint16 (outbuf, 0);
  }

  /* ENCRYPT len (IA)), ENCRYPT (IA) */
  {
    uint8_t msg[HANDSHAKE_SIZE];
    if (!buildHandshakeMessage (handshake, msg))
      return tr_handshakeDone (handshake, false);

    evbuffer_add_uint16 (outbuf, sizeof (msg));
    evbuffer_add        (outbuf, msg, sizeof (msg));

    handshake->haveSentBitTorrentHandshake = true;
  }

  /* send it */
  tr_cryptoDecryptInit (handshake->crypto);
  setReadState (handshake, AWAITING_VC);
  tr_peerIoWriteBuf (handshake->io, outbuf, false);

  /* cleanup */
  evbuffer_free (outbuf);
  return READ_LATER;
}

static ReadState
readVC (tr_handshake    * handshake,
        struct evbuffer * inbuf)
{
  uint8_t tmp[VC_LENGTH];
  const int key_len = VC_LENGTH;
  const uint8_t key[VC_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0 };

  /* note: this works w/o having to `unwind' the buffer if
   * we read too much, but it is pretty brute-force.
   * it would be nice to make this cleaner. */
  for (;;)
    {
      if (evbuffer_get_length (inbuf) < VC_LENGTH)
        {
          dbgmsg (handshake, "not enough bytes... returning read_more");
          return READ_LATER;
        }

      memcpy (tmp, evbuffer_pullup (inbuf, key_len), key_len);
      tr_cryptoDecryptInit (handshake->crypto);
      tr_cryptoDecrypt (handshake->crypto, key_len, tmp, tmp);
      if (!memcmp (tmp, key, key_len))
        break;

      evbuffer_drain (inbuf, 1);
    }

  dbgmsg (handshake, "got it!");
  evbuffer_drain (inbuf, key_len);
  setState (handshake, AWAITING_CRYPTO_SELECT);
  return READ_NOW;
}

static ReadState
readCryptoSelect (tr_handshake    * handshake,
                  struct evbuffer * inbuf)
{
  uint16_t pad_d_len;
  uint32_t crypto_select;
  static const size_t needlen = sizeof (uint32_t) + sizeof (uint16_t);

  if (evbuffer_get_length (inbuf) < needlen)
    return READ_LATER;

  tr_peerIoReadUint32 (handshake->io, inbuf, &crypto_select);
  handshake->crypto_select = crypto_select;
  dbgmsg (handshake, "crypto select is %d", (int)crypto_select);
  if (!(crypto_select & getCryptoProvide (handshake)))
    {
      dbgmsg (handshake, "peer selected an encryption option we didn't offer");
      return tr_handshakeDone (handshake, false);
    }

  tr_peerIoReadUint16 (handshake->io, inbuf, &pad_d_len);
  dbgmsg (handshake, "pad_d_len is %d", (int)pad_d_len);

  if (pad_d_len > 512)
    {
      dbgmsg (handshake, "encryption handshake: pad_d_len is too long");
      return tr_handshakeDone (handshake, false);
    }

  handshake->pad_d_len = pad_d_len;

  setState (handshake, AWAITING_PAD_D);
  return READ_NOW;
}

static ReadState
readPadD (tr_handshake    * handshake,
          struct evbuffer * inbuf)
{
  const size_t needlen = handshake->pad_d_len;

  dbgmsg (handshake, "pad d: need %zu, got %zu",
          needlen, evbuffer_get_length (inbuf));
  if (evbuffer_get_length (inbuf) < needlen)
    return READ_LATER;

  tr_peerIoDrain (handshake->io, inbuf, needlen);

  tr_peerIoSetEncryption (handshake->io, handshake->crypto_select);

  setState (handshake, AWAITING_HANDSHAKE);
  return READ_NOW;
}

/***
****
****  INCOMING CONNECTIONS
****
***/

static ReadState
readHandshake (tr_handshake    * handshake,
               struct evbuffer * inbuf)
{
  uint8_t pstrlen;
  uint8_t pstr[20];
  uint8_t reserved[HANDSHAKE_FLAGS_LEN];
  uint8_t hash[SHA_DIGEST_LENGTH];

  dbgmsg (handshake, "payload: need %d, got %zu",
          INCOMING_HANDSHAKE_LEN, evbuffer_get_length (inbuf));

  if (evbuffer_get_length (inbuf) < INCOMING_HANDSHAKE_LEN)
    return READ_LATER;

  handshake->haveReadAnythingFromPeer = true;

  pstrlen = evbuffer_pullup (inbuf, 1)[0]; /* peek, don't read. We may be
                                              handing inbuf to AWAITING_YA */

  if (pstrlen == 19) /* unencrypted */
    {
      tr_peerIoSetEncryption (handshake->io, PEER_ENCRYPTION_NONE);

      if (handshake->encryptionMode == TR_ENCRYPTION_REQUIRED)
        {
          dbgmsg (handshake, "peer is unencrypted, and we're disallowing that");
          return tr_handshakeDone (handshake, false);
        }
    }
  else /* encrypted or corrupt */
    {
      tr_peerIoSetEncryption (handshake->io, PEER_ENCRYPTION_RC4);

      if (tr_peerIoIsIncoming (handshake->io))
        {
          dbgmsg (handshake, "I think peer is sending us an encrypted handshake...");
          setState (handshake, AWAITING_YA);
          return READ_NOW;
        }

      tr_cryptoDecrypt (handshake->crypto, 1, &pstrlen, &pstrlen);

      if (pstrlen != 19)
        {
          dbgmsg (handshake, "I think peer has sent us a corrupt handshake...");
          return tr_handshakeDone (handshake, false);
        }
    }

  evbuffer_drain (inbuf, 1);

  /* pstr (BitTorrent) */
  assert (pstrlen == 19);
  tr_peerIoReadBytes (handshake->io, inbuf, pstr, pstrlen);
  pstr[pstrlen] = '\0';
  if (memcmp (pstr, "BitTorrent protocol", 19))
    return tr_handshakeDone (handshake, false);

  /* reserved bytes */
  tr_peerIoReadBytes (handshake->io, inbuf, reserved, sizeof (reserved));

  /**
  *** Extensions
  **/

  tr_peerIoEnableDHT  (handshake->io, HANDSHAKE_HAS_DHT     (reserved));
  tr_peerIoEnableLTEP (handshake->io, HANDSHAKE_HAS_LTEP    (reserved));
  tr_peerIoEnableFEXT (handshake->io, HANDSHAKE_HAS_FASTEXT (reserved));

  /* torrent hash */
  tr_peerIoReadBytes (handshake->io, inbuf, hash, sizeof (hash));
  if (tr_peerIoIsIncoming (handshake->io))
    {
      if (!tr_torrentExists (handshake->session, hash))
        {
          dbgmsg (handshake, "peer is trying to connect to us for a torrent we don't have.");
          return tr_handshakeDone (handshake, false);
        }
      else
        {
          assert (!tr_peerIoHasTorrentHash (handshake->io));
          tr_peerIoSetTorrentHash (handshake->io, hash);
        }
    }
  else /* outgoing */
    {
      assert (tr_peerIoHasTorrentHash (handshake->io));

      if (memcmp (hash, tr_peerIoGetTorrentHash (handshake->io), SHA_DIGEST_LENGTH))
        {
          dbgmsg (handshake, "peer returned the wrong hash. wtf?");
          return tr_handshakeDone (handshake, false);
        }
    }

  /**
  ***  If it's an incoming message, we need to send a response handshake
  **/

  if (!handshake->haveSentBitTorrentHandshake)
    {
      uint8_t msg[HANDSHAKE_SIZE];
      if (!buildHandshakeMessage (handshake, msg))
        return tr_handshakeDone (handshake, false);
      tr_peerIoWriteBytes (handshake->io, msg, sizeof (msg), false);
      handshake->haveSentBitTorrentHandshake = true;
    }

  setReadState (handshake, AWAITING_PEER_ID);
  return READ_NOW;
}

static ReadState
readPeerId (tr_handshake    * handshake,
            struct evbuffer * inbuf)
{
  bool connected_to_self;
  char client[128];
  uint8_t peer_id[PEER_ID_LEN];
  tr_torrent * tor;

  if (evbuffer_get_length (inbuf) < PEER_ID_LEN)
    return READ_LATER;

  /* peer id */
  tr_peerIoReadBytes (handshake->io, inbuf, peer_id, PEER_ID_LEN);
  tr_peerIoSetPeersId (handshake->io, peer_id);
  handshake->havePeerID = true;
  tr_clientForId (client, sizeof (client), peer_id);
  dbgmsg (handshake, "peer-id is [%s] ... isIncoming is %d", client,
          tr_peerIoIsIncoming (handshake->io));

  /* if we've somehow connected to ourselves, don't keep the connection */
  tor = tr_torrentFindFromHash (handshake->session, tr_peerIoGetTorrentHash (handshake->io));
  connected_to_self = (tor != NULL) && !memcmp (peer_id, tr_torrentGetPeerId(tor), PEER_ID_LEN);

  return tr_handshakeDone (handshake, !connected_to_self);
}

static ReadState
readYa (tr_handshake    * handshake,
        struct evbuffer * inbuf)
{
  uint8_t ya[KEY_LEN];
  uint8_t * walk, outbuf[KEY_LEN + PadB_MAXLEN];
  const uint8_t * myKey;
  int len;

  dbgmsg (handshake, "in readYa... need %d, have %zu",
          KEY_LEN, evbuffer_get_length (inbuf));
  if (evbuffer_get_length (inbuf) < KEY_LEN)
    return READ_LATER;

  /* read the incoming peer's public key */
  evbuffer_remove (inbuf, ya, KEY_LEN);
  if (!tr_cryptoComputeSecret (handshake->crypto, ya))
    return tr_handshakeDone (handshake, false);

  computeRequestHash (handshake, "req1", handshake->myReq1);

  /* send our public key to the peer */
  dbgmsg (handshake, "sending B->A: Diffie Hellman Yb, PadB");
  walk = outbuf;
  myKey = tr_cryptoGetMyPublicKey (handshake->crypto, &len);
  memcpy (walk, myKey, len);
  walk += len;
  len = tr_rand_int (PadB_MAXLEN);
  tr_rand_buffer (walk, len);
  walk += len;

  setReadState (handshake, AWAITING_PAD_A);
  tr_peerIoWriteBytes (handshake->io, outbuf, walk - outbuf, false);
  return READ_NOW;
}

static ReadState
readPadA (tr_handshake * handshake, struct evbuffer * inbuf)
{
  /* resynchronizing on HASH ('req1',S) */
  struct evbuffer_ptr ptr = evbuffer_search (inbuf, (const char*)handshake->myReq1, SHA_DIGEST_LENGTH, NULL);

  if (ptr.pos != -1) /* match */
    {
      evbuffer_drain (inbuf, ptr.pos);
      dbgmsg (handshake, "found it... looking setting to awaiting_crypto_provide");
      setState (handshake, AWAITING_CRYPTO_PROVIDE);
      return READ_NOW;
    }
  else
    {
      const size_t len = evbuffer_get_length (inbuf);
      if (len > SHA_DIGEST_LENGTH)
        evbuffer_drain (inbuf, len - SHA_DIGEST_LENGTH);
      return READ_LATER;
    }
}

static ReadState
readCryptoProvide (tr_handshake    * handshake,
                   struct evbuffer * inbuf)
{
  /* HASH ('req2', SKEY) xor HASH ('req3', S), ENCRYPT (VC, crypto_provide, len (PadC)) */

  int i;
  uint8_t vc_in[VC_LENGTH];
  uint8_t req2[SHA_DIGEST_LENGTH];
  uint8_t req3[SHA_DIGEST_LENGTH];
  uint8_t obfuscatedTorrentHash[SHA_DIGEST_LENGTH];
  uint16_t padc_len = 0;
  uint32_t crypto_provide = 0;
  tr_torrent * tor;
  const size_t needlen = SHA_DIGEST_LENGTH /* HASH ('req1',s) */
                       + SHA_DIGEST_LENGTH /* HASH ('req2', SKEY) xor HASH ('req3', S) */
                       + VC_LENGTH
                       + sizeof (crypto_provide)
                       + sizeof (padc_len);

  if (evbuffer_get_length (inbuf) < needlen)
    return READ_LATER;

  /* TODO: confirm they sent HASH ('req1',S) here? */
  evbuffer_drain (inbuf, SHA_DIGEST_LENGTH);

  /* This next piece is HASH ('req2', SKEY) xor HASH ('req3', S) ...
   * we can get the first half of that (the obufscatedTorrentHash)
   * by building the latter and xor'ing it with what the peer sent us */
  dbgmsg (handshake, "reading obfuscated torrent hash...");
  evbuffer_remove (inbuf, req2, SHA_DIGEST_LENGTH);
  computeRequestHash (handshake, "req3", req3);
  for (i=0; i<SHA_DIGEST_LENGTH; ++i)
    obfuscatedTorrentHash[i] = req2[i] ^ req3[i];
  if ((tor = tr_torrentFindFromObfuscatedHash (handshake->session, obfuscatedTorrentHash)))
    {
      const bool clientIsSeed = tr_torrentIsSeed (tor);
      const bool peerIsSeed = tr_peerMgrPeerIsSeed (tor, tr_peerIoGetAddress (handshake->io, NULL));
      dbgmsg (handshake, "got INCOMING connection's encrypted handshake for torrent [%s]", tr_torrentName (tor));
      tr_peerIoSetTorrentHash (handshake->io, tor->info.hash);

      if (clientIsSeed && peerIsSeed)
        {
          dbgmsg (handshake, "another seed tried to reconnect to us!");
          return tr_handshakeDone (handshake, false);
        }
    }
  else
    {
      dbgmsg (handshake, "can't find that torrent...");
      return tr_handshakeDone (handshake, false);
    }

  /* next part: ENCRYPT (VC, crypto_provide, len (PadC), */

  tr_cryptoDecryptInit (handshake->crypto);

  tr_peerIoReadBytes (handshake->io, inbuf, vc_in, VC_LENGTH);

  tr_peerIoReadUint32 (handshake->io, inbuf, &crypto_provide);
  handshake->crypto_provide = crypto_provide;
  dbgmsg (handshake, "crypto_provide is %d", (int)crypto_provide);

  tr_peerIoReadUint16 (handshake->io, inbuf, &padc_len);
  dbgmsg (handshake, "padc is %d", (int)padc_len);
  handshake->pad_c_len = padc_len;
  setState (handshake, AWAITING_PAD_C);
  return READ_NOW;
}

static ReadState
readPadC (tr_handshake    * handshake,
          struct evbuffer * inbuf)
{
  char * padc;
  uint16_t ia_len;
  const size_t needlen = handshake->pad_c_len + sizeof (uint16_t);

  if (evbuffer_get_length (inbuf) < needlen)
    return READ_LATER;

  /* read the throwaway padc */
  padc = tr_new (char, handshake->pad_c_len);
  tr_peerIoReadBytes (handshake->io, inbuf, padc, handshake->pad_c_len);
  tr_free (padc);

  /* read ia_len */
  tr_peerIoReadUint16 (handshake->io, inbuf, &ia_len);
  dbgmsg (handshake, "ia_len is %d", (int)ia_len);
  handshake->ia_len = ia_len;
  setState (handshake, AWAITING_IA);
  return READ_NOW;
}

static ReadState
readIA (tr_handshake    * handshake,
        struct evbuffer * inbuf)
{
  const size_t needlen = handshake->ia_len;
  struct evbuffer * outbuf;
  uint32_t crypto_select;

  dbgmsg (handshake, "reading IA... have %zu, need %zu",
          evbuffer_get_length (inbuf), needlen);
  if (evbuffer_get_length (inbuf) < needlen)
    return READ_LATER;

  /**
  ***  B->A: ENCRYPT (VC, crypto_select, len (padD), padD), ENCRYPT2 (Payload Stream)
  **/

  tr_cryptoEncryptInit (handshake->crypto);
  outbuf = evbuffer_new ();

  {
    /* send VC */
    uint8_t vc[VC_LENGTH];
    memset (vc, 0, VC_LENGTH);
    evbuffer_add (outbuf, vc, VC_LENGTH);
    dbgmsg (handshake, "sending vc");
  }

  /* send crypto_select */
  crypto_select = getCryptoSelect (handshake, handshake->crypto_provide);
  if (crypto_select)
    {
      dbgmsg (handshake, "selecting crypto mode '%d'", (int)crypto_select);
      evbuffer_add_uint32 (outbuf, crypto_select);
    }
  else
    {
      dbgmsg (handshake, "peer didn't offer an encryption mode we like.");
      evbuffer_free (outbuf);
      return tr_handshakeDone (handshake, false);
    }

  dbgmsg (handshake, "sending pad d");
  /* ENCRYPT (VC, crypto_provide, len (PadD), PadD
   * PadD is reserved for future extensions to the handshake...
   * standard practice at this time is for it to be zero-length */
  {
    const uint16_t len = 0;
    evbuffer_add_uint16 (outbuf, len);
  }

  /* maybe de-encrypt our connection */
  if (crypto_select == CRYPTO_PROVIDE_PLAINTEXT)
    {
      tr_peerIoWriteBuf (handshake->io, outbuf, false);
      tr_peerIoSetEncryption (handshake->io, PEER_ENCRYPTION_NONE);
    }

  dbgmsg (handshake, "sending handshake");
  /* send our handshake */
  {
    uint8_t msg[HANDSHAKE_SIZE];
    if (!buildHandshakeMessage (handshake, msg))
      return tr_handshakeDone (handshake, false);

    evbuffer_add (outbuf, msg, sizeof (msg));
    handshake->haveSentBitTorrentHandshake = true;
  }

  /* send it out */
  tr_peerIoWriteBuf (handshake->io, outbuf, false);
  evbuffer_free (outbuf);

  /* now await the handshake */
  setState (handshake, AWAITING_PAYLOAD_STREAM);
  return READ_NOW;
}

static ReadState
readPayloadStream (tr_handshake    * handshake,
                   struct evbuffer * inbuf)
{
  handshake_parse_err_t i;
  const size_t needlen = HANDSHAKE_SIZE;

  dbgmsg (handshake, "reading payload stream... have %zu, need %zu",
          evbuffer_get_length (inbuf), needlen);
  if (evbuffer_get_length (inbuf) < needlen)
    return READ_LATER;

  /* parse the handshake ... */
  i = parseHandshake (handshake, inbuf);
  dbgmsg (handshake, "parseHandshake returned %d", i);
  if (i != HANDSHAKE_OK)
    return tr_handshakeDone (handshake, false);

  /* we've completed the BT handshake... pass the work on to peer-msgs */
  return tr_handshakeDone (handshake, true);
}

/***
****
****
****
***/

static ReadState
canRead (struct tr_peerIo * io, void * arg, size_t * piece)
{
  ReadState ret;
  tr_handshake * handshake = arg;
  struct evbuffer * inbuf = tr_peerIoGetReadBuffer (io);
  bool readyForMore = true;

  assert (tr_isPeerIo (io));

  /* no piece data in handshake */
  *piece = 0;

  dbgmsg (handshake, "handling canRead; state is [%s]",
          getStateName (handshake->state));

  while (readyForMore)
    {
      switch (handshake->state)
        {
          case AWAITING_HANDSHAKE:
            ret = readHandshake (handshake, inbuf);
            break;

          case AWAITING_PEER_ID:
            ret = readPeerId (handshake, inbuf);
            break;

          case AWAITING_YA:
            ret = readYa (handshake, inbuf);
            break;

          case AWAITING_PAD_A:
            ret = readPadA (handshake, inbuf);
            break;

          case AWAITING_CRYPTO_PROVIDE:
            ret = readCryptoProvide (handshake, inbuf);
            break;

          case AWAITING_PAD_C:
            ret = readPadC (handshake, inbuf);
            break;

          case AWAITING_IA:
            ret = readIA (handshake, inbuf);
            break;

          case AWAITING_PAYLOAD_STREAM:
            ret = readPayloadStream (handshake, inbuf);
            break;

          case AWAITING_YB:
            ret = readYb (handshake, inbuf);
            break;

          case AWAITING_VC:
            ret = readVC (handshake, inbuf);
            break;

          case AWAITING_CRYPTO_SELECT:
            ret = readCryptoSelect (handshake, inbuf);
            break;

          case AWAITING_PAD_D:
            ret = readPadD (handshake, inbuf);
            break;

          default:
            assert (0);
        }

      if (ret != READ_NOW)
        readyForMore = false;
      else if (handshake->state == AWAITING_PAD_C)
        readyForMore = evbuffer_get_length (inbuf) >= handshake->pad_c_len;
      else if (handshake->state == AWAITING_PAD_D)
        readyForMore = evbuffer_get_length (inbuf) >= handshake->pad_d_len;
      else if (handshake->state == AWAITING_IA)
        readyForMore = evbuffer_get_length (inbuf) >= handshake->ia_len;
    }

  return ret;
}

static bool
fireDoneFunc (tr_handshake * handshake, bool isConnected)
{
  const uint8_t * peer_id = isConnected && handshake->havePeerID
                          ? tr_peerIoGetPeersId (handshake->io)
                          : NULL;
  const bool success = (*handshake->doneCB)(handshake,
                                            handshake->io,
                                            handshake->haveReadAnythingFromPeer,
                                            isConnected,
                                            peer_id,
                                            handshake->doneUserData);

  return success;
}

static void
tr_handshakeFree (tr_handshake * handshake)
{
  if (handshake->io)
    tr_peerIoUnref (handshake->io); /* balanced by the ref in tr_handshakeNew */

  event_free (handshake->timeout_timer);
  tr_free (handshake);
}

static ReadState
tr_handshakeDone (tr_handshake * handshake, bool isOK)
{
  bool success;

  dbgmsg (handshake, "handshakeDone: %s", isOK ? "connected" : "aborting");
  tr_peerIoSetIOFuncs (handshake->io, NULL, NULL, NULL, NULL);

  success = fireDoneFunc (handshake, isOK);

  tr_handshakeFree (handshake);

  return success ? READ_LATER : READ_ERR;
}

void
tr_handshakeAbort (tr_handshake * handshake)
{
  if (handshake != NULL)
    tr_handshakeDone (handshake, false);
}

static void
gotError (tr_peerIo  * io,
          short        what,
          void       * vhandshake)
{
  int errcode = errno;
  tr_handshake * handshake = vhandshake;

  if (io->utp_socket && !io->isIncoming && handshake->state == AWAITING_YB)
    {
      /* This peer probably doesn't speak uTP. */

      tr_torrent *tor;

      if (tr_peerIoHasTorrentHash (io))
        tor = tr_torrentFindFromHash (handshake->session, tr_peerIoGetTorrentHash (io));
      else
        tor = NULL;

      /* Don't mark a peer as non-uTP unless it's really a connect failure. */
      if ((errcode == ETIMEDOUT || errcode == ECONNREFUSED) && tr_isTorrent(tor))
        tr_peerMgrSetUtpFailed (tor, tr_peerIoGetAddress (io, NULL), true);

      if (!tr_peerIoReconnect (handshake->io))
        {
          uint8_t msg[HANDSHAKE_SIZE];
          buildHandshakeMessage (handshake, msg);
          handshake->haveSentBitTorrentHandshake = true;
          setReadState (handshake, AWAITING_HANDSHAKE);
          tr_peerIoWriteBytes (handshake->io, msg, sizeof (msg), false);
        }
    }

  /* if the error happened while we were sending a public key, we might
   * have encountered a peer that doesn't do encryption... reconnect and
   * try a plaintext handshake */
  if (((handshake->state == AWAITING_YB) || (handshake->state == AWAITING_VC))
      && (handshake->encryptionMode != TR_ENCRYPTION_REQUIRED)
      && (!tr_peerIoReconnect (handshake->io)))
    {
      uint8_t msg[HANDSHAKE_SIZE];

      dbgmsg (handshake, "handshake failed, trying plaintext...");
      buildHandshakeMessage (handshake, msg);
      handshake->haveSentBitTorrentHandshake = true;
      setReadState (handshake, AWAITING_HANDSHAKE);
      tr_peerIoWriteBytes (handshake->io, msg, sizeof (msg), false);
    }
  else
    {
      dbgmsg (handshake, "libevent got an error what==%d, errno=%d (%s)",
                         (int)what, errno, tr_strerror (errno));
      tr_handshakeDone (handshake, false);
    }
}

/**
***
**/

static void
handshakeTimeout (evutil_socket_t foo UNUSED, short bar UNUSED, void * handshake)
{
  tr_handshakeAbort (handshake);
}

tr_handshake*
tr_handshakeNew (tr_peerIo           * io,
                 tr_encryption_mode    encryptionMode,
                 handshakeDoneCB       doneCB,
                 void                * doneUserData)
{
  tr_handshake * handshake;
  tr_session * session = tr_peerIoGetSession (io);

  handshake = tr_new0 (tr_handshake, 1);
  handshake->io = io;
  handshake->crypto = tr_peerIoGetCrypto (io);
  handshake->encryptionMode = encryptionMode;
  handshake->doneCB = doneCB;
  handshake->doneUserData = doneUserData;
  handshake->session = session;
  handshake->timeout_timer = evtimer_new (session->event_base, handshakeTimeout, handshake);
  tr_timerAdd (handshake->timeout_timer, HANDSHAKE_TIMEOUT_SEC, 0);

  tr_peerIoRef (io); /* balanced by the unref in tr_handshakeFree */
  tr_peerIoSetIOFuncs (handshake->io, canRead, NULL, gotError, handshake);
  tr_peerIoSetEncryption (io, PEER_ENCRYPTION_NONE);

  if (tr_peerIoIsIncoming (handshake->io))
    {
      setReadState (handshake, AWAITING_HANDSHAKE);
    }
  else if (encryptionMode != TR_CLEAR_PREFERRED)
    {
      sendYa (handshake);
    }
  else
    {
      uint8_t msg[HANDSHAKE_SIZE];
      buildHandshakeMessage (handshake, msg);

      handshake->haveSentBitTorrentHandshake = true;
      setReadState (handshake, AWAITING_HANDSHAKE);
      tr_peerIoWriteBytes (handshake->io, msg, sizeof (msg), false);
    }

  return handshake;
}

struct tr_peerIo*
tr_handshakeGetIO (tr_handshake * handshake)
{
  assert (handshake != NULL);
  assert (handshake->io != NULL);

  return handshake->io;
}

struct tr_peerIo*
tr_handshakeStealIO (tr_handshake * handshake)
{
  struct tr_peerIo * io;

  assert (handshake != NULL);
  assert (handshake->io != NULL);

  io = handshake->io;
  handshake->io = NULL;
  return io;
}

const tr_address *
tr_handshakeGetAddr (const struct tr_handshake * handshake,
                     tr_port                   * port)
{
  assert (handshake != NULL);
  assert (handshake->io != NULL);

  return tr_peerIoGetAddress (handshake->io, port);
}

