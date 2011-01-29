/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <limits.h> /* INT_MAX */
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifdef WIN32
 #include <winsock2.h>
#else
 #include <arpa/inet.h> /* inet_ntoa */
#endif

#include <event2/event.h>
#include <event2/bufferevent.h>

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "crypto.h"
#include "list.h"
#include "net.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "peer-io.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"

#define MAGIC_NUMBER 206745

#ifdef WIN32
 #define EAGAIN       WSAEWOULDBLOCK
 #define EINTR        WSAEINTR
 #define EINPROGRESS  WSAEINPROGRESS
 #define EPIPE        WSAECONNRESET
#endif

static size_t
guessPacketOverhead( size_t d )
{
    /**
     * http://sd.wareonearth.com/~phil/net/overhead/
     *
     * TCP over Ethernet:
     * Assuming no header compression (e.g. not PPP)
     * Add 20 IPv4 header or 40 IPv6 header (no options)
     * Add 20 TCP header
     * Add 12 bytes optional TCP timestamps
     * Max TCP Payload data rates over ethernet are thus:
     *  (1500-40)/(38+1500) = 94.9285 %  IPv4, minimal headers
     *  (1500-52)/(38+1500) = 94.1482 %  IPv4, TCP timestamps
     *  (1500-52)/(42+1500) = 93.9040 %  802.1q, IPv4, TCP timestamps
     *  (1500-60)/(38+1500) = 93.6281 %  IPv6, minimal headers
     *  (1500-72)/(38+1500) = 92.8479 %  IPv6, TCP timestamps
     *  (1500-72)/(42+1500) = 92.6070 %  802.1q, IPv6, ICP timestamps
     */
    const double assumed_payload_data_rate = 94.0;

    return (unsigned int)( d * ( 100.0 / assumed_payload_data_rate ) - d );
}

/**
***
**/

#define dbgmsg( io, ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, tr_peerIoGetAddrStr( io ), __VA_ARGS__ ); \
    } while( 0 )

struct tr_datatype
{
    tr_bool  isPieceData;
    size_t   length;
};


/***
****
***/

static void
didWriteWrapper( tr_peerIo * io, unsigned int bytes_transferred )
{
     while( bytes_transferred && tr_isPeerIo( io ) )
     {
        struct tr_datatype * next = io->outbuf_datatypes->data;

        const unsigned int payload = MIN( next->length, bytes_transferred );
        const unsigned int overhead = guessPacketOverhead( payload );
        const uint64_t now = tr_sessionGetTimeMsec( io->session );

        tr_bandwidthUsed( &io->bandwidth, TR_UP, payload, next->isPieceData, now );

        if( overhead > 0 )
            tr_bandwidthUsed( &io->bandwidth, TR_UP, overhead, FALSE, now );

        if( io->didWrite )
            io->didWrite( io, payload, next->isPieceData, io->userData );

        if( tr_isPeerIo( io ) )
        {
            bytes_transferred -= payload;
            next->length -= payload;
            if( !next->length ) {
                tr_list_pop_front( &io->outbuf_datatypes );
                tr_free( next );
            }
        }
    }
}

static void
canReadWrapper( tr_peerIo * io )
{
    tr_bool err = 0;
    tr_bool done = 0;
    tr_session * session;

    dbgmsg( io, "canRead" );

    assert( tr_isPeerIo( io ) );
    assert( tr_isSession( io->session ) );
    tr_peerIoRef( io );

    session = io->session;

    /* try to consume the input buffer */
    if( io->canRead )
    {
        const uint64_t now = tr_sessionGetTimeMsec( io->session );

        tr_sessionLock( session );

        while( !done && !err )
        {
            size_t piece = 0;
            const size_t oldLen = evbuffer_get_length( io->inbuf );
            const int ret = io->canRead( io, io->userData, &piece );
            const size_t used = oldLen - evbuffer_get_length( io->inbuf );
            const unsigned int overhead = guessPacketOverhead( used );

            assert( tr_isPeerIo( io ) );

            if( piece || (piece!=used) )
            {
                if( piece )
                    tr_bandwidthUsed( &io->bandwidth, TR_DOWN, piece, TRUE, now );

                if( used != piece )
                    tr_bandwidthUsed( &io->bandwidth, TR_DOWN, used - piece, FALSE, now );
            }

            if( overhead > 0 )
                tr_bandwidthUsed( &io->bandwidth, TR_UP, overhead, FALSE, now );

            switch( ret )
            {
                case READ_NOW:
                    if( evbuffer_get_length( io->inbuf ) )
                        continue;
                    done = 1;
                    break;

                case READ_LATER:
                    done = 1;
                    break;

                case READ_ERR:
                    err = 1;
                    break;
            }

            assert( tr_isPeerIo( io ) );
        }

        tr_sessionUnlock( session );
    }

    assert( tr_isPeerIo( io ) );
    tr_peerIoUnref( io );
}

