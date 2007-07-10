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

#define AZ_EXT_VERSION          1

#define AZ_MSG_BT_HANDSHAKE     -1
#define AZ_MSG_BT_KEEP_ALIVE    -2
#define AZ_MSG_AZ_HANDSHAKE     -3
#define AZ_MSG_AZ_PEER_EXCHANGE -4
#define AZ_MSG_INVALID          -5

#define AZ_MSG_IS_OPTIONAL( id ) \
    ( AZ_MSG_AZ_PEER_EXCHANGE == (id) || AZ_MSG_IS_UNUSED( id ) )
#define AZ_MSG_IS_UNUSED( id ) \
    ( AZ_MSG_AZ_HANDSHAKE == (id) || AZ_MSG_BT_HANDSHAKE == (id) )

static const struct
{
    char    * label;
    const int len;
    const int id;
}
az_msgs[] = {
    { "AZ_PEER_EXCHANGE", 16, AZ_MSG_AZ_PEER_EXCHANGE },
    { "AZ_HANDSHAKE",     12, AZ_MSG_AZ_HANDSHAKE },
    { "BT_HANDSHAKE",     12, AZ_MSG_BT_HANDSHAKE },
    { "BT_KEEP_ALIVE",    13, AZ_MSG_BT_KEEP_ALIVE },
    { "BT_CHOKE",          8, PEER_MSG_CHOKE },
    { "BT_UNCHOKE",       10, PEER_MSG_UNCHOKE },
    { "BT_INTERESTED",    13, PEER_MSG_INTERESTED },
    { "BT_UNINTERESTED",  15, PEER_MSG_UNINTERESTED },
    { "BT_HAVE",           7, PEER_MSG_HAVE },
    { "BT_BITFIELD",      11, PEER_MSG_BITFIELD },
    { "BT_REQUEST",       10, PEER_MSG_REQUEST },
    { "BT_PIECE",          8, PEER_MSG_PIECE },
    { "BT_CANCEL",         9, PEER_MSG_CANCEL },
};
#define azmsgStr( idx ) ( az_msgs[(idx)].label )
#define azmsgLen( idx ) ( az_msgs[(idx)].len )
#define azmsgId( idx )  ( az_msgs[(idx)].id )
#define azmsgCount()    ( (int)(sizeof( az_msgs ) / sizeof( az_msgs[0] ) ) )

static inline int
azmsgIdIndex( int id )
{
    int ii;

    for( ii = 0; azmsgCount() > ii; ii++ )
    {
        if( id == azmsgId( ii ) )
        {
            return ii;
        }
    }

    assert( 0 );

    return 0;
}

static inline int
azmsgNameIndex( const char * name, int len )
{
    int ii;

    for( ii = 0; azmsgCount() > ii; ii++ )
    {
        if( azmsgLen( ii ) == len &&
            0 == memcmp( azmsgStr( ii ), name, len ) )
        {
            return ii;
        }
    }

    return -1;
}

