/*
 * Copyright (c) 2009-2010 by Juliusz Chroboczek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 */

/* ansi */
#include <errno.h>
#include <stdio.h>
#include <string.h> /* memcpy(), memset(), memchr(), strlen() */
#include <stdlib.h> /* atoi() */

/* posix */
#include <signal.h> /* sig_atomic_t */

#ifdef _WIN32
#include <inttypes.h>
#include <ws2tcpip.h>
#undef gai_strerror
#define gai_strerror gai_strerrorA
#else
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <netdb.h>
#include <netinet/in.h> /* sockaddr_in */
#endif

/* third party */
#include <event2/event.h>
#include <dht/dht.h>

/* libT */
#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h" /* tr_peerMgrCompactToPex() */
#include "platform.h" /* tr_threadNew() */
#include "session.h"
#include "torrent.h" /* tr_torrentFindFromHash() */
#include "tr-assert.h"
#include "tr-dht.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "variant.h"

static struct event* dht_timer = NULL;
static unsigned char myid[20];
static tr_session* session = NULL;

static void timer_callback(evutil_socket_t s, short type, void* ignore);

struct bootstrap_closure
{
    tr_session* session;
    uint8_t* nodes;
    uint8_t* nodes6;
    size_t len;
    size_t len6;
};

static bool bootstrap_done(tr_session* session, int af)
{
    int status;

    if (af == 0)
    {
        return bootstrap_done(session, AF_INET) && bootstrap_done(session, AF_INET6);
    }

    status = tr_dhtStatus(session, af, NULL);
    return status == TR_DHT_STOPPED || status >= TR_DHT_FIREWALLED;
}

static void nap(int roughly_sec)
{
    int const roughly_msec = roughly_sec * 1000;
    int const msec = roughly_msec / 2 + tr_rand_int_weak(roughly_msec);
    tr_wait_msec(msec);
}

static int bootstrap_af(tr_session* session)
{
    if (bootstrap_done(session, AF_INET6))
    {
        return AF_INET;
    }
    else if (bootstrap_done(session, AF_INET))
    {
        return AF_INET6;
    }
    else
    {
        return 0;
    }
}

static void bootstrap_from_name(char const* name, tr_port port, int af)
{
    struct addrinfo hints;
    struct addrinfo* info;
    struct addrinfo* infop;
    char pp[10];
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = af;
    /* No, just passing p + 1 to gai won't work. */
    tr_snprintf(pp, sizeof(pp), "%d", (int)port);

    rc = getaddrinfo(name, pp, &hints, &info);

    if (rc != 0)
    {
        tr_logAddNamedError("DHT", "%s:%s: %s", name, pp, gai_strerror(rc));
        return;
    }

    infop = info;

    while (infop != NULL)
    {
        dht_ping_node(infop->ai_addr, infop->ai_addrlen);

        nap(15);

        if (bootstrap_done(session, af))
        {
            break;
        }

        infop = infop->ai_next;
    }

    freeaddrinfo(info);
}

