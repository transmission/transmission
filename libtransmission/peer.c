/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "transmission.h"
#include "bencode.h"
#include "clients.h" /* for tr_clientForId() */
#include "peertree.h"
#include "list.h"
#include "net.h"

/*****
******
*****/

/**
*** The "SWIFT" system is described by Karthik Tamilmani,
*** Vinay Pai, and Alexander Mohr of Stony Brook University
*** in their paper "SWIFT: A System With Incentives For Trading"
*** http://citeseer.ist.psu.edu/tamilmani04swift.html
**/

/**
 * Use SWIFT?
 */
static const int SWIFT_ENABLED = 1;

/**
 * For every byte the peer uploads to us,
 * allow them to download this many bytes from us
 */
static const double SWIFT_REPAYMENT_RATIO = 1.33;

/**
 * Allow new peers to download this many bytes from
 * us when getting started.  This can prevent gridlock
 * with other peers using tit-for-tat algorithms
 */
static const int SWIFT_INITIAL_CREDIT = 64 * 1024; /* 64 KiB */

/**
 * We expend a fraction of our torrent's total upload speed
 * on largesse by uniformly distributing free credit to
 * all of our peers.  This too helps prevent gridlock.
 */
static const double SWIFT_LARGESSE = 0.10; /* 10% of our UL */

/**
 * How frequently to extend largesse-based credit
 */
static const int SWIFT_REFRESH_INTERVAL_SEC = 5;

/*****
******
*****/

#define PERCENT_PEER_WANTED     25      /* Percent before we start relax peers min activeness */
#define MIN_UPLOAD_IDLE         60000   /* In high peer situations we wait only 1 min
                                            until dropping peers for idling */
#define MAX_UPLOAD_IDLE         240000  /* In low peer situations we wait the
                                            4 mins until dropping peers for idling */
#define MIN_KEEP_ALIVE          180000  /* In high peer situations we wait only 3 min
                                            without a keep-alive */
#define MAX_KEEP_ALIVE          360000  /* In low peer situations we wait the
                                            6 mins without a keep-alive */
#define MIN_CON_TIMEOUT         8000    /* Time to timeout connecting to peer,
                                            during low peer situations */
#define MAX_CON_TIMEOUT         30000   /* Time to timeout connecting to peer, 
                                            during high peer situations */
#define PEX_PEER_CUTOFF         50 /* only try to add new peers from pex if
                                      we have fewer existing peers than this */
#define PEX_INTERVAL            60 /* don't send pex messages more frequently
                                      than PEX_INTERVAL +
                                      rand( PEX_INTERVAL / 10 ) seconds */
#define PEER_SUPPORTS_EXTENDED_MESSAGES( bits ) ( (bits)[5] & 0x10 )
#define PEER_SUPPORTS_AZUREUS_PROTOCOL( bits )  ( (bits)[0] & 0x80 )

#define PEER_MSG_CHOKE          0
#define PEER_MSG_UNCHOKE        1
#define PEER_MSG_INTERESTED     2
#define PEER_MSG_UNINTERESTED   3
#define PEER_MSG_HAVE           4
#define PEER_MSG_BITFIELD       5
#define PEER_MSG_REQUEST        6
#define PEER_MSG_PIECE          7
#define PEER_MSG_CANCEL         8
#define PEER_MSG_PORT           9
#define PEER_MSG_EXTENDED       20

typedef struct tr_request_s
{
    int index;
    int begin;
    int length;

} tr_request_t;

struct tr_peer_s
{
    tr_torrent_t      * tor;

    struct in_addr      addr;
    in_port_t           port;  /* peer's listening port, 0 if not known */

#define PEER_STATUS_IDLE        1 /* Need to connect */
#define PEER_STATUS_CONNECTING  2 /* Trying to send handshake */
#define PEER_STATUS_HANDSHAKE   3 /* Waiting for peer's handshake */
#define PEER_STATUS_AZ_GIVER    4 /* Sending Azureus handshake */
#define PEER_STATUS_AZ_RECEIVER 5 /* Receiving Azureus handshake */
#define PEER_STATUS_CONNECTED   6 /* Got peer's handshake */
    int                 status;
    int                 socket;
    char                from;
    char                private;
    char                azproto;  /* azureus peer protocol is being used */
    uint64_t            date;
    uint64_t            keepAlive;

#define EXTENDED_NOT_SUPPORTED   0 /* extended messages not supported */
#define EXTENDED_SUPPORTED       1 /* waiting to send extended handshake */
#define EXTENDED_HANDSHAKE       2 /* sent extended handshake */
    uint8_t             extStatus;
    uint8_t             pexStatus;   /* peer's ut_pex id, 0 if not supported */
    uint64_t            lastPex;     /* time when last pex packet was sent */
    int                 advertisedPort; /* listening port we last told peer */
    tr_peertree_t       sentPeers;

