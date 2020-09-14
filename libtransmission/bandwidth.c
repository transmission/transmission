/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* memset() */
#include <stdio.h>

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "crypto-utils.h" /* tr_rand_int_weak() */
#include "log.h"
#include "peer-io.h"
#include "tr-assert.h"
#include "utils.h"
#include "error.h"

#define dbgmsg(...) tr_logAddDeepNamed(NULL, __VA_ARGS__)

/***
****
***/

static unsigned int getSpeed_Bps(struct bratecontrol const* r, unsigned int interval_msec, uint64_t now)
{
    if (now == 0)
    {
        now = tr_time_msec();
    }

    if (now != r->cache_time)
    {
        uint64_t bytes = 0;
        uint64_t const cutoff = now - interval_msec;
        struct bratecontrol* rvolatile = (struct bratecontrol*)r;

        for (int i = r->newest; r->transfers[i].date > cutoff;)
        {
            bytes += r->transfers[i].size;

            if (--i == -1)
            {
                i = HISTORY_SIZE - 1; /* circular history */
            }

            if (i == r->newest)
            {
                break; /* we've come all the way around */
            }
        }

        rvolatile->cache_val = (unsigned int)(bytes * 1000U / interval_msec);
        rvolatile->cache_time = now;
    }

    return r->cache_val;
}