static void dht_bootstrap(void* closure)
{
    struct bootstrap_closure* cl = closure;
    int num = cl->len / 6;
    int num6 = cl->len6 / 18;

    if (session != cl->session)
    {
        return;
    }

    if (cl->len > 0)
    {
        tr_logAddNamedInfo("DHT", "Bootstrapping from %d IPv4 nodes", num);
    }

    if (cl->len6 > 0)
    {
        tr_logAddNamedInfo("DHT", "Bootstrapping from %d IPv6 nodes", num6);
    }

    for (int i = 0; i < MAX(num, num6); ++i)
    {
        if (i < num && !bootstrap_done(cl->session, AF_INET))
        {
            tr_port port;
            struct tr_address addr;

            memset(&addr, 0, sizeof(addr));
            addr.type = TR_AF_INET;
            memcpy(&addr.addr.addr4, &cl->nodes[i * 6], 4);
            memcpy(&port, &cl->nodes[i * 6 + 4], 2);
            port = ntohs(port);
            tr_dhtAddNode(cl->session, &addr, port, 1);
        }

        if (i < num6 && !bootstrap_done(cl->session, AF_INET6))
        {
            tr_port port;
            struct tr_address addr;

            memset(&addr, 0, sizeof(addr));
            addr.type = TR_AF_INET6;
            memcpy(&addr.addr.addr6, &cl->nodes6[i * 18], 16);
            memcpy(&port, &cl->nodes6[i * 18 + 16], 2);
            port = ntohs(port);
            tr_dhtAddNode(cl->session, &addr, port, 1);
        }

        /* Our DHT code is able to take up to 9 nodes in a row without
           dropping any. After that, it takes some time to split buckets.
           So ping the first 8 nodes quickly, then slow down. */
        if (i < 8)
        {
            nap(2);
        }
        else
        {
            nap(15);
        }

        if (bootstrap_done(session, 0))
        {
            break;
        }
    }

    if (!bootstrap_done(cl->session, 0))
    {
        char* bootstrap_file;
        tr_sys_file_t f = TR_BAD_SYS_FILE;

        bootstrap_file = tr_buildPath(cl->session->configDir, "dht.bootstrap", NULL);

        if (bootstrap_file != NULL)
        {
            f = tr_sys_file_open(bootstrap_file, TR_SYS_FILE_READ, 0, NULL);
        }

        if (f != TR_BAD_SYS_FILE)
        {
            tr_logAddNamedInfo("DHT", "Attempting manual bootstrap");

            for (;;)
            {
                char buf[201];
                char* p;
                int port = 0;

                if (!tr_sys_file_read_line(f, buf, 200, NULL))
                {
                    break;
                }

                p = memchr(buf, ' ', strlen(buf));

                if (p != NULL)
                {
                    port = atoi(p + 1);
                }

                if (p == NULL || port <= 0 || port >= 0x10000)
                {
                    tr_logAddNamedError("DHT", "Couldn't parse %s", buf);
                    continue;
                }

                *p = '\0';

                bootstrap_from_name(buf, port, bootstrap_af(session));

                if (bootstrap_done(cl->session, 0))
                {
                    break;
                }
            }

            tr_sys_file_close(f, NULL);
        }

        tr_free(bootstrap_file);
    }

    if (!bootstrap_done(cl->session, 0))
    {
        for (int i = 0; i < 6; ++i)
        {
            /* We don't want to abuse our bootstrap nodes, so be very
               slow.  The initial wait is to give other nodes a chance
               to contact us before we attempt to contact a bootstrap
               node, for example because we've just been restarted. */
            nap(40);

            if (bootstrap_done(cl->session, 0))
            {
                break;
            }

            if (i == 0)
            {
                tr_logAddNamedInfo("DHT", "Attempting bootstrap from dht.transmissionbt.com");
            }

            bootstrap_from_name("dht.transmissionbt.com", 6881, bootstrap_af(session));
        }
    }

    if (cl->nodes != NULL)
    {
        tr_free(cl->nodes);
    }

    if (cl->nodes6 != NULL)
    {
        tr_free(cl->nodes6);
    }

    tr_free(closure);
    tr_logAddNamedDbg("DHT", "Finished bootstrapping");
}

int tr_dhtInit(tr_session* ss)
{
    tr_variant benc;
    int rc;
    bool have_id = false;
    char* dat_file;
    uint8_t* nodes = NULL;
    uint8_t* nodes6 = NULL;
    uint8_t const* raw;
    size_t len;
    size_t len6;
    struct bootstrap_closure* cl;

    if (session != NULL) /* already initialized */
    {
        return -1;
    }

    tr_logAddNamedDbg("DHT", "Initializing DHT");

    if (tr_env_key_exists("TR_DHT_VERBOSE"))
    {
        dht_debug = stderr;
    }

    dat_file = tr_buildPath(ss->configDir, "dht.dat", NULL);
    rc = tr_variantFromFile(&benc, TR_VARIANT_FMT_BENC, dat_file, NULL) ? 0 : -1;
    tr_free(dat_file);

    if (rc == 0)
    {
        have_id = tr_variantDictFindRaw(&benc, TR_KEY_id, &raw, &len);

        if (have_id && len == 20)
        {
            memcpy(myid, raw, len);
        }

        if (ss->udp_socket != TR_BAD_SOCKET && tr_variantDictFindRaw(&benc, TR_KEY_nodes, &raw, &len) && len % 6 == 0)
        {
            nodes = tr_memdup(raw, len);
        }

        if (ss->udp6_socket != TR_BAD_SOCKET && tr_variantDictFindRaw(&benc, TR_KEY_nodes6, &raw, &len6) && len6 % 18 == 0)
        {
            nodes6 = tr_memdup(raw, len6);
        }

        tr_variantFree(&benc);
    }

    if (nodes == NULL)
    {
        len = 0;
    }

    if (nodes6 == NULL)
    {
        len6 = 0;
    }

    if (have_id)
    {
        tr_logAddNamedInfo("DHT", "Reusing old id");
    }
    else
    {
        /* Note that DHT ids need to be distributed uniformly,
         * so it should be something truly random. */
        tr_logAddNamedInfo("DHT", "Generating new id");
        tr_rand_buffer(myid, 20);
    }

    rc = dht_init(ss->udp_socket, ss->udp6_socket, myid, NULL);

    if (rc < 0)
    {
        goto fail;
    }

    session = ss;

    cl = tr_new(struct bootstrap_closure, 1);
    cl->session = session;
    cl->nodes = nodes;
    cl->nodes6 = nodes6;
    cl->len = len;
    cl->len6 = len6;
    tr_threadNew(dht_bootstrap, cl);

    dht_timer = evtimer_new(session->event_base, timer_callback, session);
    tr_timerAdd(dht_timer, 0, tr_rand_int_weak(1000000));

    tr_logAddNamedDbg("DHT", "DHT initialized");

    return 1;

fail:
    tr_free(nodes6);
    tr_free(nodes);

    tr_logAddNamedDbg("DHT", "DHT initialization failed (errno = %d)", errno);
    session = NULL;
    return -1;
}

