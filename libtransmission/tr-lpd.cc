// This file Copyright © 2010 Johannes Lieder.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <csignal> /* sig_atomic_t */
#include <cstring> /* strlen(), strncpy(), strstr(), memset() */
#include <string_view>
#include <type_traits>

#ifdef _WIN32
#include <ws2tcpip.h>
using in_port_t = uint16_t; /* all missing */
#else
#include <ctime>
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <netinet/in.h> /* sockaddr_in */
#endif

#include <event2/event.h>
#include <event2/util.h>

#include <fmt/format.h>

#include "transmission.h"

#include "log.h"
#include "net.h"
#include "peer-mgr.h" /* tr_peerMgrAddPex() */
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-lpd.h"
#include "utils.h"

using namespace std::literals;

static auto constexpr SIZEOF_HASH_STRING = TR_SHA1_DIGEST_STRLEN;

/**
* @brief Local Peer Discovery
* @file tr-lpd.c
*
* This module implements the Local Peer Discovery (LPD) protocol as supported by the
* uTorrent client application. A typical LPD datagram is 119 bytes long.
*
*/

static void event_callback(evutil_socket_t, short /*type*/, void* /*unused*/);

static auto constexpr UpkeepIntervalSecs = int{ 5 };

static struct event* upkeep_timer = nullptr;

static tr_socket_t lpd_socket; /**<separate multicast receive socket */
static tr_socket_t lpd_socket2; /**<and multicast send socket */
static event* lpd_event = nullptr;
static tr_port lpd_port;

static tr_session* session;

static auto constexpr lpd_maxDatagramLength = int{ 200 }; /**<the size an LPD datagram must not exceed */
static char constexpr lpd_mcastGroup[] = "239.192.152.143"; /**<LPD multicast group */
static auto constexpr lpd_mcastPort = int{ 6771 }; /**<LPD source and destination UPD port */
static auto lpd_mcastAddr = sockaddr_in{}; /**<initialized from the above constants in tr_lpdInit */

/**
* @brief Protocol-related information carried by a Local Peer Discovery packet */
struct lpd_protocolVersion
{
    int major;
    int minor;
};

static auto constexpr lpd_ttlSameSubnet = int{ 1 };
// static auto constexpr lpd_ttlSameSite = int{ 32 };
// static auto constexpr lpd_ttlSameRegion = int{ 64 };
// static auto constexpr lpd_ttlSameContinent = int{ 128 };
// static auto constexpr lpd_ttlUnrestricted = int{ 255 };

static auto constexpr lpd_announceInterval = int{ 4 * 60 }; /**<4 min announce interval per torrent */
static auto constexpr lpd_announceScope = int{ lpd_ttlSameSubnet }; /**<the maximum scope for LPD datagrams */

/**
* @defgroup DoS Message Flood Protection
* @{
* We want to have a means to protect the libtransmission backend against message
* flooding: the strategy is to cap event processing once more than ten messages
* per second (that is, taking the average over one of our housekeeping intervals)
* got into our processing handler.
* If we'd really hit the limit and start discarding events, we either joined an
* extremely crowded multicast group or a malevolent host is sending bogus data to
* our socket. In this situation, we rather miss some announcements than blocking
* the actual task.
* @}
*/

/**
* @ingroup DoS
* @brief allow at most ten messages per second (interval average)
* @note this constraint is only enforced once per housekeeping interval */
static auto constexpr lpd_announceCapFactor = int{ 10 };

/**
* @ingroup DoS
* @brief number of unsolicited messages during the last HK interval
* @remark counts downwards */
static int lpd_unsolicitedMsgCounter;

/**
* @def CRLF
* @brief a line-feed, as understood by the LPD protocol */
#define CRLF "\r\n"

/**
* @defgroup HttpReqProc HTTP-style request handling
* @{
*/

