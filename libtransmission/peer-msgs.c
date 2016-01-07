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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

#include "transmission.h"
#include "cache.h"
#include "completion.h"
#include "file.h"
#include "log.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-msgs.h"
#include "session.h"
#include "torrent.h"
#include "torrent-magnet.h"
#include "tr-dht.h"
#include "utils.h"
#include "variant.h"
#include "version.h"

#ifndef EBADMSG
 #define EBADMSG EINVAL
#endif

/**
***
**/

enum
{
  BT_CHOKE                = 0,
  BT_UNCHOKE              = 1,
  BT_INTERESTED           = 2,
  BT_NOT_INTERESTED       = 3,
  BT_HAVE                 = 4,
  BT_BITFIELD             = 5,
  BT_REQUEST              = 6,
  BT_PIECE                = 7,
  BT_CANCEL               = 8,
  BT_PORT                 = 9,

  BT_FEXT_SUGGEST         = 13,
  BT_FEXT_HAVE_ALL        = 14,
  BT_FEXT_HAVE_NONE       = 15,
  BT_FEXT_REJECT          = 16,
  BT_FEXT_ALLOWED_FAST    = 17,

  BT_LTEP                 = 20,

  LTEP_HANDSHAKE          = 0,

  UT_PEX_ID               = 1,
  UT_METADATA_ID          = 3,

  MAX_PEX_PEER_COUNT      = 50,

  MIN_CHOKE_PERIOD_SEC    = 10,

  /* idle seconds before we send a keepalive */
  KEEPALIVE_INTERVAL_SECS = 100,

  PEX_INTERVAL_SECS       = 90, /* sec between sendPex () calls */

  REQQ                    = 512,

  METADATA_REQQ           = 64,

  MAGIC_NUMBER            = 21549,

  /* used in lowering the outMessages queue period */
  IMMEDIATE_PRIORITY_INTERVAL_SECS = 0,
  HIGH_PRIORITY_INTERVAL_SECS = 2,
  LOW_PRIORITY_INTERVAL_SECS = 10,

  /* number of pieces we'll allow in our fast set */
  MAX_FAST_SET_SIZE = 3,

  /* how many blocks to keep prefetched per peer */
  PREFETCH_SIZE = 18,

  /* when we're making requests from another peer,
     batch them together to send enough requests to
     meet our bandwidth goals for the next N seconds */
  REQUEST_BUF_SECS = 10,

  /* defined in BEP #9 */
  METADATA_MSG_TYPE_REQUEST = 0,
  METADATA_MSG_TYPE_DATA = 1,
  METADATA_MSG_TYPE_REJECT = 2
};

enum
{
  AWAITING_BT_LENGTH,
  AWAITING_BT_ID,
  AWAITING_BT_MESSAGE,
  AWAITING_BT_PIECE
};

typedef enum
{
  ENCRYPTION_PREFERENCE_UNKNOWN,
  ENCRYPTION_PREFERENCE_YES,
  ENCRYPTION_PREFERENCE_NO
}
encryption_preference_t;

/**
***
**/

struct peer_request
{
  uint32_t index;
  uint32_t offset;
  uint32_t length;
};

static void
blockToReq (const tr_torrent     * tor,
            tr_block_index_t       block,
            struct peer_request  * setme)
{
  tr_torrentGetBlockLocation (tor, block, &setme->index,
                                          &setme->offset,
                                          &setme->length);
}

/**
***
**/

/* this is raw, unchanged data from the peer regarding
 * the current message that it's sending us. */
struct tr_incoming
{
  uint8_t                id;
  uint32_t               length; /* includes the +1 for id length */
  struct peer_request    blockReq; /* metadata for incoming blocks */
  struct evbuffer      * block; /* piece data for incoming blocks */
};

/**
 * Low-level communication state information about a connected peer.
 *
 * This structure remembers the low-level protocol states that we're
 * in with this peer, such as active requests, pex messages, and so on.
 * Its fields are all private to peer-msgs.c.
 *
 * Data not directly involved with sending & receiving messages is
 * stored in tr_peer, where it can be accessed by both peermsgs and
 * the peer manager.
 *
 * @see struct peer_atom
 * @see tr_peer
 */
struct tr_peerMsgs
{
  struct tr_peer peer; /* parent */

  uint16_t magic_number;

  /* Whether or not we've choked this peer. */
  bool peer_is_choked;

  /* whether or not the peer has indicated it will download from us. */
  bool peer_is_interested;

  /* whether or the peer is choking us. */
  bool client_is_choked;

  /* whether or not we've indicated to the peer that we would download from them if unchoked. */
  bool client_is_interested;


  bool peerSupportsPex;
  bool peerSupportsMetadataXfer;
  bool clientSentLtepHandshake;
  bool peerSentLtepHandshake;

  /*bool haveFastSet;*/

  int desiredRequestCount;

  int prefetchCount;

  int is_active[2];

  /* how long the outMessages batch should be allowed to grow before
   * it's flushed -- some messages (like requests >:) should be sent
   * very quickly; others aren't as urgent. */
  int8_t          outMessagesBatchPeriod;

  uint8_t         state;
  uint8_t         ut_pex_id;
  uint8_t         ut_metadata_id;
  uint16_t        pexCount;
  uint16_t        pexCount6;

  tr_port         dht_port;

  encryption_preference_t  encryption_preference;

  size_t                   metadata_size_hint;
#if 0
  size_t                 fastsetSize;
  tr_piece_index_t       fastset[MAX_FAST_SET_SIZE];
#endif

  tr_torrent *           torrent;

  tr_peer_callback        callback;
  void                  * callbackData;

  struct evbuffer *      outMessages; /* all the non-piece messages */

  struct peer_request    peerAskedFor[REQQ];

  int peerAskedForMetadata[METADATA_REQQ];
  int peerAskedForMetadataCount;

  tr_pex * pex;
  tr_pex * pex6;

  /*time_t clientSentPexAt;*/
  time_t clientSentAnythingAt;

  time_t chokeChangedAt;

  /* when we started batching the outMessages */
  time_t outMessagesBatchedAt;

  struct tr_incoming    incoming;

  /* if the peer supports the Extension Protocol in BEP 10 and
     supplied a reqq argument, it's stored here. Otherwise, the
     value is zero and should be ignored. */
  int64_t reqq;

  struct event * pexTimer;

  struct tr_peerIo * io;
};

/**
***
**/

static inline tr_session*
getSession (struct tr_peerMsgs * msgs)
{
  return msgs->torrent->session;
}

/**
***
**/

static void
myDebug (const char * file, int line,
         const struct tr_peerMsgs * msgs,
         const char * fmt, ...) TR_GNUC_PRINTF(4, 5);

static void
myDebug (const char * file, int line,
         const struct tr_peerMsgs * msgs,
         const char * fmt, ...)
{
  const tr_sys_file_t fp = tr_logGetFile ();

  if (fp != TR_BAD_SYS_FILE)
    {
      va_list           args;
      char              timestr[64];
      struct evbuffer * buf = evbuffer_new ();
      char *            base = tr_sys_path_basename (file, NULL);
      char *            message;

      evbuffer_add_printf (buf, "[%s] %s - %s [%s]: ",
                           tr_logGetTimeStr (timestr, sizeof (timestr)),
                           tr_torrentName (msgs->torrent),
                           tr_peerIoGetAddrStr (msgs->io),
                           tr_quark_get_string (msgs->peer.client, NULL));
      va_start (args, fmt);
      evbuffer_add_vprintf (buf, fmt, args);
      va_end (args);
      evbuffer_add_printf (buf, " (%s:%d)", base, line);

      message = evbuffer_free_to_str (buf, NULL);
      tr_sys_file_write_line (fp, message, NULL);

      tr_free (base);
      tr_free (message);
    }
}

#define dbgmsg(msgs, ...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        myDebug (__FILE__, __LINE__, msgs, __VA_ARGS__); \
    } \
  while (0)

/**
***
**/

static void
pokeBatchPeriod (tr_peerMsgs * msgs, int interval)
{
  if (msgs->outMessagesBatchPeriod > interval)
    {
      msgs->outMessagesBatchPeriod = interval;
      dbgmsg (msgs, "lowering batch interval to %d seconds", interval);
    }
}

static void
dbgOutMessageLen (tr_peerMsgs * msgs)
{
  dbgmsg (msgs, "outMessage size is now %zu", evbuffer_get_length (msgs->outMessages));
}

static void
protocolSendReject (tr_peerMsgs * msgs, const struct peer_request * req)
{
  struct evbuffer * out = msgs->outMessages;

  assert (tr_peerIoSupportsFEXT (msgs->io));

  evbuffer_add_uint32 (out, sizeof (uint8_t) + 3 * sizeof (uint32_t));
  evbuffer_add_uint8 (out, BT_FEXT_REJECT);
  evbuffer_add_uint32 (out, req->index);
  evbuffer_add_uint32 (out, req->offset);
  evbuffer_add_uint32 (out, req->length);

  dbgmsg (msgs, "rejecting %u:%u->%u...", req->index, req->offset, req->length);
  dbgOutMessageLen (msgs);
}

static void
protocolSendRequest (tr_peerMsgs * msgs, const struct peer_request * req)
{
  struct evbuffer * out = msgs->outMessages;

  evbuffer_add_uint32 (out, sizeof (uint8_t) + 3 * sizeof (uint32_t));
  evbuffer_add_uint8 (out, BT_REQUEST);
  evbuffer_add_uint32 (out, req->index);
  evbuffer_add_uint32 (out, req->offset);
  evbuffer_add_uint32 (out, req->length);

  dbgmsg (msgs, "requesting %u:%u->%u...", req->index, req->offset, req->length);
  dbgOutMessageLen (msgs);
  pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);
}

static void
protocolSendCancel (tr_peerMsgs * msgs, const struct peer_request * req)
{
  struct evbuffer * out = msgs->outMessages;

  evbuffer_add_uint32 (out, sizeof (uint8_t) + 3 * sizeof (uint32_t));
  evbuffer_add_uint8 (out, BT_CANCEL);
  evbuffer_add_uint32 (out, req->index);
  evbuffer_add_uint32 (out, req->offset);
  evbuffer_add_uint32 (out, req->length);

  dbgmsg (msgs, "cancelling %u:%u->%u...", req->index, req->offset, req->length);
  dbgOutMessageLen (msgs);
  pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);
}

static void
protocolSendPort (tr_peerMsgs *msgs, uint16_t port)
{
  struct evbuffer * out = msgs->outMessages;

  dbgmsg (msgs, "sending Port %u", port);
  evbuffer_add_uint32 (out, 3);
  evbuffer_add_uint8 (out, BT_PORT);
  evbuffer_add_uint16 (out, port);
}

