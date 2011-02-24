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

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_IO_H
#define TR_PEER_IO_H

/**
***
**/

#include <assert.h>

#include <event2/buffer.h>
#include <event2/event.h>

#include "transmission.h"
#include "bandwidth.h"
#include "list.h" /* tr_list */
#include "net.h" /* tr_address */
#include "utils.h" /* tr_time() */

struct evbuffer;
struct tr_bandwidth;
struct tr_crypto;
struct tr_peerIo;

/**
 * @addtogroup networked_io Networked IO
 * @{
 */

typedef enum
{
    READ_NOW,
    READ_LATER,
    READ_ERR
}
ReadState;

typedef ReadState ( *tr_can_read_cb  )( struct tr_peerIo * io,
                                        void             * user_data,
                                        size_t           * setme_piece_byte_count );

typedef void      ( *tr_did_write_cb )( struct tr_peerIo * io,
                                        size_t             bytesWritten,
                                        int                wasPieceData,
                                        void             * userData );

typedef void      ( *tr_net_error_cb )( struct tr_peerIo * io,
                                        short              what,
                                        void             * userData );

typedef struct tr_peerIo
{
    tr_bool               isEncrypted;
    tr_bool               isIncoming;
    tr_bool               peerIdIsSet;
    tr_bool               extendedProtocolSupported;
    tr_bool               fastExtensionSupported;
    tr_bool               dhtSupported;

    tr_priority_t         priority;

    short int             pendingEvents;

    int                   magicNumber;

    uint32_t              encryptionMode;
    tr_bool               isSeed;

    tr_port               port;
    int                   socket;

    int                   refCount;

    uint8_t               peerId[SHA_DIGEST_LENGTH];
    time_t                timeCreated;

    tr_session          * session;

    tr_address            addr;

    tr_can_read_cb        canRead;
    tr_did_write_cb       didWrite;
    tr_net_error_cb       gotError;
    void *                userData;

    struct tr_bandwidth   bandwidth;
    struct tr_crypto    * crypto;

    struct evbuffer     * inbuf;
    struct evbuffer     * outbuf;
    struct tr_list      * outbuf_datatypes; /* struct tr_datatype */

    struct event        * event_read;
    struct event        * event_write;
}
tr_peerIo;

/**
***
**/

tr_peerIo*  tr_peerIoNewOutgoing( tr_session              * session,
                                  struct tr_bandwidth     * parent,
                                  const struct tr_address * addr,
                                  tr_port                   port,
                                  const  uint8_t          * torrentHash,
                                  tr_bool                   isSeed );

tr_peerIo*  tr_peerIoNewIncoming( tr_session              * session,
                                  struct tr_bandwidth     * parent,
                                  const struct tr_address * addr,
                                  tr_port                   port,
                                  int                       socket );

void tr_peerIoRefImpl           ( const char              * file,
                                  int                       line,
                                  tr_peerIo               * io );

#define tr_peerIoRef(io) tr_peerIoRefImpl( __FILE__, __LINE__, (io) );

void tr_peerIoUnrefImpl         ( const char              * file,
                                  int                       line,
                                  tr_peerIo               * io );

#define tr_peerIoUnref(io) tr_peerIoUnrefImpl( __FILE__, __LINE__, (io) );

tr_bool     tr_isPeerIo         ( const tr_peerIo         * io );


/**
***
**/

static inline void tr_peerIoEnableFEXT( tr_peerIo * io, tr_bool flag )
{
    io->fastExtensionSupported = flag;
}
static inline tr_bool tr_peerIoSupportsFEXT( const tr_peerIo * io )
{
    return io->fastExtensionSupported;
}

static inline void tr_peerIoEnableLTEP( tr_peerIo * io, tr_bool flag )
{
    io->extendedProtocolSupported = flag;
}
static inline tr_bool tr_peerIoSupportsLTEP( const tr_peerIo * io )
{
    return io->extendedProtocolSupported;
}

static inline void tr_peerIoEnableDHT( tr_peerIo * io, tr_bool flag )
{
    io->dhtSupported = flag;
}
static inline tr_bool tr_peerIoSupportsDHT( const tr_peerIo * io )
{
    return io->dhtSupported;
}

/**
***
**/

static inline tr_session* tr_peerIoGetSession ( tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );
    assert( io->session );

    return io->session;
}

const char* tr_peerIoAddrStr( const struct tr_address * addr,
                              tr_port                   port );

const char* tr_peerIoGetAddrStr( const tr_peerIo * io );

const struct tr_address * tr_peerIoGetAddress( const tr_peerIo * io,
                                               tr_port         * port );

const uint8_t*       tr_peerIoGetTorrentHash( tr_peerIo * io );

int                  tr_peerIoHasTorrentHash( const tr_peerIo * io );

void                 tr_peerIoSetTorrentHash( tr_peerIo *     io,
                                              const uint8_t * hash );

