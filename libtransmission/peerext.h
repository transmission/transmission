/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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

#define EXTENDED_HANDSHAKE_ID   0
#define EXTENDED_PEX_ID         1

static char *
makeCommonPex( tr_torrent_t * tor, tr_peer_t * peer, int * len,
               int ( *peerfunc )( tr_peertree_t *, benc_val_t * ),
               const char * extrakey, benc_val_t * extraval )
{
    tr_peertree_t       * sent, added, common;
    int                   ii;
    tr_peer_t           * pp;
    tr_peertree_entry_t * found;
    benc_val_t            val, * addval, * delval, * extra;
    char                * buf;

    *len = 0;
    sent = &peer->sentPeers;
    peertreeInit( &added );
    peertreeInit( &common );

    /* build trees of peers added and deleted since the last pex */
    for( ii = 0; ii < tor->peerCount; ii++ )
    {
        pp = tor->peers[ii];
        if( 0 == pp->port || 0 == tr_addrcmp( &peer->addr, &pp->addr ) )
        {
            continue;
        }
        found = peertreeGet( sent, &pp->addr, pp->port );
        if( NULL != found )
        {
            peertreeMove( &common, sent, found );
        }
        else if( NULL == peertreeAdd( &added, &pp->addr, pp->port ) )
        {
            peertreeMerge( sent, &common );
            peertreeFree( &added );
            tr_bencFree( extraval );
            return NULL;
        }
    }

    /* build the dictionaries */
    tr_bencInit( &val, TYPE_DICT );
    if( ( peertreeEmpty( &added ) && peertreeEmpty( sent ) ) ||
        tr_bencDictAppendNofree( &val, extrakey, &extra, "added", &addval,
                                 "dropped", &delval, NULL ) ||
        (*peerfunc)( &added, addval ) ||
        (*peerfunc)( sent, delval ) )
    {
        tr_bencFree( &val );
        peertreeMerge( sent, &common );
        peertreeFree( &added );
        tr_bencFree( extraval );
        return NULL;
    }
    *extra = *extraval;
    memset( extraval, 0, sizeof( extraval ) );

    /* bencode it */
    buf = tr_bencSaveMalloc( &val, len );
    tr_bencFree( &val );
    if( NULL == buf )
    {
        peertreeMerge( sent, &common );
        peertreeFree( &added );
        return NULL;
    }

    peertreeSwap( sent, &common );
    peertreeMerge( sent, &added );
    peertreeFree( &common );

    return buf;
}

static char *
makeExtendedHandshake( tr_torrent_t * tor, tr_peer_t * peer, int * len )
{
    benc_val_t val, * msgsval, * portval, * versval, * pexval;
    char * buf, * vers;

    /* get human-readable version string */
    vers = NULL;
    asprintf( &vers, "%s %s", TR_NAME, VERSION_STRING );
    if( NULL == vers )
    {
        return NULL;
    }

    tr_bencInit( &val, TYPE_DICT );

    /* append v str and m dict to toplevel dictionary */
    if( tr_bencDictAppendNofree( &val, "v", &versval, "m", &msgsval, NULL ) )
    {
        free( vers );
        tr_bencFree( &val );
        return NULL;
    }

    /* human readable version string */
    tr_bencInitStr( versval, vers, 0, 0 );

    /* create dict of supported extended messages */
    tr_bencInit( msgsval, TYPE_DICT );
    if( tr_bencDictAppendNofree( msgsval, "ut_pex", &pexval, NULL ) )
    {
        tr_bencFree( &val );
        return NULL;
    }
    /* for public torrents advertise utorrent pex message */
    tr_bencInitInt( pexval, ( peer->private ? 0 : EXTENDED_PEX_ID ) );

    /* our listening port */
    if( 0 < tor->publicPort )
    {
        if( tr_bencDictAppendNofree( &val, "p", &portval, NULL ) )
        {
            tr_bencFree( &val );
            return NULL;
        }
        tr_bencInitInt( portval, tor->publicPort );
    }

    /* bencode it */
    buf = tr_bencSaveMalloc( &val, len );
    tr_bencFree( &val );

    if( NULL != buf )
    {
        peer->advertisedPort = tor->publicPort;
    }

    return buf;
}

