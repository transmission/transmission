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

#include <algorithm>
#include <cerrno>
#include <csignal> /* sig_atomic_t */
#include <cstdio>
#include <cstdlib> /* atoi() */
#include <cstring> /* memcpy(), memset(), memchr(), strlen() */

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

using namespace std::literals;

static struct event* dht_timer = nullptr;
static unsigned char myid[20];
static tr_session* session_ = nullptr;

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
    if (af == 0)
    {
        return bootstrap_done(session, AF_INET) && bootstrap_done(session, AF_INET6);
    }

    int const status = tr_dhtStatus(session, af, nullptr);
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

    if (bootstrap_done(session, AF_INET))
    {
        return AF_INET6;
    }

    return 0;
}

static void bootstrap_from_name(char const* name, tr_port port, int af)
{
    auto hints = addrinfo{};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = af;

    /* No, just passing p + 1 to gai won't work. */
    char pp[10];
    tr_snprintf(pp, sizeof(pp), "%d", (int)port);

    addrinfo* info = nullptr;
    int const rc = getaddrinfo(name, pp, &hints, &info);
    if (rc != 0)
    {
        tr_logAddNamedError("DHT", "%s:%s: %s", name, pp, gai_strerror(rc));
        return;
    }

    addrinfo* infop = info;
    while (infop != nullptr)
    {
        dht_ping_node(infop->ai_addr, infop->ai_addrlen);

        nap(15);

        if (bootstrap_done(session_, af))
        {
            break;
        }

        infop = infop->ai_next;
    }

    freeaddrinfo(info);
}

static void dht_bootstrap(void* closure)
{
    auto* cl = static_cast<struct bootstrap_closure*>(closure);
    int num = cl->len / 6;
    int num6 = cl->len6 / 18;

    if (session_ != cl->session)
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

    for (int i = 0; i < std::max(num, num6); ++i)
    {
        if (i < num && !bootstrap_done(cl->session, AF_INET))
        {
            auto port = tr_port{};
            auto addr = tr_address{};

            memset(&addr, 0, sizeof(addr));
            addr.type = TR_AF_INET;
            memcpy(&addr.addr.addr4, &cl->nodes[i * 6], 4);
            memcpy(&port, &cl->nodes[i * 6 + 4], 2);
            port = ntohs(port);
            tr_dhtAddNode(cl->session, &addr, port, 1);
        }

        if (i < num6 && !bootstrap_done(cl->session, AF_INET6))
        {
            auto port = tr_port{};
            auto addr = tr_address{};

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

        if (bootstrap_done(session_, 0))
        {
            break;
        }
    }

    if (!bootstrap_done(cl->session, 0))
    {
        auto const bootstrap_file = tr_strvPath(cl->session->configDir, "dht.bootstrap");

        tr_sys_file_t const f = tr_sys_file_open(bootstrap_file.c_str(), TR_SYS_FILE_READ, 0, nullptr);

        if (f != TR_BAD_SYS_FILE)
        {
            tr_logAddNamedInfo("DHT", "Attempting manual bootstrap");

            for (;;)
            {
                char buf[201];
                if (!tr_sys_file_read_line(f, buf, 200, nullptr))
                {
                    break;
                }

                auto* p = static_cast<char*>(memchr(buf, ' ', strlen(buf)));
                int port = 0;

                if (p != nullptr)
                {
                    port = atoi(p + 1);
                }

                if (p == nullptr || port <= 0 || port >= 0x10000)
                {
                    tr_logAddNamedError("DHT", "Couldn't parse %s", buf);
                    continue;
                }

                *p = '\0';

                bootstrap_from_name(buf, port, bootstrap_af(session_));

                if (bootstrap_done(cl->session, 0))
                {
                    break;
                }
            }

            tr_sys_file_close(f, nullptr);
        }
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

            bootstrap_from_name("dht.transmissionbt.com", 6881, bootstrap_af(session_));
        }
    }

    if (cl->nodes != nullptr)
    {
        tr_free(cl->nodes);
    }

    if (cl->nodes6 != nullptr)
    {
        tr_free(cl->nodes6);
    }

    tr_free(closure);
    tr_logAddNamedDbg("DHT", "Finished bootstrapping");
}

