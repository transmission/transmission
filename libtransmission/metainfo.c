/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#define TORRENT_MAX_SIZE (5*1024*1024)

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static int getannounce( tr_info_t * inf, benc_val_t * meta );
#define strcatUTF8( dst, src) _strcatUTF8( (dst), sizeof( dst ) - 1, (src) )
static void _strcatUTF8( char *, int, char * );

/***********************************************************************
 * tr_metainfoParse
 ***********************************************************************
 *
 **********************************************************************/
int tr_metainfoParse( tr_info_t * inf, const char * path,
                      const char * savedHash, int saveCopy )
{
    FILE       * file;
    char       * buf;
    benc_val_t   meta, * beInfo, * list, * val;
    int          i;
    struct stat  sb;

    assert( NULL == path || NULL == savedHash );
    /* if savedHash isn't null, saveCopy should be false */
    assert( NULL == savedHash || !saveCopy );

    if( NULL != savedHash )
    {
        snprintf( inf->torrent, MAX_PATH_LENGTH, "%s/%s",
                  tr_getTorrentsDirectory(), savedHash );
        path = inf->torrent;
    }

    if( stat( path, &sb ) )
    {
        tr_err( "Could not stat file (%s)", path );
        return 1;
    }
    if( ( sb.st_mode & S_IFMT ) != S_IFREG )
    {
        tr_err( "Not a regular file (%s)", path );
        return 1;
    }
    if( sb.st_size > TORRENT_MAX_SIZE )
    {
        tr_err( "Torrent file is too big (%d bytes)", (int)sb.st_size );
        return 1;
    }

    /* Load the torrent file into our buffer */
    file = fopen( path, "rb" );
    if( !file )
    {
        tr_err( "Could not open file (%s)", path );
        return 1;
    }
    buf = malloc( sb.st_size );
    fseek( file, 0, SEEK_SET );
    if( fread( buf, sb.st_size, 1, file ) != 1 )
    {
        tr_err( "Read error (%s)", path );
        free( buf );
        fclose( file );
        return 1;
    }
    fclose( file );

    /* Parse bencoded infos */
    if( tr_bencLoad( buf, sb.st_size, &meta, NULL ) )
    {
        tr_err( "Error while parsing bencoded data" );
        free( buf );
        return 1;
    }

    /* Get info hash */
    if( !( beInfo = tr_bencDictFind( &meta, "info" ) ) )
    {
        tr_err( "Could not find \"info\" dictionary" );
        tr_bencFree( &meta );
        free( buf );
        return 1;
    }
    SHA1( (uint8_t *) beInfo->begin,
          (long) beInfo->end - (long) beInfo->begin, inf->hash );
    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        sprintf( inf->hashString + i * 2, "%02x", inf->hash[i] );
    }

    if( saveCopy )
    {
        /* Save a copy of the torrent file in the private torrent directory */
        snprintf( inf->torrent, MAX_PATH_LENGTH, "%s/%s",
                  tr_getTorrentsDirectory(), inf->hashString );
        file = fopen( inf->torrent, "wb" );
        if( !file )
        {
            tr_err( "Could not open file (%s) (%s)",
                    inf->torrent, strerror(errno) );
            tr_bencFree( &meta );
            free( buf );
            return 1;
        }
        fseek( file, 0, SEEK_SET );
        if( fwrite( buf, sb.st_size, 1, file ) != 1 )
        {
            tr_err( "Write error (%s)", inf->torrent );
            tr_bencFree( &meta );
            free( buf );
            fclose( file );
            return 1;
        }
        fclose( file );
    }
    else
    {
        snprintf( inf->torrent, MAX_PATH_LENGTH, "%s", path );
    }

    /* We won't need this anymore */
    free( buf );
        
    /* Comment info */
    if( ( val = tr_bencDictFind( &meta, "comment.utf-8" ) ) || ( val = tr_bencDictFind( &meta, "comment" ) ) )
    {
        strcatUTF8( inf->comment, val->val.s.s );
    }
    
    /* Creator info */
    if( ( val = tr_bencDictFind( &meta, "created by.utf-8" ) ) || ( val = tr_bencDictFind( &meta, "created by" ) ) )
    {
        strcatUTF8( inf->creator, val->val.s.s );
    }
    
    /* Date created */
    if( ( val = tr_bencDictFind( &meta, "creation date" ) ) )
    {
        inf->dateCreated = val->val.i;
    }
    else
    {
        inf->dateCreated = 0;
    }
    
    /* Private torrent */
    if( ( val = tr_bencDictFind( beInfo, "private" ) ) && TYPE_INT == val->type )
    {
        inf->privateTorrent = val->val.i;
    }
    
    /* Piece length */
    if( !( val = tr_bencDictFind( beInfo, "piece length" ) ) )
    {
        tr_err( "No \"piece length\" entry" );
        tr_bencFree( &meta );
        return 1;
    }
    inf->pieceSize = val->val.i;

    /* Hashes */
    val = tr_bencDictFind( beInfo, "pieces" );
    if( val->val.s.i % SHA_DIGEST_LENGTH )
    {
        tr_err( "Invalid \"piece\" string (size is %d)", val->val.s.i );
        tr_bencFree( &meta );
        return 1;
    }
    inf->pieceCount = val->val.s.i / SHA_DIGEST_LENGTH;
    inf->pieces = (uint8_t *) val->val.s.s; /* Ugly, but avoids a memcpy */
    val->val.s.s = NULL;

    /* TODO add more tests so we don't crash on weird files */

    inf->totalSize = 0;
    if( ( list = tr_bencDictFind( beInfo, "files" ) ) )
    {
        /* Multi-file mode */
        int j;

        val = tr_bencDictFind( beInfo, "name.utf-8" );
        if( NULL == val )
        {
            val = tr_bencDictFind( beInfo, "name" );
        }
        strcatUTF8( inf->name, val->val.s.s );

        inf->multifile = 1;
        inf->fileCount = list->val.l.count;
        inf->files     = calloc( inf->fileCount * sizeof( tr_file_t ), 1 );

        for( i = 0; i < list->val.l.count; i++ )
        {
            val = tr_bencDictFind( &list->val.l.vals[i], "path.utf-8" );
            if( NULL == val )
            {
                val = tr_bencDictFind( &list->val.l.vals[i], "path" );
            }
            strcatUTF8( inf->files[i].name, inf->name );
            for( j = 0; j < val->val.l.count; j++ )
            {
                strcatUTF8( inf->files[i].name, "/" );
                strcatUTF8( inf->files[i].name,
                            val->val.l.vals[j].val.s.s );
            }
            val = tr_bencDictFind( &list->val.l.vals[i], "length" );
            inf->files[i].length  = val->val.i;
            inf->totalSize       += val->val.i;
        }

    }
    else
    {
        /* Single-file mode */
        inf->multifile = 0;
        inf->fileCount = 1;
        inf->files     = calloc( sizeof( tr_file_t ), 1 );

        val = tr_bencDictFind( beInfo, "name.utf-8" );
        if( NULL == val )
        {
            val = tr_bencDictFind( beInfo, "name" );
        }
        strcatUTF8( inf->files[0].name, val->val.s.s );
        strcatUTF8( inf->name, val->val.s.s );
        
        val = tr_bencDictFind( beInfo, "length" );
        inf->files[0].length  = val->val.i;
        inf->totalSize       += val->val.i;
    }

    if( (uint64_t) inf->pieceCount !=
        ( inf->totalSize + inf->pieceSize - 1 ) / inf->pieceSize )
    {
        tr_err( "Size of hashes and files don't match" );
        tr_metainfoFree( inf );
        tr_bencFree( &meta );
        return 1;
    }

    /* get announce or announce-list */
    if( getannounce( inf, &meta ) )
    {
        tr_metainfoFree( inf );
        tr_bencFree( &meta );
        return 1;
    }

    tr_bencFree( &meta );
    return 0;
}

