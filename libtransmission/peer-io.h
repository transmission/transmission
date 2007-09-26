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

#ifndef TR_PEER_IO_H
#define TR_PEER_IO_H

/**
***
**/

struct in_addr;
struct evbuffer;
struct bufferevent;
struct tr_handle;
struct tr_crypto;
typedef struct tr_peerIo tr_peerIo;
typedef struct tr_extensions tr_extensions;

/**
***
**/

tr_peerIo*
      tr_peerIoNewOutgoing( struct tr_handle   * handle,
                            struct in_addr     * addr,
                            int                  port,
                            const  uint8_t     * torrentHash );

tr_peerIo*
      tr_peerIoNewIncoming( struct tr_handle   * handle,
                            struct in_addr     * addr,
                            uint16_t             port,
                            int                  socket );

void  tr_peerIoFree      ( tr_peerIo  * io );

tr_handle* tr_peerIoGetHandle( tr_peerIo * io );

/**
***
**/

void  tr_peerIoEnableAZMP( tr_peerIo * io, int flag );
void  tr_peerIoEnableLTEP( tr_peerIo * io, int flag );
void  tr_peerIoEnableFEXT( tr_peerIo * io, int flag );

int   tr_peerIoSupportsAZMP( const tr_peerIo * io );
int   tr_peerIoSupportsLTEP( const tr_peerIo * io );
int   tr_peerIoSupportsFEXT( const tr_peerIo * io );

/**
***
**/

const char*
      tr_peerIoGetAddrStr( const tr_peerIo * io );

const struct in_addr*
      tr_peerIoGetAddress( const tr_peerIo * io, uint16_t * port );

const uint8_t*
      tr_peerIoGetTorrentHash( tr_peerIo * io );

int   tr_peerIoHasTorrentHash( const tr_peerIo * io );

void  tr_peerIoSetTorrentHash( tr_peerIo      * io,
                               const uint8_t  * hash );

int   tr_peerIoReconnect( tr_peerIo * io );

int   tr_peerIoIsIncoming( const tr_peerIo * io );

/**
***
**/

void  tr_peerIoSetPeersId( tr_peerIo      * io,
                           const uint8_t  * peer_id );

const uint8_t*
      tr_peerIoGetPeersId( const tr_peerIo * io );

/**
***
**/

typedef enum { READ_MORE, READ_AGAIN, READ_DONE } ReadState;
typedef ReadState (*tr_can_read_cb)(struct bufferevent*, void* user_data);
typedef void (*tr_did_write_cb)(struct bufferevent *, void *);
typedef void (*tr_net_error_cb)(struct bufferevent *, short what, void *);

void  tr_peerIoSetIOFuncs( tr_peerIo        * io,
                           tr_can_read_cb     readcb,
                           tr_did_write_cb    writecb,
                           tr_net_error_cb    errcb,
                           void             * user_data );

void  tr_peerIoSetIOMode ( tr_peerIo   * io,
                           short         enable_mode,
                           short         disable_mode );

size_t tr_peerIoWriteBytesWaiting( const tr_peerIo * io );

void tr_peerIoWrite( tr_peerIo   * io,
                     const void  * writeme,
                     int           writeme_len );

void tr_peerIoWriteBuf( tr_peerIo       * io,
                        struct evbuffer * buf );


float tr_peerIoGetRateToClient( const tr_peerIo * io );

float tr_peerIoGetRateToPeer( const tr_peerIo * io );

/**
***
**/

struct tr_crypto* tr_peerIoGetCrypto( tr_peerIo * io );

typedef enum
{
    /* these match the values in MSE's crypto_select */
    PEER_ENCRYPTION_NONE  = (1<<0),
    PEER_ENCRYPTION_RC4   = (1<<1)
}
EncryptionMode;

void tr_peerIoSetEncryption( tr_peerIo       * io,
                              int              encryptionMode );

int  tr_peerIoIsEncrypted( const tr_peerIo * io );

void tr_peerIoWriteBytes  ( tr_peerIo        * io,
                            struct evbuffer  * outbuf,
                            const void       * bytes,
                            int                byteCount );

void tr_peerIoWriteUint8  ( tr_peerIo        * io,
                            struct evbuffer  * outbuf,
                            uint8_t            writeme );

void tr_peerIoWriteUint16 ( tr_peerIo        * io,
                            struct evbuffer  * outbuf,
                            uint16_t           writeme );

void tr_peerIoWriteUint32 ( tr_peerIo        * io,
                            struct evbuffer  * outbuf,
                            uint32_t           writeme );

void tr_peerIoReadBytes   ( tr_peerIo        * io,
                            struct evbuffer  * inbuf,
                            void             * bytes,
                            int                byteCount );

void tr_peerIoReadUint8   ( tr_peerIo        * io,
                            struct evbuffer  * inbuf,
                            uint8_t          * setme );

void tr_peerIoReadUint16  ( tr_peerIo        * io,
                            struct evbuffer  * inbuf,
                            uint16_t         * setme );

void tr_peerIoReadUint32  ( tr_peerIo        * io,
                            struct evbuffer  * inbuf,
                            uint32_t         * setme );

void tr_peerIoDrain       ( tr_peerIo        * io,
                            struct evbuffer  * inbuf,
                            int                byteCount );


#endif
