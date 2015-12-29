/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* memset () */

#include "transmission.h"
#include "bandwidth.h"
#include "crypto-utils.h" /* tr_rand_int_weak () */
#include "log.h"
#include "peer-io.h"
#include "utils.h"

#define dbgmsg(...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        tr_logAddDeep (__FILE__, __LINE__, NULL, __VA_ARGS__); \
    } \
  while (0)

/***
****
***/

static unsigned int
getSpeed_Bps (const struct bratecontrol * r, unsigned int interval_msec, uint64_t now)
{
  if (!now)
    now = tr_time_msec ();

  if (now != r->cache_time)
    {
      int i = r->newest;
      uint64_t bytes = 0;
      const uint64_t cutoff = now - interval_msec;
      struct bratecontrol * rvolatile = (struct bratecontrol*) r;

      for (;;)
        {
          if (r->transfers[i].date <= cutoff)
            break;

          bytes += r->transfers[i].size;

          if (--i == -1)
            i = HISTORY_SIZE - 1; /* circular history */

          if (i == r->newest)
            break; /* we've come all the way around */
        }

      rvolatile->cache_val = (unsigned int)((bytes * 1000u) / interval_msec);
      rvolatile->cache_time = now;
    }

  return r->cache_val;
}

static void
bytesUsed (const uint64_t now, struct bratecontrol * r, size_t size)
{
  if (r->transfers[r->newest].date + GRANULARITY_MSEC >= now)
    {
      r->transfers[r->newest].size += size;
    }
  else
    {
      if (++r->newest == HISTORY_SIZE)
        r->newest = 0;
      r->transfers[r->newest].date = now;
      r->transfers[r->newest].size = size;
    }

  /* invalidate cache_val*/
  r->cache_time = 0;
}

/******
*******
*******
******/

static int
compareBandwidth (const void * va, const void * vb)
{
  const tr_bandwidth * a = va;
  const tr_bandwidth * b = vb;
  return a->uniqueKey - b->uniqueKey;
}

/***
****
***/

void
tr_bandwidthConstruct (tr_bandwidth * b, tr_session * session, tr_bandwidth * parent)
{
  static unsigned int uniqueKey = 0;

  b->session = session;
  b->children = TR_PTR_ARRAY_INIT;
  b->magicNumber = BANDWIDTH_MAGIC_NUMBER;
  b->uniqueKey = uniqueKey++;
  b->band[TR_UP].honorParentLimits = true;
  b->band[TR_DOWN].honorParentLimits = true;
  tr_bandwidthSetParent (b, parent);
}

void
tr_bandwidthDestruct (tr_bandwidth * b)
{
  assert (tr_isBandwidth (b));

  tr_bandwidthSetParent (b, NULL);
  tr_ptrArrayDestruct (&b->children, NULL);

  memset (b, ~0, sizeof (tr_bandwidth));
}

/***
****
***/

void
tr_bandwidthSetParent (tr_bandwidth  * b,
                       tr_bandwidth  * parent)
{
  assert (tr_isBandwidth (b));
  assert (b != parent);

  if (b->parent)
    {
      assert (tr_isBandwidth (b->parent));
      tr_ptrArrayRemoveSortedPointer (&b->parent->children, b, compareBandwidth);
      b->parent = NULL;
    }

  if (parent)
    {
      assert (tr_isBandwidth (parent));
      assert (parent->parent != b);

      assert (tr_ptrArrayFindSorted (&parent->children, b, compareBandwidth) == NULL);
      tr_ptrArrayInsertSorted (&parent->children, b, compareBandwidth);
      assert (tr_ptrArrayFindSorted (&parent->children, b, compareBandwidth) == b);
      b->parent = parent;
    }
}

/***
****
***/

