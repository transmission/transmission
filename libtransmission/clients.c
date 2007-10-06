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
#include <string.h>

#include "transmission.h"
#include "trcompat.h"

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

char * tr_clientForId( const uint8_t * id )
{
    char * ret = NULL;
    
    /* Azureus-style */
    if( id[0] == '-' && id[7] == '-' )
    {
        if( !memcmp( &id[1], "TR", 2 ) )
        {
            /* support old-style Transmission id without maintenance number */
            if ( !memcmp( &id[3], "00", 2 ) )
            {
                asprintf( &ret, "Transmission 0.%d",
                        charToInt( id[5] ) * 10 + charToInt( id[6] ) );
            }
            else
            {
                asprintf( &ret, "Transmission %d.%c%c%s",
                        charToInt( id[3] ), id[4], id[5],
                        id[6] == 'Z' ? "+" : "" );
            }
        }
        else if( !memcmp( &id[1], "AZ", 2 ) )
        {
            asprintf( &ret, "Azureus %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "UT", 2 ) )
        {
            asprintf( &ret, "\xc2\xb5Torrent %c.%d", id[3],
                      charToInt( id[4] ) * 10 + charToInt( id[5] ) );
        }
        else if( !memcmp( &id[1], "BC", 2 ) )
        {
            asprintf( &ret, "BitComet %d.%c%c",
                      charToInt( id[3] ) * 10 + charToInt( id[4] ),
                      id[5], id[6] );
        }
        else if( !memcmp( &id[1], "SZ", 2 ) || !memcmp( &id[1], "S~", 2 ) )
        {
            asprintf( &ret, "Shareaza %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "BOW", 3 ) )
        {
            if( !memcmp( &id[4], "A0C", 3 ) )
            {
                asprintf( &ret, "Bits on Wheels 1.0.6" );
            }
            else if( !memcmp( &id[4], "A0B", 3 ) )
            {
                asprintf( &ret, "Bits on Wheels 1.0.5" );
            }
            else
            {
                asprintf( &ret, "Bits on Wheels (%c%c%c)",
                        id[4], id[5], id[6] );
            }
        }
        else if( !memcmp( &id[1], "BR", 2 ) )
        {
            asprintf( &ret, "BitRocket %c.%c (%d)",
                      id[3], id[4], charToInt( id[5] ) * 10 + charToInt( id[6] ) );
        }
        else if( !memcmp( &id[1], "XX", 2 ) )
        {
            asprintf( &ret, "Xtorrent %c.%c (%d)",
                      id[3], id[4], charToInt( id[5] ) * 10 + charToInt( id[6] ) );
        }
        else if( !memcmp( &id[1], "TS", 2 ) )
        {
            asprintf( &ret, "TorrentStorm %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "KT", 2 ) )
        {
            if( id[5] == 'R' )
            {
                asprintf( &ret, "KTorrent %c.%c RC %c",
                        id[3], id[4], id[6] );
            }
            else if( id[5] == 'D' )
            {
                asprintf( &ret, "KTorrent %c.%c Dev",
                        id[3], id[4] );
            }
            else
            {
                asprintf( &ret, "KTorrent %c.%c.%c",
                        id[3], id[4], id[5] );
            }
        }
        else if( !memcmp( &id[1], "lt", 2 ) )
        {
            asprintf( &ret, "libTorrent %d.%d.%d.%d",
                      charToInt( id[3] ), charToInt( id[4] ),
                      charToInt( id[5] ), charToInt( id[6] ) );
        }
        else if( !memcmp( &id[1], "LT", 2 ) )
        {
            asprintf( &ret, "libtorrent %d.%d.%d.%d",
                      charToInt( id[3] ), charToInt( id[4] ),
                      charToInt( id[5] ), charToInt( id[6] ) );
        }
        else if( !memcmp( &id[1], "TT", 2 ) )
        {
            asprintf( &ret, "TuoTu %c.%c.%c",
                      id[3], id[4], id[5] );
        }
		else if( !memcmp( &id[1], "ES", 2 ) )
        {
            asprintf( &ret, "Electric Sheep %c.%c.%c",
                      id[3], id[4], id[5] );
        }
        else if( !memcmp( &id[1], "CD", 2 ) )
        {
            asprintf( &ret, "Enhanced CTorrent %d.%d",
                      charToInt( id[3] ) * 10 + charToInt( id[4] ),
                      charToInt( id[5] ) * 10 + charToInt( id[6] ) );
        }
		else if( !memcmp( &id[1], "CT", 2 ) )
        {
            asprintf( &ret, "CTorrent %c.%c.%d",
                      id[3], id[4],
                      charToInt( id[5] ) * 10 + charToInt( id[6] ) );
        }
        else if( !memcmp( &id[1], "LP", 2 ) )
        {
            asprintf( &ret, "Lphant %d.%c%c",
                      charToInt( id[3] ) * 10 + charToInt( id[4] ),
                      id[5], id[6] );
        }
        else if( !memcmp( &id[1], "AX", 2 ) )
        {
            asprintf( &ret, "BitPump %d.%c%c",
                      charToInt( id[3] ) * 10 + charToInt( id[4] ),
                      id[5], id[6] );
        }
        else if( !memcmp( &id[1], "DE", 2 ) )
        {
            asprintf( &ret, "Deluge %d.%d.%d",
                      charToInt( id[3] ), charToInt( id[4] ),
                      charToInt( id[5] ) );
        }
        else if( !memcmp( &id[1], "AG", 2 ) )
        {
            asprintf( &ret, "Ares Galaxy %d.%d.%d",
                      charToInt( id[3] ), charToInt( id[4] ),
                      charToInt( id[5] ) );
        }
        else if( !memcmp( &id[1], "AR", 2 ) )
        {
            asprintf( &ret, "Arctic Torrent" );
        }
        else if( !memcmp( &id[1], "BG", 2 ) )
        {
            asprintf( &ret, "BTG %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
		else if( !memcmp( &id[1], "BB", 2 ) )
        {
            asprintf( &ret, "BitBuddy %c.%c%c%c",
                      id[3], id[4], id[5], id[6] );
        }
		else if( !memcmp( &id[1], "qB", 2 ) )
        {
            asprintf( &ret, "qBittorrent %d.%d.%d",
                      charToInt( id[3] ), charToInt( id[4] ),
                      charToInt( id[5] ) );
        }
        else if( !memcmp( &id[1], "BF", 2 ) )
        {
            asprintf( &ret, "Bitflu (%d/%d/%02d)",
                      charToInt( id[6] ),
                      charToInt( id[4] ) * 10 + charToInt( id[5] ),
                      charToInt( id[3] ) );
        }
        else if( !memcmp( &id[1], "FT", 2 ) )
        {
            asprintf( &ret, "FoxTorrent (%c%c%c%c)",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "GR", 2 ) )
        {
            asprintf( &ret, "GetRight %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "PD", 2 ) )
        {
            asprintf( &ret, "Pando %c.%c.%c.%c",
                      id[3], id[4], id[5], id[6] );
        }
        else if( !memcmp( &id[1], "LW", 2 ) )
        {
            asprintf( &ret, "LimeWire" );
        }
        
        if( ret )
        {
            return ret;
        }
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
    else if( !memcmp( id, "-WT-", 4 ) )
    {
        asprintf( &ret, "BitLet" );
    }
    else if( !memcmp( id, "LIME", 4 ) )
    {
        asprintf( &ret, "LimeWire" );
    }
    else if( !memcmp( id, "-G3", 3 ) )
    {
        asprintf( &ret, "G3 Torrent" );
    }
    else if( !memcmp( id, "10-------", 9 ) )
    {
        asprintf( &ret, "JVtorrent" );
    }
    else if( !memcmp( id, "346-", 4 ) )
    {
        asprintf( &ret, "TorrentTopia" );
    }
    else if( !memcmp( id, "eX", 2 ) )
    {
        asprintf( &ret, "eXeem" );
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
