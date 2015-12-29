/*
Copyright (c) 2010 by Johannes Lieder

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* ansi */
#include <errno.h>
#include <stdio.h>
#include <string.h> /* strlen (), strncpy (), strstr (), memset () */

/* posix */
#include <signal.h> /* sig_atomic_t */
#include <ctype.h> /* toupper () */

#ifdef _WIN32
  #include <inttypes.h>
  #include <ws2tcpip.h>
  typedef uint16_t in_port_t; /* all missing */
#else
  #include <sys/time.h>
  #include <unistd.h> /* close () */
  #include <sys/types.h>
  #include <sys/socket.h> /* socket (), bind () */
  #include <netinet/in.h> /* sockaddr_in */
#endif

/* third party */
#include <event2/event.h>
#include <event2/util.h>

/* libT */
#include "transmission.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h" /* tr_peerMgrAddPex () */
#include "session.h"
#include "torrent.h" /* tr_torrentFindFromHash () */
#include "tr-lpd.h"
#include "utils.h"
#include "version.h"

/**
* @brief Local Peer Discovery
* @file tr-lpd.c
*
* This module implements the Local Peer Discovery (LPD) protocol as supported by the
* uTorrent client application. A typical LPD datagram is 119 bytes long.
*
* $Id$
*/

static void event_callback (evutil_socket_t, short, void*);

enum {
   UPKEEP_INTERVAL_SECS = 5
};
static struct event * upkeep_timer = NULL;

static tr_socket_t lpd_socket; /**<separate multicast receive socket */
static tr_socket_t lpd_socket2; /**<and multicast send socket */
static struct event * lpd_event = NULL;
static tr_port lpd_port;

static tr_torrent* lpd_torStaticType UNUSED; /* just a helper for static type analysis */
static tr_session* session;

enum { lpd_maxDatagramLength = 200 }; /**<the size an LPD datagram must not exceed */
const char lpd_mcastGroup[] = "239.192.152.143"; /**<LPD multicast group */
const int lpd_mcastPort = 6771; /**<LPD source and destination UPD port */
static struct sockaddr_in lpd_mcastAddr; /**<initialized from the above constants in tr_lpdInit */

/**
* @brief Protocol-related information carried by a Local Peer Discovery packet */
struct lpd_protocolVersion
{
    int major, minor;
};

enum lpd_enumTimeToLive {
    lpd_ttlSameSubnet = 1,
    lpd_ttlSameSite = 32,
    lpd_ttlSameRegion = 64,
    lpd_ttlSameContinent = 128,
    lpd_ttlUnrestricted = 255
};

