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
#include <errno.h>
#include <inttypes.h>
#include <limits.h> /* UCHAR_MAX */
#include <string.h>
#include <stdio.h>
#include <libgen.h> /* basename */

#include <sys/types.h> /* event.h needs this */
#include <event.h>

#include "clients.h"
#include "transmission.h"
#include "bencode.h"
#include "crypto.h"
#include "handshake.h"
#include "peer-io.h"
#include "trevent.h"
#include "utils.h"

/* enable LibTransmission extension protocol */
#define ENABLE_LTEP */

/* enable fast peers extension protocol */
/* #define ENABLE_FASTPEER */

/***
****
***/

#define HANDSHAKE_NAME "\023BitTorrent protocol"

enum
{
    /* BitTorrent Handshake Constants */
    HANDSHAKE_NAME_LEN             = 20,
    HANDSHAKE_FLAGS_LEN            = 8,
    HANDSHAKE_SIZE                 = 68,
    PEER_ID_LEN                    = 20,

    /* Encryption Constants */
    PadA_MAXLEN                    = 512,
    PadB_MAXLEN                    = 512,
    PadC_MAXLEN                    = 512,
    PadD_MAXLEN                    = 512,
    VC_LENGTH                      = 8,
    KEY_LEN                        = 96,
    CRYPTO_PROVIDE_PLAINTEXT       = 1,
    CRYPTO_PROVIDE_CRYPTO          = 2
};


#ifdef ENABLE_LTEP
#define HANDSHAKE_HAS_LTEP( bits ) ( ( (bits)[5] & 0x10 ) ? 1 : 0 )
#define HANDSHAKE_SET_LTEP( bits ) ( (bits)[5] |= 0x10 )
#else
#define HANDSHAKE_HAS_LTEP( bits ) ( 0 )
#define HANDSHAKE_SET_LTEP( bits ) ( (void)0 )
#endif

#ifdef ENABLE_FASTPEER
#define HANDSHAKE_HAS_FASTEXT( bits ) ( ( (bits)[7] & 0x04 ) ? 1 : 0 )
#define HANDSHAKE_SET_FASTEXT( bits ) ( (bits)[7] |= 0x04 )
#else
#define HANDSHAKE_HAS_FASTEXT( bits ) ( 0 )
#define HANDSHAKE_SET_FASTEXT( bits ) ( (void)0 )
#endif

/* http://www.azureuswiki.com/index.php/Extension_negotiation_protocol
   these macros are to be used if both extended messaging and the
   azureus protocol is supported, they indicate which protocol is preferred */
#define HANDSHAKE_GET_EXTPREF( reserved )      ( (reserved)[5] & 0x03 )
#define HANDSHAKE_SET_EXTPREF( reserved, val ) ( (reserved)[5] |= 0x03 & (val) )

extern const char* getPeerId( void ) ;

struct tr_handshake
{
    unsigned int havePeerID                   : 1;
    unsigned int haveSentBitTorrentHandshake  : 1;
    unsigned int allowUnencryptedPeers        : 1;
    tr_peerIo * io;
    tr_crypto * crypto;
    struct tr_handle * handle;
    uint8_t myPublicKey[KEY_LEN];
    uint8_t mySecret[KEY_LEN];
    uint8_t state;
    uint16_t pad_c_len;
    uint16_t pad_d_len;
    uint16_t  ia_len;
    uint32_t crypto_select;
    uint32_t crypto_provide;
    uint8_t myReq1[SHA_DIGEST_LENGTH];
    uint8_t peer_id[PEER_ID_LEN];
    handshakeDoneCB doneCB;
    void * doneUserData;
};

/**
***
**/

enum
{
    /* incoming */
    AWAITING_HANDSHAKE,
    AWAITING_YA,
    AWAITING_PAD_A,
    AWAITING_CRYPTO_PROVIDE,
    AWAITING_PAD_C,
    AWAITING_IA,

    /* outgoing */
    AWAITING_YB,
    AWAITING_VC,
    AWAITING_CRYPTO_SELECT,
    AWAITING_PAD_D,
};

/**
***
**/

static void
myDebug( const char * file, int line, const tr_handshake * handshake, const char * fmt, ... )
{
    FILE * fp = tr_getLog( );
    if( fp != NULL )
    {
        va_list args;
        char timestr[64];
        struct evbuffer * buf = evbuffer_new( );
        char * myfile = tr_strdup( file );

        evbuffer_add_printf( buf, "[%s] %s: ",
                             tr_getLogTimeStr( timestr, sizeof(timestr) ),
                             tr_peerIoGetAddrStr( handshake->io ) );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", basename(myfile), line );
        fwrite( EVBUFFER_DATA(buf), 1, EVBUFFER_LENGTH(buf), fp );

        tr_free( myfile );
        evbuffer_free( buf );
    }
}

#define dbgmsg(handshake, fmt...) myDebug(__FILE__, __LINE__, handshake, ##fmt )

