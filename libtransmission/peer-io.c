/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h> /* struct in_addr */
#include <arpa/inet.h> /* inet_ntoa */

#include <event.h>

#include "transmission.h"
#include "crypto.h"
#include "net.h"
#include "peer-io.h"
#include "ratecontrol.h"
#include "trevent.h"
#include "utils.h"

#define IO_TIMEOUT_SECS 8

/* arbitrary */
#define TR_RDBUF (1024*3)

/**
***
**/

struct tr_extensions
{
    unsigned int extendedProtocolSupported : 1;
    unsigned int fastPeersSupported : 1;
};

struct tr_peerIo
{
    struct tr_handle * handle;

    struct in_addr in_addr;
    int port;
    int socket;
    int encryptionMode;
    struct bufferevent * bufev;
    uint8_t peerId[20];

    tr_extensions extensions;

    unsigned int isEncrypted : 1;
    unsigned int isIncoming : 1;
    unsigned int peerIdIsSet : 1;

    tr_can_read_cb     canRead;
    tr_net_error_cb    gotError;
    void             * userData;

    tr_crypto * crypto;
};

/**
***
**/

static void
canReadWrapper( struct bufferevent * e, void * userData )
{
    int done = 0;
    tr_peerIo * c = userData;
    tr_handle * handle = c->handle;

    if( c->canRead == NULL )
        return;

    tr_globalLock( handle );

    while( !done )
    {
        const int ret = (*c->canRead)( e, c->userData );

        switch( ret )
        {
            case READ_AGAIN:
                if( EVBUFFER_LENGTH( e->input ) )
                    continue;
            case READ_MORE:
            case READ_DONE:
                done = 1;
        }
    }

    tr_globalUnlock( handle );
}

static void
gotErrorWrapper( struct bufferevent * e, short what, void * userData )
{
    tr_peerIo * c = userData;
    if( c->gotError != NULL )
        (*c->gotError)( e, what, c->userData );
}

/**
***
**/

void bufferevent_setwatermark(struct bufferevent *, short, size_t, size_t);

static tr_peerIo*
tr_peerIoNew( struct tr_handle     * handle,
              const struct in_addr * in_addr,
              uint16_t               port,
              const uint8_t        * torrentHash,
              int                    isIncoming,
              int                    socket )
{
    tr_peerIo * c;
    c = tr_new0( tr_peerIo, 1 );
    c->crypto = tr_cryptoNew( torrentHash, isIncoming );
    c->handle = handle;
    c->in_addr = *in_addr;
    c->port = port;
    c->socket = socket;
    c->isIncoming = isIncoming ? 1 : 0;
    c->bufev = bufferevent_new( c->socket,
                                canReadWrapper,
                                NULL,
                                gotErrorWrapper,
                                c );
    bufferevent_settimeout( c->bufev, IO_TIMEOUT_SECS, IO_TIMEOUT_SECS );
    bufferevent_enable( c->bufev, EV_READ|EV_WRITE );
    bufferevent_setwatermark( c->bufev, EV_READ, 0, TR_RDBUF );

    return c;
}

tr_peerIo*
tr_peerIoNewIncoming( struct tr_handle      * handle,
                      const struct in_addr  * in_addr,
                      uint16_t                port,
                      int                     socket )
{
    assert( handle != NULL );
    assert( in_addr != NULL );
    assert( socket >= 0 );

    return tr_peerIoNew( handle, in_addr, port,
                         NULL, 1,
                         socket );
}

tr_peerIo*
tr_peerIoNewOutgoing( struct tr_handle      * handle,
                      const struct in_addr  * in_addr,
                      int                     port,
                      const uint8_t         * torrentHash )
{
    assert( handle != NULL );
    assert( in_addr != NULL );
    assert( port >= 0 );
    assert( torrentHash != NULL );

    return tr_peerIoNew( handle, in_addr, port,
                         torrentHash, 0,
                         tr_netOpenTCP( in_addr, port, 0 ) );
}

