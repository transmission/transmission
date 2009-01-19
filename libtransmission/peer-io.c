/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
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

#include <event.h>

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "crypto.h"
#include "list.h"
#include "net.h"
#include "peer-io.h"
#include "platform.h" /* MAX_STACK_ARRAY_SIZE */
#include "trevent.h"
#include "utils.h"

#define MAGIC_NUMBER 206745

static size_t
getPacketOverhead( size_t d )
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
    static const double assumed_payload_data_rate = 94.0;

    return (size_t)( d * ( 100.0 / assumed_payload_data_rate ) - d );
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
    struct __tr_list head;
};

/***
****
***/

static void
didWriteWrapper( tr_peerIo * io, size_t bytes_transferred )
{
    while( bytes_transferred )
    {
        struct tr_datatype * next = __tr_list_entry( io->outbuf_datatypes.next, struct tr_datatype, head );
        const size_t payload = MIN( next->length, bytes_transferred );
        const size_t overhead = getPacketOverhead( payload );

        tr_bandwidthUsed( &io->bandwidth, TR_UP, payload, next->isPieceData );

        if( overhead > 0 )
            tr_bandwidthUsed( &io->bandwidth, TR_UP, overhead, FALSE );

        if( io->didWrite )
            io->didWrite( io, payload, next->isPieceData, io->userData );

        bytes_transferred -= payload;
        next->length -= payload;
        if( !next->length ) {
            __tr_list_remove( io->outbuf_datatypes.next );
            tr_free( next );
	}
    }
}