void tr_metainfoFree( tr_info_t * inf )
{
    int ii, jj;

    free( inf->pieces );
    free( inf->files );
    
    for( ii = 0; ii < inf->trackerTiers; ii++ )
    {
        for( jj = 0; jj < inf->trackerList[ii].count; jj++ )
        {
            free( inf->trackerList[ii].list[jj].address );
            free( inf->trackerList[ii].list[jj].announce );
        }
        free( inf->trackerList[ii].list );
    }
    free( inf->trackerList );
}

static int getannounce( tr_info_t * inf, benc_val_t * meta )
{
    benc_val_t              * val, * subval, * urlval;
    char                    * address, * announce;
    int                       ii, jj, port, random;
    tr_tracker_info_t * sublist;
    int subcount;
    void * swapping;

    /* Announce-list */
    val = tr_bencDictFind( meta, "announce-list" );
    if( NULL != val && TYPE_LIST == val->type && 0 < val->val.l.count )
    {
        inf->trackerTiers = 0;
        inf->trackerList = calloc( sizeof( inf->trackerList[0] ),
                                   val->val.l.count );

        /* iterate through the announce-list's tiers */
        for( ii = 0; ii < val->val.l.count; ii++ )
        {
            subval = &val->val.l.vals[ii];
            if( TYPE_LIST != subval->type || 0 >= subval->val.l.count )
            {
                continue;
            }
            subcount = 0;
            sublist = calloc( sizeof( sublist[0] ), subval->val.l.count );

            /* iterate through the tier's items */
            for( jj = 0; jj < subval->val.l.count; jj++ )
            {
                urlval = &subval->val.l.vals[jj];
                if( TYPE_STR != urlval->type ||
                    tr_httpParseUrl( urlval->val.s.s, urlval->val.s.i,
                                     &address, &port, &announce ) )
                {
                    continue;
                }

                /* place the item info in a random location in the sublist */
                random = tr_rand( subcount + 1 );
                if( random != subcount )
                {
                    sublist[subcount] = sublist[random];
                }
                sublist[random].address  = address;
                sublist[random].port     = port;
                sublist[random].announce = announce;
                subcount++;
            }

            /* just use sublist as is if it's full */
            if( subcount == subval->val.l.count )
            {
                inf->trackerList[inf->trackerTiers].list = sublist;
                inf->trackerList[inf->trackerTiers].count = subcount;
                inf->trackerTiers++;
            }
            /* if we skipped some of the tier's items then trim the sublist */
            else if( 0 < subcount )
            {
                inf->trackerList[inf->trackerTiers].list = calloc( sizeof( sublist[0] ), subcount );
                memcpy( inf->trackerList[inf->trackerTiers].list, sublist,
                        sizeof( sublist[0] ) * subcount );
                inf->trackerList[inf->trackerTiers].count = subcount;
                inf->trackerTiers++;
                free( sublist );
            }
            /* drop the whole sublist if we didn't use any items at all */
            else
            {
                free( sublist );
            }
        }

        /* did we use any of the tiers? */
        if( 0 == inf->trackerTiers )
        {
            tr_inf( "No valid entries in \"announce-list\"" );
            free( inf->trackerList );
            inf->trackerList = NULL;
        }
        /* trim unused sublist pointers */
        else if( inf->trackerTiers < val->val.l.count )
        {
            swapping = inf->trackerList;
            inf->trackerList = calloc( sizeof( inf->trackerList[0] ),
                                       inf->trackerTiers );
            memcpy( inf->trackerList, swapping,
                    sizeof( inf->trackerList[0] ) * inf->trackerTiers );
            free( swapping );
        }
    }

    /* Regular announce value */
    if( 0 == inf->trackerTiers )
    {
        val = tr_bencDictFind( meta, "announce" );
        if( NULL == val || TYPE_STR != val->type )
        {
            tr_err( "No \"announce\" entry" );
            return 1;
        }

        if( tr_httpParseUrl( val->val.s.s, val->val.s.i,
                             &address, &port, &announce ) )
        {
            tr_err( "Invalid announce URL (%s)", val->val.s.s );
            return 1;
        }

        sublist                   = calloc( sizeof( sublist[0] ), 1 );
        sublist[0].address        = address;
        sublist[0].port           = port;
        sublist[0].announce       = announce;
        inf->trackerList          = calloc( sizeof( inf->trackerList[0] ), 1 );
        inf->trackerList[0].list  = sublist;
        inf->trackerList[0].count = 1;
        inf->trackerTiers         = 1;
    }

    return 0;
}