static const char* getStateName( short state )
{
    const char * str = "f00!";
    switch( state ) {
        case AWAITING_HANDSHAKE:      str = "awaiting handshake"; break;
        case AWAITING_YA:             str = "awaiting ya"; break;
        case AWAITING_PAD_A:          str = "awaiting pad a"; break;
        case AWAITING_CRYPTO_PROVIDE: str = "awaiting crypto_provide"; break;
        case AWAITING_PAD_C:          str = "awaiting pad c"; break;
        case AWAITING_IA:             str = "awaiting ia"; break;
        case AWAITING_YB:             str = "awaiting yb"; break;
        case AWAITING_VC:             str = "awaiting vc"; break;
        case AWAITING_CRYPTO_SELECT:  str = "awaiting crypto select"; break;
        case AWAITING_PAD_D:          str = "awaiting pad d"; break;
    }
    return str;
}

static void
setState( tr_handshake * handshake, short state )
{
    dbgmsg( handshake, "setting to state [%s]", getStateName(state) );
    handshake->state = state;
}

static void
setReadState( tr_handshake * handshake, int state )
{
    setState( handshake, state );
}

static uint8_t *
buildHandshakeMessage( tr_handshake * handshake,
                       int          * setme_len )
{
    uint8_t * buf = tr_new0( uint8_t, HANDSHAKE_SIZE );
    uint8_t * walk = buf;
    const uint8_t * torrentHash = tr_cryptoGetTorrentHash( handshake->crypto );

    memcpy( walk, HANDSHAKE_NAME, HANDSHAKE_NAME_LEN );
    walk += HANDSHAKE_NAME_LEN;
    memset( walk, 0, HANDSHAKE_FLAGS_LEN );
    HANDSHAKE_SET_LTEP( walk );
    HANDSHAKE_SET_FASTEXT( walk );

    walk += HANDSHAKE_FLAGS_LEN;
    memcpy( walk, torrentHash, SHA_DIGEST_LENGTH );
    walk += SHA_DIGEST_LENGTH;
    memcpy( walk, getPeerId(), TR_ID_LEN );
    walk += TR_ID_LEN;

    assert( walk-buf == HANDSHAKE_SIZE );
    *setme_len = walk - buf;
    return buf;
}

static void
tr_handshakeDone( tr_handshake * handshake, int isConnected );

enum
{
    HANDSHAKE_OK,
    HANDSHAKE_ENCRYPTION_WRONG,
    HANDSHAKE_BAD_TORRENT,
    HANDSHAKE_PEER_IS_SELF,
};

static int
parseHandshake( tr_handshake     * handshake,
                struct evbuffer  * inbuf )
{
    uint8_t name[HANDSHAKE_NAME_LEN];
    uint8_t reserved[HANDSHAKE_FLAGS_LEN];
    uint8_t hash[SHA_DIGEST_LENGTH];

    dbgmsg( handshake, "payload: need %d, got %d", (int)HANDSHAKE_SIZE, (int)EVBUFFER_LENGTH(inbuf) );

    if( EVBUFFER_LENGTH(inbuf) < HANDSHAKE_SIZE )
        return READ_MORE;

    /* confirm the protocol */
    tr_peerIoReadBytes( handshake->io, inbuf, name, HANDSHAKE_NAME_LEN );
    if( memcmp( name, HANDSHAKE_NAME, HANDSHAKE_NAME_LEN ) )
        return HANDSHAKE_ENCRYPTION_WRONG;

    /* read the reserved bytes */
    tr_peerIoReadBytes( handshake->io, inbuf, reserved, HANDSHAKE_FLAGS_LEN );

    /* torrent hash */
    tr_peerIoReadBytes( handshake->io, inbuf, hash, sizeof(hash) );
    assert( tr_torrentExists( handshake->handle, hash ) );
    assert( tr_peerIoHasTorrentHash( handshake->io ) );
    if( memcmp( hash, tr_peerIoGetTorrentHash(handshake->io), SHA_DIGEST_LENGTH ) ) {
        dbgmsg( handshake, "peer returned the wrong hash. wtf?" );
        return HANDSHAKE_BAD_TORRENT;
    }

    /* peer_id */
    tr_peerIoReadBytes( handshake->io, inbuf, handshake->peer_id, sizeof(handshake->peer_id) );
    tr_peerIoSetPeersId( handshake->io, handshake->peer_id );

    /* peer id */
    handshake->havePeerID = TRUE;
    dbgmsg( handshake, "peer-id is [%*.*s]", PEER_ID_LEN, PEER_ID_LEN, handshake->peer_id );
    if( !memcmp( handshake->peer_id, getPeerId(), PEER_ID_LEN ) ) {
        dbgmsg( handshake, "streuth!  we've connected to ourselves." );
        return HANDSHAKE_PEER_IS_SELF;
    }

    /**
    *** Extensions
    **/