static void
allocateBandwidth (tr_bandwidth  * b,
                   tr_priority_t   parent_priority,
                   tr_direction    dir,
                   unsigned int    period_msec,
                   tr_ptrArray   * peer_pool)
{
  const tr_priority_t priority = MAX (parent_priority, b->priority);

  assert (tr_isBandwidth (b));
  assert (tr_isDirection (dir));

  /* set the available bandwidth */
  if (b->band[dir].isLimited)
    {
      const uint64_t nextPulseSpeed = b->band[dir].desiredSpeed_Bps;
      b->band[dir].bytesLeft = nextPulseSpeed * period_msec / 1000u;
    }

  /* add this bandwidth's peer, if any, to the peer pool */
  if (b->peer != NULL)
    {
      b->peer->priority = priority;
      tr_ptrArrayAppend (peer_pool, b->peer);
    }

  /* traverse & repeat for the subtree */
  if (1)
    {
      int i;
      struct tr_bandwidth ** children = (struct tr_bandwidth**) tr_ptrArrayBase (&b->children);
      const int n = tr_ptrArraySize (&b->children);
      for (i=0; i<n; ++i)
        allocateBandwidth (children[i], priority, dir, period_msec, peer_pool);
    }
}

static void
phaseOne (tr_ptrArray * peerArray, tr_direction dir)
{
  int n;
  int peerCount = tr_ptrArraySize (peerArray);
  struct tr_peerIo ** peers = (struct tr_peerIo**) tr_ptrArrayBase (peerArray);

  /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
   * peers from starving the others. Loop through the peers, giving each a
   * small chunk of bandwidth. Keep looping until we run out of bandwidth
   * and/or peers that can use it */
  n = peerCount;
  dbgmsg ("%d peers to go round-robin for %s", n, (dir==TR_UP?"upload":"download"));
  while (n > 0)
    {
      const int i = tr_rand_int_weak (n); /* pick a peer at random */

      /* value of 3000 bytes chosen so that when using uTP we'll send a full-size
       * frame right away and leave enough buffered data for the next frame to go
       * out in a timely manner. */
      const size_t increment = 3000;

      const int bytesUsed = tr_peerIoFlush (peers[i], dir, increment);

      dbgmsg ("peer #%d of %d used %d bytes in this pass", i, n, bytesUsed);

      if (bytesUsed != (int)increment)
        {
          /* peer is done writing for now; move it to the end of the list */
          tr_peerIo * pio = peers[i];
          peers[i] = peers[n-1];
          peers[n-1] = pio;
          --n;
        }
    }
}

void
tr_bandwidthAllocate (tr_bandwidth  * b,
                      tr_direction    dir,
                      unsigned int    period_msec)
{
  int i, peerCount;
  tr_ptrArray tmp = TR_PTR_ARRAY_INIT;
  tr_ptrArray low = TR_PTR_ARRAY_INIT;
  tr_ptrArray high = TR_PTR_ARRAY_INIT;
  tr_ptrArray normal = TR_PTR_ARRAY_INIT;
  struct tr_peerIo ** peers;

  /* allocateBandwidth () is a helper function with two purposes:
   * 1. allocate bandwidth to b and its subtree
   * 2. accumulate an array of all the peerIos from b and its subtree. */
  allocateBandwidth (b, TR_PRI_LOW, dir, period_msec, &tmp);
  peers = (struct tr_peerIo**) tr_ptrArrayBase (&tmp);
  peerCount = tr_ptrArraySize (&tmp);

  for (i=0; i<peerCount; ++i)
    {
      tr_peerIo * io = peers[i];
      tr_peerIoRef (io);

      tr_peerIoFlushOutgoingProtocolMsgs (io);

      switch (io->priority)
        {
          case TR_PRI_HIGH:   tr_ptrArrayAppend (&high,   io); /* fall through */
          case TR_PRI_NORMAL: tr_ptrArrayAppend (&normal, io); /* fall through */
          default:            tr_ptrArrayAppend (&low,    io);
        }
    }

  /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
   * peers from starving the others. Loop through the peers, giving each a
   * small chunk of bandwidth. Keep looping until we run out of bandwidth
   * and/or peers that can use it */
  phaseOne (&high, dir);
  phaseOne (&normal, dir);
  phaseOne (&low, dir);

  /* Second phase of IO. To help us scale in high bandwidth situations,
   * enable on-demand IO for peers with bandwidth left to burn.
   * This on-demand IO is enabled until (1) the peer runs out of bandwidth,
   * or (2) the next tr_bandwidthAllocate () call, when we start over again. */
  for (i=0; i<peerCount; ++i)
    tr_peerIoSetEnabled (peers[i], dir, tr_peerIoHasBandwidthLeft (peers[i], dir));

  for (i=0; i<peerCount; ++i)
    tr_peerIoUnref (peers[i]);

  /* cleanup */
  tr_ptrArrayDestruct (&normal, NULL);
  tr_ptrArrayDestruct (&high, NULL);
  tr_ptrArrayDestruct (&low, NULL);
  tr_ptrArrayDestruct (&tmp, NULL);
}