void tr_dhtUninit(tr_session* ss)
{
    if (session != ss)
    {
        return;
    }

    tr_logAddNamedDbg("DHT", "Uninitializing DHT");

    if (dht_timer != NULL)
    {
        event_free(dht_timer);
        dht_timer = NULL;
    }

    /* Since we only save known good nodes, avoid erasing older data if we
       don't know enough nodes. */
    if (tr_dhtStatus(ss, AF_INET, NULL) < TR_DHT_FIREWALLED && tr_dhtStatus(ss, AF_INET6, NULL) < TR_DHT_FIREWALLED)
    {
        tr_logAddNamedInfo("DHT", "Not saving nodes, DHT not ready");
    }
    else
    {
        tr_variant benc;
        struct sockaddr_in sins[300];
        struct sockaddr_in6 sins6[300];
        char compact[300 * 6];
        char compact6[300 * 18];
        char* dat_file;
        int num = 300;
        int num6 = 300;
        int n = dht_get_nodes(sins, &num, sins6, &num6);

        tr_logAddNamedInfo("DHT", "Saving %d (%d + %d) nodes", n, num, num6);

        for (int i = 0, j = 0; i < num; ++i, j += 6)
        {
            memcpy(compact + j, &sins[i].sin_addr, 4);
            memcpy(compact + j + 4, &sins[i].sin_port, 2);
        }

        for (int i = 0, j = 0; i < num6; ++i, j += 18)
        {
            memcpy(compact6 + j, &sins6[i].sin6_addr, 16);
            memcpy(compact6 + j + 16, &sins6[i].sin6_port, 2);
        }

        tr_variantInitDict(&benc, 3);
        tr_variantDictAddRaw(&benc, TR_KEY_id, myid, 20);

        if (num > 0)
        {
            tr_variantDictAddRaw(&benc, TR_KEY_nodes, compact, num * 6);
        }

        if (num6 > 0)
        {
            tr_variantDictAddRaw(&benc, TR_KEY_nodes6, compact6, num6 * 18);
        }

        dat_file = tr_buildPath(ss->configDir, "dht.dat", NULL);
        tr_variantToFile(&benc, TR_VARIANT_FMT_BENC, dat_file);
        tr_variantFree(&benc);
        tr_free(dat_file);
    }

    dht_uninit();
    tr_logAddNamedDbg("DHT", "Done uninitializing DHT");

    session = NULL;
}

bool tr_dhtEnabled(tr_session const* ss)
{
    return ss != NULL && ss == session;
}

struct getstatus_closure
{
    int af;
    sig_atomic_t status;
    sig_atomic_t count;
};

static void getstatus(void* cl)
{
    struct getstatus_closure* closure = cl;
    int good;
    int dubious;
    int incoming;

    dht_nodes(closure->af, &good, &dubious, NULL, &incoming);

    closure->count = good + dubious;

    if (good < 4 || good + dubious <= 8)
    {
        closure->status = TR_DHT_BROKEN;
    }
    else if (good < 40)
    {
        closure->status = TR_DHT_POOR;
    }
    else if (incoming < 8)
    {
        closure->status = TR_DHT_FIREWALLED;
    }
    else
    {
        closure->status = TR_DHT_GOOD;
    }
}

