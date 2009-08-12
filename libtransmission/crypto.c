/* * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <stdlib.h> /* for abs() */
#include <limits.h> /* for INT_MAX */
#include <sys/types.h> /* for event.h, as well as netinet/in.h on some platforms
                         */
#include <assert.h>
#include <inttypes.h> /* uint8_t */
#include <string.h> /* memcpy */
#include <stdarg.h>

#include <openssl/bn.h>
#include <openssl/dh.h>
#include <openssl/err.h>
#include <openssl/rc4.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#include <event.h>

#include "crypto.h"
#include "utils.h"

#define MY_NAME "tr_crypto"

/**
***
**/

void
tr_sha1( uint8_t *    setme,
         const void * content1,
         int          content1_len,
         ... )
{
    va_list vl;
    SHA_CTX sha;

    SHA1_Init( &sha );
    SHA1_Update( &sha, content1, content1_len );

    va_start( vl, content1_len );
    for( ; ; )
    {
        const void * content = (const void*) va_arg( vl, const void* );
        const int    content_len = content ? (int) va_arg( vl, int ) : -1;
        if( content == NULL || content_len < 1 )
            break;
        SHA1_Update( &sha, content, content_len );
    }
    va_end( vl );
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
    RC4_KEY         dec_key;
    RC4_KEY         enc_key;
    uint8_t         torrentHash[SHA_DIGEST_LENGTH];
    tr_bool         isIncoming;
    tr_bool         torrentHashIsSet;
    tr_bool         mySecretIsSet;
    uint8_t         myPublicKey[KEY_LEN];
    uint8_t         mySecret[KEY_LEN];
};

/**
***
**/

#define logErrorFromSSL( ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_ERR ) ) { \
            char buf[512]; \
            ERR_error_string_n( ERR_get_error( ), buf, sizeof( buf ) ); \
            tr_msg( __FILE__, __LINE__, TR_MSG_ERR, MY_NAME, "%s", buf ); \
        } \
    } while( 0 )

static DH*
getSharedDH( void )
{
    static DH * dh = NULL;

    if( dh == NULL )
    {
        dh = DH_new( );

        dh->p = BN_bin2bn( dh_P, sizeof( dh_P ), NULL );
        if( dh->p == NULL )
            logErrorFromSSL( );

        dh->g = BN_bin2bn( dh_G, sizeof( dh_G ), NULL );
        if( dh->g == NULL )
            logErrorFromSSL( );

        if( !DH_generate_key( dh ) )
            logErrorFromSSL( );
    }

    return dh;
}

tr_crypto *
tr_cryptoNew( const uint8_t * torrentHash,
              int             isIncoming )
{
    int         len, offset;
    tr_crypto * crypto;
    DH *        dh = getSharedDH( );

    crypto = tr_new0( tr_crypto, 1 );
    crypto->isIncoming = isIncoming ? 1 : 0;
    tr_cryptoSetTorrentHash( crypto, torrentHash );

    /* DH can generate key sizes that are smaller than the size of
       P with exponentially decreasing probability, in which case
       the msb's of myPublicKey need to be zeroed appropriately. */
    len = DH_size( dh );
    offset = KEY_LEN - len;
    assert( len <= KEY_LEN );
    memset( crypto->myPublicKey, 0, offset );
    BN_bn2bin( dh->pub_key, crypto->myPublicKey + offset );

    return crypto;
}

void
tr_cryptoFree( tr_crypto * crypto )
{
    tr_free( crypto );
}

/**
***
**/

const uint8_t*
tr_cryptoComputeSecret( tr_crypto *     crypto,
                        const uint8_t * peerPublicKey )
{
    int      len;
    uint8_t  secret[KEY_LEN];
    BIGNUM * bn = BN_bin2bn( peerPublicKey, KEY_LEN, NULL );
    DH *     dh = getSharedDH( );

    assert( DH_size( dh ) == KEY_LEN );

    len = DH_compute_key( secret, bn, dh );
    if( len == -1 )
        logErrorFromSSL( );
    else {
        int offset;
        assert( len <= KEY_LEN );
        offset = KEY_LEN - len;
        memset( crypto->mySecret, 0, offset );
        memcpy( crypto->mySecret + offset, secret, len );
        crypto->mySecretIsSet = 1;
    }

    BN_free( bn );
    return crypto->mySecret;
}

const uint8_t*
tr_cryptoGetMyPublicKey( const tr_crypto * crypto,
                         int *             setme_len )
{
    *setme_len = KEY_LEN;
    return crypto->myPublicKey;
}

/**
***
**/

static void
initRC4( tr_crypto *  crypto,
         RC4_KEY *    setme,
         const char * key )
{
    SHA_CTX sha;
    uint8_t buf[SHA_DIGEST_LENGTH];

    assert( crypto->torrentHashIsSet );
    assert( crypto->mySecretIsSet );

    if( SHA1_Init( &sha )
        && SHA1_Update( &sha, key, 4 )
        && SHA1_Update( &sha, crypto->mySecret, KEY_LEN )
        && SHA1_Update( &sha, crypto->torrentHash, SHA_DIGEST_LENGTH )
        && SHA1_Final( buf, &sha ) )
    {
        RC4_set_key( setme, SHA_DIGEST_LENGTH, buf );
    }
    else
    {
        logErrorFromSSL( );
    }
}

