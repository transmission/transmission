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
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

/* ansi */
#include <errno.h>
#include <stdio.h>

/* posix */
#include <netinet/in.h> /* sockaddr_in */
#include <signal.h> /* sig_atomic_t */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <unistd.h> /* close() */
#include <fcntl.h> /* fcntl(), O_NONBLOCK */
#include <ctype.h>

/* third party */
#include <event.h>

/* libT */
#include "transmission.h"
#include "crypto.h"
#include "net.h"
#include "peer-mgr.h" /* tr_peerMgrAddPex() */
#include "session.h"
#include "torrent.h" /* tr_torrentFindFromHash() */
#include "tr-lds.h"
#include "utils.h"
#include "version.h"

/**
* @brief Local Peer Discovery
* @file tr-lds.c
*
* This module implements the Local Peer Discovery (LDS) protocol as supported by the
* uTorrent client application.  A typical LDS datagram is 119 bytes long.
*
* $Id$
*/

static void event_callback( int, short, void* );

static int lds_socket; /**<separate multicast receive socket */
static int lds_socket2; /**<and multicast send socket */
static struct event lds_event;
static tr_port lds_port;

static tr_torrent* lds_torStaticType UNUSED; /* just a helper for static type analysis */
static tr_session* session;

enum { lds_maxDatagramLength = 200 }; /**<the size an LDS datagram must not exceed */
const char lds_mcastGroup[] = "239.192.152.143"; /**<LDS multicast group */
const int lds_mcastPort = 6771; /**<LDS source and destination UPD port */
static struct sockaddr_in lds_mcastAddr; /**<initialized from the above constants in tr_ldsInit */

/**
* @brief Protocol-related information carried by a Local Peer Discovery packet */
struct lds_protocolVersion
{
    int major, minor;
};

enum lds_enumTimeToLive {
    lds_ttlSameSubnet = 1,
    lds_ttlSameSite = 32,
    lds_ttlSameRegion = 64,
    lds_ttlSameContinent = 128,
    lds_ttlUnrestricted = 255
};