    char                amChoking;
    char                amInterested;
    char                peerChoking;
    char                peerInterested;

    int                 optimistic;
    int                 timesChoked;
    uint64_t            lastChoke;

    uint8_t             id[TR_ID_LEN];

    /* The pieces that the peer has */
    tr_bitfield_t     * bitfield;

    /* blocks we've requested from this peer */
    tr_bitfield_t     * reqfield;
    int                 pieceCount;
    float               progress;

    int                 goodPcs;
    int                 badPcs;
    int                 banned;
    /* The pieces that the peer is contributing to */
    tr_bitfield_t     * blamefield;
    /* The bad pieces that the peer has contributed to */
    tr_bitfield_t     * banfield;

    uint8_t           * buf;
    int                 size;
    int                 pos;

    uint8_t           * outMessages;
    int                 outMessagesSize;
    int                 outMessagesPos;
    uint8_t             outBlock[25+16384];
    int                 outBlockSize;
    int                 outBlockLoaded;
    int                 outBlockSending;

    int                 inRequestCount;
    int                 inRequestMax;
    int                 inRequestAlloc;
    tr_request_t      * inRequests;
    int                 inIndex;
    int                 inBegin;
    int                 inLength;

    tr_list_t         * outRequests;
    uint64_t            outDate;

    tr_ratecontrol_t  * download;
    tr_ratecontrol_t  * upload;

    char              * client;

    int64_t             credit;
};

#define peer_dbg( a... ) __peer_dbg( peer, ## a )
static void __peer_dbg( tr_peer_t * peer, char * msg, ... ) PRINTF( 2, 3 );

static void __peer_dbg( tr_peer_t * peer, char * msg, ... )
{
    char    string[256];
    va_list args;

    va_start( args, msg );
    snprintf( string, sizeof string, "%08x:%04x ",
             (uint32_t) peer->addr.s_addr, peer->port );
    vsnprintf( &string[14], sizeof( string ) - 14, msg, args );
    va_end( args ); 

    tr_dbg( "%s", string );
}

#include "peerext.h"
#include "peeraz.h"
#include "peermessages.h"
#include "peerutils.h"
#include "peerparse.h"

/***********************************************************************
 * tr_peerInit
 ***********************************************************************
 * Initializes a new peer.
 **********************************************************************/
tr_peer_t * tr_peerInit( struct in_addr addr, in_port_t port, int s, int from )
{
    tr_peer_t * peer;

    assert( 0 <= from && TR_PEER_FROM__MAX > from );

    peer              = tr_new0( tr_peer_t, 1 );
    peertreeInit( &peer->sentPeers );
    peer->amChoking   = TRUE;
    peer->peerChoking = TRUE;
    peer->date        = tr_date();
    peer->keepAlive   = peer->date;
    peer->download    = tr_rcInit();
    peer->upload      = tr_rcInit();

    peer->inRequestMax = peer->inRequestAlloc = 2;
    peer->inRequests = tr_new0( tr_request_t, peer->inRequestAlloc );

    peer->socket = s;
    peer->addr = addr;
    peer->port = port;
    peer->from = from;
    peer->credit = SWIFT_INITIAL_CREDIT;
    if( s >= 0 )
    {
        assert( TR_PEER_FROM_INCOMING == from );
        peer->status = PEER_STATUS_CONNECTING;
    }
    else
    {
        peer->status = PEER_STATUS_IDLE;
    }

    return peer;
}

void tr_peerDestroy( tr_peer_t * peer )
{
    tr_torrent_t * tor = peer->tor;
    tr_request_t * r;
    int i, block;

    peertreeFree( &peer->sentPeers );
    for( i = 0; i < peer->inRequestCount; i++ )
    {
        r = &peer->inRequests[i];
        block = tr_block( r->index, r->begin );
        tr_cpDownloaderRem( tor->completion, block );
    }
    tr_bitfieldFree( peer->bitfield );
    tr_bitfieldFree( peer->blamefield );
    tr_bitfieldFree( peer->banfield );
    tr_bitfieldFree( peer->reqfield );
    tr_list_foreach( peer->outRequests, tr_free );
    tr_list_free( peer->outRequests );
    tr_free( peer->inRequests );
    tr_free( peer->buf );
    tr_free( peer->outMessages );
    if( peer->status > PEER_STATUS_IDLE )
    {
        tr_netClose( peer->socket );
    }
    tr_rcClose( peer->download );
    tr_rcClose( peer->upload );
    free( peer->client );
    free( peer );
}