/**
* @brief Checks for BT-SEARCH method and separates the parameter section
* @param[in] s The request string
* @param[out] ver If non-nullptr, gets filled with protocol info from the request
* @return Returns a relative pointer to the beginning of the parameter section.
*         If result is nullptr, s was invalid and no information will be returned
* @remark Note that the returned pointer is only usable as long as the given
*         pointer s is valid; that is, return storage is temporary.
*
* Determines whether the given string checks out to be a valid BT-SEARCH message.
* If so, the return value points to the beginning of the parameter section (note:
* in this case the function returns a character sequence beginning with CRLF).
* If parameter is not nullptr, the declared protocol version is returned as part
* of the lpd_protocolVersion structure.
*/
static char const* lpd_extractHeader(char const* s, struct lpd_protocolVersion* const ver)
{
    TR_ASSERT(s != nullptr);

    int major = -1;
    int minor = -1;
    size_t len = strlen(s);

    /* something might be rotten with this chunk of data */
    if (len == 0 || len > lpd_maxDatagramLength)
    {
        return nullptr;
    }

    /* now we can attempt to look up the BT-SEARCH header */
    if (sscanf(s, "BT-SEARCH * HTTP/%d.%d" CRLF, &major, &minor) != 2)
    {
        return nullptr;
    }

    if (major < 0 || minor < 0)
    {
        return nullptr;
    }

    {
        /* a pair of blank lines at the end of the string, no place else */
        char const* const two_blank = CRLF CRLF CRLF;
        char const* const end = strstr(s, two_blank);

        if (end == nullptr || strlen(end) > strlen(two_blank))
        {
            return nullptr;
        }
    }

    if (ver != nullptr)
    {
        ver->major = major;
        ver->minor = minor;
    }

    /* separate the header, begins with CRLF */
    return strstr(s, CRLF);
}

/**
* @brief Return the value of a named parameter
*
* @param[in] str Input string of "\r\nName: Value" pairs without HTTP-style method part
* @param[in] name Name of parameter to extract
* @param[in] n Maximum available storage for value to return
* @param[out] val Output parameter for the actual value
* @return Returns 1 if value could be copied successfully
*
* Extracts the associated value of a named parameter from a HTTP-style header by
* performing the following steps:
*   - assemble search string "\r\nName: " and locate position
*   - copy back value from end to next "\r\n"
*/
// TODO: string_view
static bool lpd_extractParam(char const* const str, char const* const name, int n, char* const val)
{
    TR_ASSERT(str != nullptr);
    TR_ASSERT(name != nullptr);
    TR_ASSERT(val != nullptr);

    auto const key = tr_strbuf<char, 30>{ CRLF, name, ": "sv };

    char const* const pos = strstr(str, key);
    if (pos == nullptr)
    {
        return false; /* search was not successful */
    }

    {
        char const* const beg = pos + std::size(key);
        char const* const new_line = strstr(beg, CRLF);

        /* the value is delimited by the next CRLF */
        int const len = new_line - beg;

        /* if value string hits the length limit n,
         * leave space for a trailing '\0' character */
        n = std::min(len, n - 1);
        strncpy(val, beg, n);
        val[n] = 0;
    }

    /* we successfully returned the value string */
    return true;
}

/**
* @} */

static void on_upkeep_timer(evutil_socket_t, short /*unused*/, void* /*unused*/);

