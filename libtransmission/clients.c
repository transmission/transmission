/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
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

#include <ctype.h> /* isprint */
#include <stdio.h>
#include <stdlib.h> /* bsearch */
#include <string.h>

#include <sys/types.h> /* event.h needs this */
#include <event.h>

#include "transmission.h"
#include "trcompat.h"
#include "utils.h" /* tr_strdup */

static int charToInt( char character )
{
    int value;
    if( character >= 'A' && character <= 'Z' )
        value = 10 + character - 'A';
    else if( character >= 'a' && character <= 'z')
        value = 36 + character - 'a';
    else
        value = character - '0';
    
    return value;
}

/*
 * Azureus-style uses the following encoding: '-', two characters for client id,
 * four ascii digits for version number, '-', followed by random numbers.
 * For example: '-AZ2060-'... 
 */

struct az_client
{
    const char * abbreviation;
    int version_width[4];
    const char * name;
};

static struct az_client azureus_clients[] =
{
    { "AG", {  1,  1,  1,  0 }, "Ares" },
    { "AR", { -1, -1, -1, -1 }, "Arctic Torrent" },
    { "AV", { -1, -1, -1, -1 }, "Avicora" },
    { "AX", {  2,  2,  0,  0 }, "BitPump" },
    { "AZ", {  1,  1,  1,  1 }, "Azureus" },
    { "A~", {  1,  1,  1,  0 }, "Ares" },
    { "BB", {  1,  3,  0,  0 }, "BitBuddy" },
    { "BC", {  2,  2,  0,  0 }, "BitComet" },
    { "BF", { -1, -1, -1, -1 }, "Bitflu" },
    { "BG", {  1,  1,  1,  1 }, "BTG" },
    { "BR", {  1,  1,  2,  0 }, "BitRocket" },
    { "BS", { -1, -1, -1, -1 }, "BTSlave" },
    { "BX", { -1, -1, -1, -1 }, "Bittorrent X" },
    { "CD", {  2,  2,  0,  0 }, "Enhanced CTorrent" },
    { "CT", {  1,  1,  2,  0 }, "CTorrent" },
    { "DE", {  1,  1,  1,  1 }, "DelugeTorrent" },
    { "DP", { -1, -1, -1, -1 }, "Propagate" },
    { "EB", { -1, -1, -1, -1 }, "EBit" },
    { "ES", {  1,  1,  1,  1 }, "Electric Sheep" },
    { "FT", {  4,  0,  0,  0 }, "FoxTorrent" },
    { "GR", {  1,  1,  1,  1 }, "GetRight" },
    { "GS", {  4,  0,  0,  0 }, "GSTorrent" },
    { "HL", {  1,  1,  1,  0 }, "Halite" },
    { "HN", { -1, -1, -1, -1 }, "Hydranode" },
    { "KT", {  1,  1,  1,  0 }, "KTorrent" },
    { "LH", { -1, -1, -1, -1 }, "LH-ABC" },
    { "LK", { -1, -1, -1, -1 }, "Linkage" },
    { "LP", {  2,  2,  0,  0 }, "Lphant" },
    { "LT", {  1,  1,  1,  1 }, "Libtorrent" },
    { "LW", { -1, -1, -1, -1 }, "LimeWire" },
    { "ML", { -1, -1, -1, -1 }, "MLDonkey" },
    { "MO", { -1, -1, -1, -1 }, "MonoTorrent" },
    { "MP", { -1, -1, -1, -1 }, "MooPolice" },
    { "MT", { -1, -1, -1, -1 }, "MoonlightTorrent" },
    { "PD", {  1,  1,  1,  1 }, "Pando" },
    { "QD", { -1, -1, -1, -1 }, "QQDownload" },
    { "QT", { -1, -1, -1, -1 }, "Qt 4 Torrent Example" },
    { "RT", { -1, -1, -1, -1 }, "Retriever" },
    { "SB", { -1, -1, -1, -1 }, "Swiftbit" },
    { "SN", { -1, -1, -1, -1 }, "ShareNet" },
    { "SS", { -1, -1, -1, -1 }, "SwarmScope" },
    { "ST", { -1, -1, -1, -1 }, "SymTorrent" },
    { "SZ", {  1,  1,  1,  1 }, "Shareaza" },
    { "S~", {  1,  1,  1,  1 }, "Shareaza (beta)" },
    { "TN", { -1, -1, -1, -1 }, "Torrent.Net" },
    { "TR", {  1,  2,  1,  0 }, "Transmission" },
    { "TS", {  1,  1,  1,  1 }, "TorrentStorm" },
    { "TT", {  1,  1,  1,  1 }, "TuoTu" },
    { "UL", { -1, -1, -1, -1 }, "uLeecher" },
    { "UT", {  1,  2,  0,  0 }, "uTorrent" },
    { "WT", { -1, -1, -1, -1 }, "BitLet" },
    { "WY", { -1, -1, -1, -1 }, "FireTorrent" },
    { "XL", { -1, -1, -1, -1 }, "Xunlei" },
    { "XT", { -1, -1, -1, -1 }, "XanTorrent" },
    { "XX", {  1,  1,  2,  0 }, "Xtorrent" },
    { "ZT", { -1, -1, -1, -1 }, "ZipTorrent" },
    { "lt", {  1,  1,  1,  1 }, "libTorrent" },
    { "qB", {  1,  1,  1,  0 }, "qBittorrent" },
    { "st", { -1, -1, -1, -1 }, "sharktorrent" }
};