const char *
tr_peerClient( tr_peer_t * peer )
{
    if( PEER_STATUS_HANDSHAKE >= peer->status )
    {
        return "not connected";
    }

    if( NULL == peer->client )
    {
        peer->client = tr_clientForId( peer->id );
    }

    return peer->client;
}

void tr_peerSetPrivate( tr_peer_t * peer, int private )
{
    if( peer->private == private )
    {
        return;
    }

    peer->private = private;

    if( !private )
    {
        peer->lastPex = 0;
    }

    if( EXTENDED_HANDSHAKE == peer->extStatus )
    {
        sendExtended( peer->tor, peer, EXTENDED_HANDSHAKE_ID );
    }
}

void tr_peerSetTorrent( tr_peer_t * peer, tr_torrent_t * tor )
{
    peer->tor = tor;
}

/***********************************************************************
 * tr_peerRead
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerRead( tr_peer_t * peer )
{
    tr_torrent_t * tor = peer->tor;
    int ret;
    uint64_t date;

    /* Try to read */
    for( ;; )
    {
        if( tor )
        {
            if( tor->customDownloadLimit
                ? !tr_rcCanTransfer( tor->download )
                : !tr_rcCanTransfer( tor->handle->download ) )
            {
                break;
            }
        }

        if( peer->size < 1 )
        {
            peer->size = 1024;
            peer->buf  = malloc( peer->size );
        }
        else if( peer->pos >= peer->size )
        {
            peer->size *= 2;
            peer->buf   = realloc( peer->buf, peer->size );
        }

        /* Read in smallish chunks, otherwise we might read more
         * than the download cap is supposed to allow us */
        ret = tr_netRecv( peer->socket, &peer->buf[peer->pos],
                          MIN( 1024, peer->size - peer->pos ) ); 

        if( ret & TR_NET_CLOSE )
        {
            peer_dbg( "connection closed" );
            return TR_ERROR;
        }
        else if( ret & TR_NET_BLOCK )
        {
            break;
        }
        date        = tr_date();
        peer->date  = date;
        peer->pos  += ret;
        if( NULL != tor )
        {
            if( tr_peerAmInterested( peer ) && !tr_peerIsChoking( peer ) )
            {
                tor->activityDate = date;
            }
            
            if( ( ret = parseBuf( tor, peer ) ) )
            {
                return ret;
            }
        }
        else
        {
            if( ( ret = parseBufHeader( peer ) ) )
            {
                return ret;
            }
        }
    }

    return TR_OK;
}

uint64_t tr_peerDate( const tr_peer_t * peer )
{
    return peer->date;
}

/***********************************************************************
 * tr_peerAddress
 ***********************************************************************
 * 
 **********************************************************************/
struct in_addr * tr_peerAddress( tr_peer_t * peer )
{
    return &peer->addr;
}

/***********************************************************************
 * tr_peerHash
 ***********************************************************************
 *
 **********************************************************************/
const uint8_t * tr_peerHash( const tr_peer_t * peer )
{
    return parseBufHash( peer );
}

/***********************************************************************
 * tr_peerPulse
 ***********************************************************************
 *
 **********************************************************************/