tr_bool
tr_isPeerIo( const tr_peerIo * io )
{
    return ( io != NULL )
        && ( io->magicNumber == MAGIC_NUMBER )
        && ( io->refCount >= 0 )
        && ( tr_isBandwidth( &io->bandwidth ) )
        && ( tr_isAddress( &io->addr ) );
}

static void
event_read_cb( int fd, short event UNUSED, void * vio )
{
    int res;
    int e;
    tr_peerIo * io = vio;

    /* Limit the input buffer to 256K, so it doesn't grow too large */
    unsigned int howmuch;
    unsigned int curlen;
    const tr_direction dir = TR_DOWN;
    const unsigned int max = 256 * 1024;

    assert( tr_isPeerIo( io ) );

    io->pendingEvents &= ~EV_READ;

    curlen = evbuffer_get_length( io->inbuf );
    howmuch = curlen >= max ? 0 : max - curlen;
    howmuch = tr_bandwidthClamp( &io->bandwidth, TR_DOWN, howmuch );

    dbgmsg( io, "libevent says this peer is ready to read" );

    /* if we don't have any bandwidth left, stop reading */
    if( howmuch < 1 ) {
        tr_peerIoSetEnabled( io, dir, FALSE );
        return;
    }

    EVUTIL_SET_SOCKET_ERROR( 0 );
    res = evbuffer_read( io->inbuf, fd, (int)howmuch );
    e = EVUTIL_SOCKET_ERROR( );

    if( res > 0 )
    {
        tr_peerIoSetEnabled( io, dir, TRUE );

        /* Invoke the user callback - must always be called last */
        canReadWrapper( io );
    }
    else
    {
        char errstr[512];
        short what = BEV_EVENT_READING;

        if( res == 0 ) /* EOF */
            what |= BEV_EVENT_EOF;
        else if( res == -1 ) {
            if( e == EAGAIN || e == EINTR ) {
                tr_peerIoSetEnabled( io, dir, TRUE );
                return;
            }
            what |= BEV_EVENT_ERROR;
        }

        tr_net_strerror( errstr, sizeof( errstr ), e );
        dbgmsg( io, "event_read_cb got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e, errstr );

        if( io->gotError != NULL )
            io->gotError( io, what, io->userData );
    }
}

static int
tr_evbuffer_write( tr_peerIo * io, int fd, size_t howmuch )
{
    int e;
    int n;
    char errstr[256];

    EVUTIL_SET_SOCKET_ERROR( 0 );
    n = evbuffer_write_atmost( io->outbuf, fd, howmuch );
    e = EVUTIL_SOCKET_ERROR( );
    dbgmsg( io, "wrote %d to peer (%s)", n, (n==-1?tr_net_strerror(errstr,sizeof(errstr),e):"") );

    return n;
}

static void
event_write_cb( int fd, short event UNUSED, void * vio )
{
    int res = 0;
    int e;
    short what = BEV_EVENT_WRITING;
    tr_peerIo * io = vio;
    size_t howmuch;
    const tr_direction dir = TR_UP;

    assert( tr_isPeerIo( io ) );

    io->pendingEvents &= ~EV_WRITE;

    dbgmsg( io, "libevent says this peer is ready to write" );

    /* Write as much as possible, since the socket is non-blocking, write() will
     * return if it can't write any more data without blocking */
    howmuch = tr_bandwidthClamp( &io->bandwidth, dir, evbuffer_get_length( io->outbuf ) );

    /* if we don't have any bandwidth left, stop writing */
    if( howmuch < 1 ) {
        tr_peerIoSetEnabled( io, dir, FALSE );
        return;
    }

    EVUTIL_SET_SOCKET_ERROR( 0 );
    res = tr_evbuffer_write( io, fd, howmuch );
    e = EVUTIL_SOCKET_ERROR( );

    if (res == -1) {
        if (!e || e == EAGAIN || e == EINTR || e == EINPROGRESS)
            goto reschedule;
        /* error case */
        what |= BEV_EVENT_ERROR;
    } else if (res == 0) {
        /* eof case */
        what |= BEV_EVENT_EOF;
    }
    if (res <= 0)
        goto error;

    if( evbuffer_get_length( io->outbuf ) )
        tr_peerIoSetEnabled( io, dir, TRUE );

    didWriteWrapper( io, res );
    return;

 reschedule:
    if( evbuffer_get_length( io->outbuf ) )
        tr_peerIoSetEnabled( io, dir, TRUE );
    return;

 error:

    dbgmsg( io, "event_write_cb got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e, strerror( e ) );

    if( io->gotError != NULL )
        io->gotError( io, what, io->userData );
}

/**
***
**/

static void
maybeSetCongestionAlgorithm( int socket, const char * algorithm )
{
    if( algorithm && *algorithm )
    {
        const int rc = tr_netSetCongestionControl( socket, algorithm );

        if( rc < 0 )
            tr_ninf( "Net", "Can't set congestion control algorithm '%s': %s",
                     algorithm, tr_strerror( errno ));
    }
}

static tr_peerIo*
tr_peerIoNew( tr_session       * session,
              tr_bandwidth     * parent,
              const tr_address * addr,
              tr_port            port,
              const uint8_t    * torrentHash,
              tr_bool            isIncoming,
              tr_bool            isSeed,
              int                socket )
{
    tr_peerIo * io;

    assert( session != NULL );
    assert( session->events != NULL );
    assert( tr_isBool( isIncoming ) );
    assert( tr_isBool( isSeed ) );
    assert( tr_amInEventThread( session ) );

    if( socket >= 0 ) {
        tr_netSetTOS( socket, session->peerSocketTOS );
        maybeSetCongestionAlgorithm( socket, session->peer_congestion_algorithm );
    }

    io = tr_new0( tr_peerIo, 1 );
    io->magicNumber = MAGIC_NUMBER;
    io->refCount = 1;
    io->crypto = tr_cryptoNew( torrentHash, isIncoming );
    io->session = session;
    io->addr = *addr;
    io->isSeed = isSeed;
    io->port = port;
    io->socket = socket;
    io->isIncoming = isIncoming != 0;
    io->timeCreated = tr_time( );
    io->inbuf = evbuffer_new( );
    io->outbuf = evbuffer_new( );
    io->event_read = event_new( session->event_base, io->socket, EV_READ, event_read_cb, io );
    io->event_write = event_new( session->event_base, io->socket, EV_WRITE, event_write_cb, io );
    tr_bandwidthConstruct( &io->bandwidth, session, parent );
    tr_bandwidthSetPeer( &io->bandwidth, io );
    dbgmsg( io, "bandwidth is %p; its parent is %p", &io->bandwidth, parent );

    return io;
}

tr_peerIo*
tr_peerIoNewIncoming( tr_session        * session,
                      tr_bandwidth      * parent,
                      const tr_address  * addr,
                      tr_port             port,
                      int                 fd )
{
    assert( session );
    assert( tr_isAddress( addr ) );
    assert( fd >= 0 );

    return tr_peerIoNew( session, parent, addr, port, NULL, TRUE, FALSE, fd );
}

tr_peerIo*
tr_peerIoNewOutgoing( tr_session        * session,
                      tr_bandwidth      * parent,
                      const tr_address  * addr,
                      tr_port             port,
                      const uint8_t     * torrentHash,
                      tr_bool             isSeed )
{
    int fd;

    assert( session );
    assert( tr_isAddress( addr ) );
    assert( torrentHash );

    fd = tr_netOpenPeerSocket( session, addr, port, isSeed );
    dbgmsg( NULL, "tr_netOpenPeerSocket returned fd %d", fd );

    return fd < 0 ? NULL
                  : tr_peerIoNew( session, parent, addr, port, torrentHash, FALSE, isSeed, fd );
}

/***
****
***/

static void
event_enable( tr_peerIo * io, short event )
{
    assert( tr_amInEventThread( io->session ) );
    assert( io->session != NULL );
    assert( io->session->events != NULL );
    assert( event_initialized( io->event_read ) );
    assert( event_initialized( io->event_write ) );

    if( io->socket < 0 )
        return;

    if( ( event & EV_READ ) && ! ( io->pendingEvents & EV_READ ) )
    {
        dbgmsg( io, "enabling libevent ready-to-read polling" );
        event_add( io->event_read, NULL );
        io->pendingEvents |= EV_READ;
    }

    if( ( event & EV_WRITE ) && ! ( io->pendingEvents & EV_WRITE ) )
    {
        dbgmsg( io, "enabling libevent ready-to-write polling" );
        event_add( io->event_write, NULL );
        io->pendingEvents |= EV_WRITE;
    }
}

static void
event_disable( struct tr_peerIo * io, short event )
{
    assert( tr_amInEventThread( io->session ) );
    assert( io->session != NULL );
    assert( io->session->events != NULL );
    assert( event_initialized( io->event_read ) );
    assert( event_initialized( io->event_write ) );

    if( ( event & EV_READ ) && ( io->pendingEvents & EV_READ ) )
    {
        dbgmsg( io, "disabling libevent ready-to-read polling" );
        event_del( io->event_read );
        io->pendingEvents &= ~EV_READ;
    }

    if( ( event & EV_WRITE ) && ( io->pendingEvents & EV_WRITE ) )
    {
        dbgmsg( io, "disabling libevent ready-to-write polling" );
        event_del( io->event_write );
        io->pendingEvents &= ~EV_WRITE;
    }
}

void
tr_peerIoSetEnabled( tr_peerIo    * io,
                     tr_direction   dir,
                     tr_bool        isEnabled )
{
    const short event = dir == TR_UP ? EV_WRITE : EV_READ;

    assert( tr_isPeerIo( io ) );
    assert( tr_isDirection( dir ) );
    assert( tr_amInEventThread( io->session ) );
    assert( io->session->events != NULL );

    if( isEnabled )
        event_enable( io, event );
    else
        event_disable( io, event );
}

/***
****
***/

static void
io_dtor( void * vio )
{
    tr_peerIo * io = vio;

    assert( tr_isPeerIo( io ) );
    assert( tr_amInEventThread( io->session ) );
    assert( io->session->events != NULL );

    dbgmsg( io, "in tr_peerIo destructor" );
    event_disable( io, EV_READ | EV_WRITE );
    event_free( io->event_read );
    event_free( io->event_write );
    tr_bandwidthDestruct( &io->bandwidth );
    evbuffer_free( io->outbuf );
    evbuffer_free( io->inbuf );
    tr_netClose( io->session, io->socket );
    tr_cryptoFree( io->crypto );
    tr_list_free( &io->outbuf_datatypes, tr_free );

    memset( io, ~0, sizeof( tr_peerIo ) );
    tr_free( io );
}

static void
tr_peerIoFree( tr_peerIo * io )
{
    if( io )
    {
        dbgmsg( io, "in tr_peerIoFree" );
        io->canRead = NULL;
        io->didWrite = NULL;
        io->gotError = NULL;
        tr_runInEventThread( io->session, io_dtor, io );
    }
}

void
tr_peerIoRefImpl( const char * file, int line, tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );

    dbgmsg( io, "%s:%d is incrementing the IO's refcount from %d to %d",
                file, line, io->refCount, io->refCount+1 );

    ++io->refCount;
}

void
tr_peerIoUnrefImpl( const char * file, int line, tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );

    dbgmsg( io, "%s:%d is decrementing the IO's refcount from %d to %d",
                file, line, io->refCount, io->refCount-1 );

    if( !--io->refCount )
        tr_peerIoFree( io );
}