static void
protocolSendHave (tr_peerMsgs * msgs, uint32_t index)
{
  struct evbuffer * out = msgs->outMessages;

  evbuffer_add_uint32 (out, sizeof (uint8_t) + sizeof (uint32_t));
  evbuffer_add_uint8 (out, BT_HAVE);
  evbuffer_add_uint32 (out, index);

  dbgmsg (msgs, "sending Have %u", index);
  dbgOutMessageLen (msgs);
  pokeBatchPeriod (msgs, LOW_PRIORITY_INTERVAL_SECS);
}

#if 0
static void
protocolSendAllowedFast (tr_peerMsgs * msgs, uint32_t pieceIndex)
{
  tr_peerIo       * io  = msgs->io;
  struct evbuffer * out = msgs->outMessages;

  assert (tr_peerIoSupportsFEXT (msgs->io));

  evbuffer_add_uint32 (io, out, sizeof (uint8_t) + sizeof (uint32_t));
  evbuffer_add_uint8 (io, out, BT_FEXT_ALLOWED_FAST);
  evbuffer_add_uint32 (io, out, pieceIndex);

  dbgmsg (msgs, "sending Allowed Fast %u...", pieceIndex);
  dbgOutMessageLen (msgs);
}
#endif

static void
protocolSendChoke (tr_peerMsgs * msgs, int choke)
{
  struct evbuffer * out = msgs->outMessages;

  evbuffer_add_uint32 (out, sizeof (uint8_t));
  evbuffer_add_uint8 (out, choke ? BT_CHOKE : BT_UNCHOKE);

  dbgmsg (msgs, "sending %s...", choke ? "Choke" : "Unchoke");
  dbgOutMessageLen (msgs);
  pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);
}

static void
protocolSendHaveAll (tr_peerMsgs * msgs)
{
  struct evbuffer * out = msgs->outMessages;

  assert (tr_peerIoSupportsFEXT (msgs->io));

  evbuffer_add_uint32 (out, sizeof (uint8_t));
  evbuffer_add_uint8 (out, BT_FEXT_HAVE_ALL);

  dbgmsg (msgs, "sending HAVE_ALL...");
  dbgOutMessageLen (msgs);
  pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);
}

static void
protocolSendHaveNone (tr_peerMsgs * msgs)
{
  struct evbuffer * out = msgs->outMessages;

  assert (tr_peerIoSupportsFEXT (msgs->io));

  evbuffer_add_uint32 (out, sizeof (uint8_t));
  evbuffer_add_uint8 (out, BT_FEXT_HAVE_NONE);

  dbgmsg (msgs, "sending HAVE_NONE...");
  dbgOutMessageLen (msgs);
  pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);
}

/**
***  EVENTS
**/

static void
publish (tr_peerMsgs * msgs, tr_peer_event * e)
{
  if (msgs->callback != NULL)
    (*msgs->callback) (&msgs->peer, e, msgs->callbackData);
}

static void
fireError (tr_peerMsgs * msgs, int err)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_ERROR;
  e.err = err;
  publish (msgs, &e);
}

static void
fireGotBlock (tr_peerMsgs * msgs, const struct peer_request * req)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_BLOCK;
  e.pieceIndex = req->index;
  e.offset = req->offset;
  e.length = req->length;
  publish (msgs, &e);
}

static void
fireGotRej (tr_peerMsgs * msgs, const struct peer_request * req)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_REJ;
  e.pieceIndex = req->index;
  e.offset = req->offset;
  e.length = req->length;
  publish (msgs, &e);
}

static void
fireGotChoke (tr_peerMsgs * msgs)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_CHOKE;
  publish (msgs, &e);
}

static void
fireClientGotHaveAll (tr_peerMsgs * msgs)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_HAVE_ALL;
  publish (msgs, &e);
}

static void
fireClientGotHaveNone (tr_peerMsgs * msgs)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_HAVE_NONE;
  publish (msgs, &e);
}

static void
fireClientGotPieceData (tr_peerMsgs * msgs, uint32_t length)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.length = length;
  e.eventType = TR_PEER_CLIENT_GOT_PIECE_DATA;
  publish (msgs, &e);
}

static void
firePeerGotPieceData (tr_peerMsgs * msgs, uint32_t length)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.length = length;
  e.eventType = TR_PEER_PEER_GOT_PIECE_DATA;
  publish (msgs, &e);
}

static void
fireClientGotSuggest (tr_peerMsgs * msgs, uint32_t pieceIndex)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_SUGGEST;
  e.pieceIndex = pieceIndex;
  publish (msgs, &e);
}

static void
fireClientGotPort (tr_peerMsgs * msgs, tr_port port)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_PORT;
  e.port = port;
  publish (msgs, &e);
}

static void
fireClientGotAllowedFast (tr_peerMsgs * msgs, uint32_t pieceIndex)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_ALLOWED_FAST;
  e.pieceIndex = pieceIndex;
  publish (msgs, &e);
}

static void
fireClientGotBitfield (tr_peerMsgs * msgs, tr_bitfield * bitfield)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_BITFIELD;
  e.bitfield = bitfield;
  publish (msgs, &e);
}

static void
fireClientGotHave (tr_peerMsgs * msgs, tr_piece_index_t index)
{
  tr_peer_event e = TR_PEER_EVENT_INIT;
  e.eventType = TR_PEER_CLIENT_GOT_HAVE;
  e.pieceIndex = index;
  publish (msgs, &e);
}


/**
***  ALLOWED FAST SET
***  For explanation, see http://www.bittorrent.org/beps/bep_0006.html
**/

#if 0
size_t
tr_generateAllowedSet (tr_piece_index_t * setmePieces,
                       size_t             desiredSetSize,
                       size_t             pieceCount,
                       const uint8_t    * infohash,
                       const tr_address * addr)
{
    size_t setSize = 0;

    assert (setmePieces);
    assert (desiredSetSize <= pieceCount);
    assert (desiredSetSize);
    assert (pieceCount);
    assert (infohash);
    assert (addr);

    if (addr->type == TR_AF_INET)
    {
        uint8_t w[SHA_DIGEST_LENGTH + 4], *walk=w;
        uint8_t x[SHA_DIGEST_LENGTH];

        uint32_t ui32 = ntohl (htonl (addr->addr.addr4.s_addr) & 0xffffff00);   /* (1) */
        memcpy (w, &ui32, sizeof (uint32_t));
        walk += sizeof (uint32_t);
        memcpy (walk, infohash, SHA_DIGEST_LENGTH);                 /* (2) */
        walk += SHA_DIGEST_LENGTH;
        tr_sha1 (x, w, walk-w, NULL);                               /* (3) */
        assert (sizeof (w) == walk-w);

        while (setSize<desiredSetSize)
        {
            int i;
            for (i=0; i<5 && setSize<desiredSetSize; ++i)           /* (4) */
            {
                size_t k;
                uint32_t j = i * 4;                                  /* (5) */
                uint32_t y = ntohl (* (uint32_t*)(x + j));       /* (6) */
                uint32_t index = y % pieceCount;                     /* (7) */

                for (k=0; k<setSize; ++k)                           /* (8) */
                    if (setmePieces[k] == index)
                        break;

                if (k == setSize)
                    setmePieces[setSize++] = index;                  /* (9) */
            }

            tr_sha1 (x, x, sizeof (x), NULL);                      /* (3) */
        }
    }

    return setSize;
}

static void
updateFastSet (tr_peerMsgs * msgs UNUSED)
{
    const bool fext = tr_peerIoSupportsFEXT (msgs->io);
    const int peerIsNeedy = msgs->peer->progress < 0.10;

    if (fext && peerIsNeedy && !msgs->haveFastSet)
    {
        size_t i;
        const struct tr_address * addr = tr_peerIoGetAddress (msgs->io, NULL);
        const tr_info * inf = &msgs->torrent->info;
        const size_t numwant = MIN (MAX_FAST_SET_SIZE, inf->pieceCount);

        /* build the fast set */
        msgs->fastsetSize = tr_generateAllowedSet (msgs->fastset, numwant, inf->pieceCount, inf->hash, addr);
        msgs->haveFastSet = true;

        /* send it to the peer */
        for (i=0; i<msgs->fastsetSize; ++i)
            protocolSendAllowedFast (msgs, msgs->fastset[i]);
    }
}
#endif

/***
****  ACTIVE
***/

static bool
tr_peerMsgsCalculateActive (const tr_peerMsgs * msgs, tr_direction direction)
{
  bool is_active;

  assert (tr_isPeerMsgs (msgs));
  assert (tr_isDirection (direction));

  if (direction == TR_CLIENT_TO_PEER)
    {
      is_active = tr_peerMsgsIsPeerInterested (msgs)
              && !tr_peerMsgsIsPeerChoked (msgs);

      /* FIXME: https://trac.transmissionbt.com/ticket/5505
      if (is_active)
        assert (!tr_peerIsSeed (&msgs->peer));
      */
    }
  else /* TR_PEER_TO_CLIENT */
    {
      if (!tr_torrentHasMetadata (msgs->torrent))
        {
          is_active = true;
        }
      else
        {
          is_active = tr_peerMsgsIsClientInterested (msgs)
                  && !tr_peerMsgsIsClientChoked (msgs);

          if (is_active)
            assert (!tr_torrentIsSeed (msgs->torrent));
        }
    }

  return is_active;
}

bool
tr_peerMsgsIsActive (const tr_peerMsgs  * msgs, tr_direction direction)
{
  bool is_active;

  assert (tr_isPeerMsgs (msgs));
  assert (tr_isDirection (direction));

  is_active = msgs->is_active[direction];

  assert (is_active == tr_peerMsgsCalculateActive (msgs, direction));

  return is_active;
}

static void
tr_peerMsgsSetActive (tr_peerMsgs  * msgs,
                      tr_direction   direction,
                      bool           is_active)
{
  dbgmsg (msgs, "direction [%d] is_active [%d]", (int)direction, (int)is_active);

  if (msgs->is_active[direction] != is_active)
    {
      msgs->is_active[direction] = is_active;

      tr_swarmIncrementActivePeers (msgs->torrent->swarm, direction, is_active);
    }
}

void
tr_peerMsgsUpdateActive (tr_peerMsgs * msgs, tr_direction direction)
{
  const bool is_active = tr_peerMsgsCalculateActive (msgs, direction);

  tr_peerMsgsSetActive (msgs, direction, is_active);
}

/**
***  INTEREST
**/

static void
sendInterest (tr_peerMsgs * msgs, bool b)
{
  struct evbuffer * out = msgs->outMessages;

  assert (msgs);
  assert (tr_isBool (b));

  msgs->client_is_interested = b;
  dbgmsg (msgs, "Sending %s", b ? "Interested" : "Not Interested");
  evbuffer_add_uint32 (out, sizeof (uint8_t));
  evbuffer_add_uint8 (out, b ? BT_INTERESTED : BT_NOT_INTERESTED);

  pokeBatchPeriod (msgs, HIGH_PRIORITY_INTERVAL_SECS);
  dbgOutMessageLen (msgs);
}