int tr_peerPulse( tr_peer_t * peer )
{
    tr_torrent_t * tor = peer->tor;
    int ret, size;
    uint8_t * p;
    uint64_t date;
    const int isSeeding = tr_cpGetStatus( tor->completion ) != TR_CP_INCOMPLETE;

    if( ( ret = checkPeer( peer ) ) )
    {
        return ret;
    }

    /* Connect */
    if( PEER_STATUS_IDLE == peer->status )
    {
        peer->socket = tr_netOpenTCP( peer->addr, peer->port, 0 );
        if( peer->socket < 0 )
        {
            return TR_ERROR;
        }
        peer->status = PEER_STATUS_CONNECTING;
    }
    
    /* Disconnect if seeder and torrent is seeding */
    if(   ( peer->progress >= 1.0 )
       && ( peer->tor->cpStatus != TR_CP_INCOMPLETE ) )
    {
        return TR_ERROR;
    }

    /* Try to send handshake */
    if( PEER_STATUS_CONNECTING == peer->status )
    {
        uint8_t buf[68];
        tr_info_t * inf = &tor->info;

        buf[0] = 19;
        memcpy( &buf[1], "BitTorrent protocol", 19 );
        memset( &buf[20], 0, 8 );
        buf[20] = 0x80;         /* azureus protocol */
        buf[25] = 0x10;         /* extended messages */
        memcpy( &buf[28], inf->hash, 20 );
        memcpy( &buf[48], tor->id, 20 );

        switch( tr_netSend( peer->socket, buf, 68 ) )
        {
            case 68:
                peer_dbg( "SEND handshake" );
                peer->status = PEER_STATUS_HANDSHAKE;
                break;
            case TR_NET_BLOCK:
                break;
            default:
                peer_dbg( "connection closed" );
                return TR_ERROR;
        }
    }
    if( peer->status < PEER_STATUS_HANDSHAKE )
    {
        /* Nothing more we can do till we sent the handshake */
        return TR_OK;
    }

    /* Read incoming messages */
    if( ( ret = tr_peerRead( peer ) ) )
    {
        return ret;
    }

    /* Try to send Azureus handshake */
    if( PEER_STATUS_AZ_GIVER == peer->status )
    {
        switch( sendAZHandshake( tor, peer ) )
        {
            case TR_NET_BLOCK:
                break;
            case TR_NET_CLOSE:
                peer_dbg( "connection closed" );
                return TR_ERROR;
            default:
                peer->status = PEER_STATUS_AZ_RECEIVER;
                break;
        }
    }

    if( peer->status < PEER_STATUS_CONNECTED )
    {
        /* Nothing more we can do till we got the other guy's handshake */
        return TR_OK;
    }

    /* Try to write */
writeBegin:

    /* Send all smaller messages regardless of the upload cap */
    while( ( p = messagesPending( peer, &size ) ) )
    {
        ret = tr_netSend( peer->socket, p, size );
        if( ret & TR_NET_CLOSE )
        {
            return TR_ERROR;
        }
        else if( ret & TR_NET_BLOCK )
        {
            goto writeEnd;
        }
        messagesSent( peer, ret );
    }

    /* Send pieces if we can */
    while( ( p = blockPending( tor, peer, &size ) ) )
    {
        if( SWIFT_ENABLED && !isSeeding && (peer->credit<0) )
            break;

        if( tor->customUploadLimit
            ? !tr_rcCanTransfer( tor->upload )
            : !tr_rcCanTransfer( tor->handle->upload ) )
            break;

        ret = tr_netSend( peer->socket, p, size );
        if( ret & TR_NET_CLOSE )
            return TR_ERROR;

        if( ret & TR_NET_BLOCK )
            break;

        blockSent( peer, ret );

        if( ret > 0 )
            tr_peerGotBlockFromUs( peer, ret );

        date              = tr_date();
        peer->outDate     = date;
        
        if( !tr_peerAmChoking( peer ) )
            tor->activityDate = date;

        /* In case this block is done, you may have messages
           pending. Send them before we start the next block */
        goto writeBegin;
    }
writeEnd:

    /* Ask for a block whenever possible */
    if( !isSeeding
        && !peer->amInterested
        && tor->peerCount > TR_MAX_PEER_COUNT - 2 )
    {
        /* This peer is no use to us, and it seems there are
           more */
        peer_dbg( "not interesting" );
        return TR_ERROR;
    }

    if(     peer->amInterested
        && !peer->peerChoking
        && !peer->banned
        &&  peer->inRequestCount < peer->inRequestMax )
    {
        int i;
        int poolSize = 0;
        int * pool = getPreferredPieces ( tor, peer, &poolSize );
        const int endgame = !poolSize;

        if( endgame ) /* endgame -- request everything we don't already have */
        {
            for( i=0; i<tor->blockCount && peer->inRequestCount<peer->inRequestMax; ++i )
            {
                if( !isBlockInteresting( tor, peer, i ) )
                    continue;
                if( tr_bitfieldHas( peer->reqfield, i ) ) /* we've already asked them for it */
                    continue;
                if( !peer->reqfield )
                    peer->reqfield = tr_bitfieldNew( tor->blockCount );
                tr_bitfieldAdd( peer->reqfield, i );
                sendRequest( tor, peer, i );
            }
        }
        else for( i=0; i<poolSize && peer->inRequestCount<peer->inRequestMax;  )
        {
            int unused;
            const int piece = pool[i];
            const int block = endgame
                ? tr_cpMostMissingBlockInPiece( tor->completion, piece, &unused)
                : tr_cpMissingBlockInPiece ( tor->completion, piece );

            if( block>=0 )
                sendRequest( tor, peer, block );
            else ++i;
        }

        tr_free( pool );
    }

    return TR_OK;
}