const tr_address*
tr_peerIoGetAddress( const tr_peerIo * io, tr_port   * port )
{
    assert( tr_isPeerIo( io ) );

    if( port )
        *port = io->port;

    return &io->addr;
}

const char*
tr_peerIoAddrStr( const tr_address * addr, tr_port port )
{
    static char buf[512];

    if( addr->type == TR_AF_INET )
        tr_snprintf( buf, sizeof( buf ), "%s:%u", tr_ntop_non_ts( addr ), ntohs( port ) );
    else
        tr_snprintf( buf, sizeof( buf ), "[%s]:%u", tr_ntop_non_ts( addr ), ntohs( port ) );
    return buf;
}

const char* tr_peerIoGetAddrStr( const tr_peerIo * io )
{
    return tr_isPeerIo( io ) ? tr_peerIoAddrStr( &io->addr, io->port ) : "error";
}

void
tr_peerIoSetIOFuncs( tr_peerIo        * io,
                     tr_can_read_cb     readcb,
                     tr_did_write_cb    writecb,
                     tr_net_error_cb    errcb,
                     void             * userData )
{
    io->canRead = readcb;
    io->didWrite = writecb;
    io->gotError = errcb;
    io->userData = userData;
}

void
tr_peerIoClear( tr_peerIo * io )
{
    tr_peerIoSetIOFuncs( io, NULL, NULL, NULL, NULL );
    tr_peerIoSetEnabled( io, TR_UP, FALSE );
    tr_peerIoSetEnabled( io, TR_DOWN, FALSE );
}