    if( HANDSHAKE_HAS_LTEP( reserved ) )
    {
        tr_peerIoEnableLTEP( handshake->io, 1 );
        dbgmsg(handshake,"using ltep" );
    }

    if( HANDSHAKE_HAS_FASTEXT( reserved ) )
    {
        tr_peerIoEnableFEXT( handshake->io, 1 );
        dbgmsg(handshake,"using fext" );
    }
        
    return HANDSHAKE_OK;
}

/***
****
****  OUTGOING CONNECTIONS
****
***/

/* 1 A->B: Diffie Hellman Ya, PadA */
static void
sendYa( tr_handshake * handshake )
{
    int i;
    int len;
    const uint8_t * public_key;
    struct evbuffer * outbuf = evbuffer_new( );
    uint8_t pad_a[PadA_MAXLEN];

    /* add our public key (Ya) */
    public_key = tr_cryptoGetMyPublicKey( handshake->crypto, &len );
    assert( len == KEY_LEN );
    assert( public_key != NULL );
    evbuffer_add( outbuf, public_key, len );

    /* add some bullshit padding */
    len = tr_rand( PadA_MAXLEN );
    for( i=0; i<len; ++i )
        pad_a[i] = tr_rand( UCHAR_MAX );
    evbuffer_add( outbuf, pad_a, len );

    /* send it */
    setReadState( handshake, AWAITING_YB );
    tr_peerIoWriteBuf( handshake->io, outbuf );

    /* cleanup */
    evbuffer_free( outbuf );
}

static uint32_t
getCryptoProvide( const tr_handshake * handshake UNUSED )
{
    uint32_t i = 0;

    i |= CRYPTO_PROVIDE_CRYPTO; /* always allow crypto */

#if 0
    /* by the time we send a crypto_provide, we _know_
     * the peer supports encryption. */
    if( handshake->allowUnencryptedPeers )
        i |= CRYPTO_PROVIDE_PLAINTEXT;
#endif

   return i;
}

static int
readYb( tr_handshake * handshake, struct evbuffer * inbuf )
{
    int isEncrypted;
    const uint8_t * secret;
    uint8_t yb[KEY_LEN];
    struct evbuffer * outbuf;
    size_t needlen = HANDSHAKE_NAME_LEN;

    if( EVBUFFER_LENGTH(inbuf) < needlen )
        return READ_MORE;

    isEncrypted = memcmp( EVBUFFER_DATA(inbuf), HANDSHAKE_NAME, HANDSHAKE_NAME_LEN );
    if( isEncrypted ) {
        needlen = KEY_LEN;
        if( EVBUFFER_LENGTH(inbuf) < needlen )
            return READ_MORE;
    }

    dbgmsg( handshake, "got a %s handshake", (isEncrypted ? "encrypted" : "plaintext") );

    tr_peerIoSetEncryption( handshake->io, isEncrypted ? PEER_ENCRYPTION_RC4
                                                       : PEER_ENCRYPTION_NONE );
    if( !isEncrypted ) {
        setState( handshake, AWAITING_HANDSHAKE );
        return READ_AGAIN;
    }

    /* compute the secret */
    evbuffer_remove( inbuf, yb, KEY_LEN );
    secret = tr_cryptoComputeSecret( handshake->crypto, yb );
    memcpy( handshake->mySecret, secret, KEY_LEN );

    /* now send these: HASH('req1', S), HASH('req2', SKEY) xor HASH('req3', S),
     * ENCRYPT(VC, crypto_provide, len(PadC), PadC, len(IA)), ENCRYPT(IA) */
    outbuf = evbuffer_new( );

    /* HASH('req1', S) */
    {
        uint8_t req1[SHA_DIGEST_LENGTH];
        tr_sha1( req1, "req1", 4, secret, KEY_LEN, NULL );
        evbuffer_add( outbuf, req1, SHA_DIGEST_LENGTH );
    }

    /* HASH('req2', SKEY) xor HASH('req3', S) */
    {
        int i;
        uint8_t req2[SHA_DIGEST_LENGTH];
        uint8_t req3[SHA_DIGEST_LENGTH];
        uint8_t buf[SHA_DIGEST_LENGTH];
        tr_sha1( req2, "req2", 4, tr_cryptoGetTorrentHash(handshake->crypto), SHA_DIGEST_LENGTH, NULL );
        tr_sha1( req3, "req3", 4, secret, KEY_LEN, NULL );
        for( i=0; i<SHA_DIGEST_LENGTH; ++i )
            buf[i] = req2[i] ^ req3[i];
        evbuffer_add( outbuf, buf, SHA_DIGEST_LENGTH );
    }
      
    /* ENCRYPT(VC, crypto_provide, len(PadC), PadC
     * PadC is reserved for future extensions to the handshake...
     * standard practice at this time is for it to be zero-length */
    {
        uint8_t vc[VC_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0 };

        tr_cryptoEncryptInit( handshake->crypto );
        tr_peerIoSetEncryption( handshake->io, PEER_ENCRYPTION_RC4 );
      
        tr_peerIoWriteBytes( handshake->io, outbuf, vc, VC_LENGTH ); 
        tr_peerIoWriteUint32( handshake->io, outbuf, getCryptoProvide( handshake ) );
        tr_peerIoWriteUint16( handshake->io, outbuf, 0 );
    }

    /* ENCRYPT len(IA)), ENCRYPT(IA) */
    {
        int msglen;
        uint8_t * msg = buildHandshakeMessage( handshake, &msglen );

        tr_peerIoWriteUint16( handshake->io, outbuf, msglen );
        tr_peerIoWriteBytes( handshake->io, outbuf, msg, msglen );

        handshake->haveSentBitTorrentHandshake = 1;
        tr_free( msg );
    }

    /* send it */
    tr_cryptoDecryptInit( handshake->crypto );
    setReadState( handshake, AWAITING_VC );
    tr_peerIoWriteBuf( handshake->io, outbuf );

    /* cleanup */
    evbuffer_free( outbuf );
    return READ_DONE;
}