static void
io_dtor( void * vio )
{
    tr_peerIo * io = vio;

    bufferevent_free( io->bufev );
    tr_netClose( io->socket );
    tr_cryptoFree( io->crypto );
    tr_free( io );
}

void
tr_peerIoFree( tr_peerIo * io )
{
    if( io != NULL )
    {
        io->canRead = NULL;
        io->gotError = NULL;
        tr_runInEventThread( io->handle, io_dtor, io );
    }
}

tr_handle*
tr_peerIoGetHandle( tr_peerIo * io )
{
    assert( io != NULL );
    assert( io->handle != NULL );

    return io->handle;
}

const struct in_addr*
tr_peerIoGetAddress( const tr_peerIo * io, uint16_t * port )
{
    assert( io != NULL );

    if( port != NULL )
       *port = io->port;

    return &io->in_addr;
}

const char*
tr_peerIoAddrStr( const struct in_addr * addr, uint16_t port )
{
    static char buf[512];
    snprintf( buf, sizeof(buf), "%s:%u", inet_ntoa( *addr ), (unsigned int)port );
    return buf;
}

const char*
tr_peerIoGetAddrStr( const tr_peerIo * io )
{
    return tr_peerIoAddrStr( &io->in_addr, io->port );
}

void
tr_peerIoTryRead( tr_peerIo * io )
{
    if( EVBUFFER_LENGTH( io->bufev->input ) )
        canReadWrapper( io->bufev, io );
}

void 
tr_peerIoSetIOFuncs( tr_peerIo          * io,
                     tr_can_read_cb       readcb,
                     tr_net_error_cb      errcb,
                     void               * userData )
{
    io->canRead = readcb;
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

    io->socket = tr_netOpenTCP( &io->in_addr, io->port, 0 );
  
    if( io->socket >= 0 )
    {
        bufferevent_free( io->bufev );

        io->bufev = bufferevent_new( io->socket,
                                     canReadWrapper, NULL, gotErrorWrapper,
                                     io );
        bufferevent_settimeout( io->bufev, IO_TIMEOUT_SECS, IO_TIMEOUT_SECS );
        bufferevent_enable( io->bufev, EV_READ|EV_WRITE );
        bufferevent_setwatermark( io->bufev, EV_READ, 0, TR_RDBUF );

        return 0;
    }
 
    return -1;
}

void
tr_peerIoSetTimeoutSecs( tr_peerIo * io, int secs )
{
    bufferevent_settimeout( io->bufev, secs, secs );
}

/**
***
**/

void
tr_peerIoSetTorrentHash( tr_peerIo     * io,
                         const uint8_t * hash )
{
    assert( io != NULL );

    tr_cryptoSetTorrentHash( io->crypto, hash );
}

const uint8_t*
tr_peerIoGetTorrentHash( tr_peerIo * io )
{
    assert( io != NULL );
    assert( io->crypto != NULL );

    return tr_cryptoGetTorrentHash( io->crypto );
}

int
tr_peerIoHasTorrentHash( const tr_peerIo * io )
{
    assert( io != NULL );
    assert( io->crypto != NULL );

    return tr_cryptoHasTorrentHash( io->crypto );
}

/**
***
**/

void
tr_peerIoSetPeersId( tr_peerIo     * io,
                     const uint8_t * peer_id )
{
    assert( io != NULL );

    if(( io->peerIdIsSet = peer_id != NULL ))
        memcpy( io->peerId, peer_id, 20 );
    else
        memset( io->peerId, 0, 20 );
}

const uint8_t* 
tr_peerIoGetPeersId( const tr_peerIo * io )
{
    assert( io != NULL );
    assert( io->peerIdIsSet );

    return io->peerId;
}

/**
***
**/

void
tr_peerIoEnableLTEP( tr_peerIo * io, int flag )
{
    assert( io != NULL );
    assert( flag==0 || flag==1 );
    
    io->extensions.extendedProtocolSupported = flag;
}

void
tr_peerIoEnableFEXT( tr_peerIo * io, int flag )
{
    assert( io != NULL );
    assert( flag==0 || flag==1 );
    
    io->extensions.fastPeersSupported = flag;
}

