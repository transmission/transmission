/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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
#include <limits.h> /* INT_MAX */
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifdef WIN32
 #include <winsock2.h>
#else
 #include <netinet/in.h> /* struct in_addr */
 #include <arpa/inet.h> /* inet_ntoa */
#endif

#include <event.h>

#include "transmission.h"
#include "bandwidth.h"
#include "crypto.h"
#include "list.h"
#include "net.h"
#include "peer-io.h"
#include "trevent.h"
#include "utils.h"

#define IO_TIMEOUT_SECS 8

static size_t
addPacketOverhead( size_t d )
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

    return (size_t)( d * ( 100.0 / assumed_payload_data_rate ) );
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
    unsigned int    isPieceData : 1;
    size_t          length;
};

struct tr_peerIo
{
    unsigned int           isEncrypted               : 1;
    unsigned int           isIncoming                : 1;
    unsigned int           peerIdIsSet               : 1;
    unsigned int           extendedProtocolSupported : 1;
    unsigned int           fastPeersSupported        : 1;

    uint8_t                encryptionMode;
    uint8_t                timeout;
    uint16_t               port;
    int                    socket;

    uint8_t                peerId[20];
    time_t                 timeCreated;

    tr_session           * session;

    struct in_addr         in_addr;
    struct bufferevent   * bufev;
    struct evbuffer      * output;
    tr_list              * output_datatypes; /* struct tr_datatype */

    tr_can_read_cb         canRead;
    tr_did_write_cb        didWrite;
    tr_net_error_cb        gotError;
    void *                 userData;

    size_t                 bufferSize[2];

    tr_bandwidth         * bandwidth[2];
    tr_crypto            * crypto;
};

/**
***
**/

static void
adjustOutputBuffer( tr_peerIo * io )
{
    struct evbuffer * live = EVBUFFER_OUTPUT( io->bufev );
    size_t curLive = EVBUFFER_LENGTH( live );
    size_t maxLive = tr_bandwidthClamp( io->bandwidth[TR_UP], io->session->so_sndbuf );

    if( ( curLive < maxLive ) && EVBUFFER_LENGTH( io->output ) )
    {
        size_t freeSpace = maxLive - curLive;
        size_t n = MIN( freeSpace, EVBUFFER_LENGTH( io->output ) );
        bufferevent_write( io->bufev, EVBUFFER_DATA( io->output ), n );
        evbuffer_drain( io->output, n );
        curLive += n;
    }

    io->bufferSize[TR_UP] = curLive;

    if( curLive )
        bufferevent_enable( io->bufev, EV_WRITE );

    dbgmsg( io, "after adjusting the output buffer, its size is now %zu", curLive );
}

static void
adjustInputBuffer( tr_peerIo * io )
{
    /* FIXME: the max read size probably needs to vary depending on the
     * number of peers that we have connected...  1024 is going to force
     * us way over the limit when there are lots of peers */
    static const int maxBufSize = 1024;
    const size_t n = tr_bandwidthClamp( io->bandwidth[TR_DOWN], maxBufSize );

    if( !n )
    {
        dbgmsg( io, "disabling reads because we've hit our limit" );
        bufferevent_disable( io->bufev, EV_READ );
    }
    else
    {
        dbgmsg( io, "enabling reading of %zu more bytes", n );
        bufferevent_setwatermark( io->bufev, EV_READ, 0, n );
        bufferevent_enable( io->bufev, EV_READ );
    }

    io->bufferSize[TR_DOWN] = EVBUFFER_LENGTH( EVBUFFER_INPUT( io->bufev ) );
}

/***
****
***/

static void
didWriteWrapper( struct bufferevent * e,
                 void *               vio )
{
    tr_peerIo *  io = vio;
    const size_t len = EVBUFFER_LENGTH( EVBUFFER_OUTPUT( e ) );

    dbgmsg( io, "didWrite... io->outputBufferSize was %zu, is now %zu",
            io->bufferSize[TR_UP], len );

    if( len < io->bufferSize[TR_UP] )
    {
        size_t payload = io->bufferSize[TR_UP] - len;

        while( payload )
        {
            struct tr_datatype * next = io->output_datatypes->data;
            const size_t chunk_length = MIN( next->length, payload );
            const size_t n = addPacketOverhead( chunk_length );

            if( io->didWrite )
                io->didWrite( io, n, next->isPieceData, io->userData );

            payload -= chunk_length;
            next->length -= chunk_length;
            if( !next->length )
                tr_free( tr_list_pop_front( &io->output_datatypes ) );
        }
    }

    adjustOutputBuffer( io );

}