int tr_dhtStatus(tr_session* session, int af, int* nodes_return)
{
    struct getstatus_closure closure = { .af = af, .status = -1, .count = -1 };

    if (!tr_dhtEnabled(session) || (af == AF_INET && session->udp_socket == TR_BAD_SOCKET) ||
        (af == AF_INET6 && session->udp6_socket == TR_BAD_SOCKET))
    {
        if (nodes_return != NULL)
        {
            *nodes_return = 0;
        }

        return TR_DHT_STOPPED;
    }

    tr_runInEventThread(session, getstatus, &closure);

    while (closure.status < 0)
    {
        tr_wait_msec(50 /*msec*/);
    }

    if (nodes_return != NULL)
    {
        *nodes_return = closure.count;
    }

    return closure.status;
}

tr_port tr_dhtPort(tr_session* ss)
{
    return tr_dhtEnabled(ss) ? ss->udp_port : 0;
}

bool tr_dhtAddNode(tr_session* ss, tr_address const* address, tr_port port, bool bootstrap)
{
    int af = address->type == TR_AF_INET ? AF_INET : AF_INET6;

    if (!tr_dhtEnabled(ss))
    {
        return false;
    }

    /* Since we don't want to abuse our bootstrap nodes,
     * we don't ping them if the DHT is in a good state. */

    if (bootstrap)
    {
        if (tr_dhtStatus(ss, af, NULL) >= TR_DHT_FIREWALLED)
        {
            return false;
        }
    }

    if (address->type == TR_AF_INET)
    {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        memcpy(&sin.sin_addr, &address->addr.addr4, 4);
        sin.sin_port = htons(port);
        dht_ping_node((struct sockaddr*)&sin, sizeof(sin));
        return true;
    }
    else if (address->type == TR_AF_INET6)
    {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        memcpy(&sin6.sin6_addr, &address->addr.addr6, 16);
        sin6.sin6_port = htons(port);
        dht_ping_node((struct sockaddr*)&sin6, sizeof(sin6));
        return true;
    }

    return false;
}

char const* tr_dhtPrintableStatus(int status)
{
    switch (status)
    {
    case TR_DHT_STOPPED:
        return "stopped";

    case TR_DHT_BROKEN:
        return "broken";

    case TR_DHT_POOR:
        return "poor";

    case TR_DHT_FIREWALLED:
        return "firewalled";

    case TR_DHT_GOOD:
        return "good";

    default:
        return "???";
    }
}

static void callback(void* ignore UNUSED, int event, unsigned char const* info_hash, void const* data, size_t data_len)
{
    if (event == DHT_EVENT_VALUES || event == DHT_EVENT_VALUES6)
    {
        tr_torrent* tor;
        tr_sessionLock(session);
        tor = tr_torrentFindFromHash(session, info_hash);

        if (tor != NULL && tr_torrentAllowsDHT(tor))
        {
            size_t n;
            tr_pex* pex;

            if (event == DHT_EVENT_VALUES)
            {
                pex = tr_peerMgrCompactToPex(data, data_len, NULL, 0, &n);
            }
            else
            {
                pex = tr_peerMgrCompact6ToPex(data, data_len, NULL, 0, &n);
            }

            for (size_t i = 0; i < n; ++i)
            {
                tr_peerMgrAddPex(tor, TR_PEER_FROM_DHT, pex + i, -1);
            }

            tr_free(pex);
            tr_logAddTorDbg(tor, "Learned %d %s peers from DHT", (int)n, event == DHT_EVENT_VALUES6 ? "IPv6" : "IPv4");
        }

        tr_sessionUnlock(session);
    }
    else if (event == DHT_EVENT_SEARCH_DONE || event == DHT_EVENT_SEARCH_DONE6)
    {
        tr_torrent* tor = tr_torrentFindFromHash(session, info_hash);

        if (tor != NULL)
        {
            if (event == DHT_EVENT_SEARCH_DONE)
            {
                tr_logAddTorInfo(tor, "%s", "IPv4 DHT announce done");
                tor->dhtAnnounceInProgress = false;
            }
            else
            {
                tr_logAddTorInfo(tor, "%s", "IPv6 DHT announce done");
                tor->dhtAnnounce6InProgress = false;
            }
        }
    }
}