static int
isStyleAzureus( const uint8_t * id )
{
    return id[0]=='-' && id[7]=='-';
}

static char*
getAzureusAbbreviation( const uint8_t * id, char * setme )
{
    setme[0] = id[1];
    setme[1] = id[2];
    setme[2] = '\0';
    return setme;
}

static int
compareAzureusAbbreviations( const void * va, const void * vb )
{
    const char * abbreviation = va;
    const struct az_client * b = vb;
    return strcmp( abbreviation, b->abbreviation );
}

static const struct az_client*
getAzureusClient( const uint8_t * peer_id )
{
    const struct az_client * ret = NULL;
    
    if( isStyleAzureus( peer_id ) )
    {
        char abbreviation[3];
        ret = bsearch( getAzureusAbbreviation( peer_id, abbreviation ),
                       azureus_clients,
                       sizeof(azureus_clients) / sizeof(azureus_clients[0]),
                       sizeof(struct az_client),
                       compareAzureusAbbreviations );
    }

    return ret;
}


struct generic_client
{
    int offset;
    char const * id;
    char const * name;
};

static struct generic_client generic_clients[] =
{
    { 0, "346-", "TorrentTopia" },
    { 0, "10-------", "JVtorrent" },
    { 0, "Deadman Walking-", "Deadman" },
    { 5, "Azureus", "Azureus 2.0.3.2" },
    { 0, "DansClient", "XanTorrent" },
    { 4, "btfans", "SimpleBT" },
    { 0, "eX", "eXeem" },
    { 0, "PRC.P---", "Bittorrent Plus! II" },
    { 0, "P87.P---", "Bittorrent Plus!" },
    { 0, "S587Plus", "Bittorrent Plus!" },
    { 0, "martini", "Martini Man" },
    { 0, "Plus---", "Bittorrent Plus" },
    { 0, "turbobt", "TurboBT" },
    { 0, "a00---0", "Swarmy" },
    { 0, "a02---0", "Swarmy" },
    { 0, "T00---0", "Teeweety" },
    { 0, "BTDWV-", "Deadman Walking" },
    { 2, "BS", "BitSpirit" },
    { 0, "Pando-", "Pando" },
    { 0, "LIME", "LimeWire" },
    { 0, "btuga", "BTugaXP" },
    { 0, "oernu", "BTugaXP" },
    { 0, "Mbrst", "Burst!" },
    { 0, "PEERAPP", "PeerApp" },
    { 0, "Plus", "Plus!" },
    { 0, "-Qt-", "Qt" },
    { 0, "exbc", "BitComet" },
    { 0, "DNA", "BitTorrent DNA" },
    { 0, "-G3", "G3 Torrent" },
    { 0, "-FG", "FlashGet" },
    { 0, "-ML", "MLdonkey" },
    { 0, "XBT", "XBT" },
    { 0, "OP", "Opera" },
    { 2, "RS", "Rufus" }
};

