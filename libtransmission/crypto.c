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
#include <inttypes.h> /* uint8_t */
#include <string.h> /* memcpy */
#include <stdarg.h>
#include <arpa/inet.h>

#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/rc4.h>
#include <openssl/sha.h>

#include <event.h>
#include "crypto.h"
#include "utils.h"

/**
***
**/

void
tr_sha1( uint8_t    * setme,
         const void * content1,
         int          content1_len,
         ... )
{
    va_list vl;
    SHA_CTX sha;

    SHA1_Init( &sha );
    SHA1_Update( &sha, content1, content1_len );

    va_start( vl, content1_len );
    for( ;; ) {
        const void * content = (const void*) va_arg( vl, const void* );
        const int content_len = content ? (int) va_arg( vl, int ) : -1;
        if( content==NULL || content_len<1 )
            break;
        SHA1_Update( &sha, content, content_len );
    }
    SHA1_Final( setme, &sha );
}

/**
***
**/

#define KEY_LEN 96

#define PRIME_LEN 96

static const uint8_t dh_P[PRIME_LEN] =
{
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2,
    0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
    0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6,
    0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D,
    0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
    0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9,
    0xA6, 0x3A, 0x36, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x05, 0x63,
};

static const uint8_t dh_G[] = { 2 };

struct tr_crypto
{
    DH * dh;
    RC4_KEY dec_key;
    RC4_KEY enc_key;
    uint8_t torrentHash[SHA_DIGEST_LENGTH];
    unsigned int isIncoming       : 1;
    unsigned int torrentHashIsSet : 1;
    unsigned int mySecretIsSet    : 1;
    uint8_t myPublicKey[KEY_LEN];
    uint8_t mySecret[KEY_LEN];

};

/**
***
**/

tr_crypto * 
tr_cryptoNew( const uint8_t * torrentHash,
              int             isIncoming )
{
    int len, offset;
    tr_crypto * crypto;

    crypto = tr_new0( tr_crypto, 1 );
    crypto->isIncoming = isIncoming ? 1 : 0;
    tr_cryptoSetTorrentHash( crypto, torrentHash );

    crypto->dh = DH_new( );
    crypto->dh->p = BN_bin2bn( dh_P, sizeof(dh_P), NULL );
    crypto->dh->g = BN_bin2bn( dh_G, sizeof(dh_G), NULL );
    DH_generate_key( crypto->dh );

    // DH can generate key sizes that are smaller than the size of
    // P with exponentially decreasing probability, in which case
    // the msb's of myPublicKey need to be zeroed appropriately.
    len = DH_size( crypto->dh );
    offset = KEY_LEN - len;
    assert( len <= KEY_LEN );
    memset( crypto->myPublicKey, 0, offset );
    BN_bn2bin( crypto->dh->pub_key, crypto->myPublicKey + offset );

    return crypto;
}

void
tr_cryptoFree( tr_crypto * crypto )
{
    assert( crypto != NULL );
    assert( crypto->dh != NULL );

    DH_free( crypto->dh );
    tr_free( crypto );
}

/**
***
**/

const uint8_t*
tr_cryptoComputeSecret( tr_crypto      * crypto,
                        const uint8_t  * peerPublicKey )
{
    int len, offset;
    uint8_t secret[KEY_LEN];
    BIGNUM * bn = BN_bin2bn( peerPublicKey, KEY_LEN, NULL );
    assert( DH_size(crypto->dh) == KEY_LEN );

    len = DH_compute_key( secret, bn, crypto->dh );
    assert( len <= KEY_LEN );
    offset = KEY_LEN - len;
    memset( crypto->mySecret, 0, offset );
    memcpy( crypto->mySecret + offset, secret, len );
    crypto->mySecretIsSet = 1;
    
    BN_free( bn );

    return crypto->mySecret;
}

const uint8_t*
tr_cryptoGetMyPublicKey( const tr_crypto * crypto, int * setme_len )
{
    *setme_len = KEY_LEN;
    return crypto->myPublicKey;
}

/**
***
**/

static void
initRC4( tr_crypto * crypto, RC4_KEY * setme, const char * key )
{
    SHA_CTX sha;
    uint8_t buf[SHA_DIGEST_LENGTH];

    assert( crypto->torrentHashIsSet );
    assert( crypto->mySecretIsSet );

    SHA1_Init( &sha );
    SHA1_Update( &sha, key, 4 );
    SHA1_Update( &sha, crypto->mySecret, KEY_LEN );
    SHA1_Update( &sha, crypto->torrentHash, SHA_DIGEST_LENGTH );
    SHA1_Final( buf, &sha );
    RC4_set_key( setme, SHA_DIGEST_LENGTH, buf );
}

void
tr_cryptoDecryptInit( tr_crypto * crypto )
{
    unsigned char discard[1024];
    const char * txt = crypto->isIncoming ? "keyA" : "keyB";
    initRC4( crypto, &crypto->dec_key, txt );
    RC4( &crypto->dec_key, sizeof(discard), discard, discard );
}

void
tr_cryptoDecrypt( tr_crypto  * crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void       * buf_out )
{
    RC4( &crypto->dec_key, buf_len,
         (const unsigned char*)buf_in,
         (unsigned char*)buf_out );
}

void
tr_cryptoEncryptInit( tr_crypto * crypto )
{
    unsigned char discard[1024];
    const char * txt = crypto->isIncoming ? "keyB" : "keyA";
    initRC4( crypto, &crypto->enc_key, txt );
    RC4( &crypto->enc_key, sizeof(discard), discard, discard );
}

void
tr_cryptoEncrypt( tr_crypto  * crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void       * buf_out )
{
    RC4( &crypto->enc_key, buf_len,
         (const unsigned char*)buf_in,
         (unsigned char*)buf_out );
}

/**
***
**/

void
tr_cryptoSetTorrentHash( tr_crypto     * crypto,
                         const uint8_t * hash )
{
    crypto->torrentHashIsSet = hash ? 1 : 0;

    if( hash != NULL )
        memcpy( crypto->torrentHash, hash, SHA_DIGEST_LENGTH );
    else
        memset( crypto->torrentHash, 0, SHA_DIGEST_LENGTH );
}

const uint8_t*
tr_cryptoGetTorrentHash( const tr_crypto * crypto )
{
    assert( crypto != NULL );
    assert( crypto->torrentHashIsSet );

    return crypto->torrentHash;
}

int
tr_cryptoHasTorrentHash( const tr_crypto * crypto )
{
    assert( crypto != NULL );

    return crypto->torrentHashIsSet ? 1 : 0;
}