static int
readVC( tr_handshake * handshake, struct evbuffer * inbuf )
{
    const uint8_t key[VC_LENGTH] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    const int key_len = VC_LENGTH;
    uint8_t tmp[VC_LENGTH];

    /* note: this works w/o having to `unwind' the buffer if
     * we read too much, but it is pretty brute-force.
     * it would be nice to make this cleaner. */
    for( ;; )
    {
        if( EVBUFFER_LENGTH(inbuf) < VC_LENGTH ) {
            dbgmsg( handshake, "not enough bytes... returning read_more" );
            return READ_MORE;
        }

        memcpy( tmp, EVBUFFER_DATA(inbuf), key_len );
        tr_cryptoDecryptInit( handshake->crypto );
        tr_cryptoDecrypt( handshake->crypto, key_len, tmp, tmp );
        if( !memcmp( tmp, key, key_len ) )
            break;

        evbuffer_drain( inbuf, 1 );
    }

    dbgmsg( handshake, "got it!" );
    evbuffer_drain( inbuf, key_len );
    setState( handshake, AWAITING_CRYPTO_SELECT );
    return READ_AGAIN;
}

static int
readCryptoSelect( tr_handshake * handshake, struct evbuffer * inbuf )
{
    uint32_t crypto_select;
    uint16_t pad_d_len;
    const size_t needlen = sizeof(uint32_t) + sizeof(uint16_t);

    if( EVBUFFER_LENGTH(inbuf) < needlen )
        return READ_MORE;

    tr_peerIoReadUint32( handshake->io, inbuf, &crypto_select );
    handshake->crypto_select = crypto_select;
    dbgmsg( handshake, "crypto select is %d", (int)crypto_select );
    if( ! ( crypto_select & getCryptoProvide( handshake ) ) )
    {
        dbgmsg( handshake, "peer selected an encryption option we didn't provide" );
        tr_handshakeDone( handshake, FALSE );
        return READ_DONE;
    }

    tr_peerIoReadUint16( handshake->io, inbuf, &pad_d_len );
    dbgmsg( handshake, "pad_d_len is %d", (int)pad_d_len );
    assert( pad_d_len <= 512 );
    handshake->pad_d_len = pad_d_len;

    setState( handshake, AWAITING_PAD_D );
    return READ_AGAIN;
}

static int
readPadD( tr_handshake * handshake, struct evbuffer * inbuf )
{
    const size_t needlen = handshake->pad_d_len;
    uint8_t * tmp;

    dbgmsg( handshake, "pad d: need %d, got %d", (int)needlen, (int)EVBUFFER_LENGTH(inbuf) );
    if( EVBUFFER_LENGTH(inbuf) < needlen )
        return READ_MORE;

    tmp = tr_new( uint8_t, needlen );
    tr_peerIoReadBytes( handshake->io, inbuf, tmp, needlen );
    tr_free( tmp );

    tr_peerIoSetEncryption( handshake->io,
                            handshake->crypto_select );

    setState( handshake, AWAITING_HANDSHAKE );
    return READ_AGAIN;
}

/***
****
****  INCOMING CONNECTIONS
****
***/