int tr_dhtInit(tr_session* ss)
{
    if (session_ != nullptr) /* already initialized */
    {
        return -1;
    }

    tr_logAddNamedDbg("DHT", "Initializing DHT");

    if (tr_env_key_exists("TR_DHT_VERBOSE"))
    {
        dht_debug = stderr;
    }

    auto const dat_file = tr_strvPath(ss->configDir, "dht.dat"sv);
    auto benc = tr_variant{};
    auto const ok = tr_variantFromFile(&benc, TR_VARIANT_PARSE_BENC, dat_file.c_str());

    bool have_id = false;
    uint8_t* nodes = nullptr;
    uint8_t* nodes6 = nullptr;
    size_t len = 0;
    size_t len6 = 0;
    if (ok)
    {
        auto sv = std::string_view{};
        have_id = tr_variantDictFindStrView(&benc, TR_KEY_id, &sv);
        if (have_id && std::size(sv) == 20)
        {
            std::copy(std::begin(sv), std::end(sv), myid);
        }

        uint8_t const* raw = nullptr;
        if (ss->udp_socket != TR_BAD_SOCKET && tr_variantDictFindRaw(&benc, TR_KEY_nodes, &raw, &len) && len % 6 == 0)
        {
            nodes = static_cast<uint8_t*>(tr_memdup(raw, len));
            if (nodes == nullptr)
            {
                len = 0;
            }
        }

        if (ss->udp6_socket != TR_BAD_SOCKET && tr_variantDictFindRaw(&benc, TR_KEY_nodes6, &raw, &len6) && len6 % 18 == 0)
        {
            nodes6 = static_cast<uint8_t*>(tr_memdup(raw, len6));
            if (nodes6 == nullptr)
            {
                len6 = 0;
            }
        }

        tr_variantFree(&benc);
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

    if (int rc = dht_init(ss->udp_socket, ss->udp6_socket, myid, nullptr); rc < 0)
    {
        tr_free(nodes6);
        tr_free(nodes);

        tr_logAddNamedDbg("DHT", "DHT initialization failed (errno = %d)", errno);
        session_ = nullptr;
        return -1;
    }

    session_ = ss;

    auto* const cl = tr_new(struct bootstrap_closure, 1);
    cl->session = session_;
    cl->nodes = nodes;
    cl->nodes6 = nodes6;
    cl->len = len;
    cl->len6 = len6;
    tr_threadNew(dht_bootstrap, cl);

    dht_timer = evtimer_new(session_->event_base, timer_callback, session_);
    tr_timerAdd(dht_timer, 0, tr_rand_int_weak(1000000));

    tr_logAddNamedDbg("DHT", "DHT initialized");

    return 1;
}

void tr_dhtUninit(tr_session* ss)
{
    if (session_ != ss)
    {
        return;
    }

    tr_logAddNamedDbg("DHT", "Uninitializing DHT");

    if (dht_timer != nullptr)
    {
        event_free(dht_timer);
        dht_timer = nullptr;
    }

    /* Since we only save known good nodes, avoid erasing older data if we
       don't know enough nodes. */
    if (tr_dhtStatus(ss, AF_INET, nullptr) < TR_DHT_FIREWALLED && tr_dhtStatus(ss, AF_INET6, nullptr) < TR_DHT_FIREWALLED)
    {
        tr_logAddNamedInfo("DHT", "Not saving nodes, DHT not ready");
    }
    else
    {
        auto constexpr MaxNodes = size_t{ 300 };
        auto constexpr PortLen = size_t{ 2 };
        auto constexpr CompactAddrLen = size_t{ 4 };
        auto constexpr CompactLen = size_t{ CompactAddrLen + PortLen };
        auto constexpr Compact6AddrLen = size_t{ 16 };
        auto constexpr Compact6Len = size_t{ Compact6AddrLen + PortLen };

        struct sockaddr_in sins[MaxNodes];
        struct sockaddr_in6 sins6[MaxNodes];
        int num = MaxNodes;
        int num6 = MaxNodes;
        int n = dht_get_nodes(sins, &num, sins6, &num6);
        tr_logAddNamedInfo("DHT", "Saving %d (%d + %d) nodes", n, num, num6);

        tr_variant benc;
        tr_variantInitDict(&benc, 3);
        tr_variantDictAddRaw(&benc, TR_KEY_id, myid, 20);

        if (num > 0)
        {
            char compact[MaxNodes * CompactLen];
            char* out = compact;
            for (struct sockaddr_in const* in = sins; in < sins + num; ++in)
            {
                memcpy(out, &in->sin_addr, CompactAddrLen);
                out += CompactAddrLen;
                memcpy(out, &in->sin_port, PortLen);
                out += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes, compact, out - compact);
        }

        if (num6 > 0)
        {
            char compact6[MaxNodes * Compact6Len];
            char* out6 = compact6;
            for (struct sockaddr_in6 const* in = sins6; in < sins6 + num6; ++in)
            {
                memcpy(out6, &in->sin6_addr, Compact6AddrLen);
                out6 += Compact6AddrLen;
                memcpy(out6, &in->sin6_port, PortLen);
                out6 += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes6, compact6, out6 - compact6);
        }

        auto const dat_file = tr_strvPath(ss->configDir, "dht.dat");
        tr_variantToFile(&benc, TR_VARIANT_FMT_BENC, dat_file.c_str());
        tr_variantFree(&benc);
    }

    dht_uninit();
    tr_logAddNamedDbg("DHT", "Done uninitializing DHT");

    session_ = nullptr;
}

bool tr_dhtEnabled(tr_session const* ss)
{
    return ss != nullptr && ss == session_;
}

struct getstatus_closure
{
    int af;
    sig_atomic_t status;
    sig_atomic_t count;
};

static void getstatus(void* cl)
{
    auto* closure = static_cast<struct getstatus_closure*>(cl);

    int good = 0;
    int dubious = 0;
    int incoming = 0;
    dht_nodes(closure->af, &good, &dubious, nullptr, &incoming);

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
    auto closure = getstatus_closure{ af, -1, -1 };

    if (!tr_dhtEnabled(session) || (af == AF_INET && session->udp_socket == TR_BAD_SOCKET) ||
        (af == AF_INET6 && session->udp6_socket == TR_BAD_SOCKET))
    {
        if (nodes_return != nullptr)
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

    if (nodes_return != nullptr)
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

    if (bootstrap && (tr_dhtStatus(ss, af, nullptr) >= TR_DHT_FIREWALLED))
    {
        return false;
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

    if (address->type == TR_AF_INET6)
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

static void callback(void* /*ignore*/, int event, unsigned char const* info_hash, void const* data, size_t data_len)
{
    if (event == DHT_EVENT_VALUES || event == DHT_EVENT_VALUES6)
    {
        auto const lock = session_->unique_lock();

        tr_torrent* const tor = tr_torrentFindFromHash(session_, info_hash);
        if (tor != nullptr && tr_torrentAllowsDHT(tor))
        {
            size_t n = 0;
            tr_pex* const pex = event == DHT_EVENT_VALUES ? tr_peerMgrCompactToPex(data, data_len, nullptr, 0, &n) :
                                                            tr_peerMgrCompact6ToPex(data, data_len, nullptr, 0, &n);

            tr_peerMgrAddPex(tor, TR_PEER_FROM_DHT, pex, n);

            tr_free(pex);
            tr_logAddTorDbg(tor, "Learned %d %s peers from DHT", (int)n, event == DHT_EVENT_VALUES6 ? "IPv6" : "IPv4");
        }
    }
    else if (event == DHT_EVENT_SEARCH_DONE || event == DHT_EVENT_SEARCH_DONE6)
    {
        tr_torrent* tor = tr_torrentFindFromHash(session_, info_hash);

        if (tor != nullptr)
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

enum class AnnounceResult
{
    INVALID,
    OK,
    FAILED
};

static AnnounceResult tr_dhtAnnounce(tr_torrent* tor, int af, bool announce)
{
    if (!tr_torrentAllowsDHT(tor))
    {
        return AnnounceResult::INVALID;
    }

    int numnodes = 0;
    int const status = tr_dhtStatus(tor->session, af, &numnodes);
    if (status == TR_DHT_STOPPED)
    {
        // let the caller believe everything is all right.
        return AnnounceResult::OK;
    }

    if (status < TR_DHT_POOR)
    {
        tr_logAddTorDbg(
            tor,
            "%s DHT not ready (%s, %d nodes)",
            af == AF_INET6 ? "IPv6" : "IPv4",
            tr_dhtPrintableStatus(status),
            numnodes);
        return AnnounceResult::FAILED;
    }

    int const rc = dht_search(tor->info.hash, announce ? tr_sessionGetPeerPort(session_) : 0, af, callback, nullptr);
    if (rc < 0)
    {
        tr_logAddTorErr(
            tor,
            "%s DHT announce failed (%s, %d nodes): %s",
            af == AF_INET6 ? "IPv6" : "IPv4",
            tr_dhtPrintableStatus(status),
            numnodes,
            tr_strerror(errno));
        return AnnounceResult::FAILED;
    }

    tr_logAddTorInfo(
        tor,
        "Starting %s DHT announce (%s, %d nodes)",
        af == AF_INET6 ? "IPv6" : "IPv4",
        tr_dhtPrintableStatus(status),
        numnodes);

    if (af == AF_INET)
    {
        tor->dhtAnnounceInProgress = true;
    }
    else
    {
        tor->dhtAnnounce6InProgress = true;
    }

    return AnnounceResult::OK;
}

void tr_dhtUpkeep(tr_session* session)
{
    time_t const now = tr_time();

    for (auto* tor : session->torrents)
    {
        if (!tor->isRunning || !tr_torrentAllowsDHT(tor))
        {
            continue;
        }

        if (tor->dhtAnnounceAt <= now)
        {
            auto const rc = tr_dhtAnnounce(tor, AF_INET, true);

            tor->dhtAnnounceAt = now +
                ((rc == AnnounceResult::FAILED) ? 5 + tr_rand_int_weak(5) : 25 * 60 + tr_rand_int_weak(3 * 60));
        }

        if (tor->dhtAnnounce6At <= now)
        {
            auto const rc = tr_dhtAnnounce(tor, AF_INET6, true);

            tor->dhtAnnounce6At = now +
                ((rc == AnnounceResult::FAILED) ? 5 + tr_rand_int_weak(5) : 25 * 60 + tr_rand_int_weak(3 * 60));
        }
    }
}

void tr_dhtCallback(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen, void* sv)
{
    TR_ASSERT(tr_isSession(static_cast<tr_session*>(sv)));

    if (sv != session_)
    {
        return;
    }

    time_t tosleep = 0;
    int rc = dht_periodic(buf, buflen, from, fromlen, &tosleep, callback, nullptr);

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
    tr_timerAdd(dht_timer, (int)tosleep, tr_rand_int_weak(1000000));
}

static void timer_callback(evutil_socket_t /*s*/, short /*type*/, void* session)
{
    tr_dhtCallback(nullptr, 0, nullptr, 0, session);
}

/* This function should return true when a node is blacklisted.  We do
   not support using a blacklist with the DHT in Transmission, since
   massive (ab)use of this feature could harm the DHT.  However, feel
   free to add support to your private copy as long as you don't
   redistribute it. */

int dht_blacklisted(sockaddr const* /*sa*/, int /*salen*/)
{
    return 0;
}

void dht_hash(void* hash_return, int hash_size, void const* v1, int len1, void const* v2, int len2, void const* v3, int len3)
{
    unsigned char sha1[SHA_DIGEST_LENGTH];
    tr_sha1(sha1, v1, len1, v2, len2, v3, len3, nullptr);
    memset(hash_return, 0, hash_size);
    memcpy(hash_return, sha1, std::min(hash_size, SHA_DIGEST_LENGTH));
}

int dht_random_bytes(void* buf, size_t size)
{
    tr_rand_buffer(buf, size);
    return size;
}

int dht_sendto(int sockfd, void const* buf, int len, int flags, struct sockaddr const* to, int tolen)
{
    return sendto(sockfd, static_cast<char const*>(buf), len, flags, to, tolen);
}

#if defined(_WIN32) && !defined(__MINGW32__)

extern "C" int dht_gettimeofday(struct timeval* tv, struct timezone* tz)
{
    TR_ASSERT(tz == nullptr);

    return tr_gettimeofday(tv);
}

#endif
