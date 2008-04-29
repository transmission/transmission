/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

/* thanks amc1! */

#include <ctype.h> /* isprint, tolower */
#include <stdio.h>
#include <stdlib.h> /* strtol */
#include <string.h>

#include "transmission.h"
#include "trcompat.h" /* strlcpy */
#include "utils.h"

static int
charint( char ch )
{
    if( '0' <= ch && ch <= '9' ) return      ch - '0';
    if( 'A' <= ch && ch <= 'Z' ) return 10 + ch - 'A';
    if( 'a' <= ch && ch <= 'z' ) return 36 + ch - 'a';
    return 0;
}

static int
strint( const void * pch, int span )
{
    char tmp[64];
    memcpy( tmp, pch, span );
    tmp[span] = '\0';
    return strtol( tmp, NULL, 0 );
}

static const char*
getMnemonic( char ch )
{
    switch( ch )
    {
        case 'b': case 'B': return "Beta";
        case 'd': return "Debug";
        case 'x': case 'X': case 'Z': return "(Dev)";
        default: return "";
    }
}

static void
three_digits( char * buf, size_t buflen, const char * name, const uint8_t * digits )
{
    snprintf( buf, buflen, "%s %d.%d.%d", name,
              charint( digits[0] ),
              charint( digits[1] ),
              charint( digits[2] ) );
}
static void
four_digits( char * buf, size_t buflen, const char * name, const uint8_t * digits )
{
    snprintf( buf, buflen, "%s %d.%d.%d.%d", name,
              charint( digits[0] ),
              charint( digits[1] ),
              charint( digits[2] ),
              charint( digits[3] ) );
}
static void
two_major_two_minor( char * buf, size_t buflen, const char * name, const uint8_t * digits )
{
    snprintf( buf, buflen, "%s %02d.%02d", name,
              strint( digits+0, 2 ),
              strint( digits+2, 2 ) );
}
static void
no_version( char * buf, size_t buflen, const char * name )
{
    strlcpy( buf, name, buflen );
}

static void
mainline_style( char * buf, size_t buflen, const char * name, const uint8_t * id )
{
    if( id[4] == '-' && id[6] == '-' )
        snprintf( buf, buflen, "%s %c.%c.%c", name, id[1], id[3], id[5] );
    else if( id[5] == '-' )
        snprintf( buf, buflen, "%s %c.%c%c.%c", name, id[1], id[3], id[4], id[6] );
}

static int
isMainlineStyle( const uint8_t * peer_id )
{
    /**
     * One of the following styles will be used:
     *   Mx-y-z--
     *   Mx-yy-z-
     */ 
    return peer_id[2]=='-'
        && peer_id[7]=='-'
        && ( peer_id[4]=='-' || peer_id[5]=='-' );
}