int
tr_peerIoReconnect( tr_peerIo * io )
{
    short int pendingEvents;
    tr_session * session;

    assert( tr_isPeerIo( io ) );
    assert( !tr_peerIoIsIncoming( io ) );

    session = tr_peerIoGetSession( io );

    pendingEvents = io->pendingEvents;
    event_disable( io, EV_READ | EV_WRITE );

    if( io->socket >= 0 )
        tr_netClose( session, io->socket );

    event_del( io->event_read );
    event_del( io->event_write );
    io->socket = tr_netOpenPeerSocket( session, &io->addr, io->port, io->isSeed );
    io->event_read = event_new( session->event_base, io->socket, EV_READ, event_read_cb, io );
    io->event_write = event_new( session->event_base, io->socket, EV_WRITE, event_write_cb, io );

    if( io->socket >= 0 )
    {
        event_enable( io, pendingEvents );
        tr_netSetTOS( io->socket, session->peerSocketTOS );
        maybeSetCongestionAlgorithm( io->socket, session->peer_congestion_algorithm );
        return 0;
    }

    return -1;
}

/**
***
**/

void
tr_peerIoSetTorrentHash( tr_peerIo *     io,
                         const uint8_t * hash )
{
    assert( tr_isPeerIo( io ) );

    tr_cryptoSetTorrentHash( io->crypto, hash );
}

