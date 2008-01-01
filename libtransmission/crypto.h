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

#ifndef TR_ENCRYPTION_H
#define TR_ENCRYPTION_H

#include <inttypes.h>

/**
***
**/

struct evbuffer;
typedef struct tr_crypto tr_crypto;

/**
***
**/

tr_crypto*  tr_cryptoNew ( const uint8_t * torrentHash,
                           int             isIncoming );

void tr_cryptoFree( tr_crypto * crypto );

/**
***
**/

void tr_cryptoSetTorrentHash( tr_crypto     * crypto,
                              const uint8_t * torrentHash );

const uint8_t*  tr_cryptoGetTorrentHash( const tr_crypto * crypto );

int tr_cryptoHasTorrentHash( const tr_crypto * crypto );

/**
***
**/

const uint8_t*  tr_cryptoComputeSecret   ( tr_crypto     * crypto,
                                           const uint8_t * peerPublicKey );

const uint8_t*  tr_cryptoGetMyPublicKey ( const tr_crypto * crypto,
                                          int             * setme_len );

void            tr_cryptoDecryptInit( tr_crypto   * crypto );

void            tr_cryptoDecrypt    ( tr_crypto   * crypto,
                                      size_t        buflen,
                                      const void  * buf_in,
                                      void        * buf_out );

/**
***
**/

void  tr_cryptoEncryptInit ( tr_crypto   * crypto );

void  tr_cryptoEncrypt     ( tr_crypto   * crypto,
                             size_t        buflen,
                             const void  * buf_in,
                             void        * buf_out );

void  tr_sha1              ( uint8_t     * setme,
                             const void  * content1,
                             int           content1_len,
                             ... );

#endif