static void bytesUsed(uint64_t const now, struct bratecontrol* r, size_t size)
{
    if (r->transfers[r->newest].date + GRANULARITY_MSEC >= now)
    {
        r->transfers[r->newest].size += size;
    }
    else
    {
        if (++r->newest == HISTORY_SIZE)
        {
            r->newest = 0;
        }

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

static int compareBandwidth(void const* va, void const* vb)
{
    tr_bandwidth const* a = va;
    tr_bandwidth const* b = vb;
    return a->uniqueKey - b->uniqueKey;
}

/***
****
***/

void tr_bandwidthConstruct(tr_bandwidth* b, tr_session* session, tr_bandwidth* parent)
{
    static unsigned int uniqueKey = 0;

    b->session = session;
    b->children = TR_PTR_ARRAY_INIT;
    b->magicNumber = BANDWIDTH_MAGIC_NUMBER;
    b->uniqueKey = uniqueKey++;
    b->band[TR_UP].honorParentLimits = true;
    b->band[TR_DOWN].honorParentLimits = true;
    tr_bandwidthSetParent(b, parent);
}

void tr_bandwidthDestruct(tr_bandwidth* b)
{
    TR_ASSERT(tr_isBandwidth(b));

    tr_bandwidthSetParent(b, NULL);
    tr_ptrArrayDestruct(&b->children, NULL);

    memset(b, ~0, sizeof(tr_bandwidth));
}

/***
****
***/

void tr_bandwidthSetParent(tr_bandwidth* b, tr_bandwidth* parent)
{
    TR_ASSERT(tr_isBandwidth(b));
    TR_ASSERT(b != parent);

    if (b->parent != NULL)
    {
        TR_ASSERT(tr_isBandwidth(b->parent));
        tr_ptrArrayRemoveSortedPointer(&b->parent->children, b, compareBandwidth);
        b->parent = NULL;
    }

    if (parent != NULL)
    {
        TR_ASSERT(tr_isBandwidth(parent));
        TR_ASSERT(parent->parent != b);

        TR_ASSERT(tr_ptrArrayFindSorted(&parent->children, b, compareBandwidth) == NULL);
        tr_ptrArrayInsertSorted(&parent->children, b, compareBandwidth);
        TR_ASSERT(tr_ptrArrayFindSorted(&parent->children, b, compareBandwidth) == b);
        b->parent = parent;
    }
}

/***
****
***/

static void allocateBandwidth(tr_bandwidth* b, tr_priority_t parent_priority, tr_direction dir, unsigned int period_msec,
    tr_ptrArray* peer_pool)
{
    TR_ASSERT(tr_isBandwidth(b));
    TR_ASSERT(tr_isDirection(dir));

    tr_priority_t const priority = MAX(parent_priority, b->priority);

    /* set the available bandwidth */
    if (b->band[dir].isLimited)
    {
        uint64_t const nextPulseSpeed = b->band[dir].desiredSpeed_Bps;
        b->band[dir].bytesLeft = nextPulseSpeed * period_msec / 1000U;
    }

    /* add this bandwidth's peer, if any, to the peer pool */
    if (b->peer != NULL)
    {
        b->peer->priority = priority;
        tr_ptrArrayAppend(peer_pool, b->peer);
    }

    /* traverse & repeat for the subtree */
    if (true)
    {
        struct tr_bandwidth** children = (struct tr_bandwidth**)tr_ptrArrayBase(&b->children);

        for (int i = 0, n = tr_ptrArraySize(&b->children); i < n; ++i)
        {
            allocateBandwidth(children[i], priority, dir, period_msec, peer_pool);
        }
    }
}

static void phaseOne(tr_ptrArray* peerArray, tr_direction dir)
{
    int n;
    int peerCount = tr_ptrArraySize(peerArray);
    struct tr_peerIo** peers = (struct tr_peerIo**)tr_ptrArrayBase(peerArray);

    /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others. Loop through the peers, giving each a
     * small chunk of bandwidth. Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    n = peerCount;
    dbgmsg("%d peers to go round-robin for %s", n, dir == TR_UP ? "upload" : "download");

    while (n > 0)
    {
        int const i = tr_rand_int_weak(n); /* pick a peer at random */

        /* value of 3000 bytes chosen so that when using uTP we'll send a full-size
         * frame right away and leave enough buffered data for the next frame to go
         * out in a timely manner. */
        size_t const increment = 3000;

        int const bytesUsed = tr_peerIoFlush(peers[i], dir, increment);

        dbgmsg("peer #%d of %d used %d bytes in this pass", i, n, bytesUsed);

        if (bytesUsed != (int)increment)
        {
            /* peer is done writing for now; move it to the end of the list */
            tr_peerIo* pio = peers[i];
            peers[i] = peers[n - 1];
            peers[n - 1] = pio;
            --n;
        }
    }
}

void tr_bandwidthAllocate(tr_bandwidth* b, tr_direction dir, unsigned int period_msec)
{
    int peerCount;
    tr_ptrArray tmp = TR_PTR_ARRAY_INIT;
    tr_ptrArray low = TR_PTR_ARRAY_INIT;
    tr_ptrArray high = TR_PTR_ARRAY_INIT;
    tr_ptrArray normal = TR_PTR_ARRAY_INIT;
    struct tr_peerIo** peers;

    /* allocateBandwidth () is a helper function with two purposes:
     * 1. allocate bandwidth to b and its subtree
     * 2. accumulate an array of all the peerIos from b and its subtree. */
    allocateBandwidth(b, TR_PRI_LOW, dir, period_msec, &tmp);
    peers = (struct tr_peerIo**)tr_ptrArrayBase(&tmp);
    peerCount = tr_ptrArraySize(&tmp);

    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerIo* io = peers[i];
        tr_peerIoRef(io);

        tr_peerIoFlushOutgoingProtocolMsgs(io);

        switch (io->priority)
        {
        case TR_PRI_HIGH:
            tr_ptrArrayAppend(&high, io); /* fall through */

        case TR_PRI_NORMAL:
            tr_ptrArrayAppend(&normal, io); /* fall through */

        default:
            tr_ptrArrayAppend(&low, io);
        }
    }

    /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others. Loop through the peers, giving each a
     * small chunk of bandwidth. Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    phaseOne(&high, dir);
    phaseOne(&normal, dir);
    phaseOne(&low, dir);

    /* Second phase of IO. To help us scale in high bandwidth situations,
     * enable on-demand IO for peers with bandwidth left to burn.
     * This on-demand IO is enabled until (1) the peer runs out of bandwidth,
     * or (2) the next tr_bandwidthAllocate () call, when we start over again. */
    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerIoSetEnabled(peers[i], dir, tr_peerIoHasBandwidthLeft(peers[i], dir));
    }

    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerIoUnref(peers[i]);
    }

    /* cleanup */
    tr_ptrArrayDestruct(&normal, NULL);
    tr_ptrArrayDestruct(&high, NULL);
    tr_ptrArrayDestruct(&low, NULL);
    tr_ptrArrayDestruct(&tmp, NULL);
}