static void
updateInterest (tr_peerMsgs * msgs UNUSED)
{
    /* FIXME -- might need to poke the mgr on startup */
}

void
tr_peerMsgsSetInterested (tr_peerMsgs * msgs, bool b)
{
  assert (tr_isBool (b));

  if (msgs->client_is_interested != b)
    {
      sendInterest (msgs, b);

      tr_peerMsgsUpdateActive (msgs, TR_PEER_TO_CLIENT);
    }
}

static bool
popNextMetadataRequest (tr_peerMsgs * msgs, int * piece)
{
  if (msgs->peerAskedForMetadataCount == 0)
    return false;

  *piece = msgs->peerAskedForMetadata[0];

  tr_removeElementFromArray (msgs->peerAskedForMetadata, 0, sizeof (int),
                             msgs->peerAskedForMetadataCount--);

  return true;
}

static bool
popNextRequest (tr_peerMsgs * msgs, struct peer_request * setme)
{
  if (msgs->peer.pendingReqsToClient == 0)
    return false;

  *setme = msgs->peerAskedFor[0];

  tr_removeElementFromArray (msgs->peerAskedFor,
                             0,
                             sizeof (struct peer_request),
                             msgs->peer.pendingReqsToClient--);

  return true;
}

static void
cancelAllRequestsToClient (tr_peerMsgs * msgs)
{
  struct peer_request req;
  const int mustSendCancel = tr_peerIoSupportsFEXT (msgs->io);

  while (popNextRequest (msgs, &req))
    if (mustSendCancel)
      protocolSendReject (msgs, &req);
}

void
tr_peerMsgsSetChoke (tr_peerMsgs * msgs, bool peer_is_choked)
{
  const time_t now = tr_time ();
  const time_t fibrillationTime = now - MIN_CHOKE_PERIOD_SEC;

  assert (msgs != NULL);
  assert (tr_isBool (peer_is_choked));

  if (msgs->chokeChangedAt > fibrillationTime)
    {
      dbgmsg (msgs, "Not changing choke to %d to avoid fibrillation", peer_is_choked);
    }
  else if (msgs->peer_is_choked != peer_is_choked)
    {
      msgs->peer_is_choked = peer_is_choked;
      if (peer_is_choked)
        cancelAllRequestsToClient (msgs);
      protocolSendChoke (msgs, peer_is_choked);
      msgs->chokeChangedAt = now;
      tr_peerMsgsUpdateActive (msgs, TR_CLIENT_TO_PEER);
    }
}

/**
***
**/

void
tr_peerMsgsHave (tr_peerMsgs * msgs, uint32_t index)
{
  protocolSendHave (msgs, index);

  /* since we have more pieces now, we might not be interested in this peer */
  updateInterest (msgs);
}

/**
***
**/

static bool
reqIsValid (const tr_peerMsgs * peer,
            uint32_t            index,
            uint32_t            offset,
            uint32_t            length)
{
    return tr_torrentReqIsValid (peer->torrent, index, offset, length);
}

static bool
requestIsValid (const tr_peerMsgs * msgs, const struct peer_request * req)
{
    return reqIsValid (msgs, req->index, req->offset, req->length);
}

void
tr_peerMsgsCancel (tr_peerMsgs * msgs, tr_block_index_t block)
{
    struct peer_request req;
/*fprintf (stderr, "SENDING CANCEL MESSAGE FOR BLOCK %zu\n\t\tFROM PEER %p ------------------------------------\n", (size_t)block, msgs->peer);*/
    blockToReq (msgs->torrent, block, &req);
    protocolSendCancel (msgs, &req);
}

/**
***
**/

static void
sendLtepHandshake (tr_peerMsgs * msgs)
{
    tr_variant val;
    bool allow_pex;
    bool allow_metadata_xfer;
    struct evbuffer * payload;
    struct evbuffer * out = msgs->outMessages;
    const unsigned char * ipv6 = tr_globalIPv6 ();
    static tr_quark version_quark = 0;

    if (msgs->clientSentLtepHandshake)
        return;

    if (!version_quark)
      version_quark = tr_quark_new (TR_NAME " " USERAGENT_PREFIX, TR_BAD_SIZE);

    dbgmsg (msgs, "sending an ltep handshake");
    msgs->clientSentLtepHandshake = true;

    /* decide if we want to advertise metadata xfer support (BEP 9) */
    if (tr_torrentIsPrivate (msgs->torrent))
        allow_metadata_xfer = false;
    else
        allow_metadata_xfer = true;

    /* decide if we want to advertise pex support */
    if (!tr_torrentAllowsPex (msgs->torrent))
        allow_pex = false;
    else if (msgs->peerSentLtepHandshake)
        allow_pex = msgs->peerSupportsPex;
    else
        allow_pex = true;

    tr_variantInitDict (&val, 8);
    tr_variantDictAddInt (&val, TR_KEY_e, getSession (msgs)->encryptionMode != TR_CLEAR_PREFERRED);
    if (ipv6 != NULL)
        tr_variantDictAddRaw (&val, TR_KEY_ipv6, ipv6, 16);
    if (allow_metadata_xfer && tr_torrentHasMetadata (msgs->torrent)
                            && (msgs->torrent->infoDictLength > 0))
        tr_variantDictAddInt (&val, TR_KEY_metadata_size, msgs->torrent->infoDictLength);
    tr_variantDictAddInt (&val, TR_KEY_p, tr_sessionGetPublicPeerPort (getSession (msgs)));
    tr_variantDictAddInt (&val, TR_KEY_reqq, REQQ);
    tr_variantDictAddInt (&val, TR_KEY_upload_only, tr_torrentIsSeed (msgs->torrent));
    tr_variantDictAddQuark (&val, TR_KEY_v, version_quark);
    if (allow_metadata_xfer || allow_pex) {
        tr_variant * m  = tr_variantDictAddDict (&val, TR_KEY_m, 2);
        if (allow_metadata_xfer)
            tr_variantDictAddInt (m, TR_KEY_ut_metadata, UT_METADATA_ID);
        if (allow_pex)
            tr_variantDictAddInt (m, TR_KEY_ut_pex, UT_PEX_ID);
    }

    payload = tr_variantToBuf (&val, TR_VARIANT_FMT_BENC);

    evbuffer_add_uint32 (out, 2 * sizeof (uint8_t) + evbuffer_get_length (payload));
    evbuffer_add_uint8 (out, BT_LTEP);
    evbuffer_add_uint8 (out, LTEP_HANDSHAKE);
    evbuffer_add_buffer (out, payload);
    pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);
    dbgOutMessageLen (msgs);

    /* cleanup */
    evbuffer_free (payload);
    tr_variantFree (&val);
}

static void
parseLtepHandshake (tr_peerMsgs * msgs, uint32_t len, struct evbuffer * inbuf)
{
    int64_t   i;
    tr_variant   val, * sub;
    uint8_t * tmp = tr_new (uint8_t, len);
    const uint8_t *addr;
    size_t addr_len;
    tr_pex pex;
    int8_t seedProbability = -1;

    memset (&pex, 0, sizeof (tr_pex));

    tr_peerIoReadBytes (msgs->io, inbuf, tmp, len);
    msgs->peerSentLtepHandshake = true;

    if (tr_variantFromBenc (&val, tmp, len) || !tr_variantIsDict (&val))
    {
        dbgmsg (msgs, "GET  extended-handshake, couldn't get dictionary");
        tr_free (tmp);
        return;
    }

    /* arbitrary limit, should be more than enough */
    if (len <= 4096)
      dbgmsg (msgs, "here is the handshake: [%*.*s]", (int) len, (int) len, tmp);
    else
      dbgmsg (msgs, "handshake length is too big (%" PRIu32 "), printing skipped", len);

    /* does the peer prefer encrypted connections? */
    if (tr_variantDictFindInt (&val, TR_KEY_e, &i)) {
        msgs->encryption_preference = i ? ENCRYPTION_PREFERENCE_YES
                                        : ENCRYPTION_PREFERENCE_NO;
        if (i)
            pex.flags |= ADDED_F_ENCRYPTION_FLAG;
    }

    /* check supported messages for utorrent pex */
    msgs->peerSupportsPex = false;
    msgs->peerSupportsMetadataXfer = false;

    if (tr_variantDictFindDict (&val, TR_KEY_m, &sub)) {
        if (tr_variantDictFindInt (sub, TR_KEY_ut_pex, &i)) {
            msgs->peerSupportsPex = i != 0;
            msgs->ut_pex_id = (uint8_t) i;
            dbgmsg (msgs, "msgs->ut_pex is %d", (int)msgs->ut_pex_id);
        }
        if (tr_variantDictFindInt (sub, TR_KEY_ut_metadata, &i)) {
            msgs->peerSupportsMetadataXfer = i != 0;
            msgs->ut_metadata_id = (uint8_t) i;
            dbgmsg (msgs, "msgs->ut_metadata_id is %d", (int)msgs->ut_metadata_id);
        }
        if (tr_variantDictFindInt (sub, TR_KEY_ut_holepunch, &i)) {
            /* Mysterious µTorrent extension that we don't grok.  However,
               it implies support for µTP, so use it to indicate that. */
            tr_peerMgrSetUtpFailed (msgs->torrent,
                                    tr_peerIoGetAddress (msgs->io, NULL),
                                    false);
        }
    }

    /* look for metainfo size (BEP 9) */
    if (tr_variantDictFindInt (&val, TR_KEY_metadata_size, &i)) {
        if (tr_torrentSetMetadataSizeHint (msgs->torrent, i))
            msgs->metadata_size_hint = (size_t) i;
    }

    /* look for upload_only (BEP 21) */
    if (tr_variantDictFindInt (&val, TR_KEY_upload_only, &i))
        seedProbability = i==0 ? 0 : 100;

    /* get peer's listening port */
    if (tr_variantDictFindInt (&val, TR_KEY_p, &i)) {
        pex.port = htons ((uint16_t)i);
        fireClientGotPort (msgs, pex.port);
        dbgmsg (msgs, "peer's port is now %d", (int)i);
    }

    if (tr_peerIoIsIncoming (msgs->io)
        && tr_variantDictFindRaw (&val, TR_KEY_ipv4, &addr, &addr_len)
        && (addr_len == 4))
    {
        pex.addr.type = TR_AF_INET;
        memcpy (&pex.addr.addr.addr4, addr, 4);
        tr_peerMgrAddPex (msgs->torrent, TR_PEER_FROM_LTEP, &pex, seedProbability);
    }

    if (tr_peerIoIsIncoming (msgs->io)
        && tr_variantDictFindRaw (&val, TR_KEY_ipv6, &addr, &addr_len)
        && (addr_len == 16))
    {
        pex.addr.type = TR_AF_INET6;
        memcpy (&pex.addr.addr.addr6, addr, 16);
        tr_peerMgrAddPex (msgs->torrent, TR_PEER_FROM_LTEP, &pex, seedProbability);
    }

    /* get peer's maximum request queue size */
    if (tr_variantDictFindInt (&val, TR_KEY_reqq, &i))
        msgs->reqq = i;

    tr_variantFree (&val);
    tr_free (tmp);
}