int
tr_peerIoSupportsLTEP( const tr_peerIo * io )
{
    assert( io != NULL );
    
    return io->extensions.extendedProtocolSupported;
}

int
tr_peerIoSupportsFEXT( const tr_peerIo * io )
{
    assert( io != NULL );
    
    return io->extensions.fastPeersSupported;
}
/**
***
**/

size_t
tr_peerIoWriteBytesWaiting( const tr_peerIo * io )
{
    return EVBUFFER_LENGTH( EVBUFFER_OUTPUT( io->bufev ) );
}
 
void
tr_peerIoWrite( tr_peerIo   * io,
                const void  * writeme,
                int           writeme_len )
{
    assert( tr_amInEventThread( io->handle ) );
    bufferevent_write( io->bufev, (void*)writeme, writeme_len );
}

void
tr_peerIoWriteBuf( tr_peerIo       * io,
                   struct evbuffer * buf )
{
    tr_peerIoWrite( io, EVBUFFER_DATA(buf), EVBUFFER_LENGTH(buf) );
    evbuffer_drain( buf, ~0 );
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
    assert( io != NULL );
    assert( encryptionMode==PEER_ENCRYPTION_NONE || encryptionMode==PEER_ENCRYPTION_RC4 );

    io->encryptionMode = encryptionMode;
}

int
tr_peerIoIsEncrypted( const tr_peerIo * io )
{
    return io!=NULL && io->encryptionMode==PEER_ENCRYPTION_RC4;
}

void
tr_peerIoWriteBytes( tr_peerIo        * io,
                     struct evbuffer  * outbuf,
                     const void       * bytes,
                     int                byteCount )
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
tr_peerIoWriteUint16( tr_peerIo        * io,
                      struct evbuffer  * outbuf,
                      uint16_t           writeme )
{
    uint16_t tmp = htons( writeme );
    tr_peerIoWriteBytes( io, outbuf, &tmp, sizeof(uint16_t) );
}

void
tr_peerIoWriteUint8( tr_peerIo        * io,
                     struct evbuffer  * outbuf,
                     uint8_t            writeme )
{
    tr_peerIoWriteBytes( io, outbuf, &writeme, sizeof(uint8_t) );
}

void
tr_peerIoWriteUint32( tr_peerIo        * io,
                      struct evbuffer  * outbuf,
                      uint32_t           writeme )
{
    uint32_t tmp = htonl( writeme );
    tr_peerIoWriteBytes( io, outbuf, &tmp, sizeof(uint32_t) );
}

void
tr_peerIoReadBytes( tr_peerIo        * io,
                    struct evbuffer  * inbuf,
                    void             * bytes,
                    int                byteCount )
{
    assert( (int)EVBUFFER_LENGTH( inbuf ) >= byteCount );

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
tr_peerIoReadUint16( tr_peerIo         * io,
                     struct evbuffer   * inbuf,
                     uint16_t          * setme )
{
    uint16_t tmp;
    tr_peerIoReadBytes( io, inbuf, &tmp, sizeof(uint16_t) );
    *setme = ntohs( tmp );
}

void
tr_peerIoReadUint8( tr_peerIo         * io,
                    struct evbuffer   * inbuf,
                    uint8_t           * setme )
{
    tr_peerIoReadBytes( io, inbuf, setme, sizeof(uint8_t) );
}

void
tr_peerIoReadUint32( tr_peerIo         * io,
                     struct evbuffer   * inbuf,
                     uint32_t          * setme )
{
    uint32_t tmp;
    tr_peerIoReadBytes( io, inbuf, &tmp, sizeof(uint32_t) );
    *setme = ntohl( tmp );
}

void
tr_peerIoDrain( tr_peerIo        * io,
                struct evbuffer  * inbuf,
                int                byteCount )
{
    uint8_t * tmp = tr_new( uint8_t, byteCount );
    tr_peerIoReadBytes( io, inbuf, tmp, byteCount );
    tr_free( tmp );
}