static int
readHandshake( tr_handshake * handshake, struct evbuffer * inbuf )
{
    uint8_t pstrlen;
    uint8_t * pstr;
    uint8_t reserved[HANDSHAKE_FLAGS_LEN];
    uint8_t hash[SHA_DIGEST_LENGTH];
    char * client;

/* FIXME: use  readHandshake here */

    dbgmsg( handshake, "payload: need %d, got %d", (int)HANDSHAKE_SIZE, (int)EVBUFFER_LENGTH(inbuf) );

    if( EVBUFFER_LENGTH(inbuf) < HANDSHAKE_SIZE )
        return READ_MORE;

    pstrlen = EVBUFFER_DATA(inbuf)[0]; /* peek, don't read.  We may be
                                          handing inbuf to AWAITING_YA */

    if( pstrlen == 19 ) /* unencrypted */
    {
        tr_peerIoSetEncryption( handshake->io, PEER_ENCRYPTION_NONE );

        if( !handshake->allowUnencryptedPeers )
        {
            dbgmsg( handshake, "peer is unencrypted, and we're disallowing that" );
            tr_handshakeDone( handshake, FALSE );
            return READ_DONE;
        }
    }
    else /* encrypted or corrupt */
    {
        tr_peerIoSetEncryption( handshake->io, PEER_ENCRYPTION_RC4 );

        if( tr_peerIoIsIncoming( handshake->io ) )
        {
            dbgmsg( handshake, "I think peer is sending us an encrypted handshake..." );
            setState( handshake, AWAITING_YA );
            return READ_AGAIN;
        }
        tr_cryptoDecrypt( handshake->crypto, 1, &pstrlen, &pstrlen );

        if( pstrlen != 19 )
        {
            dbgmsg( handshake, "I think peer has sent us a corrupt handshake..." );
            tr_handshakeDone( handshake, FALSE );
            return READ_DONE;
        }
    }

    evbuffer_drain( inbuf, 1 );

    /* pstr (BitTorrent) */
    pstr = tr_new( uint8_t, pstrlen+1 );
    tr_peerIoReadBytes( handshake->io, inbuf, pstr, pstrlen );
    pstr[pstrlen] = '\0';
    if( strcmp( (char*)pstr, "BitTorrent protocol" ) ) {
        tr_free( pstr );
        tr_handshakeDone( handshake, FALSE );
        return READ_DONE;
    }
    tr_free( pstr );

    /* reserved bytes */
    tr_peerIoReadBytes( handshake->io, inbuf, reserved, sizeof(reserved) );

    /* torrent hash */
    tr_peerIoReadBytes( handshake->io, inbuf, hash, sizeof(hash) );
    if( tr_peerIoIsIncoming( handshake->io ) )
    {
        if( !tr_torrentExists( handshake->handle, hash ) )
        {
            dbgmsg( handshake, "peer is trying to connect to us for a torrent we don't have." );
            tr_handshakeDone( handshake, FALSE );
            return READ_DONE;
        }
        else
        {
            assert( !tr_peerIoHasTorrentHash( handshake->io ) );
            tr_peerIoSetTorrentHash( handshake->io, hash );
        }
    }
    else /* outgoing */
    {
        assert( tr_peerIoHasTorrentHash( handshake->io ) );
        if( memcmp( hash, tr_peerIoGetTorrentHash(handshake->io), SHA_DIGEST_LENGTH ) )
        {
            dbgmsg( handshake, "peer returned the wrong hash. wtf?" );
            tr_handshakeDone( handshake, FALSE );
            return READ_DONE;
        }
    }

    /* peer id */
    tr_peerIoReadBytes( handshake->io, inbuf, handshake->peer_id, sizeof(handshake->peer_id) );
    tr_peerIoSetPeersId( handshake->io, handshake->peer_id );
    handshake->havePeerID = TRUE;
    client = tr_clientForId( handshake->peer_id );
    dbgmsg( handshake, "peer-id is [%s]", client );
    tr_free( client );
    if( !memcmp( handshake->peer_id, getPeerId(), PEER_ID_LEN ) ) {
        dbgmsg( handshake, "streuth!  we've connected to ourselves." );
        tr_handshakeDone( handshake, FALSE );
        return READ_DONE;
    }

    /**
    *** Extension negotiation
    **/

    if( HANDSHAKE_HAS_LTEP( reserved ) )
    {
        tr_peerIoEnableLTEP( handshake->io, 1 );
        dbgmsg( handshake,"using ltep" );
    }
    if( HANDSHAKE_HAS_FASTEXT( reserved ) )
    {
        tr_peerIoEnableFEXT( handshake->io, 1 );
        dbgmsg( handshake,"using fext" );
    }
    
    /**
    ***  If this is an incoming message, then we need to send a response handshake
    **/

    if( !handshake->haveSentBitTorrentHandshake )
    {
        int msgSize;
        uint8_t * msg = buildHandshakeMessage( handshake, &msgSize );
        tr_peerIoWrite( handshake->io, msg, msgSize );
        tr_free( msg );
        handshake->haveSentBitTorrentHandshake = 1;
    }

    /* we've completed the BT handshake... pass the work on to peer-msgs */
    tr_handshakeDone( handshake, TRUE );
    return READ_DONE;
}