/**
* @brief Initializes Local Peer Discovery for this node
*
* For the most part, this means setting up an appropriately configured multicast socket
* and event-based message handling.
*
* @remark Since the LPD service does not use another protocol family yet, this code is
* IPv4 only for the time being.
*/
int tr_lpdInit(tr_session* ss, tr_address* /*tr_addr*/)
{
    /* if this check fails (i.e. the definition of hashString changed), update
     * string handling in tr_lpdSendAnnounce() and tr_lpdConsiderAnnounce().
     * However, the code should work as long as interfaces to the rest of
     * libtransmission are compatible with char* strings. */
    static_assert(
        std::is_same_v<std::string const&, std::remove_pointer_t<decltype(std::declval<tr_torrent>().infoHashString())>>);

    struct ip_mreq mcastReq;
    int const opt_on = 1;
    int const opt_off = 0;

    if (session != nullptr) /* already initialized */
    {
        return -1;
    }

    TR_ASSERT(lpd_announceInterval > 0);
    TR_ASSERT(lpd_announceScope > 0);

    lpd_port = ss->peerPort();
    if (std::empty(lpd_port))
    {
        return -1;
    }

    tr_logAddDebug("Initialising Local Peer Discovery");

    /* setup datagram socket (receive) */
    {
        lpd_socket = socket(PF_INET, SOCK_DGRAM, 0);

        if (lpd_socket == TR_BAD_SOCKET)
        {
            goto fail;
        }

        if (evutil_make_socket_nonblocking(lpd_socket) == -1)
        {
            goto fail;
        }

        if (setsockopt(lpd_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&opt_on), sizeof(opt_on)) == -1)
        {
            goto fail;
        }

        memset(&lpd_mcastAddr, 0, sizeof(lpd_mcastAddr));
        lpd_mcastAddr.sin_family = AF_INET;
        lpd_mcastAddr.sin_port = htons(lpd_mcastPort);

        if (evutil_inet_pton(lpd_mcastAddr.sin_family, lpd_mcastGroup, &lpd_mcastAddr.sin_addr) == -1)
        {
            goto fail;
        }

        if (bind(lpd_socket, (struct sockaddr*)&lpd_mcastAddr, sizeof(lpd_mcastAddr)) == -1)
        {
            goto fail;
        }

        /* we want to join that LPD multicast group */
        memset(&mcastReq, 0, sizeof(mcastReq));
        mcastReq.imr_multiaddr = lpd_mcastAddr.sin_addr;
        mcastReq.imr_interface.s_addr = htonl(INADDR_ANY);

        if (setsockopt(lpd_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char const*>(&mcastReq), sizeof(mcastReq)) ==
            -1)
        {
            goto fail;
        }

        if (setsockopt(lpd_socket, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<char const*>(&opt_off), sizeof(opt_off)) ==
            -1)
        {
            goto fail;
        }
    }

    /* setup datagram socket (send) */
    {
        unsigned char const scope = lpd_announceScope;

        lpd_socket2 = socket(PF_INET, SOCK_DGRAM, 0);

        if (lpd_socket2 == TR_BAD_SOCKET)
        {
            goto fail;
        }

        if (evutil_make_socket_nonblocking(lpd_socket2) == -1)
        {
            goto fail;
        }

        /* configure outbound multicast TTL */
        if (setsockopt(lpd_socket2, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<char const*>(&scope), sizeof(scope)) == -1)
        {
            goto fail;
        }

        if (setsockopt(lpd_socket2, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<char const*>(&opt_off), sizeof(opt_off)) ==
            -1)
        {
            goto fail;
        }
    }

    session = ss;

    /* Note: lpd_unsolicitedMsgCounter remains 0 until the first timeout event, thus
     * any announcement received during the initial interval will be discarded. */

    lpd_event = event_new(ss->event_base, lpd_socket, EV_READ | EV_PERSIST, event_callback, nullptr);
    event_add(lpd_event, nullptr);

    upkeep_timer = evtimer_new(ss->event_base, on_upkeep_timer, ss);
    tr_timerAdd(*upkeep_timer, UpkeepIntervalSecs, 0);

    tr_logAddDebug("Local Peer Discovery initialised");

    return 1;

fail:
    {
        int const save = errno;
        evutil_closesocket(lpd_socket);
        evutil_closesocket(lpd_socket2);
        lpd_socket = lpd_socket2 = TR_BAD_SOCKET;
        session = nullptr;
        tr_logAddWarn(fmt::format(
            _("Couldn't initialize LPD: {error} ({error_code})"),
            fmt::arg("error", tr_strerror(save)),
            fmt::arg("error_code", save)));
        errno = save;
    }

    return -1;
}

void tr_lpdUninit(tr_session* ss)
{
    if (session != ss)
    {
        return;
    }

    tr_logAddTrace("Uninitialising Local Peer Discovery");

    event_free(lpd_event);
    lpd_event = nullptr;

    evtimer_del(upkeep_timer);
    upkeep_timer = nullptr;

    /* just shut down, we won't remember any former nodes */
    evutil_closesocket(lpd_socket);
    evutil_closesocket(lpd_socket2);
    tr_logAddTrace("Done uninitialising Local Peer Discovery");

    session = nullptr;
}