int                  tr_peerIoReconnect( tr_peerIo * io );

static inline tr_bool tr_peerIoIsIncoming( const tr_peerIo * io )
{
    return io->isIncoming;
}

static inline int    tr_peerIoGetAge( const tr_peerIo * io )
{
    return tr_time() - io->timeCreated;
}


/**
***
**/

void                 tr_peerIoSetPeersId( tr_peerIo *     io,
                                          const uint8_t * peer_id );

static inline const uint8_t* tr_peerIoGetPeersId( const tr_peerIo * io )
{
    assert( tr_isPeerIo( io ) );
    assert( io->peerIdIsSet );

    return io->peerId;
}

/**
***
**/

void    tr_peerIoSetIOFuncs      ( tr_peerIo        * io,
                                   tr_can_read_cb     readcb,
                                   tr_did_write_cb    writecb,
                                   tr_net_error_cb    errcb,
                                   void             * user_data );

void    tr_peerIoClear           ( tr_peerIo        * io );

/**
***
**/

void    tr_peerIoWriteBytes     ( tr_peerIo         * io,
                                  const void        * writeme,
                                  size_t              writemeLen,
                                  tr_bool             isPieceData );

void    tr_peerIoWriteBuf       ( tr_peerIo         * io,
                                  struct evbuffer   * buf,
                                  tr_bool             isPieceData );

/**
***
**/

static inline struct tr_crypto * tr_peerIoGetCrypto( tr_peerIo * io )
{
    return io->crypto;
}

typedef enum
{
    /* these match the values in MSE's crypto_select */
    PEER_ENCRYPTION_NONE  = ( 1 << 0 ),
    PEER_ENCRYPTION_RC4   = ( 1 << 1 )
}
EncryptionMode;

void tr_peerIoSetEncryption( tr_peerIo * io, uint32_t encryptionMode );

static inline tr_bool
tr_peerIoIsEncrypted( const tr_peerIo * io )
{
    return ( io != NULL ) && ( io->encryptionMode == PEER_ENCRYPTION_RC4 );
}

void evbuffer_add_uint8 ( struct evbuffer * outbuf, uint8_t byte );
void evbuffer_add_uint16( struct evbuffer * outbuf, uint16_t hs );
void evbuffer_add_uint32( struct evbuffer * outbuf, uint32_t hl );

void tr_peerIoReadBytes( tr_peerIo        * io,
                         struct evbuffer  * inbuf,
                         void             * bytes,
                         size_t             byteCount );

static inline void tr_peerIoReadUint8( tr_peerIo        * io,
                                          struct evbuffer  * inbuf,
                                          uint8_t          * setme )
{
    tr_peerIoReadBytes( io, inbuf, setme, sizeof( uint8_t ) );
}

void tr_peerIoReadUint16( tr_peerIo        * io,
                          struct evbuffer  * inbuf,
                          uint16_t         * setme );

void tr_peerIoReadUint32( tr_peerIo        * io,
                          struct evbuffer  * inbuf,
                          uint32_t         * setme );

void      tr_peerIoDrain( tr_peerIo        * io,
                          struct evbuffer  * inbuf,
                          size_t             byteCount );

/**
***
**/

size_t    tr_peerIoGetWriteBufferSpace( const tr_peerIo * io, uint64_t now );

static inline void tr_peerIoSetParent( tr_peerIo            * io,
                                          struct tr_bandwidth  * parent )
{
    assert( tr_isPeerIo( io ) );

    tr_bandwidthSetParent( &io->bandwidth, parent );
}

void      tr_peerIoBandwidthUsed( tr_peerIo           * io,
                                  tr_direction          direction,
                                  size_t                byteCount,
                                  int                   isPieceData );

static inline tr_bool
tr_peerIoHasBandwidthLeft( const tr_peerIo * io, tr_direction dir )
{
    assert( tr_isPeerIo( io ) );

    return tr_bandwidthClamp( &io->bandwidth, dir, 1024 ) > 0;
}

static inline unsigned int
tr_peerIoGetPieceSpeed_Bps( const tr_peerIo * io, uint64_t now, tr_direction dir )
{
    assert( tr_isPeerIo( io ) );
    assert( tr_isDirection( dir ) );

    return tr_bandwidthGetPieceSpeed_Bps( &io->bandwidth, now, dir );
}

/**
***
**/

void      tr_peerIoSetEnabled( tr_peerIo    * io,
                               tr_direction   dir,
                               tr_bool        isEnabled );

int       tr_peerIoFlush( tr_peerIo     * io,
                          tr_direction    dir,
                          size_t          byteLimit );

int       tr_peerIoFlushOutgoingProtocolMsgs( tr_peerIo * io );

/**
***
**/

static inline struct evbuffer * tr_peerIoGetReadBuffer( tr_peerIo * io )
{
    return io->inbuf;
}

/* @} */

#endif