static int
readYa( tr_handshake * handshake, struct evbuffer  * inbuf )
{
    uint8_t ya[KEY_LEN];
    uint8_t *walk, outbuf[KEY_LEN + PadB_MAXLEN];
    const uint8_t *myKey, *secret;
    int len;

dbgmsg( handshake, "in readYa... need %d, have %d", (int)KEY_LEN, (int)EVBUFFER_LENGTH( inbuf ) );
    if( EVBUFFER_LENGTH( inbuf ) < KEY_LEN )
        return READ_MORE;

    /* read the incoming peer's public key */
    evbuffer_remove( inbuf, ya, KEY_LEN );
    secret = tr_cryptoComputeSecret( handshake->crypto, ya );
    memcpy( handshake->mySecret, secret, KEY_LEN );
    tr_sha1( handshake->myReq1, "req1", 4, secret, KEY_LEN, NULL );

dbgmsg( handshake, "sending B->A: Diffie Hellman Yb, PadB" );
    /* send our public key to the peer */
    walk = outbuf;
    myKey = tr_cryptoGetMyPublicKey( handshake->crypto, &len );
    memcpy( walk, myKey, len );
    walk += len;
    len = tr_rand( PadB_MAXLEN );
    while( len-- )
        *walk++ = tr_rand( UCHAR_MAX );

    setReadState( handshake, AWAITING_PAD_A );
    tr_peerIoWrite( handshake->io, outbuf, walk-outbuf );
    return READ_AGAIN;
}

static int
readPadA( tr_handshake * handshake, struct evbuffer * inbuf )
{
    uint8_t * pch;

dbgmsg( handshake, "looking to get past pad a... & resync on hash('req',S) ... have %d bytes", (int)EVBUFFER_LENGTH(inbuf) );
    /**
    *** Resynchronizing on HASH('req1',S)
    **/

    pch = memchr( EVBUFFER_DATA(inbuf),
                  handshake->myReq1[0],
                  EVBUFFER_LENGTH(inbuf) );
    if( pch == NULL ) {
        dbgmsg( handshake, "no luck so far.. draining %d bytes", (int)EVBUFFER_LENGTH(inbuf) );
        evbuffer_drain( inbuf, EVBUFFER_LENGTH(inbuf) );
        return READ_MORE;
    }
    dbgmsg( handshake, "looking for hash('req',S) ... draining %d bytes", (int)(pch-EVBUFFER_DATA(inbuf)) );
    evbuffer_drain( inbuf, pch-EVBUFFER_DATA(inbuf) );
    if( EVBUFFER_LENGTH(inbuf) < SHA_DIGEST_LENGTH )
        return READ_MORE;
    if( memcmp( EVBUFFER_DATA(inbuf), handshake->myReq1, SHA_DIGEST_LENGTH ) ) {
        dbgmsg( handshake, "draining one more byte" );
        evbuffer_drain( inbuf, 1 );
        return READ_AGAIN;
    }

dbgmsg( handshake, "found it... looking setting to awaiting_crypto_provide" );
    setState( handshake, AWAITING_CRYPTO_PROVIDE );
    return READ_AGAIN;
}

static int
readCryptoProvide( tr_handshake * handshake, struct evbuffer * inbuf )
{
    /* HASH('req2', SKEY) xor HASH('req3', S), ENCRYPT(VC, crypto_provide, len(PadC)) */

    int i;
    uint8_t vc_in[VC_LENGTH];
    uint8_t req2[SHA_DIGEST_LENGTH];
    uint8_t req3[SHA_DIGEST_LENGTH];
    uint8_t obfuscatedTorrentHash[SHA_DIGEST_LENGTH];
    uint16_t padc_len = 0;
    uint32_t crypto_provide = 0;
    const size_t needlen = SHA_DIGEST_LENGTH /* HASH('req1',s) */
                         + SHA_DIGEST_LENGTH /* HASH('req2', SKEY) xor HASH('req3', S) */
                         + VC_LENGTH
                         + sizeof(crypto_provide)
                         + sizeof(padc_len);
    tr_torrent * tor = NULL;

    if( EVBUFFER_LENGTH(inbuf) < needlen )
        return READ_MORE;

    /* TODO: confirm they sent HASH('req1',S) here? */
    evbuffer_drain( inbuf, SHA_DIGEST_LENGTH );

    /* This next piece is HASH('req2', SKEY) xor HASH('req3', S) ...
     * we can get the first half of that (the obufscatedTorrentHash)
     * by building the latter and xor'ing it with what the peer sent us */
    dbgmsg( handshake, "reading obfuscated torrent hash..." );
    evbuffer_remove( inbuf, req2, SHA_DIGEST_LENGTH );
    tr_sha1( req3, "req3", 4, handshake->mySecret, KEY_LEN, NULL );
    for( i=0; i<SHA_DIGEST_LENGTH; ++i )
        obfuscatedTorrentHash[i] = req2[i] ^ req3[i];
    tor = tr_torrentFindFromObfuscatedHash( handshake->handle, obfuscatedTorrentHash );
    if( tor != NULL ) {
        dbgmsg( handshake, "found the torrent; it's [%s]", tor->info.name );
        tr_peerIoSetTorrentHash( handshake->io, tor->info.hash );
    } else {
        dbgmsg( handshake, "can't find that torrent..." );
        tr_handshakeDone( handshake, FALSE );
        return READ_DONE;
    }

    /* next part: ENCRYPT(VC, crypto_provide, len(PadC), */

    tr_cryptoDecryptInit( handshake->crypto );

    tr_peerIoReadBytes( handshake->io, inbuf, vc_in, VC_LENGTH );

    tr_peerIoReadUint32( handshake->io, inbuf, &crypto_provide );
    handshake->crypto_provide = crypto_provide;
    dbgmsg( handshake, "crypto_provide is %d", (int)crypto_provide );

    tr_peerIoReadUint16( handshake->io, inbuf, &padc_len );
    dbgmsg( handshake, "padc is %d", (int)padc_len );
    handshake->pad_c_len = padc_len;
    setState( handshake, AWAITING_PAD_C );
    return READ_AGAIN;
}