static uint8_t *
makeAZHandshake( tr_torrent_t * tor, tr_peer_t * peer, int * buflen )
{
    char       * buf;
    benc_val_t   val, * msgsval, * msgdictval;
    int          len, max, idx;
    uint8_t      vers;

    *buflen = 0;
    idx = azmsgIdIndex( AZ_MSG_AZ_HANDSHAKE );
    len = 4 + 4 + azmsgLen( idx ) + 1;
    buf = malloc( len );
    if( NULL == buf )
    {
        return NULL;
    }

    /* set length to zero for now, we won't know it until after bencoding */
    TR_HTONL( 0, buf );
    /* set name length, name, and version */
    TR_HTONL( azmsgLen( idx ), buf + 4 );
    memcpy( buf + 8, azmsgStr( idx ), azmsgLen( idx ) );
    buf[8 + azmsgLen( idx )] = AZ_EXT_VERSION;

    /* start building a dictionary for handshake data */
    tr_bencInit( &val, TYPE_DICT );
    if( tr_bencDictReserve( &val, 5 ) )
    {
        free( buf );
        tr_bencFree( &val );
        return NULL;
    }

    /* fill in the dictionary values */
    tr_bencInitStr( tr_bencDictAdd( &val, "identity" ),
                    tor->azId, TR_AZ_ID_LEN, 1 );
    tr_bencInitStr( tr_bencDictAdd( &val, "client" ),   TR_NAME, 0, 1 );
    tr_bencInitStr( tr_bencDictAdd( &val, "version" ),  SHORT_VERSION_STRING, 0, 1 );
    if( 0 < tor->publicPort )
    {
        tr_bencInitInt( tr_bencDictAdd( &val, "tcp_port" ), tor->publicPort );
    }

    /* initialize supported message list */
    msgsval = tr_bencDictAdd( &val, "messages" );
    tr_bencInit( msgsval, TYPE_LIST );
    if( tr_bencListReserve( msgsval, azmsgCount() ) )
    {
        tr_bencFree( &val );
        free( buf );
        return NULL;
    }

    /* fill in the message list */
    vers = AZ_EXT_VERSION;
    for( idx = 0; azmsgCount() > idx; idx++ )
    {
        if( AZ_MSG_IS_UNUSED( azmsgId( idx ) ) )
        {
            continue;
        }
        if( AZ_MSG_AZ_PEER_EXCHANGE == azmsgId( idx ) && peer->private )
        {
            /* no point in saying we can do pex if the torrent is private */
            continue;
        }
        /* each item in the list is a dict with id and ver keys */
        msgdictval = tr_bencListAdd( msgsval );
        tr_bencInit( msgdictval, TYPE_DICT );
        if( tr_bencDictReserve( msgdictval, 2 ) )
        {
            tr_bencFree( &val );
            free( buf );
            return NULL;
        }
        tr_bencInitStr( tr_bencDictAdd( msgdictval, "id" ),
                        azmsgStr( idx ), azmsgLen( idx ), 1 );
        tr_bencInitStr( tr_bencDictAdd( msgdictval, "ver" ), &vers, 1, 1 );
    }

    /* bencode the dictionary and append it to the buffer */
    max = len;
    if( tr_bencSave( &val, &buf, &len, &max ) )
    {
        tr_bencFree( &val );
        free( buf );
        return NULL;
    }

    tr_bencFree( &val );
    /* we know the length now, fill it in */
    TR_HTONL( len - 4, buf );

    /* XXX is there a way to tell azureus that the public port has changed? */
    peer->advertisedPort = tor->publicPort;

    *buflen = len;
    return ( uint8_t * )buf;
}

static int
peertreeToBencAZ( tr_peertree_t * tree, benc_val_t * val )
{
    int                   count;
    tr_peertree_entry_t * ii;

    tr_bencInit( val, TYPE_LIST );
    count = peertreeCount( tree );
    if( 0 == count )
    {
        return 0;
    }
    if( tr_bencListReserve( val, count ) )
    {
        return 1;
    }

    ii = peertreeFirst( tree );
    while( NULL != ii )
    {
        tr_bencInitStr( tr_bencListAdd( val ), ii->peer, 6, 1 );
        ii = peertreeNext( tree, ii );
    }

    return 0;
}

static int
makeAZPex( tr_torrent_t * tor, tr_peer_t * peer, char ** buf, int * len )
{
    benc_val_t val;

    assert( !peer->private );
    tr_bencInitStr( &val, tor->info.hash, sizeof( tor->info.hash ), 1 );
    return makeCommonPex( tor, peer, peertreeToBencAZ, "infohash", &val,
                          buf, len);
}

static int
sendAZHandshake( tr_torrent_t * tor, tr_peer_t * peer )
{
    uint8_t * buf;
    int len;

    /* XXX this is kind of evil to use this buffer like this */
    if( NULL == peer->outMessages )
    {
        buf = makeAZHandshake( tor, peer, &len );
        if( NULL == buf )
        {
            return TR_NET_CLOSE;
        }
        peer->outMessages     = buf;
        peer->outMessagesSize = len;
        peer->outMessagesPos  = 0;
    }

    len = tr_netSend( peer->socket, peer->outMessages + peer->outMessagesPos,
                      peer->outMessagesSize - peer->outMessagesPos );
    if( peer->outMessagesPos + len < peer->outMessagesSize )
    {
        peer->outMessagesPos += len;
        return TR_NET_BLOCK;
    }

    peer_dbg( "SEND azureus-handshake" );

    len = peer->outMessagesSize;
    free( peer->outMessages );
    peer->outMessages     = NULL;
    peer->outMessagesSize = 0;
    peer->outMessagesPos  = 0;

    return len;
}