const uint8_t*
tr_peerIoGetTorrentHash( tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );
    assert( io->crypto );

    return tr_cryptoGetTorrentHash( io->crypto );
}

int
tr_peerIoHasTorrentHash( const tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );
    assert( io->crypto );

    return tr_cryptoHasTorrentHash( io->crypto );
}

/**
***
**/

void
tr_peerIoSetPeersId( tr_peerIo *     io,
                     const uint8_t * peer_id )
{
    assert( tr_isPeerIo( io ) );

    if( ( io->peerIdIsSet = peer_id != NULL ) )
        memcpy( io->peerId, peer_id, 20 );
    else
        memset( io->peerId, 0, 20 );
}

/**
***
**/

static unsigned int
getDesiredOutputBufferSize( const tr_peerIo * io, uint64_t now )
{
    /* this is all kind of arbitrary, but what seems to work well is
     * being large enough to hold the next 20 seconds' worth of input,
     * or a few blocks, whichever is bigger.
     * It's okay to tweak this as needed */
    const unsigned int currentSpeed_Bps = tr_bandwidthGetPieceSpeed_Bps( &io->bandwidth, now, TR_UP );
    const unsigned int period = 15u; /* arbitrary */
    /* the 3 is arbitrary; the .5 is to leave room for messages */
    static const unsigned int ceiling =  (unsigned int)( MAX_BLOCK_SIZE * 3.5 );
    return MAX( ceiling, currentSpeed_Bps*period );
}

size_t
tr_peerIoGetWriteBufferSpace( const tr_peerIo * io, uint64_t now )
{
    const size_t desiredLen = getDesiredOutputBufferSize( io, now );
    const size_t currentLen = evbuffer_get_length( io->outbuf );
    size_t freeSpace = 0;

    if( desiredLen > currentLen )
        freeSpace = desiredLen - currentLen;

    return freeSpace;
}