static int
peertreeToBencUT( tr_peertree_t * tree, benc_val_t * val )
{
    char                * buf;
    tr_peertree_entry_t * ii;
    int                   count;

    count = peertreeCount( tree );
    if( 0 == count )
    {
        tr_bencInitStr( val, NULL, 0, 1 );
        return 0;
    }

    buf = malloc( 6 * count );
    if( NULL == buf )
    {
        return 1;
    }
    tr_bencInitStr( val, buf, 6 * count, 0 );

    for( ii = peertreeFirst( tree ); NULL != ii;
         ii = peertreeNext( tree, ii ) )
    {
        assert( 0 < count );
        count--;
        memcpy( buf + 6 * count, ii->peer, 6 );
    }
    assert( 0 == count );

    return 0;
}

static char *
makeUTPex( tr_torrent_t * tor, tr_peer_t * peer, int * len )
{
    benc_val_t val;
    char     * ret;

    assert( !peer->private );
    tr_bencInitStr( &val, NULL, 0, 1 );
    ret = makeCommonPex( tor, peer, len, peertreeToBencUT, "added.f", &val );

    return ret;
}

static inline int
parseExtendedHandshake( tr_peer_t * peer, uint8_t * buf, int len )
{
    benc_val_t     val, * sub;

    if( tr_bencLoad( buf, len, &val, NULL ) )
    {
        peer_dbg( "invalid bencoding in extended handshake" );
        return TR_ERROR;
    }
    if( TYPE_DICT != val.type )
    {
        peer_dbg( "extended handshake is not a dictionary" );
        tr_bencFree( &val );
        return TR_ERROR;
    }

    /* check supported messages for utorrent pex */
    sub = tr_bencDictFind( &val, "m" );
    if( NULL != sub && TYPE_DICT == sub->type )
    {
        sub = tr_bencDictFind( sub, "ut_pex" );
        if( NULL != sub && TYPE_INT == sub->type )
        {
            peer->pexStatus = 0;
            if( 0x0 < sub->val.i && 0xff >= sub->val.i )
            {
                peer->pexStatus = sub->val.i;
            }
        }
    }

    /* get peer's listening port */
    sub = tr_bencDictFind( &val, "p" );
    if( NULL != sub && TYPE_INT == sub->type &&
        0x0 < sub->val.i && 0xffff >= sub->val.i )
    {
        peer->port = htons( (uint16_t) sub->val.i );
        peer_dbg( "got listening port %i", ntohs( peer->port ) );
    }

    tr_bencFree( &val );
    return TR_OK;
}

static inline int
parseUTPex( tr_torrent_t * tor, tr_peer_t * peer, uint8_t * buf, int len )
{
    benc_val_t val, * sub;

    if( peer->private || PEX_PEER_CUTOFF <= tor->peerCount )
    {
        return TR_OK;
    }

    if( tr_bencLoad( buf, len, &val, NULL ) )
    {
        peer_dbg( "invalid bencoding in extended peer exchange" );
        return TR_ERROR;
    }
    if( TYPE_DICT != val.type )
    {
        tr_bencFree( &val );
        peer_dbg( "extended peer exchange is not a dictionary" );
        return TR_ERROR;
    }

    sub = tr_bencDictFind( &val, "added" );
    if( NULL != sub && TYPE_STR == sub->type && 0 == sub->val.s.i % 6 )
    {
        tr_torrentAddCompact( tor, TR_PEER_FROM_PEX,
                              ( uint8_t * )sub->val.s.s, sub->val.s.i / 6 );
    }

    return TR_OK;
}