void tr_metainfoRemoveSaved( const char * hashString )
{
    char file[MAX_PATH_LENGTH];

    snprintf( file, MAX_PATH_LENGTH, "%s/%s",
              tr_getTorrentsDirectory(), hashString );
    unlink(file);
}

/***********************************************************************
 * strcatUTF8
 ***********************************************************************
 * According to the official specification, all strings in the torrent
 * file are supposed to be UTF-8 encoded. However, there are
 * non-compliant torrents around... If we encounter an invalid UTF-8
 * character, we assume it is ISO 8859-1 and convert it to UTF-8.
 **********************************************************************/
#define WANTBYTES( want, got ) \
    if( (want) > (got) ) { return; } else { (got) -= (want); }
static void _strcatUTF8( char * s, int len, char * append )
{
    char * p;

    /* Go to the end of the destination string */
    while( s[0] )
    {
        s++;
        len--;
    }

    /* Now start appending, converting on the fly if necessary */
    for( p = append; p[0]; )
    {
        if( !( p[0] & 0x80 ) )
        {
            /* ASCII character */
            WANTBYTES( 1, len );
            *(s++) = *(p++);
            continue;
        }

        if( ( p[0] & 0xE0 ) == 0xC0 && ( p[1] & 0xC0 ) == 0x80 )
        {
            /* 2-bytes UTF-8 character */
            WANTBYTES( 2, len );
            *(s++) = *(p++); *(s++) = *(p++);
            continue;
        }

        if( ( p[0] & 0xF0 ) == 0xE0 && ( p[1] & 0xC0 ) == 0x80 &&
            ( p[2] & 0xC0 ) == 0x80 )
        {
            /* 3-bytes UTF-8 character */
            WANTBYTES( 3, len );
            *(s++) = *(p++); *(s++) = *(p++);
            *(s++) = *(p++);
            continue;
        }

        if( ( p[0] & 0xF8 ) == 0xF0 && ( p[1] & 0xC0 ) == 0x80 &&
            ( p[2] & 0xC0 ) == 0x80 && ( p[3] & 0xC0 ) == 0x80 )
        {
            /* 4-bytes UTF-8 character */
            WANTBYTES( 4, len );
            *(s++) = *(p++); *(s++) = *(p++);
            *(s++) = *(p++); *(s++) = *(p++);
            continue;
        }

        /* ISO 8859-1 -> UTF-8 conversion */
        WANTBYTES( 2, len );
        *(s++) = 0xC0 | ( ( *p & 0xFF ) >> 6 );
        *(s++) = 0x80 | ( *(p++) & 0x3F );
    }
}