void
tr_clientForId( char * buf, size_t buflen, const void * id_in )
{
    const uint8_t * id = id_in;

    *buf = '\0';

    if( !id )
        return;
    
    /* Azureus-style */
    if( id[0] == '-' && id[7] == '-' )
    {
        if( !memcmp( id+1, "UT", 2 ) )
        {
            snprintf( buf, buflen, "\xc2\xb5Torrent %d.%d.%d %s",
                      strint(id+3,1), strint(id+4,1), strint(id+5,1), getMnemonic(id[6]) );
        }

        /* we win the most fucked-up peer_id award!  hooray! */
        else if( !memcmp( id+1, "TR", 2 ) )
        {
            if( !memcmp( id+3, "000", 3 ) ) /* very old client style: -TR0006- is 0.6 */
                snprintf( buf, buflen, "Transmission 0.%c", id[6] );
            else if( !memcmp( id+3, "00", 2) ) /* previous client style: -TR0072- is 0.72 */
                snprintf( buf, buflen, "Transmission %d.%02d", strint(id+3,2), strint(id+5,2) );
            else /* current client style: -TR072Z- is 0.72 (Dev) */
                snprintf( buf, buflen, "Transmission %d.%02d %s", strint(id+3,1), strint(id+4,2), getMnemonic(id[6]) );
        }

        else if( !memcmp( id+1, "KT", 2 ) )
        {
            if( id[5] == 'D' )
                snprintf( buf, buflen, "KTorrent %d.%d Dev %d", charint(id[3]), charint(id[4]), charint(id[6]) );
            else if( id[5] == 'R' )
                snprintf( buf, buflen, "KTorrent %d.%d RC %d", charint(id[3]), charint(id[4]), charint(id[6]) );
            else
                three_digits( buf, buflen, "KTorrent", id+3 );
        }

        else if( !memcmp( id+1, "AR", 2 ) ) four_digits( buf, buflen, "Ares", id+3 );
        else if( !memcmp( id+1, "AT", 2 ) ) four_digits( buf, buflen, "Artemis", id+3 );
        else if( !memcmp( id+1, "AV", 2 ) ) four_digits( buf, buflen, "Avicora", id+3 );
        else if( !memcmp( id+1, "AZ", 2 ) ) four_digits( buf, buflen, "Azureus", id+3 );
        else if( !memcmp( id+1, "BG", 2 ) ) four_digits( buf, buflen, "BTGetit", id+3 );
        else if( !memcmp( id+1, "BM", 2 ) ) four_digits( buf, buflen, "BitMagnet", id+3 );
        else if( !memcmp( id+1, "BX", 2 ) ) four_digits( buf, buflen, "BittorrentX", id+3 );
        else if( !memcmp( id+1, "bk", 2 ) ) four_digits( buf, buflen, "BitKitten (libtorrent)", id+3 );
        else if( !memcmp( id+1, "BS", 2 ) ) four_digits( buf, buflen, "BTSlave", id+3 );
        else if( !memcmp( id+1, "BX", 2 ) ) four_digits( buf, buflen, "BittorrentX", id+3 );
        else if( !memcmp( id+1, "EB", 2 ) ) four_digits( buf, buflen, "EBit", id+3 );
        else if( !memcmp( id+1, "DE", 2 ) ) four_digits( buf, buflen, "Deluge", id+3 );
        else if( !memcmp( id+1, "DP", 2 ) ) four_digits( buf, buflen, "Propogate Data Client", id+3 );
        else if( !memcmp( id+1, "FC", 2 ) ) four_digits( buf, buflen, "FileCroc", id+3 );
        else if( !memcmp( id+1, "FT", 2 ) ) four_digits( buf, buflen, "FoxTorrent/RedSwoosh", id+3 );
        else if( !memcmp( id+1, "GR", 2 ) ) four_digits( buf, buflen, "GetRight", id+3 );
        else if( !memcmp( id+1, "HN", 2 ) ) four_digits( buf, buflen, "Hydranode", id+3 );
        else if( !memcmp( id+1, "LH", 2 ) ) four_digits( buf, buflen, "LH-ABC", id+3 );
        else if( !memcmp( id+1, "NX", 2 ) ) four_digits( buf, buflen, "Net Transport", id+3 );
        else if( !memcmp( id+1, "MO", 2 ) ) four_digits( buf, buflen, "MonoTorrent", id+3 );
        else if( !memcmp( id+1, "MR", 2 ) ) four_digits( buf, buflen, "Miro", id+3 );
        else if( !memcmp( id+1, "MT", 2 ) ) four_digits( buf, buflen, "Moonlight", id+3 );
        else if( !memcmp( id+1, "PD", 2 ) ) four_digits( buf, buflen, "Pando", id+3 );
        else if( !memcmp( id+1, "RS", 2 ) ) four_digits( buf, buflen, "Rufus", id+3 );
        else if( !memcmp( id+1, "RT", 2 ) ) four_digits( buf, buflen, "Retriever", id+3 );
        else if( !memcmp( id+1, "SS", 2 ) ) four_digits( buf, buflen, "SwarmScope", id+3 );
        else if( !memcmp( id+1, "SZ", 2 ) ) four_digits( buf, buflen, "Shareaza", id+3 );
        else if( !memcmp( id+1, "S~", 2 ) ) four_digits( buf, buflen, "Shareaza", id+3 );
        else if( !memcmp( id+1, "st", 2 ) ) four_digits( buf, buflen, "SharkTorrent", id+3 );
        else if( !memcmp( id+1, "TN", 2 ) ) four_digits( buf, buflen, "Torrent .NET", id+3 );
        else if( !memcmp( id+1, "TS", 2 ) ) four_digits( buf, buflen, "TorrentStorm", id+3 );
        else if( !memcmp( id+1, "UL", 2 ) ) four_digits( buf, buflen, "uLeecher!", id+3 );
        else if( !memcmp( id+1, "VG", 2 ) ) four_digits( buf, buflen, "Vagaa", id+3 );
        else if( !memcmp( id+1, "WT", 2 ) ) four_digits( buf, buflen, "BitLet", id+3 );
        else if( !memcmp( id+1, "WY", 2 ) ) four_digits( buf, buflen, "Wyzo", id+3 );
        else if( !memcmp( id+1, "XL", 2 ) ) four_digits( buf, buflen, "Xunlei", id+3 );
        else if( !memcmp( id+1, "XT", 2 ) ) four_digits( buf, buflen, "XanTorrent", id+3 );
        else if( !memcmp( id+1, "ZT", 2 ) ) four_digits( buf, buflen, "Zip Torrent", id+3 );

        else if( !memcmp( id+1, "AG", 2 ) ) three_digits( buf, buflen, "Ares", id+3 );
        else if( !memcmp( id+1, "A~", 2 ) ) three_digits( buf, buflen, "Ares", id+3 );
        else if( !memcmp( id+1, "ES", 2 ) ) three_digits( buf, buflen, "Electric Sheep", id+3 );
        else if( !memcmp( id+1, "HL", 2 ) ) three_digits( buf, buflen, "Halite", id+3 );
        else if( !memcmp( id+1, "LT", 2 ) ) three_digits( buf, buflen, "libtorrent (Rasterbar)", id+3 );
        else if( !memcmp( id+1, "lt", 2 ) ) three_digits( buf, buflen, "libTorrent (Rakshasa)", id+3 );
        else if( !memcmp( id+1, "MP", 2 ) ) three_digits( buf, buflen, "MooPolice", id+3 );
        else if( !memcmp( id+1, "TT", 2 ) ) three_digits( buf, buflen, "TuoTu", id+3 );
        else if( !memcmp( id+1, "qB", 2 ) ) three_digits( buf, buflen, "qBittorrent", id+3 );

        else if( !memcmp( id+1, "AX", 2 ) ) two_major_two_minor( buf, buflen, "BitPump", id+3 );
        else if( !memcmp( id+1, "BC", 2 ) ) two_major_two_minor( buf, buflen, "BitComet", id+3 );
        else if( !memcmp( id+1, "CD", 2 ) ) two_major_two_minor( buf, buflen, "Enhanced CTorrent", id+3 );
        else if( !memcmp( id+1, "FG", 2 ) ) two_major_two_minor( buf, buflen, "FlashGet", id+3 );
        else if( !memcmp( id+1, "LP", 2 ) ) two_major_two_minor( buf, buflen, "Lphant", id+3 );

        else if( !memcmp( id+1, "BF", 2 ) ) no_version( buf, buflen, "BitFlu" );
        else if( !memcmp( id+1, "LW", 2 ) ) no_version( buf, buflen, "LimeWire" );

        else if( !memcmp( id+1, "BB", 2 ) )
        {
            snprintf( buf, buflen, "BitBuddy %c.%c%c%c", id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( id+1, "BR", 2 ) )
        {
            snprintf( buf, buflen, "BitRocket %c.%c (%c%c)", id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( id+1, "CT", 2 ) )
        {
            snprintf( buf, buflen, "CTorrent %d.%d.%02d", charint(id[3]), charint(id[4]), strint(id+5,2) );
        }
        else if( !memcmp( id+1, "XX", 2 ) )
        {
            snprintf( buf, buflen, "Xtorrent %d.%d (%d)", charint(id[3]), charint(id[4]), strint(id+5,2) );
        }
        else if( !memcmp( id+1, "BOW", 3 ) )
        {
                 if( !memcmp( &id[4], "A0B", 3 ) ) snprintf( buf, buflen, "Bits on Wheels 1.0.5" );
            else if( !memcmp( &id[4], "A0C", 3 ) ) snprintf( buf, buflen, "Bits on Wheels 1.0.6" );
            else                                   snprintf( buf, buflen, "Bits on Wheels %c.%c.%c", id[4], id[5], id[5] );
        }

        if( *buf )
            return;
    }
    
    /* Shad0w-style */
    if( !memcmp( &id[4], "----", 4 ) || !memcmp( &id[4], "--0", 3 ) )
    {
        switch( *id ) {
            case 'A': three_digits( buf, buflen, "ABC", id+1 ); break;
            case 'O': three_digits( buf, buflen, "Osprey", id+1 ); break;
            case 'Q': three_digits( buf, buflen, "BTQueue", id+1 ); break;
            case 'R': three_digits( buf, buflen, "Tribler", id+1 ); break;
            case 'S': three_digits( buf, buflen, "Shad0w", id+1 ); break;
            case 'T': three_digits( buf, buflen, "BitTornado", id+1 ); break;
            default: break;
        }
        if( *buf ) return;
    }

    /* Mainline */
    if( isMainlineStyle( id ) )
    {
        if( *id=='M' ) mainline_style( buf, buflen, "BitTorrent", id );
        if( *id=='Q' ) mainline_style( buf, buflen, "Queen Bee", id );
        if( *buf ) return;
    }

    /* Clients with no version */
         if( !memcmp( id, "AZ2500BT", 8 ) )  no_version( buf, buflen, "BitTyrant (Azureus Mod)" );
    else if( !memcmp( id, "LIME", 4 ) )      no_version( buf, buflen, "Limewire" );
    else if( !memcmp( id, "martini", 7 ) )   no_version( buf, buflen, "Martini Man" );
    else if( !memcmp( id, "Pando", 5 ) )     no_version( buf, buflen, "Pando" );
    else if( !memcmp( id, "a00---0", 7 ) )   no_version( buf, buflen, "Swarmy" );
    else if( !memcmp( id, "a02---0", 7 ) )   no_version( buf, buflen, "Swarmy" );
    else if( !memcmp( id, "-G3", 3 ) )       no_version( buf, buflen, "G3 Torrent" );
    else if( !memcmp( id, "10-------", 9 ) ) no_version( buf, buflen, "JVtorrent" );
    else if( !memcmp( id, "346-", 4 ) )      no_version( buf, buflen, "TorrentTopia" );
    else if( !memcmp( id, "eX", 2 ) )        no_version( buf, buflen, "eXeem" );
   
    /* Everything else */ 
    else if( !memcmp( id, "S3", 2 ) && id[2] == '-' && id[4] == '-' && id[6] == '-' )
    {
        snprintf( buf, buflen, "Amazon S3 %c.%c.%c", id[3], id[5], id[7] );
    }
    else if( !memcmp( id, "OP", 2 ) )
    {
        snprintf( buf, buflen, "Opera (Build %c%c%c%c)", id[2], id[3], id[4], id[5] );
    }
    else if( !memcmp( id, "-ML", 3 ) )
    {
        snprintf( buf, buflen, "MLDonkey %c%c%c%c%c", id[3], id[4], id[5], id[6], id[7] );
    }
    else if( !memcmp( id, "DNA", 3 ) )
    {
        snprintf( buf, buflen, "BitTorrent DNA %d.%d.%d", strint(id+3,2),
                                                          strint(id+5,2),
                                                          strint(id+7,2) );
    }
    else if( !memcmp( id, "Plus", 4 ) )
    {
        snprintf( buf, buflen, "Plus! v2 %c.%c%c", id[4], id[5], id[6] );
    }
    else if( !memcmp( id, "XBT", 3 ) )
    {
        snprintf( buf, buflen, "XBT Client %c.%c.%c %s", id[3], id[4], id[5], getMnemonic(id[6]) );
    }
    else if( !memcmp( id, "Mbrst", 5 ) )
    {
        snprintf( buf, buflen, "burst! %c.%c.%c", id[5], id[7], id[9] );
    }
    else if( !memcmp( id, "btpd", 4 ) )
    {
        snprintf( buf, buflen, "BT Protocol Daemon %c%c%c", id[5], id[6], id[7] );
    }
    else if( !memcmp( id, "BLZ", 3 ) )
    {
        snprintf( buf, buflen, "Blizzard Downloader %d.%d", id[3]+1, id[4] );
    }
    else if( '\0' == id[0] && !memcmp( &id[1], "BS", 2 ) )
    {
        snprintf( buf, buflen, "BitSpirit %u", ( id[1] == 0 ? 1 : id[1] ) );
    }

    /* No match */
    if( !*buf )
    {
        if( isprint( id[0] ) && isprint( id[1] ) && isprint( id[2] ) &&
            isprint( id[3] ) && isprint( id[4] ) && isprint( id[5] ) &&
            isprint( id[6] ) && isprint( id[7] ) )
                snprintf( buf, buflen, "%c%c%c%c%c%c%c%c",
                          id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7] );
        else
                snprintf( buf, buflen, "0x%02x%02x%02x%02x%02x%02x%02x%02x",
                          id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7] );
    }
}