/**
***
**/

void
tr_peerIoSetEncryption( tr_peerIo * io, uint32_t encryptionMode )
{
    assert( tr_isPeerIo( io ) );
    assert( encryptionMode == PEER_ENCRYPTION_NONE
         || encryptionMode == PEER_ENCRYPTION_RC4 );

    io->encryptionMode = encryptionMode;
}

/**
***
**/

static void
addDatatype( tr_peerIo * io, size_t byteCount, tr_bool isPieceData )
{
    struct tr_datatype * d;

    d = tr_new( struct tr_datatype, 1 );
    d->isPieceData = isPieceData != 0;
    d->length = byteCount;
    tr_list_append( &io->outbuf_datatypes, d );
}

static struct evbuffer_iovec *
evbuffer_peek_all( struct evbuffer * buf, size_t * setme_vecCount )
{
    const size_t byteCount = evbuffer_get_length( buf );
    const int vecCount = evbuffer_peek( buf, byteCount, NULL, NULL, 0 );
    struct evbuffer_iovec * iovec = tr_new0( struct evbuffer_iovec, vecCount );
    const int n = evbuffer_peek( buf, byteCount, NULL, iovec, vecCount );
    assert( vecCount == n );
    *setme_vecCount = vecCount;
    return iovec;
}

static void
maybeEncryptBuffer( tr_peerIo * io, struct evbuffer * buf )
{
    if( io->encryptionMode == PEER_ENCRYPTION_RC4 )
    {
        size_t i, n;
        struct evbuffer_iovec * iovec = evbuffer_peek_all( buf, &n );

        for( i=0; i<n; ++i )
            tr_cryptoEncrypt( io->crypto, iovec[i].iov_len, iovec[i].iov_base, iovec[i].iov_base );

        tr_free( iovec );
    }
}

void
tr_peerIoWriteBuf( tr_peerIo * io, struct evbuffer * buf, tr_bool isPieceData )
{
    const size_t byteCount = evbuffer_get_length( buf );
    maybeEncryptBuffer( io, buf );
    evbuffer_add_buffer( io->outbuf, buf );
    addDatatype( io, byteCount, isPieceData );
}

void
tr_peerIoWriteBytes( tr_peerIo * io, const void * bytes, size_t byteCount, tr_bool isPieceData )
{
    struct evbuffer * buf = evbuffer_new( );
    evbuffer_add( buf, bytes, byteCount );
    tr_peerIoWriteBuf( io, buf, isPieceData );
    evbuffer_free( buf );
}

/***
****
***/

void
evbuffer_add_uint16( struct evbuffer * outbuf, uint16_t addme_hs )
{
    const uint16_t ns = htons( addme_hs );
    evbuffer_add( outbuf, &ns, sizeof( ns ) );
}

void
evbuffer_add_uint32( struct evbuffer * outbuf, uint32_t addme_hl )
{
    const uint32_t nl = htonl( addme_hl );
    evbuffer_add( outbuf, &nl, sizeof( nl ) );
}

/***
****
***/

void
tr_peerIoReadBytes( tr_peerIo * io, struct evbuffer * inbuf, void * bytes, size_t byteCount )
{
    assert( tr_isPeerIo( io ) );
    assert( evbuffer_get_length( inbuf )  >= byteCount );

    switch( io->encryptionMode )
    {
        case PEER_ENCRYPTION_NONE:
            evbuffer_remove( inbuf, bytes, byteCount );
            break;

        case PEER_ENCRYPTION_RC4:
            evbuffer_remove( inbuf, bytes, byteCount );
            tr_cryptoDecrypt( io->crypto, byteCount, bytes, bytes );
            break;

        default:
            assert( 0 );
    }
}

void
tr_peerIoReadUint16( tr_peerIo        * io,
                     struct evbuffer  * inbuf,
                     uint16_t         * setme )
{
    uint16_t tmp;
    tr_peerIoReadBytes( io, inbuf, &tmp, sizeof( uint16_t ) );
    *setme = ntohs( tmp );
}