static int
readPadC( tr_handshake * handshake, struct evbuffer * inbuf )
{
    uint16_t ia_len;
    const size_t needlen = handshake->pad_c_len + sizeof(uint16_t);

    if( EVBUFFER_LENGTH(inbuf) < needlen )
        return READ_MORE;

    evbuffer_drain( inbuf, handshake->pad_c_len );

    tr_peerIoReadUint16( handshake->io, inbuf, &ia_len );
    dbgmsg( handshake, "ia_len is %d", (int)ia_len );
    handshake->ia_len = ia_len;
    setState( handshake, AWAITING_IA );
    return READ_AGAIN;
}

static int
readIA( tr_handshake * handshake, struct evbuffer * inbuf )
{
    int i;
    const size_t needlen = handshake->ia_len;
    struct evbuffer * outbuf = evbuffer_new( );
    uint32_t crypto_select;

dbgmsg( handshake, "reading IA... have %d, need %d", (int)EVBUFFER_LENGTH(inbuf), (int)needlen );
    if( EVBUFFER_LENGTH(inbuf) < needlen )
        return READ_MORE;

dbgmsg( handshake, "reading IA..." );
    /* parse the handshake ... */
    i = parseHandshake( handshake, inbuf );
dbgmsg( handshake, "parseHandshake returned %d", i );
    if( i != HANDSHAKE_OK ) {
        evbuffer_free( outbuf );
        tr_handshakeDone( handshake, FALSE );
        return READ_DONE;
    }

    /**
    ***  B->A: ENCRYPT(VC, crypto_select, len(padD), padD), ENCRYPT2(Payload Stream)
    **/

    tr_cryptoEncryptInit( handshake->crypto );

dbgmsg( handshake, "sending vc" );
    /* send VC */
    {
        uint8_t vc[VC_LENGTH];
        memset( vc, 0, VC_LENGTH );
        tr_peerIoWriteBytes( handshake->io, outbuf, vc, VC_LENGTH );
    }

dbgmsg( handshake, "sending crypto_select" );
    /* send crypto_select */
    {
dbgmsg( handshake, "handshake->crypto_provide is %d", (int)handshake->crypto_provide );
        if( handshake->crypto_provide & CRYPTO_PROVIDE_CRYPTO )
            crypto_select = CRYPTO_PROVIDE_CRYPTO;
        else if( handshake->allowUnencryptedPeers )
            crypto_select = CRYPTO_PROVIDE_PLAINTEXT;
        else {
dbgmsg( handshake, "gronk..." );
            evbuffer_free( outbuf );
            tr_handshakeDone( handshake, FALSE );
            return READ_DONE;
        }

dbgmsg( handshake, "we select crypto_select as %d...", (int)crypto_select );
        tr_peerIoWriteUint32( handshake->io, outbuf, crypto_select );
    }

dbgmsg( handshake, "sending pad d" );
    /* ENCRYPT(VC, crypto_provide, len(PadC), PadC
     * PadD is reserved for future extensions to the handshake...
     * standard practice at this time is for it to be zero-length */
    {
        const int len = 0;
        tr_peerIoWriteUint16( handshake->io, outbuf, len );
    }

    /* maybe de-encrypt our connection */
    if( crypto_select == CRYPTO_PROVIDE_PLAINTEXT )
        tr_peerIoSetEncryption( handshake->io, PEER_ENCRYPTION_NONE );

dbgmsg( handshake, "sending handshake" );
    /* send our handshake */
    {
        int msgSize;
        uint8_t * msg = buildHandshakeMessage( handshake, &msgSize );
        tr_peerIoWriteBytes( handshake->io, outbuf, msg, msgSize );
        handshake->haveSentBitTorrentHandshake = 1;
        tr_free( msg );
    }

    /* send it out */
    tr_peerIoWriteBuf( handshake->io, outbuf );
    evbuffer_free( outbuf );

    /* we've completed the BT handshake... pass the work on to peer-msgs */
    tr_handshakeDone( handshake, TRUE );
    return READ_DONE;
}