static void
parseUtMetadata (tr_peerMsgs * msgs, uint32_t msglen, struct evbuffer * inbuf)
{
    tr_variant dict;
    char * msg_end;
    const char * benc_end;
    int64_t msg_type = -1;
    int64_t piece = -1;
    int64_t total_size = 0;
    uint8_t * tmp = tr_new (uint8_t, msglen);

    tr_peerIoReadBytes (msgs->io, inbuf, tmp, msglen);
    msg_end = (char*)tmp + msglen;

    if (!tr_variantFromBencFull (&dict, tmp, msglen, NULL, &benc_end))
    {
        tr_variantDictFindInt (&dict, TR_KEY_msg_type, &msg_type);
        tr_variantDictFindInt (&dict, TR_KEY_piece, &piece);
        tr_variantDictFindInt (&dict, TR_KEY_total_size, &total_size);
        tr_variantFree (&dict);
    }

    dbgmsg (msgs, "got ut_metadata msg: type %d, piece %d, total_size %d",
          (int)msg_type, (int)piece, (int)total_size);

    if (msg_type == METADATA_MSG_TYPE_REJECT)
    {
        /* NOOP */
    }

    if ((msg_type == METADATA_MSG_TYPE_DATA)
        && (!tr_torrentHasMetadata (msgs->torrent))
        && (msg_end - benc_end <= METADATA_PIECE_SIZE)
        && (piece * METADATA_PIECE_SIZE + (msg_end - benc_end) <= total_size))
    {
        const int pieceLen = msg_end - benc_end;
        tr_torrentSetMetadataPiece (msgs->torrent, piece, benc_end, pieceLen);
    }

    if (msg_type == METADATA_MSG_TYPE_REQUEST)
    {
        if ((piece >= 0)
            && tr_torrentHasMetadata (msgs->torrent)
            && !tr_torrentIsPrivate (msgs->torrent)
            && (msgs->peerAskedForMetadataCount < METADATA_REQQ))
        {
            msgs->peerAskedForMetadata[msgs->peerAskedForMetadataCount++] = piece;
        }
        else
        {
            tr_variant tmp;
            struct evbuffer * payload;
            struct evbuffer * out = msgs->outMessages;

            /* build the rejection message */
            tr_variantInitDict (&tmp, 2);
            tr_variantDictAddInt (&tmp, TR_KEY_msg_type, METADATA_MSG_TYPE_REJECT);
            tr_variantDictAddInt (&tmp, TR_KEY_piece, piece);
            payload = tr_variantToBuf (&tmp, TR_VARIANT_FMT_BENC);

            /* write it out as a LTEP message to our outMessages buffer */
            evbuffer_add_uint32 (out, 2 * sizeof (uint8_t) + evbuffer_get_length (payload));
            evbuffer_add_uint8 (out, BT_LTEP);
            evbuffer_add_uint8 (out, msgs->ut_metadata_id);
            evbuffer_add_buffer (out, payload);
            pokeBatchPeriod (msgs, HIGH_PRIORITY_INTERVAL_SECS);
            dbgOutMessageLen (msgs);

            /* cleanup */
            evbuffer_free (payload);
            tr_variantFree (&tmp);
        }
    }

    tr_free (tmp);
}

static void
parseUtPex (tr_peerMsgs * msgs, uint32_t msglen, struct evbuffer * inbuf)
{
    int loaded = 0;
    uint8_t * tmp = tr_new (uint8_t, msglen);
    tr_variant val;
    tr_torrent * tor = msgs->torrent;
    const uint8_t * added;
    size_t added_len;

    tr_peerIoReadBytes (msgs->io, inbuf, tmp, msglen);

    if (tr_torrentAllowsPex (tor)
      && ((loaded = !tr_variantFromBenc (&val, tmp, msglen))))
    {
        if (tr_variantDictFindRaw (&val, TR_KEY_added, &added, &added_len))
        {
            tr_pex * pex;
            size_t i, n;
            size_t added_f_len = 0;
            const uint8_t * added_f = NULL;

            tr_variantDictFindRaw (&val, TR_KEY_added_f, &added_f, &added_f_len);
            pex = tr_peerMgrCompactToPex (added, added_len, added_f, added_f_len, &n);

            n = MIN (n, MAX_PEX_PEER_COUNT);
            for (i=0; i<n; ++i)
            {
                int seedProbability = -1;
                if (i < added_f_len) seedProbability = (added_f[i] & ADDED_F_SEED_FLAG) ? 100 : 0;
                tr_peerMgrAddPex (tor, TR_PEER_FROM_PEX, pex+i, seedProbability);
            }

            tr_free (pex);
        }

        if (tr_variantDictFindRaw (&val, TR_KEY_added6, &added, &added_len))
        {
            tr_pex * pex;
            size_t i, n;
            size_t added_f_len = 0;
            const uint8_t * added_f = NULL;

            tr_variantDictFindRaw (&val, TR_KEY_added6_f, &added_f, &added_f_len);
            pex = tr_peerMgrCompact6ToPex (added, added_len, added_f, added_f_len, &n);

            n = MIN (n, MAX_PEX_PEER_COUNT);
            for (i=0; i<n; ++i)
            {
                int seedProbability = -1;
                if (i < added_f_len) seedProbability = (added_f[i] & ADDED_F_SEED_FLAG) ? 100 : 0;
                tr_peerMgrAddPex (tor, TR_PEER_FROM_PEX, pex+i, seedProbability);
            }

            tr_free (pex);
        }
    }

    if (loaded)
        tr_variantFree (&val);
    tr_free (tmp);
}

static void sendPex (tr_peerMsgs * msgs);

static void
parseLtep (tr_peerMsgs * msgs, uint32_t msglen, struct evbuffer * inbuf)
{
    uint8_t ltep_msgid;

    assert (msglen > 0);

    tr_peerIoReadUint8 (msgs->io, inbuf, &ltep_msgid);
    msglen--;

    if (ltep_msgid == LTEP_HANDSHAKE)
    {
        dbgmsg (msgs, "got ltep handshake");
        parseLtepHandshake (msgs, msglen, inbuf);
        if (tr_peerIoSupportsLTEP (msgs->io))
        {
            sendLtepHandshake (msgs);
            sendPex (msgs);
        }
    }
    else if (ltep_msgid == UT_PEX_ID)
    {
        dbgmsg (msgs, "got ut pex");
        msgs->peerSupportsPex = true;
        parseUtPex (msgs, msglen, inbuf);
    }
    else if (ltep_msgid == UT_METADATA_ID)
    {
        dbgmsg (msgs, "got ut metadata");
        msgs->peerSupportsMetadataXfer = true;
        parseUtMetadata (msgs, msglen, inbuf);
    }
    else
    {
        dbgmsg (msgs, "skipping unknown ltep message (%d)", (int)ltep_msgid);
        evbuffer_drain (inbuf, msglen);
    }
}

static int
readBtLength (tr_peerMsgs * msgs, struct evbuffer * inbuf, size_t inlen)
{
    uint32_t len;

    if (inlen < sizeof (len))
        return READ_LATER;

    tr_peerIoReadUint32 (msgs->io, inbuf, &len);

    if (len == 0) /* peer sent us a keepalive message */
        dbgmsg (msgs, "got KeepAlive");
    else
    {
        msgs->incoming.length = len;
        msgs->state = AWAITING_BT_ID;
    }

    return READ_NOW;
}

static int readBtMessage (tr_peerMsgs *, struct evbuffer *, size_t);

static int
readBtId (tr_peerMsgs * msgs, struct evbuffer * inbuf, size_t inlen)
{
    uint8_t id;

    if (inlen < sizeof (uint8_t))
        return READ_LATER;

    tr_peerIoReadUint8 (msgs->io, inbuf, &id);
    msgs->incoming.id = id;
    dbgmsg (msgs, "msgs->incoming.id is now %d; msgs->incoming.length is %zu", id, (size_t)msgs->incoming.length);

    if (id == BT_PIECE)
    {
        msgs->state = AWAITING_BT_PIECE;
        return READ_NOW;
    }
    else if (msgs->incoming.length != 1)
    {
        msgs->state = AWAITING_BT_MESSAGE;
        return READ_NOW;
    }
    else return readBtMessage (msgs, inbuf, inlen - 1);
}

static void
updatePeerProgress (tr_peerMsgs * msgs)
{
  tr_peerUpdateProgress (msgs->torrent, &msgs->peer);

  /*updateFastSet (msgs);*/
  updateInterest (msgs);
}

static void
prefetchPieces (tr_peerMsgs *msgs)
{
  int i;

  if (!getSession (msgs)->isPrefetchEnabled)
    return;

  for (i=msgs->prefetchCount; i<msgs->peer.pendingReqsToClient && i<PREFETCH_SIZE; ++i)
    {
      const struct peer_request * req = msgs->peerAskedFor + i;
      if (requestIsValid (msgs, req))
        {
          tr_cachePrefetchBlock (getSession (msgs)->cache, msgs->torrent, req->index, req->offset, req->length);
          ++msgs->prefetchCount;
        }
    }
}

static void
peerMadeRequest (tr_peerMsgs * msgs, const struct peer_request * req)
{
    const bool fext = tr_peerIoSupportsFEXT (msgs->io);
    const int reqIsValid = requestIsValid (msgs, req);
    const int clientHasPiece = reqIsValid && tr_torrentPieceIsComplete (msgs->torrent, req->index);
    const int peerIsChoked = msgs->peer_is_choked;

    bool allow = false;

    if (!reqIsValid)
        dbgmsg (msgs, "rejecting an invalid request.");
    else if (!clientHasPiece)
        dbgmsg (msgs, "rejecting request for a piece we don't have.");
    else if (peerIsChoked)
        dbgmsg (msgs, "rejecting request from choked peer");
    else if (msgs->peer.pendingReqsToClient + 1 >= REQQ)
        dbgmsg (msgs, "rejecting request ... reqq is full");
    else
        allow = true;

    if (allow) {
        msgs->peerAskedFor[msgs->peer.pendingReqsToClient++] = *req;
        prefetchPieces (msgs);
    } else if (fext) {
        protocolSendReject (msgs, req);
    }
}