void tr_bandwidthSetPeer(tr_bandwidth* b, tr_peerIo* peer)
{
    TR_ASSERT(tr_isBandwidth(b));
    TR_ASSERT(peer == NULL || tr_isPeerIo(peer));

    b->peer = peer;
}

/***
****
***/

static unsigned int bandwidthClamp(tr_bandwidth const* b, uint64_t now, tr_direction dir, unsigned int byteCount)
{
    TR_ASSERT(tr_isBandwidth(b));
    TR_ASSERT(tr_isDirection(dir));

    if (b != NULL)
    {
        if (b->band[dir].isLimited)
        {
            byteCount = MIN(byteCount, b->band[dir].bytesLeft);

            /* if we're getting close to exceeding the speed limit,
             * clamp down harder on the bytes available */
            if (byteCount > 0)
            {
                double current;
                double desired;
                double r;

                if (now == 0)
                {
                    now = tr_time_msec();
                }

                current = tr_bandwidthGetRawSpeed_Bps(b, now, TR_DOWN);
                desired = tr_bandwidthGetDesiredSpeed_Bps(b, TR_DOWN);
                r = desired >= 1 ? current / desired : 0;

                if (r > 1.0)
                {
                    byteCount = 0;
                }
                else if (r > 0.9)
                {
                    byteCount *= 0.8;
                }
                else if (r > 0.8)
                {
                    byteCount *= 0.9;
                }
            }
        }

        if (b->parent != NULL && b->band[dir].honorParentLimits && byteCount > 0)
        {
            byteCount = bandwidthClamp(b->parent, now, dir, byteCount);
        }
    }

    return byteCount;
}

unsigned int tr_bandwidthClamp(tr_bandwidth const* b, tr_direction dir, unsigned int byteCount)
{
    return bandwidthClamp(b, 0, dir, byteCount);
}

unsigned int tr_bandwidthGetRawSpeed_Bps(tr_bandwidth const* b, uint64_t const now, tr_direction const dir)
{
    TR_ASSERT(tr_isBandwidth(b));
    TR_ASSERT(tr_isDirection(dir));

    return getSpeed_Bps(&b->band[dir].raw, HISTORY_MSEC, now);
}

unsigned int tr_bandwidthGetPieceSpeed_Bps(tr_bandwidth const* b, uint64_t const now, tr_direction const dir)
{
    TR_ASSERT(tr_isBandwidth(b));
    TR_ASSERT(tr_isDirection(dir));

    return getSpeed_Bps(&b->band[dir].piece, HISTORY_MSEC, now);
}

void tr_bandwidthUsed(tr_bandwidth* b, tr_direction dir, size_t byteCount, bool isPieceData, uint64_t now)
{
    TR_ASSERT(tr_isBandwidth(b));
    TR_ASSERT(tr_isDirection(dir));

    struct tr_band* band = &b->band[dir];

    if (band->isLimited && isPieceData)
    {
        band->bytesLeft -= MIN(band->bytesLeft, byteCount);
    }

#ifdef DEBUG_DIRECTION

    if (dir == DEBUG_DIRECTION && band->isLimited)
    {
        fprintf(stderr, "%p consumed %5zu bytes of %5s data... was %6zu, now %6zu left\n", b, byteCount,
            isPieceData ? "piece" : "raw", oldBytesLeft, band->bytesLeft);
    }

#endif

    bytesUsed(now, &band->raw, byteCount);

    if (isPieceData)
    {
        bytesUsed(now, &band->piece, byteCount);
    }

    if (b->parent != NULL)
    {
        tr_bandwidthUsed(b->parent, dir, byteCount, isPieceData, now);
    }
}

/***
****
***/