/***
****
***/

char*
tr_clientForId( const uint8_t * id )
{
    char * ret = NULL;
    
    if( isStyleAzureus( id ) )
    {
        const struct az_client * client = getAzureusClient( id );

        if( client != NULL )
        {
            struct evbuffer * buf = evbuffer_new( );

            evbuffer_add_printf( buf, "%s ", client->name );

            if( client->version_width[0] == -1 )
                evbuffer_add( buf, id+3, 4 );
            else {
                int i, offset;
                for( i=0, offset=3; i<4; ++i ) {
                    const int width = client->version_width[i];
                    const int isdigit = isdigit( id[offset] );
                    if( !width )
                        break;
                    if( offset!=3 )
                        evbuffer_add( buf, (isdigit?".":" "), 1 );
                    if( !isdigit || width==1 || offset!=3 )
                        evbuffer_add( buf, id+offset, width );
                    else{
                        char * tmp = tr_strndup( (char*)(id+offset), width );
                        evbuffer_add_printf( buf, "%d", atoi(tmp) );
                        tr_free( tmp );
                    }
                    offset += width;
                }
            }

            evbuffer_add( buf, "\0", 1 );
            ret = tr_strdup( (char*) EVBUFFER_DATA( buf ) );
            evbuffer_free( buf );
        }
        else if( !memcmp( &id[1], "BF", 2 ) )
        {
            asprintf( &ret, "Bitflu (%d/%d/%02d)",
                      charToInt( id[6] ),
                      charToInt( id[4] ) * 10 + charToInt( id[5] ),
                      charToInt( id[3] ) );
        }
        else if( !memcmp( &id[1], "BOW", 3 ) )
        {
            if( !memcmp( &id[4], "A0C", 3 ) )
                asprintf( &ret, "Bits on Wheels 1.0.6" );
            else if( !memcmp( &id[4], "A0B", 3 ) )
                asprintf( &ret, "Bits on Wheels 1.0.5" );
            else
                asprintf( &ret, "Bits on Wheels (%3.3s", id+4 );
        }
        if( ret )
        {
fprintf( stderr, "peer_id [%8.8s] returns [%s]\n", id, ret );
            return ret;
        }
    }

    /* generic clients */
    {
        int i;
        const int n = sizeof(generic_clients) / sizeof(generic_clients[0]);
        for( i=0; !ret && i<n; ++i ) {
            const struct generic_client * client = &generic_clients[i];
            if( !memcmp( id+client->offset, client->id, strlen(client->id) ) )
                ret = tr_strdup( client->name );
        }

        if( ret )
            return ret;
    }
    
    /* Tornado-style */
    if( !memcmp( &id[4], "----", 4 ) || !memcmp( &id[4], "--0", 3 ) )
    {
        if( id[0] == 'T' )
        {
            asprintf( &ret, "BitTornado %d.%d.%d", charToInt( id[1] ),
                        charToInt( id[2] ), charToInt( id[3] ) );
        }
        else if( id[0] == 'A' )
        {
            asprintf( &ret, "ABC %d.%d.%d", charToInt( id[1] ),
                        charToInt( id[2] ), charToInt( id[3] ) );
        }
        else if( id[0] == 'R' )
        {
            asprintf( &ret, "Tribler %c.%c", id[1], id[2] );
        }
        else if( id[0] == 'S' )
        {
            asprintf( &ret, "Shad0w's Client %d.%d.%d", charToInt( id[1] ),
                        charToInt( id[2] ), charToInt( id[3] ) );
        }
        
        if( ret )
        {
            return ret;
        }
    }
    
    /* Different formatting per client */
    if( id[0] == 'M' && id[2] == '-' && id[7] == '-' )
    {
        if( id[4] == '-' && id[6] == '-' )
        {
            asprintf( &ret, "BitTorrent %c.%c.%c", id[1], id[3], id[5] );
        }
        else if( id[5] == '-' )
        {
            asprintf( &ret, "BitTorrent %c.%c%c.%c", id[1], id[3], id[4], id[6] );
        }
        
        if( ret )
        {
            return ret;
        }
    }
    if( id[0] == 'Q' && id[2] == '-' && id[7] == '-' )
    {
        if( id[4] == '-' && id[6] == '-' )
        {
            asprintf( &ret, "Queen Bee %c.%c.%c", id[1], id[3], id[5] );
        }
        else if( id[5] == '-' )
        {
            asprintf( &ret, "Queen Bee %c.%c%c.%c", id[1], id[3], id[4], id[6] );
        }
        
        if( ret )
        {
            return ret;
        }
    }
    
    /* All versions of each client are formatted the same */
    if( !memcmp( id, "exbc", 4 ) )
    {
        asprintf( &ret, "%s %d.%02d",
                    !memcmp( &id[6], "LORD", 4 ) ? "BitLord" : "BitComet",
                    id[4], id[5] );
    }
    else if( !memcmp( id, "OP", 2 ) )
    {
        asprintf( &ret, "Opera (%c%c%c%c)", id[2], id[3], id[4], id[5] );
    }
    else if( !memcmp( id, "-ML", 3 ) )
    {
        asprintf( &ret, "MLDonkey %c%c%c%c%c",
                  id[3], id[4], id[5], id[6], id[7] );
    }
    else if( !memcmp( id, "AZ", 2 ) && !memcmp( &id[6], "BT", 2 ) )
    {
        asprintf( &ret, "BitTyrant %c.%c.%c.%c",
                    id[2], id[3], id[4], id[5] );
    }
    else if( !memcmp( id, "-FG", 3 ) )
    {
        asprintf( &ret, "FlashGet %d.%c%c",
                  charToInt( id[3] ) * 10 + charToInt( id[4] ),
                      id[5], id[6] );
    }
    else if( !memcmp( id, "DNA", 3 ) )
    {
        asprintf( &ret, "BitTorrent DNA %d.%d.%d", charToInt( id[3] ) * 10 + charToInt( id[4] ),
                    charToInt( id[5] ) * 10 + charToInt( id[6] ), charToInt( id[7] ) * 10 + charToInt( id[8] ) );
    }
    else if( !memcmp( id, "Plus", 4 ) )
    {
        asprintf( &ret, "Plus! v2 %c.%c%c", id[4], id[5], id[6] );
    }
    else if( !memcmp( id, "XBT", 3 ) )
    {
        asprintf( &ret, "XBT Client %c%c%c%s", id[3], id[4], id[5],
                    id[6] == 'd' ? " (debug)" : "" );
    }
    else if( !memcmp( id, "Mbrst", 5 ) )
    {
        asprintf( &ret, "burst! %c.%c.%c", id[5], id[7], id[9] );
    }
    else if( !memcmp( id, "btpd", 4 ) )
    {
        asprintf( &ret, "BT Protocol Daemon %c%c%c", id[5], id[6], id[7] );
    }
    else if( id[0] == 'Q' && !memcmp( &id[4], "--", 2 ) )
    {
        asprintf( &ret, "BTQueue %d.%d.%d", charToInt( id[1] ),
                    charToInt( id[2] ), charToInt( id[3] ) );
    }
    else if( !memcmp( id, "BLZ", 3 ) )
    {
        asprintf( &ret, "Blizzard Downloader %d.%d", id[3] + 1, id[4] );
    }
    else if( '\0' == id[0] && !memcmp( &id[1], "BS", 2 ) )
    {
        asprintf( &ret, "BitSpirit %u", ( id[1] == 0 ? 1 : id[1] ) );
    }

    /* No match */
    if( !ret )
    {
        if( isprint( id[0] ) && isprint( id[1] ) && isprint( id[2] ) &&
            isprint( id[3] ) && isprint( id[4] ) && isprint( id[5] ) &&
            isprint( id[6] ) && isprint( id[7] ) )
        {
            asprintf( &ret, "unknown client (%c%c%c%c%c%c%c%c)",
                  id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7] );
        }
        else
        {
            asprintf( &ret, "unknown client (0x%02x%02x%02x%02x%02x%02x%02x%02x)",
                  id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7] );
        }
    }

    return ret;
}