enum {
    lds_announceInterval = 4 * 60, /**<4 min announce interval per torrent */
    lds_announceScope = lds_ttlSameSubnet /**<the maximum scope for LDS datagrams */
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
enum { lds_announceCapFactor = 10 };

/**
* @ingroup DoS
* @brief number of unsolicited messages during the last HK interval
* @remark counts downwards */
static int lds_unsolicitedMsgCounter;

/**
* @def CRLF
* @brief a line-feed, as understood by the LDS protocol */
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
* the lds_protocolVersion structure.
*/
static const char* lds_extractHeader( const char* s, struct lds_protocolVersion* const ver )
{
    int major = -1, minor = -1;
    size_t len;

    assert( s != NULL );
    len = strlen( s );

    /* something might be rotten with this chunk of data */
    if( len == 0 || len > lds_maxDatagramLength )
        return NULL;

    /* now we can attempt to look up the BT-SEARCH header */
    if( sscanf( s, "BT-SEARCH * HTTP/%d.%d" CRLF, &major, &minor ) != 2 )
        return NULL;

    if( major < 0 || minor < 0 )
        return NULL;

    {
        /* a pair of blank lines at the end of the string, no place else */
        const char* const two_blank = CRLF CRLF CRLF;
        const char* const end = strstr( s, two_blank );

        if( end == NULL || strlen( end ) > strlen( two_blank ) )
            return NULL;
    }

    if( ver != NULL )
    {
        ver->major = major;
        ver->minor = minor;
    }

    /* separate the header, begins with CRLF */
    return strstr( s, CRLF );
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
static int lds_extractParam( const char* const str, const char* const name, int n, char* const val )
{
    /* configure maximum length of search string here */
    enum { maxLength = 30 };
    char sstr[maxLength] = { };
    const char* pos;

    assert( str != NULL && name != NULL );
    assert( val != NULL );

    if( strlen( name ) > maxLength - strlen( CRLF ": " ) )
        return 0;

    /* compose the string token to search for */
    snprintf( sstr, maxLength, CRLF "%s: ", name );

    pos = strstr( str, sstr );
    if( pos == NULL )
        return 0; /* search was not successful */

    {
        const char* const beg = pos + strlen( sstr );
        const char* const new_line = strstr( beg, CRLF );

        /* the value is delimited by the next CRLF */
        int len = new_line - beg;

        /* if value string hits the length limit n,
         * leave space for a trailing '\0' character */
        if( len < n-- )
            n = len;

        strncpy( val, beg, n );
        val[n] = 0;
    }

    /* we successfully returned the value string */
    return 1;
}

/**
* @} */


/**
* @brief Configures additional capabilities for a socket */
static inline int lds_configureSocket( int sock, int add )
{
    /* read-modify-write socket flags */
    int flags = fcntl( sock, F_GETFL );

    if( flags < 0 )
        return -1;

    if( fcntl( sock, F_SETFL, add | flags ) == -1 )
        return -1;

    return add;
}

/**
* @brief Initializes Local Peer Discovery for this node
*
* For the most part, this means setting up an appropriately configured multicast socket
* and event-based message handling.
*
* @remark Since the LDS service does not use another protocol family yet, this code is
* IPv4 only for the time being.
*/
int tr_ldsInit( tr_session* ss, tr_address* tr_addr UNUSED )
{
    struct ip_mreq mcastReq;
    const int opt_on = 1, opt_off = 0;

    if( session ) /* already initialized */
        return -1;

    assert( lds_announceInterval > 0 );
    assert( lds_announceScope > 0 );

    lds_port = tr_sessionGetPeerPort( ss );
    if( lds_port <= 0 )
        return -1;

    tr_ndbg( "LDS", "Initialising Local Peer Discovery" );

    /* setup datagram socket (receive) */
    {
        lds_socket = socket( PF_INET, SOCK_DGRAM, 0 );
        if( lds_socket < 0 )
            goto fail;

        /* enable non-blocking operation */
        if( lds_configureSocket( lds_socket, O_NONBLOCK ) < 0 )
            goto fail;

        if( setsockopt( lds_socket, SOL_SOCKET, SO_REUSEADDR,
                &opt_on, sizeof opt_on ) < 0 )
            goto fail;

        memset( &lds_mcastAddr, 0, sizeof lds_mcastAddr );
        lds_mcastAddr.sin_family = AF_INET;
        lds_mcastAddr.sin_port = htons( lds_mcastPort );
        if( inet_pton( lds_mcastAddr.sin_family, lds_mcastGroup,
                &lds_mcastAddr.sin_addr ) < 0 )
            goto fail;

        if( bind( lds_socket, (struct sockaddr*) &lds_mcastAddr,
                sizeof lds_mcastAddr ) < 0 )
            goto fail;

        /* we want to join that LDS multicast group */
        memset( &mcastReq, 0, sizeof mcastReq );
        mcastReq.imr_multiaddr = lds_mcastAddr.sin_addr;
        mcastReq.imr_interface.s_addr = htonl( INADDR_ANY );
        if( setsockopt( lds_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                &mcastReq, sizeof mcastReq ) < 0 )
            goto fail;

        if( setsockopt( lds_socket, IPPROTO_IP, IP_MULTICAST_LOOP,
                &opt_off, sizeof opt_off ) < 0 )
            goto fail;
    }

    /* setup datagram socket (send) */
    {
        const unsigned char scope = lds_announceScope;

        lds_socket2 = socket( PF_INET, SOCK_DGRAM, 0 );
        if( lds_socket2 < 0 )
            goto fail;

        /* enable non-blocking operation */
        if( lds_configureSocket( lds_socket2, O_NONBLOCK ) < 0 )
            goto fail;

        /* configure outbound multicast TTL */
        if( setsockopt( lds_socket2, IPPROTO_IP, IP_MULTICAST_TTL,
                &scope, sizeof scope ) < 0 )
            goto fail;

        if( setsockopt( lds_socket2, IPPROTO_IP, IP_MULTICAST_LOOP,
                &opt_off, sizeof opt_off ) < 0 )
            goto fail;
    }

    session = ss;

    /* Note: lds_unsolicitedMsgCounter remains 0 until the first timeout event, thus
     * any announcement received during the initial interval will be discarded. */

    event_set( &lds_event, lds_socket, EV_READ | EV_PERSIST, event_callback, NULL );
    event_add( &lds_event, NULL );

    tr_ndbg( "LDS", "Local Peer Discovery initialised" );

    return 1;

    fail:
    {
        const int save = errno;
        close( lds_socket );
        close( lds_socket2 );
        lds_socket = lds_socket2 = -1;
        session = NULL;
        tr_ndbg( "LDS", "LDS initialisation failed (errno = %d)", save );
        errno = save;
    }


    return -1;
}

void tr_ldsUninit( tr_session* ss )
{
    if( session != ss )
        return;

    tr_ndbg( "LDS", "Uninitialising Local Peer Discovery" );

    event_del( &lds_event );

    /* just shut down, we won't remember any former nodes */
    EVUTIL_CLOSESOCKET( lds_socket );
    EVUTIL_CLOSESOCKET( lds_socket2 );
    tr_ndbg( "LDS", "Done uninitialising Local Peer Discovery" );

    session = NULL;
}

tr_bool tr_ldsEnabled( const tr_session* ss )
{
    return ss && ( ss == session );
}


/**
* @cond
* @brief Performs some (internal) software consistency checks at compile time.
* @remark Declared inline for the compiler not to allege us of feeding unused
* functions. In any other respect, lds_consistencyCheck is an orphaned function.
*/
static inline void lds_consistencyCheck( void )
{
    /* if the following check fails, the definition of a hash string has changed
     * without our knowledge; revise string handling in functions tr_ldsSendAnnounce
     * and tr_ldsConsiderAnnounce. However, the code is designed to function as long
     * as interfaces to the rest of the lib remain compatible with char* strings. */
    STATIC_ASSERT( sizeof(lds_torStaticType->info.hashString[0]) == sizeof(char) );
}
/**
* @endcond */


/**
* @defgroup LdsProto LDS announcement processing
* @{
*/

/**
* @brief Announce the given torrent on the local network
*
* @param[in] t Torrent to announce
* @return Returns TRUE on success
*
* Send a query for torrent t out to the LDS multicast group (or the LAN, for that
* matter).  A listening client on the same network might react by adding us to his
* peer pool for torrent t.
*/
tr_bool tr_ldsSendAnnounce( const tr_torrent* t )
{
    const char fmt[] =
        "BT-SEARCH * HTTP/%u.%u" CRLF
        "Host: %s:%u" CRLF
        "Port: %u" CRLF
        "Infohash: %s" CRLF
        CRLF
        CRLF;

    char hashString[lengthof( t->info.hashString )];
    char query[lds_maxDatagramLength + 1] = { };

    if( t == NULL )
        return FALSE;

    /* make sure the hash string is normalized, just in case */
    for( size_t i = 0; i < sizeof hashString; i++ )
        hashString[i] = toupper( t->info.hashString[i] );

    /* prepare a zero-terminated announce message */
    snprintf( query, lds_maxDatagramLength + 1, fmt, 1, 1,
        lds_mcastGroup, lds_mcastPort, lds_port, hashString );

    /* actually send the query out using [lds_socket2] */
    {
        const int len = strlen( query );

        /* destination address info has already been set up in tr_ldsInit(),
         * so we refrain from preparing another sockaddr_in here */
        int res = sendto( lds_socket2, query, len, 0,
            (const struct sockaddr*) &lds_mcastAddr, sizeof lds_mcastAddr );

        if( res != len )
            return FALSE;
    }

    tr_tordbg( t, "LDS announce message away" );

    return TRUE;
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
* @note The port information gets added to the peer structure if tr_ldsConsiderAnnounce
* is able to extract the necessary information from the announce message.  That is, if
* return != 0, the caller may retrieve the value from the passed structure.
*/
static int tr_ldsConsiderAnnounce( tr_pex* peer, const char* const msg )
{
    enum
    {
        maxValueLen = 25,
        maxHashLen = lengthof(lds_torStaticType->info.hashString)
    };

    struct lds_protocolVersion ver = { -1, -1 };
    char value[maxValueLen] = { };
    char hashString[maxHashLen] = { };
    int res = 0, peerPort = 0;

    if( peer != NULL && msg != NULL )
    {
        tr_torrent* tor = NULL;

        const char* params = lds_extractHeader( msg, &ver );
        if( params == NULL || ver.major != 1 ) /* allow messages of protocol v1 */
            return 0;

        /* save the effort to check Host, which seems to be optional anyway */

        if( lds_extractParam( params, "Port", maxValueLen, value ) == 0 )
            return 0;

        /* determine announced peer port, refuse if value too large */
        if( sscanf( value, "%u", &peerPort ) != 1 || peerPort > (in_port_t)-1 )
            return 0;

        peer->port = htons( peerPort );
        res = -1; /* signal caller side-effect to peer->port via return != 0 */

        if( lds_extractParam( params, "Infohash", maxHashLen, hashString ) == 0 )
            return res;

        tor = tr_torrentFindFromHashString( session, hashString );

        if( tr_isTorrent( tor ) && tr_torrentAllowsLDS( tor ) )
        {
            /* we found a suitable peer, add it to the torrent */
            tr_peerMgrAddPex( tor, TR_PEER_FROM_LDS, peer, -1 );
            tr_tordbg( tor, "Learned %d local peer from LDS (%s:%u)",
                1, inet_ntoa( peer->addr.addr.addr4 ), peerPort );

            /* periodic reconnectPulse() deals with the rest... */

            return 1;
        }
        else
            tr_ndbg( "LDS", "Cannot serve torrent #%s", hashString );
    }

    return res;
}

/**
* @} */

/**
* @note Since it possible for tr_ldsAnnounceMore to get called from outside the LDS module,
* the function needs to be informed of the externally employed housekeeping interval.
* Further, by setting interval to zero (or negative) the caller may actually disable LDS
* announces on a per-interval basis.
*/
int tr_ldsAnnounceMore( const time_t now, const int interval )
{
    tr_torrent* tor = NULL;
    int announcesSent = 0;

    if( !tr_isSession( session ) )
        return -1;

    while(( tor = tr_torrentNext( session, tor ) )
          && tr_sessionAllowsLDS( session ) )
    {
        if( tr_isTorrent( tor ) )
        {
            if( !tr_torrentAllowsLDS( tor ) || (
                    ( tr_torrentGetActivity( tor ) != TR_STATUS_DOWNLOAD ) &&
                    ( tr_torrentGetActivity( tor ) != TR_STATUS_SEED ) ) )
                continue;

            if( tor->ldsAnnounceAt <= now )
            {
                if( tr_ldsSendAnnounce( tor ) )
                    announcesSent++;

                tor->ldsAnnounceAt = now + lds_announceInterval;
                break; /* that's enough; for this interval */
            }
        }
    }

    /* perform housekeeping for the flood protection mechanism */
    {
        const int maxAnnounceCap = interval * lds_announceCapFactor;

        if( lds_unsolicitedMsgCounter < 0 )
            tr_ninf( "LDS", "Dropped %d announces in the last interval (max. %d "
                     "allowed)", -lds_unsolicitedMsgCounter, maxAnnounceCap );

        lds_unsolicitedMsgCounter = maxAnnounceCap;
    }

    return announcesSent;
}

/**
* @brief Processing of timeout notifications and incoming data on the socket
* @note maximum rate of read events is limited according to @a lds_maxAnnounceCap
* @see DoS */
static void event_callback( int s UNUSED, short type, void* ignore UNUSED )
{
    assert( tr_isSession( session ) );

    /* do not allow announces to be processed if LDS is disabled */
    if( !tr_sessionAllowsLDS( session ) )
        return;

    if( ( type & EV_READ ) != 0 )
    {
        struct sockaddr_in foreignAddr;
        int addrLen = sizeof foreignAddr;

        /* be paranoid enough about zero terminating the foreign string */
        char foreignMsg[lds_maxDatagramLength + 1] = { };

        /* process local announcement from foreign peer */
        int res = recvfrom( lds_socket, foreignMsg, lds_maxDatagramLength,
            0, (struct sockaddr*) &foreignAddr, (socklen_t*) &addrLen );

        /* besides, do we get flooded? then bail out! */
        if( --lds_unsolicitedMsgCounter < 0 )
            return;

        if( res > 0 && res <= lds_maxDatagramLength )
        {
            struct tr_pex foreignPeer =
                {
                    .port = 0, /* the peer-to-peer port is yet unknown */
                    .flags = 0
                };

            foreignPeer.addr.addr.addr4 = foreignAddr.sin_addr;
            if( tr_ldsConsiderAnnounce( &foreignPeer, foreignMsg ) != 0 )
                return; /* OK so far, no log message */
        }

        tr_ndbg( "LDS", "Discarded invalid multicast message" );
    }
}