static inline int
parseAZMessageHeader( tr_peer_t * peer, uint8_t * buf, int len,
                      int * msgidret, int * msglenret )
{
    uint8_t * name, vers;
    int       off, namelen, msglen, index, msgid;

    if( 8 > len )
    {
        return TR_NET_BLOCK;
    }
    /* message length */
    TR_NTOHL( buf, msglen );
    msglen += 4;
    off = 4;
    if( msglen > len )
    {
        return TR_NET_BLOCK;
    }
    if( 9 > msglen )
    {
        peer_dbg( "azureus peer message is too short to make sense" );
        return TR_NET_CLOSE;
    }
    /* name length */
    TR_NTOHL( buf + off, namelen );
    off += 4;
    if( off + namelen + 1 > msglen )
    {
        peer_dbg( "azureus peer message name is too long to make sense" );
        return TR_NET_CLOSE;
    }
    /* message name */
    name = buf + off;
    off += namelen;
    /* message version */
    vers = buf[off];
    off++;
    /* get payload length from message length */
    msglen -= off;

    index = azmsgNameIndex( ( char * )name, namelen );
    if( AZ_EXT_VERSION != vers )
    {
        /* XXX should we close the connection here? */
        peer_dbg( "GET  unsupported azureus message version %hhu", vers );
        msgid = AZ_MSG_INVALID;
    }
    else if( 0 > index )
    {
        name[namelen] = '\0';
        peer_dbg( "GET  unknown azureus message: \"%s\"", name );
        name[namelen] = vers;
        msgid = AZ_MSG_INVALID;
    }
    else
    {
        msgid = azmsgId( index );
    }

    *msgidret  = msgid;
    *msglenret = msglen;

    return off;
}