void tr_peerIoReadUint32( tr_peerIo        * io,
                          struct evbuffer  * inbuf,
                          uint32_t         * setme )
{
    uint32_t tmp;
    tr_peerIoReadBytes( io, inbuf, &tmp, sizeof( uint32_t ) );
    *setme = ntohl( tmp );
}

void
tr_peerIoDrain( tr_peerIo       * io,
                struct evbuffer * inbuf,
                size_t            byteCount )
{
    void * buf = tr_sessionGetBuffer( io->session );
    const size_t buflen = SESSION_BUFFER_SIZE;

    while( byteCount > 0 )
    {
        const size_t thisPass = MIN( byteCount, buflen );
        tr_peerIoReadBytes( io, inbuf, buf, thisPass );
        byteCount -= thisPass;
    }

    tr_sessionReleaseBuffer( io->session );
}

/***
****
***/

static int
tr_peerIoTryRead( tr_peerIo * io, size_t howmuch )
{
    int res = 0;

    if(( howmuch = tr_bandwidthClamp( &io->bandwidth, TR_DOWN, howmuch )))
    {
        int e;

        EVUTIL_SET_SOCKET_ERROR( 0 );
        res = evbuffer_read( io->inbuf, io->socket, (int)howmuch );
        e = EVUTIL_SOCKET_ERROR( );

        dbgmsg( io, "read %d from peer (%s)", res, (res==-1?strerror(e):"") );

        if( evbuffer_get_length( io->inbuf ) )
            canReadWrapper( io );

        if( ( res <= 0 ) && ( io->gotError ) && ( e != EAGAIN ) && ( e != EINTR ) && ( e != EINPROGRESS ) )
        {
            char errstr[512];
            short what = BEV_EVENT_READING | BEV_EVENT_ERROR;
            if( res == 0 )
                what |= BEV_EVENT_EOF;
            tr_net_strerror( errstr, sizeof( errstr ), e );
            dbgmsg( io, "tr_peerIoTryRead got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e, errstr );
            io->gotError( io, what, io->userData );
        }
    }

    return res;
}

static int
tr_peerIoTryWrite( tr_peerIo * io, size_t howmuch )
{
    int n = 0;

    if(( howmuch = tr_bandwidthClamp( &io->bandwidth, TR_UP, howmuch )))
    {
        int e;
        EVUTIL_SET_SOCKET_ERROR( 0 );
        n = tr_evbuffer_write( io, io->socket, howmuch );
        e = EVUTIL_SOCKET_ERROR( );

        if( n > 0 )
            didWriteWrapper( io, n );

        if( ( n < 0 ) && ( io->gotError ) && e && ( e != EPIPE ) && ( e != EAGAIN ) && ( e != EINTR ) && ( e != EINPROGRESS ) )
        {
            char errstr[512];
            const short what = BEV_EVENT_WRITING | BEV_EVENT_ERROR;

            tr_net_strerror( errstr, sizeof( errstr ), e );
            dbgmsg( io, "tr_peerIoTryWrite got an error. res is %d, what is %hd, errno is %d (%s)", n, what, e, errstr );

            if( io->gotError != NULL )
                io->gotError( io, what, io->userData );
        }
    }

    return n;
}

int
tr_peerIoFlush( tr_peerIo  * io, tr_direction dir, size_t limit )
{
    int bytesUsed = 0;

    assert( tr_isPeerIo( io ) );
    assert( tr_isDirection( dir ) );

    if( dir == TR_DOWN )
        bytesUsed = tr_peerIoTryRead( io, limit );
    else
        bytesUsed = tr_peerIoTryWrite( io, limit );

    dbgmsg( io, "flushing peer-io, direction %d, limit %zu, bytesUsed %d", (int)dir, limit, bytesUsed );
    return bytesUsed;
}

int
tr_peerIoFlushOutgoingProtocolMsgs( tr_peerIo * io )
{
    size_t byteCount = 0;
    tr_list * it;

    /* count up how many bytes are used by non-piece-data messages
       at the front of our outbound queue */
    for( it=io->outbuf_datatypes; it!=NULL; it=it->next )
    {
        struct tr_datatype * d = it->data;

        if( d->isPieceData )
            break;

        byteCount += d->length;
    }

    return tr_peerIoFlush( io, TR_UP, byteCount );
}