static bool
messageLengthIsCorrect (const tr_peerMsgs * msg, uint8_t id, uint32_t len)
{
    switch (id)
    {
        case BT_CHOKE:
        case BT_UNCHOKE:
        case BT_INTERESTED:
        case BT_NOT_INTERESTED:
        case BT_FEXT_HAVE_ALL:
        case BT_FEXT_HAVE_NONE:
            return len == 1;

        case BT_HAVE:
        case BT_FEXT_SUGGEST:
        case BT_FEXT_ALLOWED_FAST:
            return len == 5;

        case BT_BITFIELD:
            if (tr_torrentHasMetadata (msg->torrent))
                return len == (msg->torrent->info.pieceCount >> 3) + (msg->torrent->info.pieceCount & 7 ? 1 : 0) + 1u;
            /* we don't know the piece count yet,
               so we can only guess whether to send true or false */
            if (msg->metadata_size_hint > 0)
                return len <= msg->metadata_size_hint;
            return true;

        case BT_REQUEST:
        case BT_CANCEL:
        case BT_FEXT_REJECT:
            return len == 13;

        case BT_PIECE:
            return len > 9 && len <= 16393;

        case BT_PORT:
            return len == 3;

        case BT_LTEP:
            return len >= 2;

        default:
            return false;
    }
}

static int clientGotBlock (tr_peerMsgs *               msgs,
                           struct evbuffer *           block,
                           const struct peer_request * req);

static int
readBtPiece (tr_peerMsgs      * msgs,
             struct evbuffer  * inbuf,
             size_t             inlen,
             size_t           * setme_piece_bytes_read)
{
    struct peer_request * req = &msgs->incoming.blockReq;

    assert (evbuffer_get_length (inbuf) >= inlen);
    dbgmsg (msgs, "In readBtPiece");

    if (!req->length)
    {
        if (inlen < 8)
            return READ_LATER;

        tr_peerIoReadUint32 (msgs->io, inbuf, &req->index);
        tr_peerIoReadUint32 (msgs->io, inbuf, &req->offset);
        req->length = msgs->incoming.length - 9;
        dbgmsg (msgs, "got incoming block header %u:%u->%u", req->index, req->offset, req->length);
        return READ_NOW;
    }
    else
    {
        int err;
        size_t n;
        size_t nLeft;
        struct evbuffer * block_buffer;

        if (msgs->incoming.block == NULL)
            msgs->incoming.block = evbuffer_new ();
        block_buffer = msgs->incoming.block;

        /* read in another chunk of data */
        nLeft = req->length - evbuffer_get_length (block_buffer);
        n = MIN (nLeft, inlen);

        tr_peerIoReadBytesToBuf (msgs->io, inbuf, block_buffer, n);

        fireClientGotPieceData (msgs, n);
        *setme_piece_bytes_read += n;
        dbgmsg (msgs, "got %zu bytes for block %u:%u->%u ... %d remain",
               n, req->index, req->offset, req->length,
             (int)(req->length - evbuffer_get_length (block_buffer)));
        if (evbuffer_get_length (block_buffer) < req->length)
            return READ_LATER;

        /* pass the block along... */
        err = clientGotBlock (msgs, block_buffer, req);
        evbuffer_drain (block_buffer, evbuffer_get_length (block_buffer));

        /* cleanup */
        req->length = 0;
        msgs->state = AWAITING_BT_LENGTH;
        return err ? READ_ERR : READ_NOW;
    }
}

static void updateDesiredRequestCount (tr_peerMsgs * msgs);

static int
readBtMessage (tr_peerMsgs * msgs, struct evbuffer * inbuf, size_t inlen)
{
    uint32_t      ui32;
    uint32_t      msglen = msgs->incoming.length;
    const uint8_t id = msgs->incoming.id;
#ifndef NDEBUG
    const size_t  startBufLen = evbuffer_get_length (inbuf);
#endif
    const bool fext = tr_peerIoSupportsFEXT (msgs->io);

    assert (msglen > 0);

    --msglen; /* id length */

    dbgmsg (msgs, "got BT id %d, len %d, buffer size is %zu", (int)id, (int)msglen, inlen);

    if (inlen < msglen)
        return READ_LATER;

    if (!messageLengthIsCorrect (msgs, id, msglen + 1))
    {
        dbgmsg (msgs, "bad packet - BT message #%d with a length of %d", (int)id, (int)msglen);
        fireError (msgs, EMSGSIZE);
        return READ_ERR;
    }

    switch (id)
    {
        case BT_CHOKE:
            dbgmsg (msgs, "got Choke");
            msgs->client_is_choked = true;
            if (!fext)
                fireGotChoke (msgs);
            tr_peerMsgsUpdateActive (msgs, TR_PEER_TO_CLIENT);
            break;

        case BT_UNCHOKE:
            dbgmsg (msgs, "got Unchoke");
            msgs->client_is_choked = false;
            tr_peerMsgsUpdateActive (msgs, TR_PEER_TO_CLIENT);
            updateDesiredRequestCount (msgs);
            break;

        case BT_INTERESTED:
            dbgmsg (msgs, "got Interested");
            msgs->peer_is_interested = true;
            tr_peerMsgsUpdateActive (msgs, TR_CLIENT_TO_PEER);
            break;

        case BT_NOT_INTERESTED:
            dbgmsg (msgs, "got Not Interested");
            msgs->peer_is_interested = false;
            tr_peerMsgsUpdateActive (msgs, TR_CLIENT_TO_PEER);
            break;

        case BT_HAVE:
            tr_peerIoReadUint32 (msgs->io, inbuf, &ui32);
            dbgmsg (msgs, "got Have: %u", ui32);
            if (tr_torrentHasMetadata (msgs->torrent)
                    && (ui32 >= msgs->torrent->info.pieceCount))
            {
                fireError (msgs, ERANGE);
                return READ_ERR;
            }

            /* a peer can send the same HAVE message twice... */
            if (!tr_bitfieldHas (&msgs->peer.have, ui32)) {
                tr_bitfieldAdd (&msgs->peer.have, ui32);
                fireClientGotHave (msgs, ui32);
            }
            updatePeerProgress (msgs);
            break;

        case BT_BITFIELD: {
            uint8_t * tmp = tr_new (uint8_t, msglen);
            dbgmsg (msgs, "got a bitfield");
            tr_peerIoReadBytes (msgs->io, inbuf, tmp, msglen);
            tr_bitfieldSetRaw (&msgs->peer.have, tmp, msglen, tr_torrentHasMetadata (msgs->torrent));
            fireClientGotBitfield (msgs, &msgs->peer.have);
            updatePeerProgress (msgs);
            tr_free (tmp);
            break;
        }

        case BT_REQUEST:
        {
            struct peer_request r;
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.index);
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.offset);
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.length);
            dbgmsg (msgs, "got Request: %u:%u->%u", r.index, r.offset, r.length);
            peerMadeRequest (msgs, &r);
            break;
        }

        case BT_CANCEL:
        {
            int i;
            struct peer_request r;
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.index);
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.offset);
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.length);
            tr_historyAdd (&msgs->peer.cancelsSentToClient, tr_time (), 1);
            dbgmsg (msgs, "got a Cancel %u:%u->%u", r.index, r.offset, r.length);

            for (i=0; i<msgs->peer.pendingReqsToClient; ++i) {
                const struct peer_request * req = msgs->peerAskedFor + i;
                if ((req->index == r.index) && (req->offset == r.offset) && (req->length == r.length))
                    break;
            }

            if (i < msgs->peer.pendingReqsToClient)
                tr_removeElementFromArray (msgs->peerAskedFor, i, sizeof (struct peer_request),
                                           msgs->peer.pendingReqsToClient--);
            break;
        }

        case BT_PIECE:
            assert (0); /* handled elsewhere! */
            break;

        case BT_PORT:
            dbgmsg (msgs, "Got a BT_PORT");
            tr_peerIoReadUint16 (msgs->io, inbuf, &msgs->dht_port);
            if (msgs->dht_port > 0)
                tr_dhtAddNode (getSession (msgs),
                               tr_peerAddress (&msgs->peer),
                               msgs->dht_port, 0);
            break;

        case BT_FEXT_SUGGEST:
            dbgmsg (msgs, "Got a BT_FEXT_SUGGEST");
            tr_peerIoReadUint32 (msgs->io, inbuf, &ui32);
            if (fext)
                fireClientGotSuggest (msgs, ui32);
            else {
                fireError (msgs, EMSGSIZE);
                return READ_ERR;
            }
            break;

        case BT_FEXT_ALLOWED_FAST:
            dbgmsg (msgs, "Got a BT_FEXT_ALLOWED_FAST");
            tr_peerIoReadUint32 (msgs->io, inbuf, &ui32);
            if (fext)
                fireClientGotAllowedFast (msgs, ui32);
            else {
                fireError (msgs, EMSGSIZE);
                return READ_ERR;
            }
            break;

        case BT_FEXT_HAVE_ALL:
            dbgmsg (msgs, "Got a BT_FEXT_HAVE_ALL");
            if (fext) {
                tr_bitfieldSetHasAll (&msgs->peer.have);
assert (tr_bitfieldHasAll (&msgs->peer.have));
                fireClientGotHaveAll (msgs);
                updatePeerProgress (msgs);
            } else {
                fireError (msgs, EMSGSIZE);
                return READ_ERR;
            }
            break;

        case BT_FEXT_HAVE_NONE:
            dbgmsg (msgs, "Got a BT_FEXT_HAVE_NONE");
            if (fext) {
                tr_bitfieldSetHasNone (&msgs->peer.have);
                fireClientGotHaveNone (msgs);
                updatePeerProgress (msgs);
            } else {
                fireError (msgs, EMSGSIZE);
                return READ_ERR;
            }
            break;

        case BT_FEXT_REJECT:
        {
            struct peer_request r;
            dbgmsg (msgs, "Got a BT_FEXT_REJECT");
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.index);
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.offset);
            tr_peerIoReadUint32 (msgs->io, inbuf, &r.length);
            if (fext)
                fireGotRej (msgs, &r);
            else {
                fireError (msgs, EMSGSIZE);
                return READ_ERR;
            }
            break;
        }

        case BT_LTEP:
            dbgmsg (msgs, "Got a BT_LTEP");
            parseLtep (msgs, msglen, inbuf);
            break;

        default:
            dbgmsg (msgs, "peer sent us an UNKNOWN: %d", (int)id);
            tr_peerIoDrain (msgs->io, inbuf, msglen);
            break;
    }

    assert (msglen + 1 == msgs->incoming.length);
    assert (evbuffer_get_length (inbuf) == startBufLen - msglen);

    msgs->state = AWAITING_BT_LENGTH;
    return READ_NOW;
}