static void
canReadWrapper( tr_peerIo * io )
{
    tr_bool done = 0;
    tr_bool err = 0;
    tr_session * session = io->session;

    dbgmsg( io, "canRead" );

    tr_peerIoRef( io );

    /* try to consume the input buffer */
    if( io->canRead )
    {
        tr_globalLock( session );

        while( !done && !err )
        {
            size_t piece = 0;
            const size_t oldLen = EVBUFFER_LENGTH( io->inbuf );
            const int ret = io->canRead( io, io->userData, &piece );

            const size_t used = oldLen - EVBUFFER_LENGTH( io->inbuf );

            assert( tr_isPeerIo( io ) );

            if( piece )
                tr_bandwidthUsed( &io->bandwidth, TR_DOWN, piece, TRUE );

            if( used != piece )
                tr_bandwidthUsed( &io->bandwidth, TR_DOWN, used - piece, FALSE );

            switch( ret )
            {
                case READ_NOW:
                    if( EVBUFFER_LENGTH( io->inbuf ) )
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
        }

        tr_globalUnlock( session );
    }

    tr_peerIoUnref( io );
}

tr_bool
tr_isPeerIo( const tr_peerIo * io )
{
    return ( io != NULL )
        && ( io->magicNumber == MAGIC_NUMBER )
        && ( io->refCount > 0 )
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
    size_t howmuch;
    const tr_direction dir = TR_DOWN;
    const size_t max = 256 * 1024;
    size_t curlen;

    assert( tr_isPeerIo( io ) );

    io->hasFinishedConnecting = TRUE;

    curlen = EVBUFFER_LENGTH( io->inbuf );
    howmuch = curlen >= max ? 0 : max - curlen;
    howmuch = tr_bandwidthClamp( &io->bandwidth, TR_DOWN, howmuch );

    dbgmsg( io, "libevent says this peer is ready to read" );

    /* if we don't have any bandwidth left, stop reading */
    if( howmuch < 1 ) {
        tr_peerIoSetEnabled( io, dir, FALSE );
        return;
    }

    errno = 0;
    res = evbuffer_read( io->inbuf, fd, howmuch );
    e = errno;

    if( res > 0 )
    {
        tr_peerIoSetEnabled( io, dir, TRUE );

        /* Invoke the user callback - must always be called last */
        canReadWrapper( io );
    }
    else
    {
        short what = EVBUFFER_READ;

        if( res == 0 ) /* EOF */
            what |= EVBUFFER_EOF;
        else if( res == -1 ) {
            if( e == EAGAIN || e == EINTR ) {
                tr_peerIoSetEnabled( io, dir, TRUE );
                return;
            }
            what |= EVBUFFER_ERROR;
        }

        dbgmsg( io, "event_read_cb got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e, strerror( e ) );

        if( io->gotError != NULL )
            io->gotError( io, what, io->userData );
    }
}

static int
tr_evbuffer_write( tr_peerIo * io, int fd, size_t howmuch )
{
    int e;
    int n;
    struct evbuffer * buffer = io->outbuf;

    howmuch = MIN( EVBUFFER_LENGTH( buffer ), howmuch );

    errno = 0;
#ifdef WIN32
    n = (int) send(fd, buffer->buffer, howmuch,  0 );
#else
    n = (int) write(fd, buffer->buffer, howmuch );
#endif
    e = errno;
    dbgmsg( io, "wrote %d to peer (%s)", n, (n==-1?strerror(e):"") );

    if( n > 0 )
        evbuffer_drain( buffer, n );

    return n;
}

static void
event_write_cb( int fd, short event UNUSED, void * vio )
{
    int res = 0;
    int e;
    short what = EVBUFFER_WRITE;
    tr_peerIo * io = vio;
    size_t howmuch;
    const tr_direction dir = TR_UP;

    assert( tr_isPeerIo( io ) );

    io->hasFinishedConnecting = TRUE;

    dbgmsg( io, "libevent says this peer is ready to write" );

    /* Write as much as possible, since the socket is non-blocking, write() will
     * return if it can't write any more data without blocking */
    howmuch = tr_bandwidthClamp( &io->bandwidth, dir, EVBUFFER_LENGTH( io->outbuf ) );

    /* if we don't have any bandwidth left, stop writing */
    if( howmuch < 1 ) {
        tr_peerIoSetEnabled( io, dir, FALSE );
        return;
    }

    errno = 0;
    res = tr_evbuffer_write( io, fd, howmuch );
    e = errno;

    if (res == -1) {
#ifndef WIN32
/*todo. evbuffer uses WriteFile when WIN32 is set. WIN32 system calls do not
 *  *set errno. thus this error checking is not portable*/
        if (e == EAGAIN || e == EINTR || e == EINPROGRESS)
            goto reschedule;
        /* error case */
        what |= EVBUFFER_ERROR;

#else
        goto reschedule;
#endif

    } else if (res == 0) {
        /* eof case */
        what |= EVBUFFER_EOF;
    }
    if (res <= 0)
        goto error;

    if( EVBUFFER_LENGTH( io->outbuf ) )
        tr_peerIoSetEnabled( io, dir, TRUE );

    didWriteWrapper( io, res );
    return;

 reschedule:
    if( EVBUFFER_LENGTH( io->outbuf ) )
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

static tr_peerIo*
tr_peerIoNew( tr_session       * session,
              tr_bandwidth     * parent,
              const tr_address * addr,
              tr_port            port,
              const uint8_t    * torrentHash,
              int                isIncoming,
              int                socket )
{
    tr_peerIo * io;

    if( socket >= 0 )
        tr_netSetTOS( socket, session->peerSocketTOS );

    io = tr_new0( tr_peerIo, 1 );
    io->magicNumber = MAGIC_NUMBER;
    io->refCount = 1;
    io->crypto = tr_cryptoNew( torrentHash, isIncoming );
    io->session = session;
    io->addr = *addr;
    io->port = port;
    io->socket = socket;
    io->isIncoming = isIncoming != 0;
    io->hasFinishedConnecting = FALSE;
    io->timeCreated = time( NULL );
    io->inbuf = evbuffer_new( );
    io->outbuf = evbuffer_new( );
    tr_bandwidthConstruct( &io->bandwidth, session, parent );
    tr_bandwidthSetPeer( &io->bandwidth, io );
    dbgmsg( io, "bandwidth is %p; its parent is %p", &io->bandwidth, parent );

    event_set( &io->event_read, io->socket, EV_READ, event_read_cb, io );
    event_set( &io->event_write, io->socket, EV_WRITE, event_write_cb, io );

    __tr_list_init( &io->outbuf_datatypes );

    return io;
}

tr_peerIo*
tr_peerIoNewIncoming( tr_session        * session,
                      tr_bandwidth      * parent,
                      const tr_address  * addr,
                      tr_port             port,
                      int                 socket )
{
    assert( session );
    assert( tr_isAddress( addr ) );
    assert( socket >= 0 );

    return tr_peerIoNew( session, parent, addr, port, NULL, 1, socket );
}

tr_peerIo*
tr_peerIoNewOutgoing( tr_session        * session,
                      tr_bandwidth      * parent,
                      const tr_address  * addr,
                      tr_port             port,
                      const uint8_t     * torrentHash )
{
    int socket;

    assert( session );
    assert( tr_isAddress( addr ) );
    assert( torrentHash );

    socket = tr_netOpenTCP( session, addr, port );
    dbgmsg( NULL, "tr_netOpenTCP returned fd %d", socket );

    return socket < 0
           ? NULL
           : tr_peerIoNew( session, parent, addr, port, torrentHash, 0, socket );
}

static void
trDatatypeFree( void * data )
{
    struct tr_datatype * dt = __tr_list_entry( data, struct tr_datatype, head );
    tr_free(dt);
}

static void
io_dtor( void * vio )
{
    tr_peerIo * io = vio;

    event_del( &io->event_read );
    event_del( &io->event_write );
    tr_bandwidthDestruct( &io->bandwidth );
    evbuffer_free( io->outbuf );
    evbuffer_free( io->inbuf );
    tr_netClose( io->socket );
    tr_cryptoFree( io->crypto );
    __tr_list_destroy( &io->outbuf_datatypes, trDatatypeFree );

    io->magicNumber = 0xDEAD;
    tr_free( io );
}

static void
tr_peerIoFree( tr_peerIo * io )
{
    if( io )
    {
        io->canRead = NULL;
        io->didWrite = NULL;
        io->gotError = NULL;
        tr_runInEventThread( io->session, io_dtor, io );
    }
}

void
tr_peerIoRef( tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );

    ++io->refCount;
}

void
tr_peerIoUnref( tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );

    if( !--io->refCount )
        tr_peerIoFree( io );
}

const tr_address*
tr_peerIoGetAddress( const tr_peerIo * io,
                           tr_port   * port )
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
    assert( !tr_peerIoIsIncoming( io ) );

    if( io->socket >= 0 )
        tr_netClose( io->socket );

    io->socket = tr_netOpenTCP( io->session, &io->addr, io->port ); 
    if( io->socket >= 0 )
    {
        tr_netSetTOS( io->socket, io->session->peerSocketTOS );
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

void
tr_peerIoEnableFEXT( tr_peerIo * io,
                     tr_bool     flag )
{
    assert( tr_isPeerIo( io ) );
    assert( tr_isBool( flag ) );

    dbgmsg( io, "setting FEXT support flag to %d", (flag!=0) );
    io->fastExtensionSupported = flag;
}

void
tr_peerIoEnableLTEP( tr_peerIo  * io,
                     tr_bool      flag )
{
    assert( tr_isPeerIo( io ) );
    assert( tr_isBool( flag ) );

    dbgmsg( io, "setting LTEP support flag to %d", (flag!=0) );
    io->extendedProtocolSupported = flag;
}

/**
***
**/

static size_t
getDesiredOutputBufferSize( const tr_peerIo * io, uint64_t now )
{
    /* this is all kind of arbitrary, but what seems to work well is
     * being large enough to hold the next 20 seconds' worth of input,
     * or a few blocks, whichever is bigger.
     * It's okay to tweak this as needed */
    const double maxBlockSize = 16 * 1024; /* 16 KiB is from BT spec */
    const double currentSpeed = tr_bandwidthGetPieceSpeed( &io->bandwidth, now, TR_UP );
    const double period = 20; /* arbitrary */
    const double numBlocks = 5.5; /* the 5 is arbitrary; the .5 is to leave room for messages */
    return MAX( maxBlockSize*numBlocks, currentSpeed*1024*period );
}

size_t
tr_peerIoGetWriteBufferSpace( const tr_peerIo * io, uint64_t now )
{
    const size_t desiredLen = getDesiredOutputBufferSize( io, now );
    const size_t currentLen = EVBUFFER_LENGTH( io->outbuf );
    size_t freeSpace = 0;

    if( desiredLen > currentLen )
        freeSpace = desiredLen - currentLen;

    return freeSpace;
}

/**
***
**/

void
tr_peerIoSetEncryption( tr_peerIo * io,
                        int         encryptionMode )
{
    assert( tr_isPeerIo( io ) );
    assert( encryptionMode == PEER_ENCRYPTION_NONE
         || encryptionMode == PEER_ENCRYPTION_RC4 );

    io->encryptionMode = encryptionMode;
}

/**
***
**/

void
tr_peerIoWrite( tr_peerIo   * io,
                const void  * writeme,
                size_t        writemeLen,
                int           isPieceData )
{
    struct tr_datatype * datatype;

    assert( tr_amInEventThread( io->session ) );
    dbgmsg( io, "adding %zu bytes into io->output", writemeLen );

    datatype = tr_new( struct tr_datatype, 1 );
    datatype->isPieceData = isPieceData != 0;
    datatype->length = writemeLen;

    __tr_list_init( &datatype->head );
    __tr_list_append( &io->outbuf_datatypes, &datatype->head );

    evbuffer_add( io->outbuf, writeme, writemeLen );
}

void
tr_peerIoWriteBuf( tr_peerIo         * io,
                   struct evbuffer   * buf,
                   int                 isPieceData )
{
    const size_t n = EVBUFFER_LENGTH( buf );
    tr_peerIoWrite( io, EVBUFFER_DATA( buf ), n, isPieceData );
    evbuffer_drain( buf, n );
}

/**
***
**/

void
tr_peerIoWriteBytes( tr_peerIo       * io,
                     struct evbuffer * outbuf,
                     const void      * bytes,
                     size_t            byteCount )
{
    uint8_t tmp[MAX_STACK_ARRAY_SIZE];

    switch( io->encryptionMode )
    {
        case PEER_ENCRYPTION_NONE:
            evbuffer_add( outbuf, bytes, byteCount );
            break;

        case PEER_ENCRYPTION_RC4: {
            const uint8_t * walk = bytes;
            evbuffer_expand( outbuf, byteCount );
            while( byteCount > 0 ) {
                const size_t thisPass = MIN( byteCount, sizeof( tmp ) );
                tr_cryptoEncrypt( io->crypto, thisPass, walk, tmp );
                evbuffer_add( outbuf, tmp, thisPass );
                walk += thisPass;
                byteCount -= thisPass;
            }
            break;
        }

        default:
            assert( 0 );
    }
}

/***
****
***/

void
tr_peerIoReadBytes( tr_peerIo       * io,
                    struct evbuffer * inbuf,
                    void            * bytes,
                    size_t            byteCount )
{
    assert( tr_isPeerIo( io ) );
    assert( EVBUFFER_LENGTH( inbuf ) >= byteCount );

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
tr_peerIoDrain( tr_peerIo       * io,
                struct evbuffer * inbuf,
                size_t            byteCount )
{
    uint8_t tmp[MAX_STACK_ARRAY_SIZE];

    while( byteCount > 0 )
    {
        const size_t thisPass = MIN( byteCount, sizeof( tmp ) );
        tr_peerIoReadBytes( io, inbuf, tmp, thisPass );
        byteCount -= thisPass;
    }
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
        errno = 0;
        res = evbuffer_read( io->inbuf, io->socket, howmuch );
        e = errno;

        dbgmsg( io, "read %d from peer (%s)", res, (res==-1?strerror(e):"") );

        if( EVBUFFER_LENGTH( io->inbuf ) )
            canReadWrapper( io );

        if( ( res <= 0 ) && ( io->gotError ) && ( e != EAGAIN ) && ( e != EINTR ) && ( e != EINPROGRESS ) )
        {
            short what = EVBUFFER_READ | EVBUFFER_ERROR;
            if( res == 0 )
                what |= EVBUFFER_EOF;
            dbgmsg( io, "tr_peerIoTryRead got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e, strerror( e ) );
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
        errno = 0;
        n = tr_evbuffer_write( io, io->socket, howmuch );
        e = errno;

        if( n > 0 )
            didWriteWrapper( io, n );

        if( ( n < 0 ) && ( io->gotError ) && ( e != EPIPE ) && ( e != EAGAIN ) && ( e != EINTR ) && ( e != EINPROGRESS ) )
        {
            const short what = EVBUFFER_WRITE | EVBUFFER_ERROR;
            dbgmsg( io, "tr_peerIoTryWrite got an error. res is %d, what is %hd, errno is %d (%s)", n, what, e, strerror( e ) );
            io->gotError( io, what, io->userData );
        }
    }

    return n;
}

int
tr_peerIoFlush( tr_peerIo  * io, tr_direction dir, size_t limit )
{
    int bytesUsed;

    assert( tr_isPeerIo( io ) );
    assert( tr_isDirection( dir ) );

    if( io->hasFinishedConnecting )
    {
        if( dir == TR_DOWN )
            bytesUsed = tr_peerIoTryRead( io, limit );
        else
            bytesUsed = tr_peerIoTryWrite( io, limit );
    }

    dbgmsg( io, "flushing peer-io, direction %d, limit %zu, bytesUsed %d", (int)dir, limit, bytesUsed );
    return bytesUsed;
}

/***
****
****/

static void
event_enable( tr_peerIo * io, short event )
{
    if( event & EV_READ )
        event_add( &io->event_read, NULL );

    if( event & EV_WRITE )
        event_add( &io->event_write, NULL );
}

static void
event_disable( struct tr_peerIo * io, short event )
{
    if( event & EV_READ )
        event_del( &io->event_read );

    if( event & EV_WRITE )
        event_del( &io->event_write );
}


void
tr_peerIoSetEnabled( tr_peerIo    * io,
                     tr_direction   dir,
                     tr_bool        isEnabled )
{
    const short event = dir == TR_UP ? EV_WRITE : EV_READ;

    assert( tr_isPeerIo( io ) );
    assert( tr_isDirection( dir ) );

    if( isEnabled )
        event_enable( io, event );
    else
        event_disable( io, event );
}