void
tr_bandwidthSetPeer (tr_bandwidth * b, tr_peerIo * peer)
{
  assert (tr_isBandwidth (b));
  assert ((peer == NULL) || tr_isPeerIo (peer));

  b->peer = peer;
}

/***
****
***/

static unsigned int
bandwidthClamp (const tr_bandwidth  * b,
                uint64_t              now,
                tr_direction          dir,
                unsigned int          byteCount)
{
  assert (tr_isBandwidth (b));
  assert (tr_isDirection (dir));

  if (b)
    {
      if (b->band[dir].isLimited)
        {
          byteCount = MIN (byteCount, b->band[dir].bytesLeft);

          /* if we're getting close to exceeding the speed limit,
           * clamp down harder on the bytes available */
          if (byteCount > 0)
            {
              double current;
              double desired;
              double r;

              if (now == 0)
                now = tr_time_msec ();

              current = tr_bandwidthGetRawSpeed_Bps (b, now, TR_DOWN);
              desired = tr_bandwidthGetDesiredSpeed_Bps (b, TR_DOWN);
              r = desired >= 1 ? current / desired : 0;

                   if (r > 1.0) byteCount = 0;
              else if (r > 0.9) byteCount *= 0.8;
              else if (r > 0.8) byteCount *= 0.9;
            }
        }

      if (b->parent && b->band[dir].honorParentLimits && (byteCount > 0))
        byteCount = bandwidthClamp (b->parent, now, dir, byteCount);
    }

  return byteCount;
}

unsigned int
tr_bandwidthClamp (const tr_bandwidth  * b,
                   tr_direction          dir,
                   unsigned int          byteCount)
{
  return bandwidthClamp (b, 0, dir, byteCount);
}


unsigned int
tr_bandwidthGetRawSpeed_Bps (const tr_bandwidth * b, const uint64_t now, const tr_direction dir)
{
  assert (tr_isBandwidth (b));
  assert (tr_isDirection (dir));

  return getSpeed_Bps (&b->band[dir].raw, HISTORY_MSEC, now);
}

unsigned int
tr_bandwidthGetPieceSpeed_Bps (const tr_bandwidth * b, const uint64_t now, const tr_direction dir)
{
  assert (tr_isBandwidth (b));
  assert (tr_isDirection (dir));

  return getSpeed_Bps (&b->band[dir].piece, HISTORY_MSEC, now);
}

void
tr_bandwidthUsed (tr_bandwidth  * b,
                  tr_direction    dir,
                  size_t          byteCount,
                  bool            isPieceData,
                  uint64_t        now)
{
  struct tr_band * band;

  assert (tr_isBandwidth (b));
  assert (tr_isDirection (dir));

  band = &b->band[dir];

  if (band->isLimited && isPieceData)
    band->bytesLeft -= MIN (band->bytesLeft, byteCount);

#ifdef DEBUG_DIRECTION
if ((dir == DEBUG_DIRECTION) && (band->isLimited))
fprintf (stderr, "%p consumed %5zu bytes of %5s data... was %6zu, now %6zu left\n",
         b, byteCount, (isPieceData?"piece":"raw"), oldBytesLeft, band->bytesLeft);
#endif

  bytesUsed (now, &band->raw, byteCount);

  if (isPieceData)
    bytesUsed (now, &band->piece, byteCount);

  if (b->parent != NULL)
    tr_bandwidthUsed (b->parent, dir, byteCount, isPieceData, now);
}
