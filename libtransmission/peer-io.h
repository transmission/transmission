/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
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

struct evbuffer;
struct tr_address;
struct tr_bandwidth;
struct tr_crypto;
struct tr_iobuf;
typedef struct tr_peerIo tr_peerIo;

/**
***
**/

tr_peerIo*  tr_peerIoNewOutgoing( tr_session              * session,
                                  const struct tr_address * addr,
                                  tr_port                   port,
                                  const  uint8_t          * torrentHash );

tr_peerIo*  tr_peerIoNewIncoming( tr_session              * session,
                                  const struct tr_address * addr,
                                  tr_port                   port,
                                  int                       socket );

void        tr_peerIoFree       ( tr_peerIo               * io );


/**
***
**/

void        tr_peerIoEnableLTEP( tr_peerIo * io, tr_bool flag );

tr_bool     tr_peerIoSupportsLTEP( const tr_peerIo * io );

void        tr_peerIoEnableFEXT( tr_peerIo * io, tr_bool flag );

tr_bool     tr_peerIoSupportsFEXT( const tr_peerIo * io );

/**
***
**/

tr_session* tr_peerIoGetSession ( tr_peerIo * io );

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

tr_bool              tr_peerIoIsIncoming( const tr_peerIo * io );

int                  tr_peerIoGetAge( const tr_peerIo * io );


/**
***
**/

void                 tr_peerIoSetPeersId( tr_peerIo *     io,
                                          const uint8_t * peer_id );

const uint8_t*       tr_peerIoGetPeersId( const tr_peerIo * io );

/**
***
**/

typedef enum
{
    READ_NOW,
    READ_LATER,
    READ_ERR
}
ReadState;

typedef ReadState ( *tr_can_read_cb  )( tr_peerIo        * io,
                                        void             * user_data,
                                        size_t           * setme_piece_byte_count );

typedef void      ( *tr_did_write_cb )( tr_peerIo        * io,
                                        size_t             bytesWritten,
                                        int                wasPieceData,
                                        void             * userData );

typedef void      ( *tr_net_error_cb )( tr_peerIo        * io,
                                        short              what,
                                        void             * userData );

void    tr_peerIoSetIOFuncs      ( tr_peerIo        * io,
                                   tr_can_read_cb     readcb,
                                   tr_did_write_cb    writecb,
                                   tr_net_error_cb    errcb,
                                   void             * user_data );

/**
***
**/

void    tr_peerIoWrite          ( tr_peerIo         * io,
                                  const void        * writeme,
                                  size_t              writemeLen,
                                  int                 isPieceData );

void    tr_peerIoWriteBuf       ( tr_peerIo         * io,
                                  struct evbuffer   * buf,
                                  int                 isPieceData );

/**
***
**/

struct tr_crypto* tr_peerIoGetCrypto( tr_peerIo * io );

typedef enum
{
    /* these match the values in MSE's crypto_select */
    PEER_ENCRYPTION_NONE  = ( 1 << 0 ),
    PEER_ENCRYPTION_RC4   = ( 1 << 1 )
}
EncryptionMode;

void      tr_peerIoSetEncryption( tr_peerIo * io,
                                  int         encryptionMode );

int       tr_peerIoIsEncrypted( const tr_peerIo * io );

void      tr_peerIoWriteBytes( tr_peerIo *       io,
                               struct evbuffer * outbuf,
                               const void *      bytes,
                               size_t            byteCount );

void      tr_peerIoWriteUint8( tr_peerIo *       io,
                               struct evbuffer * outbuf,
                               uint8_t           writeme );

void      tr_peerIoWriteUint16( tr_peerIo *       io,
                                struct evbuffer * outbuf,
                                uint16_t          writeme );

void      tr_peerIoWriteUint32( tr_peerIo *       io,
                                struct evbuffer * outbuf,
                                uint32_t          writeme );

void      tr_peerIoReadBytes( tr_peerIo *       io,
                              struct evbuffer * inbuf,
                              void *            bytes,
                              size_t            byteCount );

void      tr_peerIoReadUint8( tr_peerIo *       io,
                              struct evbuffer * inbuf,
                              uint8_t *         setme );

void      tr_peerIoReadUint16( tr_peerIo *       io,
                               struct evbuffer * inbuf,
                               uint16_t *        setme );

void      tr_peerIoReadUint32( tr_peerIo *       io,
                               struct evbuffer * inbuf,
                               uint32_t *        setme );

void      tr_peerIoDrain( tr_peerIo *       io,
                          struct evbuffer * inbuf,
                          size_t            byteCount );

/**
***
**/

size_t    tr_peerIoGetWriteBufferSpace( const tr_peerIo * io );

void      tr_peerIoSetBandwidth( tr_peerIo            * io,
                                 struct tr_bandwidth  * bandwidth );

void      tr_peerIoBandwidthUsed( tr_peerIo           * io,
                                  tr_direction          direction,
                                  size_t                byteCount,
                                  int                   isPieceData );

/**
***
**/

int       tr_peerIoFlush( tr_peerIo     * io,
                          tr_direction    dir,
                          size_t          byteLimit );

struct evbuffer * tr_peerIoGetReadBuffer( tr_peerIo * io );




#endif