/* returns 0 on success, or an errno on failure */
static int
clientGotBlock (tr_peerMsgs                * msgs,
                struct evbuffer            * data,
                const struct peer_request  * req)
{
    int err;
    tr_torrent * tor = msgs->torrent;
    const tr_block_index_t block = _tr_block (tor, req->index, req->offset);

    assert (msgs);
    assert (req);

    if (!requestIsValid (msgs, req)) {
        dbgmsg (msgs, "dropping invalid block %u:%u->%u",
                req->index, req->offset, req->length);
        return EBADMSG;
    }

    if (req->length != tr_torBlockCountBytes (msgs->torrent, block)) {
        dbgmsg (msgs, "wrong block size -- expected %u, got %d",
                tr_torBlockCountBytes (msgs->torrent, block), req->length);
        return EMSGSIZE;
    }

    dbgmsg (msgs, "got block %u:%u->%u", req->index, req->offset, req->length);

    if (!tr_peerMgrDidPeerRequest (msgs->torrent, &msgs->peer, block)) {
        dbgmsg (msgs, "we didn't ask for this message...");
        return 0;
    }
    if (tr_torrentPieceIsComplete (msgs->torrent, req->index)) {
        dbgmsg (msgs, "we did ask for this message, but the piece is already complete...");
        return 0;
    }

    /**
    ***  Save the block
    **/

    if ((err = tr_cacheWriteBlock (getSession (msgs)->cache, tor, req->index, req->offset, req->length, data)))
        return err;

    tr_bitfieldAdd (&msgs->peer.blame, req->index);
    fireGotBlock (msgs, req);
    return 0;
}

static int peerPulse (void * vmsgs);

static void
didWrite (tr_peerIo * io, size_t bytesWritten, bool wasPieceData, void * vmsgs)
{
    tr_peerMsgs * msgs = vmsgs;

    if (wasPieceData)
      firePeerGotPieceData (msgs, bytesWritten);

    if (tr_isPeerIo (io) && io->userData)
        peerPulse (msgs);
}

static ReadState
canRead (tr_peerIo * io, void * vmsgs, size_t * piece)
{
    ReadState         ret;
    tr_peerMsgs *     msgs = vmsgs;
    struct evbuffer * in = tr_peerIoGetReadBuffer (io);
    const size_t      inlen = evbuffer_get_length (in);

    dbgmsg (msgs, "canRead: inlen is %zu, msgs->state is %d", inlen, msgs->state);

    if (!inlen)
    {
        ret = READ_LATER;
    }
    else if (msgs->state == AWAITING_BT_PIECE)
    {
        ret = readBtPiece (msgs, in, inlen, piece);
    }
    else switch (msgs->state)
    {
        case AWAITING_BT_LENGTH:
            ret = readBtLength (msgs, in, inlen); break;

        case AWAITING_BT_ID:
            ret = readBtId   (msgs, in, inlen); break;

        case AWAITING_BT_MESSAGE:
            ret = readBtMessage (msgs, in, inlen); break;

        default:
            ret = READ_ERR;
            assert (0);
    }

    dbgmsg (msgs, "canRead: ret is %d", (int)ret);

    return ret;
}

int
tr_peerMsgsIsReadingBlock (const tr_peerMsgs * msgs, tr_block_index_t block)
{
    if (msgs->state != AWAITING_BT_PIECE)
        return false;

    return block == _tr_block (msgs->torrent,
                               msgs->incoming.blockReq.index,
                               msgs->incoming.blockReq.offset);
}

/**
***
**/

static void
updateDesiredRequestCount (tr_peerMsgs * msgs)
{
    tr_torrent * const torrent = msgs->torrent;

    /* there are lots of reasons we might not want to request any blocks... */
    if (tr_torrentIsSeed (torrent) || !tr_torrentHasMetadata (torrent)
                                    || msgs->client_is_choked
                                    || !msgs->client_is_interested)
    {
        msgs->desiredRequestCount = 0;
    }
    else
    {
        int estimatedBlocksInPeriod;
        unsigned int rate_Bps;
        unsigned int irate_Bps;
        const int floor = 4;
        const int seconds = REQUEST_BUF_SECS;
        const uint64_t now = tr_time_msec ();

        /* Get the rate limit we should use.
         * FIXME: this needs to consider all the other peers as well... */
        rate_Bps = tr_peerGetPieceSpeed_Bps (&msgs->peer, now, TR_PEER_TO_CLIENT);
        if (tr_torrentUsesSpeedLimit (torrent, TR_PEER_TO_CLIENT))
            rate_Bps = MIN (rate_Bps, tr_torrentGetSpeedLimit_Bps (torrent, TR_PEER_TO_CLIENT));

        /* honor the session limits, if enabled */
        if (tr_torrentUsesSessionLimits (torrent) &&
            tr_sessionGetActiveSpeedLimit_Bps (torrent->session, TR_PEER_TO_CLIENT, &irate_Bps))
                rate_Bps = MIN (rate_Bps, irate_Bps);

        /* use this desired rate to figure out how
         * many requests we should send to this peer */
        estimatedBlocksInPeriod = (rate_Bps * seconds) / torrent->blockSize;
        msgs->desiredRequestCount = MAX (floor, estimatedBlocksInPeriod);

        /* honor the peer's maximum request count, if specified */
        if (msgs->reqq > 0)
            if (msgs->desiredRequestCount > msgs->reqq)
                msgs->desiredRequestCount = msgs->reqq;
    }
}

static void
updateMetadataRequests (tr_peerMsgs * msgs, time_t now)
{
    int piece;

    if (msgs->peerSupportsMetadataXfer
        && tr_torrentGetNextMetadataRequest (msgs->torrent, now, &piece))
    {
        tr_variant tmp;
        struct evbuffer * payload;
        struct evbuffer * out = msgs->outMessages;

        /* build the data message */
        tr_variantInitDict (&tmp, 3);
        tr_variantDictAddInt (&tmp, TR_KEY_msg_type, METADATA_MSG_TYPE_REQUEST);
        tr_variantDictAddInt (&tmp, TR_KEY_piece, piece);
        payload = tr_variantToBuf (&tmp, TR_VARIANT_FMT_BENC);

        dbgmsg (msgs, "requesting metadata piece #%d", piece);

        /* write it out as a LTEP message to our outMessages buffer */
        evbuffer_add_uint32 (out, 2 * sizeof (uint8_t) + evbuffer_get_length (payload));
        evbuffer_add_uint8 (out, BT_LTEP);
        evbuffer_add_uint8 (out, msgs->ut_metadata_id);
        evbuffer_add_buffer (out, payload);
        pokeBatchPeriod (msgs, HIGH_PRIORITY_INTERVAL_SECS);
        dbgOutMessageLen (msgs);

        /* cleanup */
        evbuffer_free (payload);
        tr_variantFree (&tmp);
    }
}

static void
updateBlockRequests (tr_peerMsgs * msgs)
{
    if (tr_torrentIsPieceTransferAllowed (msgs->torrent, TR_PEER_TO_CLIENT)
        && (msgs->desiredRequestCount > 0)
        && (msgs->peer.pendingReqsToPeer <= (msgs->desiredRequestCount * 0.66)))
    {
        int i;
        int n;
        tr_block_index_t * blocks;
        const int numwant = msgs->desiredRequestCount - msgs->peer.pendingReqsToPeer;

        assert (tr_peerMsgsIsClientInterested (msgs));
        assert (!tr_peerMsgsIsClientChoked (msgs));

        blocks = tr_new (tr_block_index_t, numwant);
        tr_peerMgrGetNextRequests (msgs->torrent, &msgs->peer, numwant, blocks, &n, false);

        for (i=0; i<n; ++i)
        {
            struct peer_request req;
            blockToReq (msgs->torrent, blocks[i], &req);
            protocolSendRequest (msgs, &req);
        }

        tr_free (blocks);
    }
}

static size_t
fillOutputBuffer (tr_peerMsgs * msgs, time_t now)
{
    int piece;
    size_t bytesWritten = 0;
    struct peer_request req;
    const bool haveMessages = evbuffer_get_length (msgs->outMessages) != 0;
    const bool fext = tr_peerIoSupportsFEXT (msgs->io);

    /**
    ***  Protocol messages
    **/

    if (haveMessages && !msgs->outMessagesBatchedAt) /* fresh batch */
    {
        dbgmsg (msgs, "started an outMessages batch (length is %zu)", evbuffer_get_length (msgs->outMessages));
        msgs->outMessagesBatchedAt = now;
    }
    else if (haveMessages && ((now - msgs->outMessagesBatchedAt) >= msgs->outMessagesBatchPeriod))
    {
        const size_t len = evbuffer_get_length (msgs->outMessages);
        /* flush the protocol messages */
        dbgmsg (msgs, "flushing outMessages... to %p (length is %zu)", (void*)msgs->io, len);
        tr_peerIoWriteBuf (msgs->io, msgs->outMessages, false);
        msgs->clientSentAnythingAt = now;
        msgs->outMessagesBatchedAt = 0;
        msgs->outMessagesBatchPeriod = LOW_PRIORITY_INTERVAL_SECS;
        bytesWritten +=  len;
    }

    /**
    ***  Metadata Pieces
    **/

    if ((tr_peerIoGetWriteBufferSpace (msgs->io, now) >= METADATA_PIECE_SIZE)
        && popNextMetadataRequest (msgs, &piece))
    {
        char * data;
        size_t dataLen;
        bool ok = false;

        data = tr_torrentGetMetadataPiece (msgs->torrent, piece, &dataLen);
        if (data != NULL)
        {
            tr_variant tmp;
            struct evbuffer * payload;
            struct evbuffer * out = msgs->outMessages;

            /* build the data message */
            tr_variantInitDict (&tmp, 3);
            tr_variantDictAddInt (&tmp, TR_KEY_msg_type, METADATA_MSG_TYPE_DATA);
            tr_variantDictAddInt (&tmp, TR_KEY_piece, piece);
            tr_variantDictAddInt (&tmp, TR_KEY_total_size, msgs->torrent->infoDictLength);
            payload = tr_variantToBuf (&tmp, TR_VARIANT_FMT_BENC);

            /* write it out as a LTEP message to our outMessages buffer */
            evbuffer_add_uint32 (out, 2 * sizeof (uint8_t) + evbuffer_get_length (payload) + dataLen);
            evbuffer_add_uint8 (out, BT_LTEP);
            evbuffer_add_uint8 (out, msgs->ut_metadata_id);
            evbuffer_add_buffer (out, payload);
            evbuffer_add     (out, data, dataLen);
            pokeBatchPeriod (msgs, HIGH_PRIORITY_INTERVAL_SECS);
            dbgOutMessageLen (msgs);

            evbuffer_free (payload);
            tr_variantFree (&tmp);
            tr_free (data);

            ok = true;
        }

        if (!ok) /* send a rejection message */
        {
            tr_variant tmp;
            struct evbuffer * payload;
            struct evbuffer * out = msgs->outMessages;

            /* build the rejection message */
            tr_variantInitDict (&tmp, 2);
            tr_variantDictAddInt (&tmp, TR_KEY_msg_type, METADATA_MSG_TYPE_REJECT);
            tr_variantDictAddInt (&tmp, TR_KEY_piece, piece);
            payload = tr_variantToBuf (&tmp, TR_VARIANT_FMT_BENC);

            /* write it out as a LTEP message to our outMessages buffer */
            evbuffer_add_uint32 (out, 2 * sizeof (uint8_t) + evbuffer_get_length (payload));
            evbuffer_add_uint8 (out, BT_LTEP);
            evbuffer_add_uint8 (out, msgs->ut_metadata_id);
            evbuffer_add_buffer (out, payload);
            pokeBatchPeriod (msgs, HIGH_PRIORITY_INTERVAL_SECS);
            dbgOutMessageLen (msgs);

            evbuffer_free (payload);
            tr_variantFree (&tmp);
        }
    }

    /**
    ***  Data Blocks
    **/

    if ((tr_peerIoGetWriteBufferSpace (msgs->io, now) >= msgs->torrent->blockSize)
        && popNextRequest (msgs, &req))
    {
        --msgs->prefetchCount;

        if (requestIsValid (msgs, &req)
            && tr_torrentPieceIsComplete (msgs->torrent, req.index))
        {
            int err;
            const uint32_t msglen = 4 + 1 + 4 + 4 + req.length;
            struct evbuffer * out;
            struct evbuffer_iovec iovec[1];

            out = evbuffer_new ();
            evbuffer_expand (out, msglen);

            evbuffer_add_uint32 (out, sizeof (uint8_t) + 2 * sizeof (uint32_t) + req.length);
            evbuffer_add_uint8 (out, BT_PIECE);
            evbuffer_add_uint32 (out, req.index);
            evbuffer_add_uint32 (out, req.offset);

            evbuffer_reserve_space (out, req.length, iovec, 1);
            err = tr_cacheReadBlock (getSession (msgs)->cache, msgs->torrent, req.index, req.offset, req.length, iovec[0].iov_base);
            iovec[0].iov_len = req.length;
            evbuffer_commit_space (out, iovec, 1);

            /* check the piece if it needs checking... */
            if (!err && tr_torrentPieceNeedsCheck (msgs->torrent, req.index))
                if ((err = !tr_torrentCheckPiece (msgs->torrent, req.index)))
                    tr_torrentSetLocalError (msgs->torrent, _("Please Verify Local Data! Piece #%zu is corrupt."), (size_t)req.index);

            if (err)
            {
                if (fext)
                    protocolSendReject (msgs, &req);
            }
            else
            {
                const size_t n = evbuffer_get_length (out);
                dbgmsg (msgs, "sending block %u:%u->%u", req.index, req.offset, req.length);
                assert (n == msglen);
                tr_peerIoWriteBuf (msgs->io, out, true);
                bytesWritten += n;
                msgs->clientSentAnythingAt = now;
                tr_historyAdd (&msgs->peer.blocksSentToPeer, tr_time (), 1);
            }

            evbuffer_free (out);

            if (err)
            {
                bytesWritten = 0;
                msgs = NULL;
            }
        }
        else if (fext) /* peer needs a reject message */
        {
            protocolSendReject (msgs, &req);
        }

        if (msgs != NULL)
            prefetchPieces (msgs);
    }

    /**
    ***  Keepalive
    **/

    if ((msgs != NULL)
        && (msgs->clientSentAnythingAt != 0)
        && ((now - msgs->clientSentAnythingAt) > KEEPALIVE_INTERVAL_SECS))
    {
        dbgmsg (msgs, "sending a keepalive message");
        evbuffer_add_uint32 (msgs->outMessages, 0);
        pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);
    }

    return bytesWritten;
}