/**
* @endcond */

/**
* @defgroup LdsProto LPD announcement processing
* @{
*/

/**
* @brief Announce the given torrent on the local network
*
* @param[in] t Torrent to announce
* @return Returns true on success
*
* Send a query for torrent t out to the LPD multicast group (or the LAN, for that
* matter). A listening client on the same network might react by adding us to his
* peer pool for torrent t.
*/
bool tr_lpdSendAnnounce(tr_torrent const* t)
{
    if (t == nullptr)
    {
        return false;
    }

    auto const query = fmt::format(
        FMT_STRING("BT-SEARCH * HTTP/{:d}.{:d}" CRLF "Host: {:s}:{:d}" CRLF "Port: {:d}" CRLF "Infohash: {:s}" CRLF CRLF CRLF),
        1,
        1,
        lpd_mcastGroup,
        lpd_mcastPort,
        lpd_port.host(),
        tr_strupper(t->infoHashString()));

    // send the query out using [lpd_socket2]
    // destination address info has already been set up in tr_lpdInit(),
    // so we refrain from preparing another sockaddr_in here
    if (auto const res = sendto(
            lpd_socket2,
            std::data(query),
            std::size(query),
            0,
            (struct sockaddr const*)&lpd_mcastAddr,
            sizeof(lpd_mcastAddr));
        res != static_cast<int>(std::size(query)))
    {
        return false;
    }

    tr_logAddTraceTor(t, "LPD announce message away");

    return true;
}

/**
* @brief Process incoming unsolicited messages and add the peer to the announced
* torrent if all checks are passed.
*
* @param[in,out] peer Address information of the peer to add
* @param[in] msg The announcement message to consider
* @return Returns 0 if any input parameter or the announce was invalid, 1 if the peer
* was successfully added, -1 if not; a non-null return value indicates a side-effect to
* the peer in/out parameter.
*
* @note The port information gets added to the peer structure if tr_lpdConsiderAnnounce
* is able to extract the necessary information from the announce message. That is, if
* return != 0, the caller may retrieve the value from the passed structure.
*/
static int tr_lpdConsiderAnnounce(tr_pex* peer, char const* const msg)
{
    auto constexpr MaxValueLen = int{ 25 };
    auto constexpr MaxHashLen = int{ SIZEOF_HASH_STRING };

    auto ver = lpd_protocolVersion{ -1, -1 };
    char value[MaxValueLen] = { 0 };
    char hashString[MaxHashLen] = { 0 };
    int res = 0;

    if (peer != nullptr && msg != nullptr)
    {
        tr_torrent* tor = nullptr;

        char const* params = lpd_extractHeader(msg, &ver);

        if (params == nullptr || ver.major != 1) /* allow messages of protocol v1 */
        {
            return 0;
        }

        /* save the effort to check Host, which seems to be optional anyway */

        if (!lpd_extractParam(params, "Port", MaxValueLen, value))
        {
            return 0;
        }

        /* determine announced peer port, refuse if value too large */
        int peer_port = 0;
        if (sscanf(value, "%d", &peer_port) != 1 || peer_port > (in_port_t)-1)
        {
            return 0;
        }

        peer->port.setHost(peer_port);
        res = -1; /* signal caller side-effect to peer->port via return != 0 */

        if (!lpd_extractParam(params, "Infohash", MaxHashLen, hashString))
        {
            return res;
        }

        tor = session->torrents().get(hashString);

        if (tr_isTorrent(tor) && tor->allowsLpd())
        {
            /* we found a suitable peer, add it to the torrent */
            tr_peerMgrAddPex(tor, TR_PEER_FROM_LPD, peer, 1);
            tr_logAddDebugTor(
                tor,
                fmt::format(FMT_STRING("Found a local peer from LPD ({:s})"), peer->addr.readable(peer->port)));

            /* periodic reconnectPulse() deals with the rest... */

            return 1;
        }

        tr_logAddDebug(fmt::format(FMT_STRING("Cannot serve torrent #{:s}"), hashString));
    }

    return res;
}