/***
****
****
****
***/

static ReadState
canRead( struct bufferevent * evin, void * arg )
{
    tr_handshake * handshake = (tr_handshake *) arg;
    struct evbuffer * inbuf = EVBUFFER_INPUT ( evin );
    ReadState ret;
    dbgmsg( handshake, "handling canRead; state is [%s]", getStateName(handshake->state) );

    switch( handshake->state )
    {
        case AWAITING_HANDSHAKE:       ret = readHandshake    ( handshake, inbuf ); break;
        case AWAITING_YA:              ret = readYa           ( handshake, inbuf ); break;
        case AWAITING_PAD_A:           ret = readPadA         ( handshake, inbuf ); break;
        case AWAITING_CRYPTO_PROVIDE:  ret = readCryptoProvide( handshake, inbuf ); break;
        case AWAITING_PAD_C:           ret = readPadC         ( handshake, inbuf ); break;
        case AWAITING_IA:              ret = readIA           ( handshake, inbuf ); break;

        case AWAITING_YB:              ret = readYb           ( handshake, inbuf ); break;
        case AWAITING_VC:              ret = readVC           ( handshake, inbuf ); break;
        case AWAITING_CRYPTO_SELECT:   ret = readCryptoSelect ( handshake, inbuf ); break;
        case AWAITING_PAD_D:           ret = readPadD         ( handshake, inbuf ); break;

        default: assert( 0 );
    }

    return ret;
}

static void
fireDoneFunc( tr_handshake * handshake, int isConnected )
{
    const uint8_t * peer_id = isConnected && handshake->havePeerID
        ? handshake->peer_id
        : NULL;
    (*handshake->doneCB)( handshake,
                          handshake->io,
                          isConnected,
                          peer_id,
                          handshake->doneUserData );
}

void
tr_handshakeDone( tr_handshake * handshake, int isOK )
{
    dbgmsg( handshake, "handshakeDone: %s", isOK ? "connected" : "aborting" );
    tr_peerIoSetIOFuncs( handshake->io, NULL, NULL, NULL, NULL );

    fireDoneFunc( handshake, isOK );

    tr_free( handshake );
}

void
tr_handshakeAbort( tr_handshake * handshake )
{
    tr_handshakeDone( handshake, FALSE );
}

static void
gotError( struct bufferevent * evbuf UNUSED, short what, void * arg )
{
    tr_handshake * handshake = (tr_handshake *) arg;

    /* if the error happened while we were sending a public key, we might
     * have encountered a peer that doesn't do encryption... reconnect and
     * try a plaintext handshake */
    if(    ( ( handshake->state == AWAITING_YB ) || ( handshake->state == AWAITING_VC ) )
        && ( handshake->allowUnencryptedPeers )
        && ( !tr_peerIoReconnect( handshake->io ) ) )
    {
        dbgmsg( handshake, "handshake failed, trying plaintext..." );
        int msgSize; 
        uint8_t * msg = buildHandshakeMessage( handshake, &msgSize );
        handshake->haveSentBitTorrentHandshake = 1;
        setReadState( handshake, AWAITING_HANDSHAKE );
        tr_peerIoWrite( handshake->io, msg, msgSize );
        tr_free( msg );
    }
    else
    {
        dbgmsg( handshake, "libevent got an error what==%d, errno=%d (%s)",
                (int)what, errno, strerror(errno) );
        tr_handshakeDone( handshake, FALSE );
    }
}

/**
***
**/

tr_handshake*
tr_handshakeNew( tr_peerIo           * io,
                 tr_encryption_mode    encryption_mode,
                 handshakeDoneCB       doneCB,
                 void                * doneUserData )
{
    tr_handshake * handshake;

    handshake = tr_new0( tr_handshake, 1 );
    handshake->io = io;
    handshake->crypto = tr_peerIoGetCrypto( io );
    handshake->allowUnencryptedPeers = encryption_mode!=TR_ENCRYPTION_REQUIRED;
    handshake->doneCB = doneCB;
    handshake->doneUserData = doneUserData;
    handshake->handle = tr_peerIoGetHandle( io );
    
    tr_peerIoSetIOMode( handshake->io, EV_READ|EV_WRITE, 0 );
    tr_peerIoSetIOFuncs( handshake->io, canRead, NULL, gotError, handshake );

    if( tr_peerIoIsIncoming( handshake->io ) )
        setReadState( handshake, AWAITING_HANDSHAKE );
    else
        sendYa( handshake );

    return handshake;
}

const struct in_addr *
tr_handshakeGetAddr( const struct tr_handshake * handshake, uint16_t * port )
{
    assert( handshake != NULL );
    assert( handshake->io != NULL );

    return tr_peerIoGetAddress( handshake->io, port );
}
