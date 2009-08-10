/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

/* thanks amc1! */

#include <ctype.h> /* isprint, tolower */
#include <stdio.h>
#include <stdlib.h> /* strtol */
#include <string.h>

#include <event.h> /* evbuffer */

#include "transmission.h"
#include "clients.h"
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
getShadowInt( char ch, int * setme )
{
    const char * str = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-";
    const char * pch = strchr( str, ch );
    if( !pch )
    return 0;
        *setme = pch - str;
    return 1;
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
getMnemonicEnd( char ch )
{
    switch( ch )
    {
        case 'b': case 'B': return " (Beta)";
        case 'd': return " (Debug)";
        case 'x': case 'X': case 'Z': return " (Dev)";
        default: return "";
    }
}

static void
three_digits( char * buf, size_t buflen, const char * name, const uint8_t * digits )
{
    tr_snprintf( buf, buflen, "%s %d.%d.%d", name,
                 charint( digits[0] ),
                 charint( digits[1] ),
                 charint( digits[2] ) );
}
static void
four_digits( char * buf, size_t buflen, const char * name, const uint8_t * digits )
{
    tr_snprintf( buf, buflen, "%s %d.%d.%d.%d", name,
                 charint( digits[0] ),
                 charint( digits[1] ),
                 charint( digits[2] ),
                 charint( digits[3] ) );
}
static void
two_major_two_minor( char * buf, size_t buflen, const char * name, const uint8_t * digits )
{
    tr_snprintf( buf, buflen, "%s %d.%02d", name,
                 strint( digits, 2 ),
                 strint( digits+2, 2 ) );
}
static void
no_version( char * buf, size_t buflen, const char * name )
{
    tr_strlcpy( buf, name, buflen );
}

static void
mainline_style( char * buf, size_t buflen, const char * name, const uint8_t * id )
{
    if( id[4] == '-' && id[6] == '-' )
        tr_snprintf( buf, buflen, "%s %c.%c.%c", name, id[1], id[3], id[5] );
    else if( id[5] == '-' )
        tr_snprintf( buf, buflen, "%s %c.%c%c.%c", name, id[1], id[3], id[4], id[6] );
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

static int
decodeBitCometClient( char * buf, size_t buflen, const uint8_t * id )
{
    int is_bitlord;
    int major, minor;
    const char * name;
    const char * mod = NULL;

    if( !memcmp( id, "exbc", 4 ) ) mod = "";
    else if( !memcmp( id, "FUTB", 4 )) mod = "(Solidox Mod) ";
    else if( !memcmp( id, "xUTB", 4 )) mod = "(Mod 2) ";
    else return FALSE;

    is_bitlord = !memcmp( id+6, "LORD", 4 );
    name = (is_bitlord) ? "BitLord " : "BitComet ";
    major = id[4];
    minor = id[5];

    /**
     * Bitcomet, and older versions of BitLord, are of the form x.yy.
     * Bitcoment 1.0 and onwards are of the form x.y.
     */
    if( is_bitlord && major>0 )
        tr_snprintf( buf, buflen, "%s%s%d.%d", name, mod, major, minor );
    else
        tr_snprintf( buf, buflen, "%s%s%d.%02d", name, mod, major, minor );

    return TRUE;
}

static int
decodeBitSpiritClient( char * buf, size_t buflen, const uint8_t * id )
{
    const int isBS = !memcmp( id+2, "BS", 2 );
    if( isBS )
    {
        const int version = id[1] ? id[1] : 1;
        tr_snprintf( buf, buflen, "BitSpirit v%d", version );
    }
    return isBS;
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
        if( !memcmp( id+1, "TR", 2 ) )
        {
            if( !memcmp( id+3, "000", 3 ) ) /* very old client style: -TR0006- is 0.6 */
                tr_snprintf( buf, buflen, "Transmission 0.%c", id[6] );
            else if( !memcmp( id+3, "00", 2) ) /* previous client style: -TR0072- is 0.72 */
                tr_snprintf( buf, buflen, "Transmission 0.%02d", strint(id+5,2) );
            else /* current client style: -TR111Z- is 1.11+ */
                tr_snprintf( buf, buflen, "Transmission %d.%02d%s", strint(id+3,1), strint(id+4,2),
                          id[6]=='Z' || id[6]=='X' ? "+" : "" );
        }

        else if( !memcmp( id+1, "UT", 2 ) )
        {
            tr_snprintf( buf, buflen, "\xc2\xb5Torrent %d.%d.%d%s",
                         strint(id+3,1), strint(id+4,1), strint(id+5,1), getMnemonicEnd(id[6]) );
        }
        else if( !memcmp( id+1, "UM", 2 ) )
        {
            tr_snprintf( buf, buflen, "\xc2\xb5Torrent Mac %d.%d.%d%s",
                         strint(id+3,1), strint(id+4,1), strint(id+5,1), getMnemonicEnd(id[6]) );
        }

        else if( !memcmp( id+1, "AZ", 2 ) )
        {
            if( id[3] > '3' || ( id[3] == '3' && id[4] >= '1' ) ) /* Vuze starts at version 3.1.0.0 */
                four_digits( buf, buflen, "Vuze", id+3 );
            else
                four_digits( buf, buflen, "Azureus", id+3 );
        }

        else if( !memcmp( id+1, "KT", 2 ) )
        {
            if( id[5] == 'D' )
                tr_snprintf( buf, buflen, "KTorrent %d.%d Dev %d", charint(id[3]), charint(id[4]), charint(id[6]) );
            else if( id[5] == 'R' )
                tr_snprintf( buf, buflen, "KTorrent %d.%d RC %d", charint(id[3]), charint(id[4]), charint(id[6]) );
            else
                three_digits( buf, buflen, "KTorrent", id+3 );
        }

        else if( !memcmp( id+1, "AR", 2 ) ) four_digits( buf, buflen, "Ares", id+3 );
        else if( !memcmp( id+1, "AT", 2 ) ) four_digits( buf, buflen, "Artemis", id+3 );
        else if( !memcmp( id+1, "AV", 2 ) ) four_digits( buf, buflen, "Avicora", id+3 );
        else if( !memcmp( id+1, "BG", 2 ) ) four_digits( buf, buflen, "BTGetit", id+3 );
        else if( !memcmp( id+1, "BM", 2 ) ) four_digits( buf, buflen, "BitMagnet", id+3 );
        else if( !memcmp( id+1, "BP", 2 ) ) four_digits( buf, buflen, "BitTorrent Pro (Azureus + Spyware)", id+3 );
        else if( !memcmp( id+1, "BX", 2 ) ) four_digits( buf, buflen, "BittorrentX", id+3 );
        else if( !memcmp( id+1, "bk", 2 ) ) four_digits( buf, buflen, "BitKitten (libtorrent)", id+3 );
        else if( !memcmp( id+1, "BS", 2 ) ) four_digits( buf, buflen, "BTSlave", id+3 );
        else if( !memcmp( id+1, "BW", 2 ) ) four_digits( buf, buflen, "BitWombat", id+3 );
        else if( !memcmp( id+1, "BX", 2 ) ) four_digits( buf, buflen, "BittorrentX", id+3 );
        else if( !memcmp( id+1, "EB", 2 ) ) four_digits( buf, buflen, "EBit", id+3 );
        else if( !memcmp( id+1, "DE", 2 ) ) four_digits( buf, buflen, "Deluge", id+3 );
        else if( !memcmp( id+1, "DP", 2 ) ) four_digits( buf, buflen, "Propogate Data Client", id+3 );
        else if( !memcmp( id+1, "FC", 2 ) ) four_digits( buf, buflen, "FileCroc", id+3 );
        else if( !memcmp( id+1, "FT", 2 ) ) four_digits( buf, buflen, "FoxTorrent/RedSwoosh", id+3 );
        else if( !memcmp( id+1, "GR", 2 ) ) four_digits( buf, buflen, "GetRight", id+3 );
        else if( !memcmp( id+1, "HN", 2 ) ) four_digits( buf, buflen, "Hydranode", id+3 );
        else if( !memcmp( id+1, "LC", 2 ) ) four_digits( buf, buflen, "LeechCraft", id+3 );
        else if( !memcmp( id+1, "LH", 2 ) ) four_digits( buf, buflen, "LH-ABC", id+3 );
        else if( !memcmp( id+1, "NX", 2 ) ) four_digits( buf, buflen, "Net Transport", id+3 );
        else if( !memcmp( id+1, "MO", 2 ) ) four_digits( buf, buflen, "MonoTorrent", id+3 );
        else if( !memcmp( id+1, "MR", 2 ) ) four_digits( buf, buflen, "Miro", id+3 );
        else if( !memcmp( id+1, "MT", 2 ) ) four_digits( buf, buflen, "Moonlight", id+3 );
        else if( !memcmp( id+1, "OT", 2 ) ) four_digits( buf, buflen, "OmegaTorrent", id+3 );
        else if( !memcmp( id+1, "PD", 2 ) ) four_digits( buf, buflen, "Pando", id+3 );
        else if( !memcmp( id+1, "QD", 2 ) ) four_digits( buf, buflen, "QQDownload", id+3 );
        else if( !memcmp( id+1, "RS", 2 ) ) four_digits( buf, buflen, "Rufus", id+3 );
        else if( !memcmp( id+1, "RT", 2 ) ) four_digits( buf, buflen, "Retriever", id+3 );
        else if( !memcmp( id+1, "RZ", 2 ) ) four_digits( buf, buflen, "RezTorrent", id+3 );
        else if( !memcmp( id+1, "SD", 2 ) ) four_digits( buf, buflen, "Xunlei", id+3 );
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
        else if( !memcmp( id+1, "LP", 2 ) ) two_major_two_minor( buf, buflen, "Lphant", id+3 );

        else if( !memcmp( id+1, "BF", 2 ) ) no_version( buf, buflen, "BitFlu" );
        else if( !memcmp( id+1, "LW", 2 ) ) no_version( buf, buflen, "LimeWire" );

        else if( !memcmp( id+1, "BB", 2 ) )
        {
            tr_snprintf( buf, buflen, "BitBuddy %c.%c%c%c", id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( id+1, "BR", 2 ) )
        {
            tr_snprintf( buf, buflen, "BitRocket %c.%c (%c%c)", id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( id+1, "CT", 2 ) )
        {
            tr_snprintf( buf, buflen, "CTorrent %d.%d.%02d", charint(id[3]), charint(id[4]), strint(id+5,2) );
        }
        else if( !memcmp( id+1, "XX", 2 ) )
        {
            tr_snprintf( buf, buflen, "Xtorrent %d.%d (%d)", charint(id[3]), charint(id[4]), strint(id+5,2) );
        }
        else if( !memcmp( id+1, "BOW", 3 ) )
        {
                 if( !memcmp( &id[4], "A0B", 3 ) ) tr_snprintf( buf, buflen, "Bits on Wheels 1.0.5" );
            else if( !memcmp( &id[4], "A0C", 3 ) ) tr_snprintf( buf, buflen, "Bits on Wheels 1.0.6" );
            else                                   tr_snprintf( buf, buflen, "Bits on Wheels %c.%c.%c", id[4], id[5], id[5] );
        }

        if( *buf )
            return;
    }

    /* Mainline */
    if( isMainlineStyle( id ) )
    {
        if( *id=='M' ) mainline_style( buf, buflen, "BitTorrent", id );
        if( *id=='Q' ) mainline_style( buf, buflen, "Queen Bee", id );
        if( *buf ) return;
    }

    if( decodeBitCometClient( buf, buflen, id ) )
        return;
    if( decodeBitSpiritClient( buf, buflen, id ) )
        return;

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
    else if( !memcmp( id, "-aria2-", 7 ) )   no_version( buf, buflen, "aria2" );
    else if( !memcmp( id, "-FG", 3 ) )       two_major_two_minor( buf, buflen, "FlashGet", id+3 );

    /* Everything else */
    else if( !memcmp( id, "S3", 2 ) && id[2] == '-' && id[4] == '-' && id[6] == '-' )
    {
        tr_snprintf( buf, buflen, "Amazon S3 %c.%c.%c", id[3], id[5], id[7] );
    }
    else if( !memcmp( id, "OP", 2 ) )
    {
        tr_snprintf( buf, buflen, "Opera (Build %c%c%c%c)", id[2], id[3], id[4], id[5] );
    }
    else if( !memcmp( id, "-ML", 3 ) )
    {
        tr_snprintf( buf, buflen, "MLDonkey %c%c%c%c%c", id[3], id[4], id[5], id[6], id[7] );
    }
    else if( !memcmp( id, "DNA", 3 ) )
    {
        tr_snprintf( buf, buflen, "BitTorrent DNA %d.%d.%d", strint(id+3,2),
                                                             strint(id+5,2),
                                                             strint(id+7,2) );
    }
    else if( !memcmp( id, "Plus", 4 ) )
    {
        tr_snprintf( buf, buflen, "Plus! v2 %c.%c%c", id[4], id[5], id[6] );
    }
    else if( !memcmp( id, "XBT", 3 ) )
    {
        tr_snprintf( buf, buflen, "XBT Client %c.%c.%c%s", id[3], id[4], id[5], getMnemonicEnd(id[6]) );
    }
    else if( !memcmp( id, "Mbrst", 5 ) )
    {
        tr_snprintf( buf, buflen, "burst! %c.%c.%c", id[5], id[7], id[9] );
    }
    else if( !memcmp( id, "btpd", 4 ) )
    {
        tr_snprintf( buf, buflen, "BT Protocol Daemon %c%c%c", id[5], id[6], id[7] );
    }
    else if( !memcmp( id, "BLZ", 3 ) )
    {
        tr_snprintf( buf, buflen, "Blizzard Downloader %d.%d", id[3]+1, id[4] );
    }
    else if( '\0' == id[0] && !memcmp( &id[1], "BS", 2 ) )
    {
        tr_snprintf( buf, buflen, "BitSpirit %u", ( id[1] == 0 ? 1 : id[1] ) );
    }
    else if( !memcmp( id, "QVOD", 4 ) )
    {
        four_digits( buf, buflen, "QVOD", id+4 );
    }
    else if( !memcmp( id, "-NE", 3 ) )
    {
        four_digits( buf, buflen, "BT Next Evolution", id+3 );
    }

    /* Shad0w-style */
    {
        int a, b, c;
        if( strchr( "AOQRSTU", id[0] )
            && getShadowInt( id[1], &a )
            && getShadowInt( id[2], &b )
            && getShadowInt( id[3], &c ) )
        {
            const char * name = NULL;

            switch( id[0] )
            {
                case 'A': name = "ABC"; break;
                case 'O': name = "Osprey"; break;
                case 'Q': name = "BTQueue"; break;
                case 'R': name = "Tribler"; break;
                case 'S': name = "Shad0w"; break;
                case 'T': name = "BitTornado"; break;
                case 'U': name = "UPnP NAT Bit Torrent"; break;
            }

            if( name )
            {
                tr_snprintf( buf, buflen, "%s %d.%d.%d", name, a, b, c );
                return;
            }
        }
    }

    /* No match */
    if( !*buf )
    {
        char out[32], *walk=out;
        const char *in, *in_end;
        for( in=(const char*)id, in_end=in+8; in!=in_end; ++in ) {
            if( isprint( *in ) )
                *walk++ = *in;
            else {
                tr_snprintf( walk, out+sizeof(out)-walk, "%%%02X", (unsigned int)*in );
                walk += 3;
            }
        }
        *walk = '\0';
        tr_strlcpy( buf, out, buflen );
    }
}