void tr_bandwidthGroupGetLimits(tr_bandwidth_group* group, bool* up_limited, unsigned int* up_limit, bool* down_limited,
    unsigned int* down_limit)
{
    *up_limit = tr_bandwidthGetDesiredSpeed_Bps(&group->bandwidth, TR_UP);
    *down_limit = tr_bandwidthGetDesiredSpeed_Bps(&group->bandwidth, TR_DOWN);
    *up_limited = tr_bandwidthIsLimited(&group->bandwidth, TR_UP);
    *down_limited = tr_bandwidthIsLimited(&group->bandwidth, TR_DOWN);
}

void tr_bandwidthGroupSetLimits(tr_bandwidth_group* group, bool up_limited, unsigned int up_limit, bool down_limited,
    unsigned int down_limit)
{
    tr_bandwidthSetDesiredSpeed_Bps(&group->bandwidth, TR_UP, up_limit);
    tr_bandwidthSetDesiredSpeed_Bps(&group->bandwidth, TR_DOWN, down_limit);
    tr_bandwidthSetLimited(&group->bandwidth, TR_UP, up_limited);
    tr_bandwidthSetLimited(&group->bandwidth, TR_DOWN, down_limited);
}

/* Should only be called if the group does not exist */
tr_bandwidth_group* tr_bandwidthGroupNew(tr_session* session, char const* name)
{
    tr_bandwidth_group* group;
    group = tr_new0(tr_bandwidth_group, 1);
    group->next = NULL;
    group->name = tr_strdup(name);
    tr_bandwidthConstruct(&group->bandwidth, session, &session->bandwidth);
    return group;
}

/* Find a bandwidth group by name. Create if it does not exist */
tr_bandwidth_group* tr_bandwidthGroupFind(tr_session* session, char const* name)
{
    tr_bandwidth_group* group;
    tr_bandwidth_group* last;

    group = session->groups;
    last = NULL;

    while (group && strcmp(name, group->name))
    {
        last = group;
        group = group->next;
    }

    if (group)
    {
        return group;
    }
    else
    {
        group = tr_bandwidthGroupNew(session, name);
        if (last)
        {
            last->next = group;
        }
        else
        {
            session->groups = group;
        }

        return group;
    }
}

void tr_bandwidthGroupRead(tr_session* session, char const* configDir)
{
    char* filename;
    char* buf;
    size_t len;

    filename = tr_buildPath(configDir, "bandwidthGroups", NULL);
    buf = (char*)tr_loadFile(filename, &len, NULL);
    if (buf)
    {
        char* line = buf - 1;
        char* next;
        while (line)
        {
            int u, d, n;
            uint32_t up, down;
            char s[64];
            line++;
            next = strchr(line, '\n');
            if ((next - line < 64) && (line[0] != '#'))
            {
                n = sscanf(line, "%s %d %u %d %u", s, &u, &up, &d, &down);
                if (n == 5)
                {
                    tr_bandwidth_group* group;
                    group = tr_bandwidthGroupFind(session, s);
                    tr_bandwidthGroupSetLimits(group, u, up, d, down);
                }
            }

            line = next;
        }

        tr_free(buf);
    }

    tr_free(filename);
}

int tr_bandwidthGroupWrite(tr_session* session, char const* configDir)
{
    char* filename;
    int fd;
    tr_error* error = NULL;
    int err = 0;

    filename = tr_buildPath(configDir, "bandwidthGroups", NULL);
    fd = tr_sys_file_open(filename, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, &error);

    if (fd != TR_BAD_SYS_FILE)
    {
        tr_bandwidth_group* group;

        group = session->groups;
        while (group)
        {
            bool u, d;
            uint32_t up, down;
            tr_bandwidthGroupGetLimits(group, &u, &up, &d, &down);
            dprintf(fd, "%s %d %u %d %u\n", group->name, u, up, d, down);
            group = group->next;
        }

        tr_sys_file_close(fd, NULL);
    }
    else
    {
        err = error->code;
        tr_logAddError(_("Couldn't save temporary file \"%1$s\": %2$s"), filename, error->message);
        tr_error_free(error);
    }

    tr_free(filename);
    return err;
}