int tr_peerIsConnected( const tr_peer_t * peer )
{
    return peer && (peer->status == PEER_STATUS_CONNECTED);
}

int tr_peerIsFrom( const tr_peer_t * peer )
{
    return peer->from;
}

int tr_peerAmChoking( const tr_peer_t * peer )
{
    return peer->amChoking;
}
int tr_peerAmInterested( const tr_peer_t * peer )
{
    return peer->amInterested;
}
int tr_peerIsChoking( const tr_peer_t * peer )
{
    return peer->peerChoking;
}
int tr_peerIsInterested( const tr_peer_t * peer )
{
    return peer->peerInterested;
}

float tr_peerProgress( const tr_peer_t * peer )
{
    return peer->progress;
}

int tr_peerPort( const tr_peer_t * peer )
{
    return ntohs( peer->port );
}

int tr_peerHasPiece( const tr_peer_t * peer, int pieceIndex )
{
    return tr_bitfieldHas( peer->bitfield, pieceIndex );
}

float tr_peerDownloadRate( const tr_peer_t * peer )
{
    return tr_rcRate( peer->download );
}

float tr_peerUploadRate( const tr_peer_t * peer )
{
    return tr_rcRate( peer->upload );
}

int tr_peerTimesChoked( const tr_peer_t * peer )
{
    return peer->timesChoked;
}

void tr_peerChoke( tr_peer_t * peer )
{
    sendChoke( peer, 1 );
    peer->lastChoke = tr_date();
    ++peer->timesChoked;
}

void tr_peerUnchoke( tr_peer_t * peer )
{
    sendChoke( peer, 0 );
    peer->lastChoke = tr_date();
}

uint64_t tr_peerLastChoke( const tr_peer_t * peer )
{
    return peer->lastChoke;
}

void tr_peerSetOptimistic( tr_peer_t * peer, int o )
{
    peer->optimistic = o;
}

int tr_peerIsOptimistic( const tr_peer_t * peer )
{
    return peer->optimistic;
}

static int peerIsBad( const tr_peer_t * peer )
{
    return peer->badPcs > 4 + 2 * peer->goodPcs;
}

static int peerIsGood( const tr_peer_t * peer )
{
    return peer->goodPcs > 3 * peer->badPcs;
}

void tr_peerBlame( tr_peer_t * peer, int piece, int success )
{
    tr_torrent_t * tor = peer->tor;

    if( !peer->blamefield || !tr_bitfieldHas( peer->blamefield, piece ) )
    {
        return;
    }

    if( success )
    {
        peer->goodPcs++;

        if( peer->banfield && peerIsGood( peer ) )
        {
            /* Assume the peer wasn't responsible for the bad pieces
               we was banned for */
            tr_bitfieldClear( peer->banfield );
        }
    }
    else
    {
        peer->badPcs++;

        /* Ban the peer for this piece */
        if( !peer->banfield )
        {
            peer->banfield = tr_bitfieldNew( tor->info.pieceCount );
        }
        tr_bitfieldAdd( peer->banfield, piece );

        if( peerIsBad( peer ) )
        {
            /* Full ban */
            peer_dbg( "banned (%d / %d)", peer->goodPcs, peer->badPcs );
            peer->banned = 1;
            peer->peerInterested = 0;
        }
    }
    tr_bitfieldRem( peer->blamefield, piece );
}

int tr_peerGetConnectable( const tr_torrent_t * tor, uint8_t ** _buf )
{
    int count = 0;
    uint8_t * buf;
    tr_peer_t * peer;
    int i;

    if( tor->peerCount < 1 )
    {
        *_buf = NULL;
        return 0;
    }

    buf = malloc( 6 * tor->peerCount );
    for( i = 0; i < tor->peerCount; i++ )
    {
        peer = tor->peers[i];

        /* Skip peers with no known listening port */
        if( 0 == peer->port )
            continue;

        memcpy( &buf[count*6], &peer->addr, 4 );
        memcpy( &buf[count*6+4], &peer->port, 2 );
        count++;
    }

    if( count < 1 )
    {
        free( buf ); buf = NULL;
    }
    *_buf = buf;

    return count * 6;
}