void
tr_cryptoDecryptInit( tr_crypto * crypto )
{
    unsigned char discard[1024];
    const char *  txt = crypto->isIncoming ? "keyA" : "keyB";

    initRC4( crypto, &crypto->dec_key, txt );
    RC4( &crypto->dec_key, sizeof( discard ), discard, discard );
}

void
tr_cryptoDecrypt( tr_crypto *  crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void *       buf_out )
{
    RC4( &crypto->dec_key, buf_len,
         (const unsigned char*)buf_in,
         (unsigned char*)buf_out );
}

void
tr_cryptoEncryptInit( tr_crypto * crypto )
{
    unsigned char discard[1024];
    const char *  txt = crypto->isIncoming ? "keyB" : "keyA";

    initRC4( crypto, &crypto->enc_key, txt );
    RC4( &crypto->enc_key, sizeof( discard ), discard, discard );
}

void
tr_cryptoEncrypt( tr_crypto *  crypto,
                  size_t       buf_len,
                  const void * buf_in,
                  void *       buf_out )
{
    RC4( &crypto->enc_key, buf_len,
         (const unsigned char*)buf_in,
         (unsigned char*)buf_out );
}

/**
***
**/

void
tr_cryptoSetTorrentHash( tr_crypto *     crypto,
                         const uint8_t * hash )
{
    crypto->torrentHashIsSet = hash ? 1 : 0;

    if( hash )
        memcpy( crypto->torrentHash, hash, SHA_DIGEST_LENGTH );
    else
        memset( crypto->torrentHash, 0, SHA_DIGEST_LENGTH );
}

const uint8_t*
tr_cryptoGetTorrentHash( const tr_crypto * crypto )
{
    assert( crypto );
    assert( crypto->torrentHashIsSet );

    return crypto->torrentHash;
}

int
tr_cryptoHasTorrentHash( const tr_crypto * crypto )
{
    assert( crypto );

    return crypto->torrentHashIsSet ? 1 : 0;
}

int
tr_cryptoRandInt( int upperBound )
{
    int noise;
    int val;

    assert( upperBound > 0 );

    if( RAND_pseudo_bytes ( (unsigned char *) &noise, sizeof noise ) >= 0 )
    {
        val = abs( noise ) % upperBound;
    }
    else /* fall back to a weaker implementation... */
    {
        val = tr_cryptoWeakRandInt( upperBound );
    }

    assert( val >= 0 );
    assert( val < upperBound );
    return val;
}

int
tr_cryptoWeakRandInt( int upperBound )
{
    int val;
    static tr_bool init = FALSE;

    assert( upperBound > 0 );

    if( !init )
    {
        srand( tr_date( ) );
        init = TRUE;
    }

    val = rand( ) % upperBound;
    assert( val >= 0 );
    assert( val < upperBound );
    return val;
}

void
tr_cryptoRandBuf( void * buf, size_t len )
{
    if( RAND_pseudo_bytes ( (unsigned char*)buf, len ) != 1 )
        logErrorFromSSL( );
}

/***
****
***/

char*
tr_ssha1( const void * plaintext )
{
    static const char * salter = "0123456789"
                                 "abcdefghijklmnopqrstuvwxyz"
                                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "./";
    static const size_t salter_len = 64;
    static const size_t saltval_len = 8;

    size_t i;
    char salt[saltval_len];
    uint8_t sha[SHA_DIGEST_LENGTH];
    char buf[2*SHA_DIGEST_LENGTH + saltval_len + 2];

    for( i=0; i<saltval_len; ++i )
        salt[i] = salter[ tr_cryptoRandInt( salter_len ) ];

    tr_sha1( sha, plaintext, strlen( plaintext ), salt, saltval_len, NULL );
    tr_sha1_to_hex( &buf[1], sha );
    memcpy( &buf[1+2*SHA_DIGEST_LENGTH], &salt, saltval_len );
    buf[1+2*SHA_DIGEST_LENGTH + saltval_len] = '\0';
    buf[0] = '{'; /* signal that this is a hash. this makes saving/restoring
                     easier */

    return tr_strdup( &buf );
}

tr_bool
tr_ssha1_matches( const char * source, const char * pass )
{
    char * salt;
    size_t saltlen;
    char * hashed;
    uint8_t buf[SHA_DIGEST_LENGTH];
    tr_bool result;

    /* extract the salt */
    saltlen = strlen( source ) - 2*SHA_DIGEST_LENGTH-1;
    salt = tr_malloc( saltlen );
    memcpy( salt, source + 2*SHA_DIGEST_LENGTH+1, saltlen );

    /* hash pass + salt */
    hashed = tr_malloc( 2*SHA_DIGEST_LENGTH + saltlen + 2 );
    tr_sha1( buf, pass, strlen( pass ), salt, saltlen, NULL );
    tr_sha1_to_hex( &hashed[1], buf );
    memcpy( hashed + 1+2*SHA_DIGEST_LENGTH, salt, saltlen );
    hashed[1+2*SHA_DIGEST_LENGTH + saltlen] = '\0';
    hashed[0] = '{';

    result = strcmp( source, hashed ) == 0 ? TRUE : FALSE;

    tr_free( hashed );
    tr_free( salt );

    return result;
}