enum {
    lpd_announceInterval = 4 * 60, /**<4 min announce interval per torrent */
    lpd_announceScope = lpd_ttlSameSubnet /**<the maximum scope for LPD datagrams */
};


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
enum { lpd_announceCapFactor = 10 };

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
* @param[out] ver If non-NULL, gets filled with protocol info from the request
* @return Returns a relative pointer to the beginning of the parameter section;
*         if result is NULL, s was invalid and no information will be returned
* @remark Note that the returned pointer is only usable as long as the given
*         pointer s is valid; that is, return storage is temporary.
*
* Determines whether the given string checks out to be a valid BT-SEARCH message.
* If so, the return value points to the beginning of the parameter section (note:
* in this case the function returns a character sequence beginning with CRLF).
* If parameter is not NULL, the declared protocol version is returned as part of
* the lpd_protocolVersion structure.
*/
static const char* lpd_extractHeader (const char* s, struct lpd_protocolVersion* const ver)
{
    int major = -1, minor = -1;
    size_t len;

    assert (s != NULL);
    len = strlen (s);

    /* something might be rotten with this chunk of data */
    if (len == 0 || len > lpd_maxDatagramLength)
        return NULL;

    /* now we can attempt to look up the BT-SEARCH header */
    if (sscanf (s, "BT-SEARCH * HTTP/%d.%d" CRLF, &major, &minor) != 2)
        return NULL;

    if (major < 0 || minor < 0)
        return NULL;

    {
        /* a pair of blank lines at the end of the string, no place else */
        const char* const two_blank = CRLF CRLF CRLF;
        const char* const end = strstr (s, two_blank);

        if (end == NULL || strlen (end) > strlen (two_blank))
            return NULL;
    }

    if (ver != NULL)
    {
        ver->major = major;
        ver->minor = minor;
    }

    /* separate the header, begins with CRLF */
    return strstr (s, CRLF);
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
static bool lpd_extractParam (const char* const str, const char* const name, int n, char* const val)
{
    /* configure maximum length of search string here */
    enum { maxLength = 30 };
    char sstr[maxLength] = { 0 };
    const char* pos;

    assert (str != NULL && name != NULL);
    assert (val != NULL);

    if (strlen (name) > maxLength - strlen (CRLF ": "))
        return false;

    /* compose the string token to search for */
    tr_snprintf (sstr, maxLength, CRLF "%s: ", name);

    pos = strstr (str, sstr);
    if (pos == NULL)
        return false; /* search was not successful */

    {
        const char* const beg = pos + strlen (sstr);
        const char* const new_line = strstr (beg, CRLF);

        /* the value is delimited by the next CRLF */
        int len = new_line - beg;

        /* if value string hits the length limit n,
         * leave space for a trailing '\0' character */
        if (len < n--)
            n = len;

        strncpy (val, beg, n);
        val[n] = 0;
    }

    /* we successfully returned the value string */
    return true;
}

/**
* @} */

static void on_upkeep_timer (evutil_socket_t, short, void *);

/**
* @brief Initializes Local Peer Discovery for this node
*
* For the most part, this means setting up an appropriately configured multicast socket
* and event-based message handling.
*
* @remark Since the LPD service does not use another protocol family yet, this code is
* IPv4 only for the time being.
*/
int tr_lpdInit (tr_session* ss, tr_address* tr_addr UNUSED)
{
    struct ip_mreq mcastReq;
    const int opt_on = 1, opt_off = 0;

    if (session) /* already initialized */
        return -1;

    assert (lpd_announceInterval > 0);
    assert (lpd_announceScope > 0);

    lpd_port = tr_sessionGetPeerPort (ss);
    if (lpd_port <= 0)
        return -1;

    tr_logAddNamedDbg ("LPD", "Initialising Local Peer Discovery");

    /* setup datagram socket (receive) */
    {
        lpd_socket = socket (PF_INET, SOCK_DGRAM, 0);
        if (lpd_socket == TR_BAD_SOCKET)
            goto fail;

        if (evutil_make_socket_nonblocking (lpd_socket) < 0)
            goto fail;

        if (setsockopt (lpd_socket, SOL_SOCKET, SO_REUSEADDR,
                (const void *) &opt_on, sizeof opt_on) < 0)
            goto fail;

        memset (&lpd_mcastAddr, 0, sizeof lpd_mcastAddr);
        lpd_mcastAddr.sin_family = AF_INET;
        lpd_mcastAddr.sin_port = htons (lpd_mcastPort);
        if (evutil_inet_pton (lpd_mcastAddr.sin_family, lpd_mcastGroup,
                &lpd_mcastAddr.sin_addr) < 0)
            goto fail;

        if (bind (lpd_socket, (struct sockaddr*) &lpd_mcastAddr,
                sizeof lpd_mcastAddr) < 0)
            goto fail;

        /* we want to join that LPD multicast group */
        memset (&mcastReq, 0, sizeof mcastReq);
        mcastReq.imr_multiaddr = lpd_mcastAddr.sin_addr;
        mcastReq.imr_interface.s_addr = htonl (INADDR_ANY);
        if (setsockopt (lpd_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                (const void *) &mcastReq, sizeof mcastReq) < 0)
            goto fail;

        if (setsockopt (lpd_socket, IPPROTO_IP, IP_MULTICAST_LOOP,
                (const void *) &opt_off, sizeof opt_off) < 0)
            goto fail;
    }

    /* setup datagram socket (send) */
    {
        const unsigned char scope = lpd_announceScope;

        lpd_socket2 = socket (PF_INET, SOCK_DGRAM, 0);
        if (lpd_socket2 == TR_BAD_SOCKET)
            goto fail;

        if (evutil_make_socket_nonblocking (lpd_socket2) < 0)
            goto fail;

        /* configure outbound multicast TTL */
        if (setsockopt (lpd_socket2, IPPROTO_IP, IP_MULTICAST_TTL,
                (const void *) &scope, sizeof scope) < 0)
            goto fail;

        if (setsockopt (lpd_socket2, IPPROTO_IP, IP_MULTICAST_LOOP,
                (const void *) &opt_off, sizeof opt_off) < 0)
            goto fail;
    }

    session = ss;

    /* Note: lpd_unsolicitedMsgCounter remains 0 until the first timeout event, thus
     * any announcement received during the initial interval will be discarded. */

    lpd_event = event_new (ss->event_base, lpd_socket, EV_READ | EV_PERSIST, event_callback, NULL);
    event_add (lpd_event, NULL);

    upkeep_timer = evtimer_new (ss->event_base, on_upkeep_timer, ss);
    tr_timerAdd (upkeep_timer, UPKEEP_INTERVAL_SECS, 0);

    tr_logAddNamedDbg ("LPD", "Local Peer Discovery initialised");

    return 1;

    fail:
    {
        const int save = errno;
        evutil_closesocket (lpd_socket);
        evutil_closesocket (lpd_socket2);
        lpd_socket = lpd_socket2 = TR_BAD_SOCKET;
        session = NULL;
        tr_logAddNamedDbg ("LPD", "LPD initialisation failed (errno = %d)", save);
        errno = save;
    }


    return -1;
}

void tr_lpdUninit (tr_session* ss)
{
    if (session != ss)
        return;

    tr_logAddNamedDbg ("LPD", "Uninitialising Local Peer Discovery");

    event_free (lpd_event);
    lpd_event = NULL;

    evtimer_del (upkeep_timer);
    upkeep_timer = NULL;

    /* just shut down, we won't remember any former nodes */
    evutil_closesocket (lpd_socket);
    evutil_closesocket (lpd_socket2);
    tr_logAddNamedDbg ("LPD", "Done uninitialising Local Peer Discovery");

    session = NULL;
}

bool
tr_lpdEnabled (const tr_session* ss)
{
    return ss && (ss == session);
}


/**
* @cond
* @brief Performs some (internal) software consistency checks at compile time.
* @remark Declared inline for the compiler not to allege us of feeding unused
* functions. In any other respect, lpd_consistencyCheck is an orphaned function.
*/
UNUSED static inline void
lpd_consistencyCheck (void)
{
    /* if the following check fails, the definition of a hash string has changed
     * without our knowledge; revise string handling in functions tr_lpdSendAnnounce
     * and tr_lpdConsiderAnnounce. However, the code is designed to function as long
     * as interfaces to the rest of the lib remain compatible with char* strings. */
    TR_STATIC_ASSERT (sizeof (lpd_torStaticType->info.hashString[0]) == sizeof (char), "");
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
bool
tr_lpdSendAnnounce (const tr_torrent* t)
{
    size_t i;
    const char fmt[] =
        "BT-SEARCH * HTTP/%u.%u" CRLF
        "Host: %s:%u" CRLF
        "Port: %u" CRLF
        "Infohash: %s" CRLF
        CRLF
        CRLF;

    char hashString[lengthof (t->info.hashString)];
    char query[lpd_maxDatagramLength + 1] = { 0 };

    if (t == NULL)
        return false;

    /* make sure the hash string is normalized, just in case */
    for (i = 0; i < sizeof hashString; i++)
        hashString[i] = toupper (t->info.hashString[i]);

    /* prepare a zero-terminated announce message */
    tr_snprintf (query, lpd_maxDatagramLength + 1, fmt, 1, 1,
                 lpd_mcastGroup, lpd_mcastPort, lpd_port, hashString);

    /* actually send the query out using [lpd_socket2] */
    {
        const int len = strlen (query);

        /* destination address info has already been set up in tr_lpdInit (),
         * so we refrain from preparing another sockaddr_in here */
        int res = sendto (lpd_socket2, (const void *) query, len, 0,
          (const struct sockaddr*) &lpd_mcastAddr, sizeof lpd_mcastAddr);

        if (res != len)
            return false;
    }

    tr_logAddTorDbg (t, "LPD announce message away");

    return true;
}

/**
* @brief Process incoming unsolicited messages and add the peer to the announced
* torrent if all checks are passed.
*
* @param[in,out] peer Adress information of the peer to add
* @param[in] msg The announcement message to consider
* @return Returns 0 if any input parameter or the announce was invalid, 1 if the peer
* was successfully added, -1 if not; a non-null return value indicates a side-effect to
* the peer in/out parameter.
*
* @note The port information gets added to the peer structure if tr_lpdConsiderAnnounce
* is able to extract the necessary information from the announce message. That is, if
* return != 0, the caller may retrieve the value from the passed structure.
*/
static int tr_lpdConsiderAnnounce (tr_pex* peer, const char* const msg)
{
    enum
    {
        maxValueLen = 25,
        maxHashLen = lengthof (lpd_torStaticType->info.hashString)
    };

    struct lpd_protocolVersion ver = { -1, -1 };
    char value[maxValueLen] = { 0 };
    char hashString[maxHashLen] = { 0 };
    int res = 0, peerPort = 0;

    if (peer != NULL && msg != NULL)
    {
        tr_torrent* tor = NULL;

        const char* params = lpd_extractHeader (msg, &ver);
        if (params == NULL || ver.major != 1) /* allow messages of protocol v1 */
            return 0;

        /* save the effort to check Host, which seems to be optional anyway */

        if (!lpd_extractParam (params, "Port", maxValueLen, value))
            return 0;

        /* determine announced peer port, refuse if value too large */
        if (sscanf (value, "%d", &peerPort) != 1 || peerPort > (in_port_t)-1)
            return 0;

        peer->port = htons (peerPort);
        res = -1; /* signal caller side-effect to peer->port via return != 0 */

        if (!lpd_extractParam (params, "Infohash", maxHashLen, hashString))
            return res;

        tor = tr_torrentFindFromHashString (session, hashString);

        if (tr_isTorrent (tor) && tr_torrentAllowsLPD (tor))
        {
            /* we found a suitable peer, add it to the torrent */
            tr_peerMgrAddPex (tor, TR_PEER_FROM_LPD, peer, -1);
            tr_logAddTorDbg (tor, "Learned %d local peer from LPD (%s:%u)",
                1, tr_address_to_string (&peer->addr), peerPort);

            /* periodic reconnectPulse () deals with the rest... */

            return 1;
        }
        else
            tr_logAddNamedDbg ("LPD", "Cannot serve torrent #%s", hashString);
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
* FIXME: since this function's been made private and is called by a periodic timer,
* most of the previous paragraph isn't true anymore... we weren't using that functionality
* before. are there cases where we should? if not, should we remove the bells & whistles?
*/
static int
tr_lpdAnnounceMore (const time_t now, const int interval)
{
    tr_torrent* tor = NULL;
    int announcesSent = 0;

    if (!tr_isSession (session))
        return -1;

    while ((tor = tr_torrentNext (session, tor))
          && tr_sessionAllowsLPD (session))
    {
        if (tr_isTorrent (tor))
        {
            int announcePrio = 0;

            if (!tr_torrentAllowsLPD (tor))
                continue;

            /* issue #3208: prioritize downloads before seeds */
            switch (tr_torrentGetActivity (tor))
            {
            case TR_STATUS_DOWNLOAD:
                announcePrio = 1;
                break;
            case TR_STATUS_SEED:
                announcePrio = 2;
                break;
            default: /* fall through */
                break;
            }

            if (announcePrio > 0 && tor->lpdAnnounceAt <= now)
            {
                if (tr_lpdSendAnnounce (tor))
                    announcesSent++;

                tor->lpdAnnounceAt = now +
                    lpd_announceInterval * announcePrio;

                break; /* that's enough; for this interval */
            }
        }
    }

    /* perform housekeeping for the flood protection mechanism */
    {
        const int maxAnnounceCap = interval * lpd_announceCapFactor;

        if (lpd_unsolicitedMsgCounter < 0)
            tr_logAddNamedInfo ("LPD", "Dropped %d announces in the last interval (max. %d "
                     "allowed)", -lpd_unsolicitedMsgCounter, maxAnnounceCap);

        lpd_unsolicitedMsgCounter = maxAnnounceCap;
    }

    return announcesSent;
}

static void
on_upkeep_timer (evutil_socket_t foo UNUSED, short bar UNUSED, void * vsession UNUSED)
{
    const time_t now = tr_time ();
    tr_lpdAnnounceMore (now, UPKEEP_INTERVAL_SECS);
    tr_timerAdd (upkeep_timer, UPKEEP_INTERVAL_SECS, 0);
}

/**
* @brief Processing of timeout notifications and incoming data on the socket
* @note maximum rate of read events is limited according to @a lpd_maxAnnounceCap
* @see DoS */
static void event_callback (evutil_socket_t s UNUSED, short type, void* ignore UNUSED)
{
    assert (tr_isSession (session));

    /* do not allow announces to be processed if LPD is disabled */
    if (!tr_sessionAllowsLPD (session))
        return;

    if ((type & EV_READ) != 0)
    {
        struct sockaddr_in foreignAddr;
        int addrLen = sizeof foreignAddr;
        char foreignMsg[lpd_maxDatagramLength + 1];

        /* process local announcement from foreign peer */
        int res = recvfrom (lpd_socket, (void *) foreignMsg, lpd_maxDatagramLength,
            0, (struct sockaddr*) &foreignAddr, (socklen_t*) &addrLen);

        /* besides, do we get flooded? then bail out! */
        if (--lpd_unsolicitedMsgCounter < 0)
            return;

        if (res > 0 && res <= lpd_maxDatagramLength)
        {
            struct tr_pex foreignPeer =
                {
                    .port = 0, /* the peer-to-peer port is yet unknown */
                    .flags = 0
                };

            /* be paranoid enough about zero terminating the foreign string */
            foreignMsg[res] = '\0';

            foreignPeer.addr.addr.addr4 = foreignAddr.sin_addr;
            if (tr_lpdConsiderAnnounce (&foreignPeer, foreignMsg) != 0)
                return; /* OK so far, no log message */
        }

        tr_logAddNamedDbg ("LPD", "Discarded invalid multicast message");
    }
}