static inline int
parseAZHandshake( tr_peer_t * peer, uint8_t * buf, int len )
{
    benc_val_t      val, * sub, * dict, * subsub;
    tr_bitfield_t * msgs;
    int             ii, idx;

    if( tr_bencLoad( buf, len, &val, NULL ) )
    {
        peer_dbg( "GET  azureus-handshake, invalid bencoding" );
        return TR_ERROR;
    }

    if( TYPE_DICT != val.type )
    {
        peer_dbg( "GET  azureus-handshake, data not a dictionary" );
        tr_bencFree( &val );
        return TR_ERROR;
    }

#if 0 /* ugh, we have to deal with encoding if we do this */
    /* get peer's client name */
    sub  = tr_bencDictFind( &val, "client" );
    sub2 = tr_bencDictFind( &val, "version" );
    if( NULL != sub  && TYPE_STR == sub->type &&
        NULL != sub2 && TYPE_STR == sub->type )
    {
        if( NULL == peer->client ||
            ( 0 != strncmp( peer->client, sub->val.s.s, sub->val.s.i ) ||
              ' ' != peer->client[sub->val.s.i] ||
              0 != strcmp( peer->client + sub->val.s.i + 1, sub2->val.s.s ) ) )
        {
            client = NULL;
            asprintf( &client, "%s %s", sub->val.s.s, sub2->val.s.s );
            if( NULL != client )
            {
                free( peer->client );
                peer->client = client;
            }
        }
    }
#endif

    /* get the peer's listening port */
    sub = tr_bencDictFind( &val, "tcp_port" );
    if( NULL != sub )
    {
        if( TYPE_INT == sub->type && 0x0 < sub->val.i && 0xffff >= sub->val.i )
        {
            peer->port = htons( sub->val.i );
        }
    }

    /* find the supported message list */
    sub = tr_bencDictFind( &val, "messages" );
    if( NULL == sub && TYPE_LIST != sub->type )
    {
        tr_bencFree( &val );
        peer_dbg( "GET  azureus-handshake, missing 'messages'" );
        return TR_ERROR;
    }

    peer_dbg( "GET  azureus-handshake, ok" );

    /* fill bitmask with supported message info */
    msgs = tr_bitfieldNew( azmsgCount() );
    for( ii = 0; ii < sub->val.l.count; ii++ )
    {
        dict = &sub->val.l.vals[ii];
        if( TYPE_DICT != dict->type )
        {
            continue;
        }
        subsub = tr_bencDictFind( dict, "id" );
        if( NULL == subsub || TYPE_STR != subsub->type )
        {
            continue;
        }
        idx = azmsgNameIndex( subsub->val.s.s, subsub->val.s.i );
        if( 0 > idx )
        {
            continue;
        }
        subsub = tr_bencDictFind( dict, "ver" );
        if( NULL == subsub || TYPE_STR != subsub->type ||
            1 != subsub->val.s.i || AZ_EXT_VERSION > subsub->val.s.s[0] )
        {
            continue;
        }
        tr_bitfieldAdd( msgs, idx );
    }

    tr_bencFree( &val );

    /* check bitmask to see if we're missing any messages */
    for( ii = 0; azmsgCount() > ii; ii++ )
    {
        if( AZ_MSG_AZ_PEER_EXCHANGE == azmsgId( ii ) )
        {
            peer->pexStatus = tr_bitfieldHas( msgs, ii );
        }
        if( !AZ_MSG_IS_OPTIONAL( azmsgId( ii ) ) &&
            !tr_bitfieldHas( msgs, ii ) )
        {
            peer_dbg( "azureus message %s not supported by peer",
                      azmsgStr( ii ) );
            tr_bitfieldFree( msgs );
            return TR_ERROR;
        }
    }
    tr_bitfieldFree( msgs );

    return TR_OK;
}

static inline int
parseAZPex( tr_torrent_t * tor, tr_peer_t * peer, uint8_t * buf, int len )
{
    tr_info_t * info = &tor->info;
    benc_val_t  val, * list, * pair;
    int         ii, used;

    if( peer->private || PEX_PEER_CUTOFF <= tor->peerCount )
    {
        peer_dbg( "GET  azureus-pex, ignoring p=%i c=(%i<=%i)",
                  peer->private, PEX_PEER_CUTOFF, tor->peerCount );
        return TR_OK;
    }

    if( tr_bencLoad( buf, len, &val, NULL ) )
    {
        peer_dbg( "GET  azureus-pex, invalid bencoding" );
        return TR_ERROR;
    }
    if( TYPE_DICT != val.type )
    {
        tr_bencFree( &val );
        peer_dbg( "GET  azureus-pex, data not a dictionary" );
        return TR_ERROR;
    }

    list = tr_bencDictFind( &val, "infohash" );
    if( NULL == list || TYPE_STR != list->type ||
        sizeof( info->hash ) != list->val.s.i ||
        0 != memcmp( info->hash, list->val.s.s, sizeof( info->hash ) ) )
    {
        tr_bencFree( &val );
        peer_dbg( "GET  azureus-pex, bad infohash" );
        return TR_ERROR;
    }

    list = tr_bencDictFind( &val, "added" );
    if( NULL == list || TYPE_LIST != list->type )
    {
        peer_dbg( "GET  azureus-pex, no peers" );
        tr_bencFree( &val );
        return TR_OK;
    }

    used = 0;
    for( ii = 0; ii < list->val.l.count; ii++ )
    {
        pair = &list->val.l.vals[ii];
        if( TYPE_STR == pair->type && 6 == pair->val.s.i )
        {
            used += tr_torrentAddCompact( tor, TR_PEER_FROM_PEX,
                                          ( uint8_t * )pair->val.s.s, 1 );
        }
    }

    peer_dbg( "GET  azureus-pex, found %i peers, using %i",
              list->val.l.count, used );

    tr_bencFree( &val );

    return TR_OK;
}