static int tr_dhtAnnounce(tr_torrent* tor, int af, bool announce)
{
    int rc;
    int status;
    int numnodes;
    int ret = 0;

    if (!tr_torrentAllowsDHT(tor))
    {
        return -1;
    }

    status = tr_dhtStatus(tor->session, af, &numnodes);

    if (status == TR_DHT_STOPPED)
    {
        /* Let the caller believe everything is all right. */
        return 1;
    }

    if (status >= TR_DHT_POOR)
    {
        rc = dht_search(tor->info.hash, announce ? tr_sessionGetPeerPort(session) : 0, af, callback, NULL);

        if (rc >= 1)
        {
            tr_logAddTorInfo(tor, "Starting %s DHT announce (%s, %d nodes)", af == AF_INET6 ? "IPv6" : "IPv4",
                tr_dhtPrintableStatus(status), numnodes);

            if (af == AF_INET)
            {
                tor->dhtAnnounceInProgress = true;
            }
            else
            {
                tor->dhtAnnounce6InProgress = true;
            }

            ret = 1;
        }
        else
        {
            tr_logAddTorErr(tor, "%s DHT announce failed (%s, %d nodes): %s", af == AF_INET6 ? "IPv6" : "IPv4",
                tr_dhtPrintableStatus(status), numnodes, tr_strerror(errno));
        }
    }
    else
    {
        tr_logAddTorDbg(tor, "%s DHT not ready (%s, %d nodes)", af == AF_INET6 ? "IPv6" : "IPv4", tr_dhtPrintableStatus(status),
            numnodes);
    }

    return ret;
}

void tr_dhtUpkeep(tr_session* session)
{
    tr_torrent* tor = NULL;
    time_t const now = tr_time();

    while ((tor = tr_torrentNext(session, tor)) != NULL)
    {
        if (!tor->isRunning || !tr_torrentAllowsDHT(tor))
        {
            continue;
        }

        if (tor->dhtAnnounceAt <= now)
        {
            int const rc = tr_dhtAnnounce(tor, AF_INET, true);

            tor->dhtAnnounceAt = now + ((rc == 0) ? 5 + tr_rand_int_weak(5) : 25 * 60 + tr_rand_int_weak(3 * 60));
        }

        if (tor->dhtAnnounce6At <= now)
        {
            int const rc = tr_dhtAnnounce(tor, AF_INET6, true);

            tor->dhtAnnounce6At = now + ((rc == 0) ? 5 + tr_rand_int_weak(5) : 25 * 60 + tr_rand_int_weak(3 * 60));
        }
    }
}

void tr_dhtCallback(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen, void* sv)
{
    TR_ASSERT(tr_isSession(sv));

    if (sv != session)
    {
        return;
    }

    time_t tosleep;
    int rc = dht_periodic(buf, buflen, from, fromlen, &tosleep, callback, NULL);

    if (rc < 0)
    {
        if (errno == EINTR)
        {
            tosleep = 0;
        }
        else
        {
            tr_logAddNamedError("DHT", "dht_periodic failed: %s", tr_strerror(errno));

            if (errno == EINVAL || errno == EFAULT)
            {
                abort();
            }

            tosleep = 1;
        }
    }

    /* Being slightly late is fine,
       and has the added benefit of adding some jitter. */
    tr_timerAdd(dht_timer, tosleep, tr_rand_int_weak(1000000));
}

static void timer_callback(evutil_socket_t s UNUSED, short type UNUSED, void* session)
{
    tr_dhtCallback(NULL, 0, NULL, 0, session);
}

/* This function should return true when a node is blacklisted.  We do
   not support using a blacklist with the DHT in Transmission, since
   massive (ab)use of this feature could harm the DHT.  However, feel
   free to add support to your private copy as long as you don't
   redistribute it. */

int dht_blacklisted(struct sockaddr const* sa UNUSED, int salen UNUSED)
{
    return 0;
}

void dht_hash(void* hash_return, int hash_size, void const* v1, int len1, void const* v2, int len2, void const* v3, int len3)
{
    unsigned char sha1[SHA_DIGEST_LENGTH];
    tr_sha1(sha1, v1, len1, v2, len2, v3, len3, NULL);
    memset(hash_return, 0, hash_size);
    memcpy(hash_return, sha1, MIN(hash_size, SHA_DIGEST_LENGTH));
}

int dht_random_bytes(void* buf, size_t size)
{
    tr_rand_buffer(buf, size);
    return size;
}

int dht_sendto(int sockfd, void const* buf, int len, int flags, struct sockaddr const* to, int tolen)
{
    return sendto(sockfd, buf, len, flags, to, tolen);
}

#if defined(_WIN32) && !defined(__MINGW32__)

int dht_gettimeofday(struct timeval* tv, struct timezone* tz)
{
    TR_ASSERT(tz == NULL);

    return tr_gettimeofday(tv);
}

#endif