static void
canReadWrapper( struct bufferevent * e,
                void *               vio )
{
    int          done = 0;
    int          err = 0;
    tr_peerIo *  io = vio;
    tr_session * session = io->session;

    dbgmsg( io, "canRead" );

    /* try to consume the input buffer */
    if( io->canRead )
    {
        tr_globalLock( session );

        while( !done && !err )
        {
            const int ret = io->canRead( e, io->userData );

            switch( ret )
            {
                case READ_NOW:
                    if( EVBUFFER_LENGTH( e->input ) )
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

    if( !err )
        adjustInputBuffer( io );
}

static void
gotErrorWrapper( struct bufferevent * e,
                 short                what,
                 void *               userData )
{
    tr_peerIo * c = userData;

    if( c->gotError )
        c->gotError( e, what, c->userData );
}

/**
***
**/

static void
bufevNew( tr_peerIo * io )
{
    io->bufev = bufferevent_new( io->socket,
                                 canReadWrapper,
                                 didWriteWrapper,
                                 gotErrorWrapper,
                                 io );

    /* tell libevent to call didWriteWrapper after every write,
     * not just when the write buffer is empty */
    bufferevent_setwatermark( io->bufev, EV_WRITE, INT_MAX, 0 );

    bufferevent_settimeout( io->bufev, io->timeout, io->timeout );

    bufferevent_enable( io->bufev, EV_READ | EV_WRITE );
}

static tr_peerIo*
tr_peerIoNew( tr_session *           session,
              const struct in_addr * in_addr,
              uint16_t               port,
              const uint8_t *        torrentHash,
              int                    isIncoming,
              int                    socket )
{
    tr_peerIo * io;

    if( socket >= 0 )
        tr_netSetTOS( socket, session->peerSocketTOS );

    io = tr_new0( tr_peerIo, 1 );
    io->crypto = tr_cryptoNew( torrentHash, isIncoming );
    io->session = session;
    io->in_addr = *in_addr;
    io->port = port;
    io->socket = socket;
    io->isIncoming = isIncoming != 0;
    io->timeout = IO_TIMEOUT_SECS;
    io->timeCreated = time( NULL );
    io->output = evbuffer_new( );
    bufevNew( io );
    return io;
}

tr_peerIo*
tr_peerIoNewIncoming( tr_session *           session,
                      const struct in_addr * in_addr,
                      uint16_t               port,
                      int                    socket )
{
    assert( session );
    assert( in_addr );
    assert( socket >= 0 );

    return tr_peerIoNew( session, in_addr, port,
                         NULL, 1,
                         socket );
}

tr_peerIo*
tr_peerIoNewOutgoing( tr_session *           session,
                      const struct in_addr * in_addr,
                      int                    port,
                      const uint8_t *        torrentHash )
{
    int socket;

    assert( session );
    assert( in_addr );
    assert( port >= 0 );
    assert( torrentHash );

    socket = tr_netOpenTCP( session, in_addr, port );

    return socket < 0
           ? NULL
           : tr_peerIoNew( session, in_addr, port, torrentHash, 0, socket );
}

static void
io_dtor( void * vio )
{
    tr_peerIo * io = vio;

    evbuffer_free( io->output );
    bufferevent_free( io->bufev );
    tr_netClose( io->socket );
    tr_cryptoFree( io->crypto );
    tr_list_free( &io->output_datatypes, tr_free );
    tr_free( io );
}

void
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

tr_session*
tr_peerIoGetSession( tr_peerIo * io )
{
    assert( io );
    assert( io->session );

    return io->session;
}

const struct in_addr*
tr_peerIoGetAddress( const tr_peerIo * io,
                           uint16_t * port )
{
    assert( io );

    if( port )
        *port = io->port;

    return &io->in_addr;
}

const char*
tr_peerIoAddrStr( const struct in_addr * addr,
                  uint16_t               port )
{
    static char buf[512];

    tr_snprintf( buf, sizeof( buf ), "%s:%u", inet_ntoa( *addr ),
                ntohs( port ) );
    return buf;
}

const char*
tr_peerIoGetAddrStr( const tr_peerIo * io )
{
    return tr_peerIoAddrStr( &io->in_addr, io->port );
}

static void
tr_peerIoTryRead( tr_peerIo * io )
{
    if( EVBUFFER_LENGTH( io->bufev->input ) )
        canReadWrapper( io->bufev, io );
}

void
tr_peerIoSetIOFuncs( tr_peerIo *     io,
                     tr_can_read_cb  readcb,
                     tr_did_write_cb writecb,
                     tr_net_error_cb errcb,
                     void *          userData )
{
    io->canRead = readcb;
    io->didWrite = writecb;
    io->gotError = errcb;
    io->userData = userData;

    tr_peerIoTryRead( io );
}

int
tr_peerIoIsIncoming( const tr_peerIo * c )
{
    return c->isIncoming ? 1 : 0;
}

int
tr_peerIoReconnect( tr_peerIo * io )
{
    assert( !tr_peerIoIsIncoming( io ) );

    if( io->socket >= 0 )
        tr_netClose( io->socket );

    io->socket = tr_netOpenTCP( io->session, &io->in_addr, io->port );

    if( io->socket >= 0 )
    {
        tr_netSetTOS( io->socket, io->session->peerSocketTOS );

        bufferevent_free( io->bufev );
        bufevNew( io );
        return 0;
    }

    return -1;
}

void
tr_peerIoSetTimeoutSecs( tr_peerIo * io,
                         int         secs )
{
    io->timeout = secs;
    bufferevent_settimeout( io->bufev, io->timeout, io->timeout );
    bufferevent_enable( io->bufev, EV_READ | EV_WRITE );
}

/**
***
**/

void
tr_peerIoSetTorrentHash( tr_peerIo *     io,
                         const uint8_t * hash )
{
    assert( io );

    tr_cryptoSetTorrentHash( io->crypto, hash );
}

const uint8_t*
tr_peerIoGetTorrentHash( tr_peerIo * io )
{
    assert( io );
    assert( io->crypto );

    return tr_cryptoGetTorrentHash( io->crypto );
}

int
tr_peerIoHasTorrentHash( const tr_peerIo * io )
{
    assert( io );
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
    assert( io );

    if( ( io->peerIdIsSet = peer_id != NULL ) )
        memcpy( io->peerId, peer_id, 20 );
    else
        memset( io->peerId, 0, 20 );
}

const uint8_t*
tr_peerIoGetPeersId( const tr_peerIo * io )
{
    assert( io );
    assert( io->peerIdIsSet );

    return io->peerId;
}

/**
***
**/

void
tr_peerIoEnableLTEP( tr_peerIo * io,
                     int         flag )
{
    assert( io );
    assert( flag == 0 || flag == 1 );

    io->extendedProtocolSupported = flag;
}

void
tr_peerIoEnableFEXT( tr_peerIo * io,
                     int         flag )
{
    assert( io );
    assert( flag == 0 || flag == 1 );

    io->fastPeersSupported = flag;
}

int
tr_peerIoSupportsLTEP( const tr_peerIo * io )
{
    assert( io );

    return io->extendedProtocolSupported;
}

int
tr_peerIoSupportsFEXT( const tr_peerIo * io )
{
    assert( io );

    return io->fastPeersSupported;
}

/**
***
**/

size_t
tr_peerIoGetWriteBufferSpace( const tr_peerIo * io )
{
    const size_t desiredLiveLen = tr_bandwidthClamp( io->bandwidth[TR_UP], io->session->so_rcvbuf );
    const size_t currentLiveLen = EVBUFFER_LENGTH( EVBUFFER_OUTPUT( io->bufev ) );
    const size_t desiredQueueLen = io->session->so_sndbuf;
    const size_t currentQueueLen = EVBUFFER_LENGTH( io->output );
    const size_t desiredLen = desiredLiveLen + desiredQueueLen;
    const size_t currentLen = currentLiveLen + currentQueueLen;
    size_t freeSpace = 0;

    if( desiredLen > currentLen )
        freeSpace = desiredLen - currentLen;

    return freeSpace;
}

void
tr_peerIoSetBandwidth( tr_peerIo     * io,
                       tr_direction    direction,
                       tr_bandwidth  * bandwidth )
{
    assert( io );
    assert( direction == TR_UP || direction == TR_DOWN );

    io->bandwidth[direction] = bandwidth;

    if( direction ==  TR_UP )
        adjustOutputBuffer( io );
    else
        adjustInputBuffer( io );
}

/**
***
**/

tr_crypto*
tr_peerIoGetCrypto( tr_peerIo * c )
{
    return c->crypto;
}

void
tr_peerIoSetEncryption( tr_peerIo * io,
                        int         encryptionMode )
{
    assert( io );
    assert( encryptionMode == PEER_ENCRYPTION_NONE
          || encryptionMode == PEER_ENCRYPTION_RC4 );

    io->encryptionMode = encryptionMode;
}

int
tr_peerIoIsEncrypted( const tr_peerIo * io )
{
    return io != NULL && io->encryptionMode == PEER_ENCRYPTION_RC4;
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

    evbuffer_add( io->output, writeme, writemeLen );

    datatype = tr_new( struct tr_datatype, 1 );
    datatype->isPieceData = isPieceData != 0;
    datatype->length = writemeLen;
    tr_list_append( &io->output_datatypes, datatype );

    adjustOutputBuffer( io );
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
tr_peerIoWriteBytes( tr_peerIo *       io,
                     struct evbuffer * outbuf,
                     const void *      bytes,
                     size_t            byteCount )
{
    uint8_t * tmp;

    switch( io->encryptionMode )
    {
        case PEER_ENCRYPTION_NONE:
            evbuffer_add( outbuf, bytes, byteCount );
            break;

        case PEER_ENCRYPTION_RC4:
            tmp = tr_new( uint8_t, byteCount );
            tr_cryptoEncrypt( io->crypto, byteCount, bytes, tmp );
            evbuffer_add( outbuf, tmp, byteCount );
            tr_free( tmp );
            break;

        default:
            assert( 0 );
    }
}

void
tr_peerIoWriteUint8( tr_peerIo *       io,
                     struct evbuffer * outbuf,
                     uint8_t           writeme )
{
    tr_peerIoWriteBytes( io, outbuf, &writeme, sizeof( uint8_t ) );
}

void
tr_peerIoWriteUint16( tr_peerIo *       io,
                      struct evbuffer * outbuf,
                      uint16_t          writeme )
{
    uint16_t tmp = htons( writeme );

    tr_peerIoWriteBytes( io, outbuf, &tmp, sizeof( uint16_t ) );
}

void
tr_peerIoWriteUint32( tr_peerIo *       io,
                      struct evbuffer * outbuf,
                      uint32_t          writeme )
{
    uint32_t tmp = htonl( writeme );

    tr_peerIoWriteBytes( io, outbuf, &tmp, sizeof( uint32_t ) );
}

/***
****
***/

void
tr_peerIoReadBytes( tr_peerIo *       io,
                    struct evbuffer * inbuf,
                    void *            bytes,
                    size_t            byteCount )
{
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
tr_peerIoReadUint8( tr_peerIo *       io,
                    struct evbuffer * inbuf,
                    uint8_t *         setme )
{
    tr_peerIoReadBytes( io, inbuf, setme, sizeof( uint8_t ) );
}

void
tr_peerIoReadUint16( tr_peerIo *       io,
                     struct evbuffer * inbuf,
                     uint16_t *        setme )
{
    uint16_t tmp;

    tr_peerIoReadBytes( io, inbuf, &tmp, sizeof( uint16_t ) );
    *setme = ntohs( tmp );
}

void
tr_peerIoReadUint32( tr_peerIo *       io,
                     struct evbuffer * inbuf,
                     uint32_t *        setme )
{
    uint32_t tmp;

    tr_peerIoReadBytes( io, inbuf, &tmp, sizeof( uint32_t ) );
    *setme = ntohl( tmp );
}

void
tr_peerIoDrain( tr_peerIo *       io,
                struct evbuffer * inbuf,
                size_t            byteCount )
{
    uint8_t * tmp = tr_new( uint8_t, byteCount );

    tr_peerIoReadBytes( io, inbuf, tmp, byteCount );
    tr_free( tmp );
}

int
tr_peerIoGetAge( const tr_peerIo * io )
{
    return time( NULL ) - io->timeCreated;
}
