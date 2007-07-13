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

static int
makeCommonPex( tr_torrent_t * tor, tr_peer_t * peer,
               int ( *peerfunc )( tr_peertree_t *, benc_val_t * ),
               const char * extrakey, benc_val_t * extraval,
               char ** retbuf, int * retlen )
{
    tr_peertree_t       * sent, added, common;
    int                   ii;
    tr_peer_t           * pp;
    tr_peertree_entry_t * found;
    benc_val_t            val, * addval, * delval, * extra;

    *retbuf = NULL;
    *retlen = 0;
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
            return 1;
        }
    }

    /* check if there were any added or deleted peers */
    if( peertreeEmpty( &added ) && peertreeEmpty( sent ) )
    {
        peertreeMerge( sent, &common );
        peertreeFree( &added );
        tr_bencFree( extraval );
        return 0;
    }

    /* build the dictionaries */
    tr_bencInit( &val, TYPE_DICT );
    if( tr_bencDictReserve( &val, 3 ) )
    {
        tr_bencFree( &val );
        peertreeMerge( sent, &common );
        peertreeFree( &added );
        tr_bencFree( extraval );
        return 1;
    }
    extra  = tr_bencDictAdd( &val, extrakey );
    addval = tr_bencDictAdd( &val, "added" );
    delval = tr_bencDictAdd( &val, "dropped" );
    assert( NULL != extra && NULL != addval && NULL != delval );
    if( (*peerfunc)( &added, addval ) || (*peerfunc)( sent, delval ) )
    {
        tr_bencFree( &val );
        peertreeMerge( sent, &common );
        peertreeFree( &added );
        tr_bencFree( extraval );
        return 1;
    }
    *extra = *extraval;
    memset( extraval, 0, sizeof( extraval ) );

    /* bencode it */
    *retbuf = tr_bencSaveMalloc( &val, retlen );
    tr_bencFree( &val );
    if( NULL == *retbuf )
    {
        peertreeMerge( sent, &common );
        peertreeFree( &added );
        return 1;
    }

    peertreeSwap( sent, &common );
    peertreeMerge( sent, &added );
    peertreeFree( &common );

    return 0;
}

static char *
makeExtendedHandshake( tr_torrent_t * tor, tr_peer_t * peer, int * len )
{
    benc_val_t val, * msgsval;
    char * buf;
    char * vers = tr_strdup( TR_NAME " " SHORT_VERSION_STRING );

    /* reserve space in toplevel dictionary for v, m, and possibly p */
    tr_bencInit( &val, TYPE_DICT );
    if( tr_bencDictReserve( &val, ( 0 < tor->publicPort ? 3 : 2 ) ) )
    {
        tr_bencFree( &val );
        return NULL;
    }

    /* human readable version string */
    tr_bencInitStr( tr_bencDictAdd( &val, "v" ), vers, 0, 0 );

    /* create dict of supported extended messages */
    msgsval  = tr_bencDictAdd( &val, "m" );
    tr_bencInit( msgsval, TYPE_DICT );
    if( tr_bencDictReserve( msgsval, 1 ) )
    {
        tr_bencFree( &val );
        return NULL;
    }
    /* for public torrents advertise utorrent pex message */
    tr_bencInitInt( tr_bencDictAdd( msgsval, "ut_pex" ),
                    ( peer->private ? 0 : EXTENDED_PEX_ID ) );

    /* our listening port */
    if( 0 < tor->publicPort )
    {
        /* XXX should inform peer when we no longer have a valid port */
        tr_bencInitInt( tr_bencDictAdd( &val, "p" ), tor->publicPort );
    }

    /* bencode it */
    buf = tr_bencSaveMalloc( &val, len );
    tr_bencFree( &val );
    if( NULL == buf )
    {
        return NULL;
    }

    peer->advertisedPort = tor->publicPort;

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

static int
makeUTPex( tr_torrent_t * tor, tr_peer_t * peer, char ** buf, int * len )
{
    benc_val_t val;

    assert( !peer->private );
    tr_bencInitStr( &val, NULL, 0, 1 );
    return makeCommonPex( tor, peer, peertreeToBencUT, "added.f", &val,
                          buf, len );
}

static int
parseExtendedHandshake( tr_peer_t * peer, uint8_t * buf, int len )
{
    benc_val_t val, * sub;
    int        dbgport, dbgpex;

    if( tr_bencLoad( buf, len, &val, NULL ) )
    {
        peer_dbg( "GET  extended-handshake, invalid bencoding" );
        return TR_ERROR;
    }
    if( TYPE_DICT != val.type )
    {
        peer_dbg( "GET  extended-handshake, not a dictionary" );
        tr_bencFree( &val );
        return TR_ERROR;
    }

    /* check supported messages for utorrent pex */
    sub = tr_bencDictFind( &val, "m" );
    dbgpex = -1;
    if( NULL != sub && TYPE_DICT == sub->type )
    {
        sub = tr_bencDictFind( sub, "ut_pex" );
        if( NULL != sub && TYPE_INT == sub->type )
        {
            peer->pexStatus = 0;
            if( 0x0 < sub->val.i && 0xff >= sub->val.i )
            {
                peer->pexStatus = sub->val.i;
                dbgpex = sub->val.i;
            }
        }
    }

#if 0 /* ugh, we have to deal with encoding if we do this */
    /* get peer's client name */
    sub = tr_bencDictFind( &val, "v" );
    if( NULL != sub && TYPE_STR == sub->type &&
        ( NULL == peer->client || 0 != strcmp( sub->val.s.s, peer->client ) ) )
    {
        client = tr_bencStealStr( sub );
        if( NULL != client )
        {
            free( peer->client );
            peer->client = client;
        }
    }
#endif

    /* get peer's listening port */
    sub = tr_bencDictFind( &val, "p" );
    dbgport = -1;
    if( NULL != sub && TYPE_INT == sub->type &&
        0x0 < sub->val.i && 0xffff >= sub->val.i )
    {
        peer->port = htons( (uint16_t) sub->val.i );
        dbgport = sub->val.i;
    }

    peer_dbg( "GET  extended-handshake, ok port=%i pex=%i", dbgport, dbgpex );

    tr_bencFree( &val );
    return TR_OK;
}

static int
parseUTPex( tr_torrent_t * tor, tr_peer_t * peer, uint8_t * buf, int len )
{
    benc_val_t val, * sub;
    int used;

    if( peer->private || PEX_PEER_CUTOFF <= tor->peerCount )
    {
        peer_dbg( "GET  extended-pex, ignoring p=%i c=(%i<=%i)",
                  peer->private, PEX_PEER_CUTOFF, tor->peerCount );
        return TR_OK;
    }

    if( tr_bencLoad( buf, len, &val, NULL ) )
    {
        peer_dbg( "GET  extended-pex, invalid bencoding" );
        return TR_ERROR;
    }
    if( TYPE_DICT != val.type )
    {
        tr_bencFree( &val );
        peer_dbg( "GET  extended-pex, not a dictionary" );
        return TR_ERROR;
    }

    sub = tr_bencDictFind( &val, "added" );
    if( NULL != sub && TYPE_STR == sub->type && 0 == sub->val.s.i % 6 )
    {
        used = tr_torrentAddCompact( tor, TR_PEER_FROM_PEX,
                                     ( uint8_t * )sub->val.s.s,
                                     sub->val.s.i / 6 );
        peer_dbg( "GET  extended-pex, got %i peers, used %i",
                  sub->val.s.i / 6, used );
    }
    else
    {
        peer_dbg( "GET  extended-pex, no peers" );
    }

    tr_bencFree( &val );
    return TR_OK;
}