/**
* @} */

/**
* @note Since it possible for tr_lpdAnnounceMore to get called from outside the LPD module,
* the function needs to be informed of the externally employed housekeeping interval.
* Further, by setting interval to zero (or negative) the caller may actually disable LPD
* announces on a per-interval basis.
*
* TODO: since this function's been made private and is called by a periodic timer,
* most of the previous paragraph isn't true anymore... we weren't using that functionality
* before. are there cases where we should? if not, should we remove the bells & whistles?
*/
static int tr_lpdAnnounceMore(time_t const now, int const interval)
{
    int announcesSent = 0;

    if (!tr_isSession(session))
    {
        return -1;
    }

    if (tr_sessionAllowsLPD(session))
    {
        for (auto* const tor : session->torrents())
        {
            int announcePrio = 0;

            if (!tor->allowsLpd())
            {
                continue;
            }

            /* issue #3208: prioritize downloads before seeds */
            switch (tr_torrentGetActivity(tor))
            {
            case TR_STATUS_DOWNLOAD:
                announcePrio = 1;
                break;

            case TR_STATUS_SEED:
                announcePrio = 2;
                break;

            default:
                break;
            }

            if (announcePrio > 0 && tor->lpdAnnounceAt <= now)
            {
                if (tr_lpdSendAnnounce(tor))
                {
                    announcesSent++;
                }

                tor->lpdAnnounceAt = now + lpd_announceInterval * announcePrio;

                break; /* that's enough; for this interval */
            }
        }
    }

    /* perform housekeeping for the flood protection mechanism */
    {
        int const maxAnnounceCap = interval * lpd_announceCapFactor;

        if (lpd_unsolicitedMsgCounter < 0)
        {
            tr_logAddTrace(fmt::format(
                "Dropped {} announces in the last interval (max. {} allowed)",
                -lpd_unsolicitedMsgCounter,
                maxAnnounceCap));
        }

        lpd_unsolicitedMsgCounter = maxAnnounceCap;
    }

    return announcesSent;
}

static void on_upkeep_timer(evutil_socket_t /*s*/, short /*type*/, void* /*user_data*/)
{
    time_t const now = tr_time();
    tr_lpdAnnounceMore(now, UpkeepIntervalSecs);
    tr_timerAdd(*upkeep_timer, UpkeepIntervalSecs, 0);
}

/**
* @brief Processing of timeout notifications and incoming data on the socket
* @note maximum rate of read events is limited according to @a lpd_maxAnnounceCap
* @see DoS */
static void event_callback(evutil_socket_t /*s*/, short type, void* /*user_data*/)
{
    TR_ASSERT(tr_isSession(session));

    /* do not allow announces to be processed if LPD is disabled */
    if (!tr_sessionAllowsLPD(session))
    {
        return;
    }

    if ((type & EV_READ) != 0)
    {
        struct sockaddr_in foreignAddr;
        int addrLen = sizeof(foreignAddr);
        char foreignMsg[lpd_maxDatagramLength + 1];

        /* process local announcement from foreign peer */
        int res = recvfrom(
            lpd_socket,
            foreignMsg,
            lpd_maxDatagramLength,
            0,
            (struct sockaddr*)&foreignAddr,
            (socklen_t*)&addrLen);

        /* besides, do we get flooded? then bail out! */
        if (--lpd_unsolicitedMsgCounter < 0)
        {
            return;
        }

        if (res > 0 && res <= lpd_maxDatagramLength)
        {
            auto foreignPeer = tr_pex{};

            /* be paranoid enough about zero terminating the foreign string */
            foreignMsg[res] = '\0';

            foreignPeer.addr.addr.addr4 = foreignAddr.sin_addr;

            if (tr_lpdConsiderAnnounce(&foreignPeer, foreignMsg) != 0)
            {
                return; /* OK so far, no log message */
            }
        }

        tr_logAddTrace("Discarded invalid multicast message");
    }
}