/***
****
***/

void
tr_peerSentBlockToUs ( tr_peer_t * peer, int byteCount )
{
    tr_torrent_t * tor = peer->tor;

    assert( byteCount >= 0 );

    tor->downloadedCur += byteCount;
    tr_rcTransferred( peer->download, byteCount );
    tr_rcTransferred( tor->download, byteCount );
    if ( !tor->customUploadLimit )
        tr_rcTransferred( tor->handle->download, byteCount );

    peer->credit += (int)(byteCount * SWIFT_REPAYMENT_RATIO);
}

void
tr_peerGotBlockFromUs ( tr_peer_t * peer, int byteCount )
{
    tr_torrent_t * tor = peer->tor;

    assert( byteCount >= 0 );

    tor->uploadedCur += byteCount;
    tr_rcTransferred( peer->upload, byteCount );
    tr_rcTransferred( tor->upload, byteCount );
    if ( !tor->customDownloadLimit )
        tr_rcTransferred( tor->handle->upload, byteCount );

    peer->credit -= byteCount;
}

static void
tr_torrentSwiftPulse ( tr_torrent_t * tor )
{
    /* Preferred # of seconds for the request queue's turnaround time.
       This is just an arbitrary number. */
    const int queueTimeSec = 5;
    const int blockSizeKiB = tor->blockSize / 1024;
    const int isSeeding = tr_cpGetStatus( tor->completion ) != TR_CP_INCOMPLETE;
    int i;

    tr_torrentWriterLock( tor );

    for( i=0; i<tor->peerCount; ++i )
    {
        double outboundSpeedKiBs;
        int size;
        tr_peer_t * peer = tor->peers[ i ];

        if( !tr_peerIsConnected( peer ) )
            continue;

        /* decide how many blocks we'll concurrently ask this peer for */
        outboundSpeedKiBs = tr_rcRate(peer->upload);
        size = queueTimeSec * outboundSpeedKiBs / blockSizeKiB;
        if( size < 4 ) /* don't let it get TOO small */
            size = 4;
        size += 4; /* and always leave room to grow */
        peer->inRequestMax = size;
        if( peer->inRequestAlloc < peer->inRequestMax ) {
            peer->inRequestAlloc = peer->inRequestMax;
            peer->inRequests = tr_renew( tr_request_t, peer->inRequests, peer->inRequestAlloc );
        }
    }

    /* if we're not seeding, decide on how much
       bandwidth to allocate for leechers */
    if( !isSeeding )
    {
        tr_peer_t ** deadbeats = tr_new( tr_peer_t*, tor->peerCount );
        int deadbeatCount = 0;

        for( i=0; i<tor->peerCount; ++i ) {
            tr_peer_t * peer = tor->peers[ i ];
            if( tr_peerIsConnected( peer ) && ( peer->credit < 0 ) )
                deadbeats[deadbeatCount++] = peer;
        }

        if( deadbeatCount )
        {
            const double ul_KiBsec = tr_rcRate( tor->download );
            const double ul_KiB = ul_KiBsec * SWIFT_REFRESH_INTERVAL_SEC;
            const double ul_bytes = ul_KiB * 1024;
            const double freeCreditTotal = ul_bytes * SWIFT_LARGESSE;
            const int freeCreditPerPeer = (int)( freeCreditTotal / deadbeatCount );
            for( i=0; i<deadbeatCount; ++i )
                deadbeats[i]->credit = freeCreditPerPeer;
            tr_dbg( "torrent %s has %d deadbeats, "
                    "who are each being granted %d bytes' credit "
                    "for a total of %.1f KiB, "
                    "%d%% of the torrent's ul speed %.1f\n", 
                tor->info.name, deadbeatCount, freeCreditPerPeer,
                ul_KiBsec*SWIFT_LARGESSE, (int)(SWIFT_LARGESSE*100), ul_KiBsec );
        }

        tr_free( deadbeats );
    }

    tr_torrentWriterUnlock( tor );
}
void
tr_swiftPulse( tr_handle_t * h )
{
    static time_t lastPulseTime = 0;

    if( lastPulseTime + SWIFT_REFRESH_INTERVAL_SEC <= time( NULL ) )
    {
        tr_torrent_t * tor;
        for( tor=h->torrentList; tor; tor=tor->next )
            tr_torrentSwiftPulse( tor );

        lastPulseTime = time( NULL );
    }
}