static int
peerPulse (void * vmsgs)
{
    tr_peerMsgs * msgs = vmsgs;
    const time_t  now = tr_time ();

    if (tr_isPeerIo (msgs->io)) {
        updateDesiredRequestCount (msgs);
        updateBlockRequests (msgs);
        updateMetadataRequests (msgs, now);
    }

    for (;;)
        if (fillOutputBuffer (msgs, now) < 1)
            break;

    return true; /* loop forever */
}

void
tr_peerMsgsPulse (tr_peerMsgs * msgs)
{
    if (msgs != NULL)
        peerPulse (msgs);
}

static void
gotError (tr_peerIo * io UNUSED, short what, void * vmsgs)
{
    if (what & BEV_EVENT_TIMEOUT)
        dbgmsg (vmsgs, "libevent got a timeout, what=%hd", what);
    if (what & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
        dbgmsg (vmsgs, "libevent got an error! what=%hd, errno=%d (%s)",
               what, errno, tr_strerror (errno));
    fireError (vmsgs, ENOTCONN);
}

static void
sendBitfield (tr_peerMsgs * msgs)
{
    void * bytes;
    size_t byte_count = 0;
    struct evbuffer * out = msgs->outMessages;

    assert (tr_torrentHasMetadata (msgs->torrent));

    bytes = tr_torrentCreatePieceBitfield (msgs->torrent, &byte_count);
    evbuffer_add_uint32 (out, sizeof (uint8_t) + byte_count);
    evbuffer_add_uint8 (out, BT_BITFIELD);
    evbuffer_add     (out, bytes, byte_count);
    dbgmsg (msgs, "sending bitfield... outMessage size is now %zu", evbuffer_get_length (out));
    pokeBatchPeriod (msgs, IMMEDIATE_PRIORITY_INTERVAL_SECS);

    tr_free (bytes);
}

static void
tellPeerWhatWeHave (tr_peerMsgs * msgs)
{
    const bool fext = tr_peerIoSupportsFEXT (msgs->io);

    if (fext && tr_torrentHasAll (msgs->torrent))
    {
        protocolSendHaveAll (msgs);
    }
    else if (fext && tr_torrentHasNone (msgs->torrent))
    {
        protocolSendHaveNone (msgs);
    }
    else if (!tr_torrentHasNone (msgs->torrent))
    {
        sendBitfield (msgs);
    }
}

/**
***
**/

/* some peers give us error messages if we send
   more than this many peers in a single pex message
   http://wiki.theory.org/BitTorrentPeerExchangeConventions */
#define MAX_PEX_ADDED 50
#define MAX_PEX_DROPPED 50

typedef struct
{
    tr_pex *  added;
    tr_pex *  dropped;
    tr_pex *  elements;
    int       addedCount;
    int       droppedCount;
    int       elementCount;
}
PexDiffs;

static void
pexAddedCb (void * vpex, void * userData)
{
    PexDiffs * diffs = userData;
    tr_pex *   pex = vpex;

    if (diffs->addedCount < MAX_PEX_ADDED)
    {
        diffs->added[diffs->addedCount++] = *pex;
        diffs->elements[diffs->elementCount++] = *pex;
    }
}

static inline void
pexDroppedCb (void * vpex, void * userData)
{
    PexDiffs * diffs = userData;
    tr_pex *   pex = vpex;

    if (diffs->droppedCount < MAX_PEX_DROPPED)
    {
        diffs->dropped[diffs->droppedCount++] = *pex;
    }
}

static inline void
pexElementCb (void * vpex, void * userData)
{
    PexDiffs * diffs = userData;
    tr_pex * pex = vpex;

    diffs->elements[diffs->elementCount++] = *pex;
}

typedef void (tr_set_func)(void * element, void * userData);

/**
 * @brief find the differences and commonalities in two sorted sets
 * @param a the first set
 * @param aCount the number of elements in the set 'a'
 * @param b the second set
 * @param bCount the number of elements in the set 'b'
 * @param compare the sorting method for both sets
 * @param elementSize the sizeof the element in the two sorted sets
 * @param in_a called for items in set 'a' but not set 'b'
 * @param in_b called for items in set 'b' but not set 'a'
 * @param in_both called for items that are in both sets
 * @param userData user data passed along to in_a, in_b, and in_both
 */
static void
tr_set_compare (const void * va, size_t aCount,
                const void * vb, size_t bCount,
                int compare (const void * a, const void * b),
                size_t elementSize,
                tr_set_func in_a_cb,
                tr_set_func in_b_cb,
                tr_set_func in_both_cb,
                void * userData)
{
    const uint8_t * a = va;
    const uint8_t * b = vb;
    const uint8_t * aend = a + elementSize * aCount;
    const uint8_t * bend = b + elementSize * bCount;

    while (a != aend || b != bend)
    {
        if (a == aend)
        {
          (*in_b_cb)((void*)b, userData);
            b += elementSize;
        }
        else if (b == bend)
        {
          (*in_a_cb)((void*)a, userData);
            a += elementSize;
        }
        else
        {
            const int val = (*compare)(a, b);

            if (!val)
            {
              (*in_both_cb)((void*)a, userData);
                a += elementSize;
                b += elementSize;
            }
            else if (val < 0)
            {
              (*in_a_cb)((void*)a, userData);
                a += elementSize;
            }
            else if (val > 0)
            {
              (*in_b_cb)((void*)b, userData);
                b += elementSize;
            }
        }
    }
}


static void
sendPex (tr_peerMsgs * msgs)
{
    if (msgs->peerSupportsPex && tr_torrentAllowsPex (msgs->torrent))
    {
        PexDiffs diffs;
        PexDiffs diffs6;
        tr_pex * newPex = NULL;
        tr_pex * newPex6 = NULL;
        const int newCount = tr_peerMgrGetPeers (msgs->torrent, &newPex, TR_AF_INET, TR_PEERS_CONNECTED, MAX_PEX_PEER_COUNT);
        const int newCount6 = tr_peerMgrGetPeers (msgs->torrent, &newPex6, TR_AF_INET6, TR_PEERS_CONNECTED, MAX_PEX_PEER_COUNT);

        /* build the diffs */
        diffs.added = tr_new (tr_pex, newCount);
        diffs.addedCount = 0;
        diffs.dropped = tr_new (tr_pex, msgs->pexCount);
        diffs.droppedCount = 0;
        diffs.elements = tr_new (tr_pex, newCount + msgs->pexCount);
        diffs.elementCount = 0;
        tr_set_compare (msgs->pex, msgs->pexCount,
                        newPex, newCount,
                        tr_pexCompare, sizeof (tr_pex),
                        pexDroppedCb, pexAddedCb, pexElementCb, &diffs);
        diffs6.added = tr_new (tr_pex, newCount6);
        diffs6.addedCount = 0;
        diffs6.dropped = tr_new (tr_pex, msgs->pexCount6);
        diffs6.droppedCount = 0;
        diffs6.elements = tr_new (tr_pex, newCount6 + msgs->pexCount6);
        diffs6.elementCount = 0;
        tr_set_compare (msgs->pex6, msgs->pexCount6,
                        newPex6, newCount6,
                        tr_pexCompare, sizeof (tr_pex),
                        pexDroppedCb, pexAddedCb, pexElementCb, &diffs6);
        dbgmsg (
            msgs,
            "pex: old peer count %d+%d, new peer count %d+%d, "
            "added %d+%d, removed %d+%d",
            msgs->pexCount, msgs->pexCount6, newCount, newCount6,
            diffs.addedCount, diffs6.addedCount,
            diffs.droppedCount, diffs6.droppedCount);

        if (!diffs.addedCount && !diffs.droppedCount && !diffs6.addedCount &&
            !diffs6.droppedCount)
        {
            tr_free (diffs.elements);
            tr_free (diffs6.elements);
        }
        else
        {
            int  i;
            tr_variant val;
            uint8_t * tmp, *walk;
            struct evbuffer * payload;
            struct evbuffer * out = msgs->outMessages;

            /* update peer */
            tr_free (msgs->pex);
            msgs->pex = diffs.elements;
            msgs->pexCount = diffs.elementCount;
            tr_free (msgs->pex6);
            msgs->pex6 = diffs6.elements;
            msgs->pexCount6 = diffs6.elementCount;

            /* build the pex payload */
            tr_variantInitDict (&val, 3); /* ipv6 support: left as 3:
                                         * speed vs. likelihood? */

            if (diffs.addedCount > 0)
            {
                /* "added" */
                tmp = walk = tr_new (uint8_t, diffs.addedCount * 6);
                for (i = 0; i < diffs.addedCount; ++i) {
                    memcpy (walk, &diffs.added[i].addr.addr, 4); walk += 4;
                    memcpy (walk, &diffs.added[i].port, 2); walk += 2;
                }
                assert ((walk - tmp) == diffs.addedCount * 6);
                tr_variantDictAddRaw (&val, TR_KEY_added, tmp, walk - tmp);
                tr_free (tmp);

                /* "added.f"
                 * unset each holepunch flag because we don't support it. */
                tmp = walk = tr_new (uint8_t, diffs.addedCount);
                for (i = 0; i < diffs.addedCount; ++i)
                    *walk++ = diffs.added[i].flags & ~ADDED_F_HOLEPUNCH;
                assert ((walk - tmp) == diffs.addedCount);
                tr_variantDictAddRaw (&val, TR_KEY_added_f, tmp, walk - tmp);
                tr_free (tmp);
            }

            if (diffs.droppedCount > 0)
            {
                /* "dropped" */
                tmp = walk = tr_new (uint8_t, diffs.droppedCount * 6);
                for (i = 0; i < diffs.droppedCount; ++i) {
                    memcpy (walk, &diffs.dropped[i].addr.addr, 4); walk += 4;
                    memcpy (walk, &diffs.dropped[i].port, 2); walk += 2;
                }
                assert ((walk - tmp) == diffs.droppedCount * 6);
                tr_variantDictAddRaw (&val, TR_KEY_dropped, tmp, walk - tmp);
                tr_free (tmp);
            }

            if (diffs6.addedCount > 0)
            {
                /* "added6" */
                tmp = walk = tr_new (uint8_t, diffs6.addedCount * 18);
                for (i = 0; i < diffs6.addedCount; ++i) {
                    memcpy (walk, &diffs6.added[i].addr.addr.addr6.s6_addr, 16);
                    walk += 16;
                    memcpy (walk, &diffs6.added[i].port, 2);
                    walk += 2;
                }
                assert ((walk - tmp) == diffs6.addedCount * 18);
                tr_variantDictAddRaw (&val, TR_KEY_added6, tmp, walk - tmp);
                tr_free (tmp);

                /* "added6.f"
                 * unset each holepunch flag because we don't support it. */
                tmp = walk = tr_new (uint8_t, diffs6.addedCount);
                for (i = 0; i < diffs6.addedCount; ++i)
                    *walk++ = diffs6.added[i].flags & ~ADDED_F_HOLEPUNCH;
                assert ((walk - tmp) == diffs6.addedCount);
                tr_variantDictAddRaw (&val, TR_KEY_added6_f, tmp, walk - tmp);
                tr_free (tmp);
            }

            if (diffs6.droppedCount > 0)
            {
                /* "dropped6" */
                tmp = walk = tr_new (uint8_t, diffs6.droppedCount * 18);
                for (i = 0; i < diffs6.droppedCount; ++i) {
                    memcpy (walk, &diffs6.dropped[i].addr.addr.addr6.s6_addr, 16);
                    walk += 16;
                    memcpy (walk, &diffs6.dropped[i].port, 2);
                    walk += 2;
                }
                assert ((walk - tmp) == diffs6.droppedCount * 18);
                tr_variantDictAddRaw (&val, TR_KEY_dropped6, tmp, walk - tmp);
                tr_free (tmp);
            }

            /* write the pex message */
            payload = tr_variantToBuf (&val, TR_VARIANT_FMT_BENC);
            evbuffer_add_uint32 (out, 2 * sizeof (uint8_t) + evbuffer_get_length (payload));
            evbuffer_add_uint8 (out, BT_LTEP);
            evbuffer_add_uint8 (out, msgs->ut_pex_id);
            evbuffer_add_buffer (out, payload);
            pokeBatchPeriod (msgs, HIGH_PRIORITY_INTERVAL_SECS);
            dbgmsg (msgs, "sending a pex message; outMessage size is now %zu", evbuffer_get_length (out));
            dbgOutMessageLen (msgs);

            evbuffer_free (payload);
            tr_variantFree (&val);
        }

        /* cleanup */
        tr_free (diffs.added);
        tr_free (diffs.dropped);
        tr_free (newPex);
        tr_free (diffs6.added);
        tr_free (diffs6.dropped);
        tr_free (newPex6);

        /*msgs->clientSentPexAt = tr_time ();*/
    }
}

static void
pexPulse (evutil_socket_t foo UNUSED, short bar UNUSED, void * vmsgs)
{
    struct tr_peerMsgs * msgs = vmsgs;

    sendPex (msgs);

    assert (msgs->pexTimer != NULL);
    tr_timerAdd (msgs->pexTimer, PEX_INTERVAL_SECS, 0);
}

/***
****  tr_peer virtual functions
***/

static bool
peermsgs_is_transferring_pieces (const struct tr_peer * peer,
                                 uint64_t               now,
                                 tr_direction           direction,
                                 unsigned int         * setme_Bps)
{
  unsigned int Bps = 0;

  if (tr_isPeerMsgs (peer))
    {
      const tr_peerMsgs * msgs = (const tr_peerMsgs *) peer;
      Bps = tr_peerIoGetPieceSpeed_Bps (msgs->io, now, direction);
    }

  if (setme_Bps != NULL)
    *setme_Bps = Bps;

  return Bps > 0;
}

static void
peermsgs_destruct (tr_peer * peer)
{
  tr_peerMsgs * msgs = PEER_MSGS (peer);

  assert (msgs != NULL);

  tr_peerMsgsSetActive (msgs, TR_UP, false);
  tr_peerMsgsSetActive (msgs, TR_DOWN, false);

  if (msgs->pexTimer != NULL)
    event_free (msgs->pexTimer);

  if (msgs->incoming.block != NULL)
    evbuffer_free (msgs->incoming.block);

  if (msgs->io)
    {
      tr_peerIoClear (msgs->io);
      tr_peerIoUnref (msgs->io); /* balanced by the ref in handshakeDoneCB () */
    }

  evbuffer_free (msgs->outMessages);
  tr_free (msgs->pex6);
  tr_free (msgs->pex);

  tr_peerDestruct (&msgs->peer);

  memset (msgs, ~0, sizeof (tr_peerMsgs));
}

static const struct tr_peer_virtual_funcs my_funcs =
{
  .destruct = peermsgs_destruct,
  .is_transferring_pieces = peermsgs_is_transferring_pieces
};

/***
****
***/

time_t
tr_peerMsgsGetConnectionAge (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return tr_peerIoGetAge (msgs->io);
}

bool
tr_peerMsgsIsPeerChoked (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return msgs->peer_is_choked;
}

bool
tr_peerMsgsIsPeerInterested (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return msgs->peer_is_interested;
}

bool
tr_peerMsgsIsClientChoked (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return msgs->client_is_choked;
}

bool
tr_peerMsgsIsClientInterested (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return msgs->client_is_interested;
}

bool
tr_peerMsgsIsUtpConnection (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return msgs->io->utp_socket != NULL;
}

bool
tr_peerMsgsIsEncrypted (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return tr_peerIoIsEncrypted (msgs->io);
}

bool
tr_peerMsgsIsIncomingConnection (const tr_peerMsgs * msgs)
{
  assert (tr_isPeerMsgs (msgs));

  return tr_peerIoIsIncoming (msgs->io);
}

/***
****
***/

bool
tr_isPeerMsgs (const void * msgs)
{
  /* FIXME: this is pretty crude */
  return (msgs != NULL)
      && (((struct tr_peerMsgs*)msgs)->magic_number == MAGIC_NUMBER);
}

tr_peerMsgs *
tr_peerMsgsCast (void * vm)
{
  return tr_isPeerMsgs(vm) ? vm : NULL;
}

tr_peerMsgs *
tr_peerMsgsNew (struct tr_torrent    * torrent,
                struct tr_peerIo     * io,
                tr_peer_callback       callback,
                void                 * callbackData)
{
  tr_peerMsgs * m;

  assert (io != NULL);

  m = tr_new0 (tr_peerMsgs, 1);

  tr_peerConstruct (&m->peer, torrent);
  m->peer.funcs = &my_funcs;

  m->magic_number = MAGIC_NUMBER;
  m->client_is_choked = true;
  m->peer_is_choked = true;
  m->client_is_interested = false;
  m->peer_is_interested = false;
  m->is_active[TR_UP] = false;
  m->is_active[TR_DOWN] = false;
  m->callback = callback;
  m->callbackData = callbackData;
  m->io = io;
  m->torrent = torrent;
  m->state = AWAITING_BT_LENGTH;
  m->outMessages = evbuffer_new ();
  m->outMessagesBatchedAt = 0;
  m->outMessagesBatchPeriod = LOW_PRIORITY_INTERVAL_SECS;

  if (tr_torrentAllowsPex (torrent))
    {
      m->pexTimer = evtimer_new (torrent->session->event_base, pexPulse, m);
      tr_timerAdd (m->pexTimer, PEX_INTERVAL_SECS, 0);
    }

  if (tr_peerIoSupportsUTP (m->io))
    {
      const tr_address * addr = tr_peerIoGetAddress (m->io, NULL);
      tr_peerMgrSetUtpSupported (torrent, addr);
      tr_peerMgrSetUtpFailed (torrent, addr, false);
    }

  if (tr_peerIoSupportsLTEP (m->io))
    sendLtepHandshake (m);

  tellPeerWhatWeHave (m);

  if (tr_dhtEnabled (torrent->session) && tr_peerIoSupportsDHT (m->io))
    {
      /* Only send PORT over IPv6 when the IPv6 DHT is running (BEP-32). */
      const struct tr_address *addr = tr_peerIoGetAddress (m->io, NULL);
      if (addr->type == TR_AF_INET || tr_globalIPv6 ())
        protocolSendPort (m, tr_dhtPort (torrent->session));
    }

  tr_peerIoSetIOFuncs (m->io, canRead, didWrite, gotError, m);
  updateDesiredRequestCount (m);

  return m;
}
