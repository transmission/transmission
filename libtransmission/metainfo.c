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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlink, stat */

#include <miniupnp/miniwget.h> /* parseURL */

#include "transmission.h"
#include "bencode.h"
#include "crypto.h" /* tr_sha1 */
#include "metainfo.h"
#include "platform.h"
#include "utils.h"


static int
tr_httpParseUrl( const char * url_in, int len,
                 char ** setme_host, int * setme_port, char ** setme_path )
{
    char * url = tr_strndup( url_in, len );
    char * path;
    char host[4096+1];
    unsigned short port;
    int success;

    success = parseURL( url, host, &port, &path );

    if( success ) {
        *setme_host = tr_strdup( host );
        *setme_port = port;
        *setme_path = tr_strdup( path );
    }

    tr_free( url );

    return !success;
}

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static int getannounce( tr_info * inf, benc_val_t * meta );
static char * announceToScrape( const char * announce );
static int parseFiles( tr_info * inf, benc_val_t * name,
                       benc_val_t * files, benc_val_t * length );

/***
****
***/

#define WANTBYTES( want, got ) \
    if( (want) > (got) ) { return; } else { (got) -= (want); }
static void
strlcat_utf8( void * dest, const void * src, size_t len, char skip )
{
    char       * s      = dest;
    const char * append = src;
    const char * p;

    /* don't overwrite the nul at the end */
    len--;

    /* Go to the end of the destination string */
    while( s[0] )
    {
        s++;
        len--;
    }

    /* Now start appending, converting on the fly if necessary */
    for( p = append; p[0]; )
    {
        /* skip over the requested character */
        if( skip == p[0] )
        {
            p++;
            continue;
        }

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

static void
savedname( char * name, size_t len, const char * hash, const char * tag )
{
    const char * torDir = tr_getTorrentsDirectory ();

    if( tag == NULL )
    {
        tr_buildPath( name, len, torDir, hash, NULL );
    }
    else
    {
        char base[1024];
        snprintf( base, sizeof(base), "%s-%s", hash, tag );
        tr_buildPath( name, len, torDir, base, NULL );
    }
}


int
tr_metainfoParse( tr_info * inf, const benc_val_t * meta_in, const char * tag )
{
    int i;
    benc_val_t * beInfo, * val, * val2;
    benc_val_t * meta = (benc_val_t *) meta_in;
    char buf[4096];

    /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
     * from the Metainfo file. Note that the value will be a bencoded 
     * dictionary, given the definition of the info key above. */
    if(( beInfo = tr_bencDictFindType( meta, "info", TYPE_DICT )))
    {
        int len;
        char * str = tr_bencSave( beInfo, &len );
        tr_sha1( inf->hash, str, len, NULL );
        tr_free( str );
    }
    else
    {
        tr_err( "info dictionary not found!" );
        return TR_EINVALID;
    }

    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        snprintf( inf->hashString + i * 2, sizeof( inf->hashString ) - i * 2,
                  "%02x", inf->hash[i] );
    }
    savedname( inf->torrent, sizeof( inf->torrent ), inf->hashString, tag );

    /* comment */
    memset( buf, '\0', sizeof( buf ) );
    val = tr_bencDictFindFirst( meta, "comment.utf-8", "comment", NULL );
    if( val && val->type == TYPE_STR )
        strlcat_utf8( buf, val->val.s.s, sizeof( buf ), 0 );
    tr_free( inf->comment );
    inf->comment = tr_strdup( buf );
    
    /* creator */
    memset( buf, '\0', sizeof( buf ) );
    val = tr_bencDictFindFirst( meta, "created by.utf-8", "created by", NULL );
    if( val && val->type == TYPE_STR )
        strlcat_utf8( buf, val->val.s.s, sizeof( buf ), 0 );
    tr_free( inf->creator );
    inf->creator = tr_strdup( buf );
    
    /* Date created */
    inf->dateCreated = 0;
    val = tr_bencDictFind( meta, "creation date" );
    if( NULL != val && TYPE_INT == val->type )
    {
        inf->dateCreated = val->val.i;
    }
    
    /* Private torrent */
    val  = tr_bencDictFind( beInfo, "private" );
    val2 = tr_bencDictFind( meta,  "private" );
    if( ( NULL != val  && ( TYPE_INT != val->type  || 0 != val->val.i ) ) ||
        ( NULL != val2 && ( TYPE_INT != val2->type || 0 != val2->val.i ) ) )
    {
        inf->isPrivate = 1;
    }
    
    /* Piece length */
    val = tr_bencDictFind( beInfo, "piece length" );
    if( NULL == val || TYPE_INT != val->type )
    {
        tr_err( "%s \"piece length\" entry", ( val ? "Invalid" : "Missing" ) );
        goto fail;
    }
    inf->pieceSize = val->val.i;

    /* Hashes */
    val = tr_bencDictFind( beInfo, "pieces" );
    if( NULL == val || TYPE_STR != val->type )
    {
        tr_err( "%s \"pieces\" entry", ( val ? "Invalid" : "Missing" ) );
        goto fail;
    }
    if( val->val.s.i % SHA_DIGEST_LENGTH )
    {
        tr_err( "Invalid \"piece\" string (size is %d)", val->val.s.i );
        goto fail;
    }
    inf->pieceCount = val->val.s.i / SHA_DIGEST_LENGTH;

    inf->pieces = calloc ( inf->pieceCount, sizeof(tr_piece) );

    for ( i=0; i<inf->pieceCount; ++i )
    {
        memcpy (inf->pieces[i].hash, &val->val.s.s[i*SHA_DIGEST_LENGTH], SHA_DIGEST_LENGTH);
    }

    /* get file or top directory name */
    val = tr_bencDictFindFirst( beInfo, "name.utf-8", "name", NULL );
    if( parseFiles( inf, tr_bencDictFindFirst( beInfo,
                                               "name.utf-8", "name", NULL ),
                    tr_bencDictFind( beInfo, "files" ),
                    tr_bencDictFind( beInfo, "length" ) ) )
    {
        goto fail;
    }

    if( !inf->fileCount )
    {
        tr_err( "Torrent has no files." );
        goto fail;
    }

    if( !inf->totalSize )
    {
        tr_err( "Torrent is zero bytes long." );
        goto fail;
    }

    /* TODO add more tests so we don't crash on weird files */

    if( (uint64_t) inf->pieceCount !=
        ( inf->totalSize + inf->pieceSize - 1 ) / inf->pieceSize )
    {
        tr_err( "Size of hashes and files don't match" );
        goto fail;
    }

    /* get announce or announce-list */
    if( getannounce( inf, meta ) )
    {
        goto fail;
    }

    return TR_OK;

  fail:
    tr_metainfoFree( inf );
    return TR_EINVALID;
}

void tr_metainfoFree( tr_info * inf )
{
    int i, j;

    for( i=0; i<inf->fileCount; ++i )
        tr_free( inf->files[i].name );

    tr_free( inf->pieces );
    tr_free( inf->files );
    tr_free( inf->comment );
    tr_free( inf->creator );
    tr_free( inf->primaryAddress );
    
    for( i=0; i<inf->trackerTiers; ++i ) {
        for( j=0; j<inf->trackerList[i].count; ++j )
            tr_trackerInfoClear( &inf->trackerList[i].list[j] );
        tr_free( inf->trackerList[i].list );
    }
    tr_free( inf->trackerList );

    memset( inf, '\0', sizeof(tr_info) );
}

static int getfile( char ** setme,
                    const char * prefix, benc_val_t * name )
{
    benc_val_t  * dir;
    const char ** list;
    int           ii, jj;
    char          buf[4096];

    if( TYPE_LIST != name->type )
    {
        return TR_EINVALID;
    }

    list = calloc( name->val.l.count, sizeof( list[0] ) );
    if( NULL == list )
    {
        return TR_EINVALID;
    }

    for( ii = jj = 0; name->val.l.count > ii; ii++ )
    {
        dir = &name->val.l.vals[ii];
        if( TYPE_STR != dir->type )
        {
            continue;
        }
        if( 0 == strcmp( "..", dir->val.s.s ) )
        {
            if( 0 < jj )
            {
                jj--;
            }
        }
        else if( 0 != strcmp( ".", dir->val.s.s ) )
        {
            list[jj] = dir->val.s.s;
            jj++;
        }
    }

    if( 0 == jj )
    {
        free( list );
        return TR_EINVALID;
    }

    memset( buf, 0, sizeof( buf ) );
    strlcat_utf8( buf, prefix, sizeof(buf), 0 );
    for( ii = 0; jj > ii; ii++ )
    {
        strlcat_utf8( buf, TR_PATH_DELIMITER_STR, sizeof(buf), 0 );
        strlcat_utf8( buf, list[ii], sizeof(buf), TR_PATH_DELIMITER );
    }
    free( list );

    tr_free( *setme );
    *setme = tr_strdup( buf );

    return TR_OK;
}

static int getannounce( tr_info * inf, benc_val_t * meta )
{
    benc_val_t        * val, * subval, * urlval;
    char              * address, * announce;
    int                 ii, jj, port, random, subcount;
    tr_tracker_info   * sublist;
    void * swapping;

    /* Announce-list */
    val = tr_bencDictFind( meta, "announce-list" );
    if( NULL != val && TYPE_LIST == val->type && 0 < val->val.l.count )
    {
        inf->trackerTiers = 0;
        inf->trackerList = calloc( val->val.l.count,
                                   sizeof( inf->trackerList[0] ) );

        /* iterate through the announce-list's tiers */
        for( ii = 0; ii < val->val.l.count; ii++ )
        {
            subval = &val->val.l.vals[ii];
            if( TYPE_LIST != subval->type || 0 >= subval->val.l.count )
            {
                continue;
            }
            subcount = 0;
            sublist = calloc( subval->val.l.count, sizeof( sublist[0] ) );

            /* iterate through the tier's items */
            for( jj = 0; jj < subval->val.l.count; jj++ )
            {
                tr_tracker_info tmp;

                urlval = &subval->val.l.vals[jj];
                if( TYPE_STR != urlval->type ||
                    tr_trackerInfoInit( &tmp, urlval->val.s.s, urlval->val.s.i ) )
                {
                    continue;
                }

                if( !inf->primaryAddress ) {
                     char buf[1024];
                     snprintf( buf, sizeof(buf), "%s:%d", tmp.address, tmp.port );
                     inf->primaryAddress = tr_strdup( buf );
                }

                /* place the item info in a random location in the sublist */
                random = tr_rand( subcount + 1 );
                if( random != subcount )
                    sublist[subcount] = sublist[random];
                sublist[random] = tmp;
                subcount++;
            }

            /* just use sublist as-is if it's full */
            if( subcount == subval->val.l.count )
            {
                inf->trackerList[inf->trackerTiers].list = sublist;
                inf->trackerList[inf->trackerTiers].count = subcount;
                inf->trackerTiers++;
            }
            /* if we skipped some of the tier's items then trim the sublist */
            else if( 0 < subcount )
            {
                inf->trackerList[inf->trackerTiers].list = calloc( subcount, sizeof( sublist[0] ) );
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
            inf->trackerList = calloc( inf->trackerTiers,
                                       sizeof( inf->trackerList[0] ) );
            memcpy( inf->trackerList, swapping,
                    sizeof( inf->trackerList[0] ) * inf->trackerTiers );
            free( swapping );
        }
    }

    /* Regular announce value */
    val = tr_bencDictFind( meta, "announce" );
    if( NULL == val || TYPE_STR != val->type )
    {
        tr_err( "No \"announce\" entry" );
        return TR_EINVALID;
    }

    if( !inf->trackerTiers )
    {

        if( tr_httpParseUrl( val->val.s.s, val->val.s.i,
                             &address, &port, &announce ) )
        {
            tr_err( "Invalid announce URL (%s)", val->val.s.s );
            return TR_EINVALID;
        }
        sublist                   = calloc( 1, sizeof( sublist[0] ) );
        sublist[0].address        = address;
        sublist[0].port           = port;
        sublist[0].announce       = announce;
        sublist[0].scrape         = announceToScrape( announce );
        inf->trackerList          = calloc( 1, sizeof( inf->trackerList[0] ) );
        inf->trackerList[0].list  = sublist;
        inf->trackerList[0].count = 1;
        inf->trackerTiers         = 1;

        if( !inf->primaryAddress ) {
            char buf[1024];
            snprintf( buf, sizeof(buf), "%s:%d", sublist[0].address, sublist[0].port );
            inf->primaryAddress = tr_strdup( buf );
        }

    }

    return TR_OK;
}

static char * announceToScrape( const char * announce )
{
    char old[]  = "announce";
    int  oldlen = 8;
    char new[]  = "scrape";
    int  newlen = 6;
    char * slash, * scrape;
    size_t scrapelen, used;

    slash = strrchr( announce, '/' );
    if( NULL == slash )
    {
        return NULL;
    }
    slash++;
    
    if( 0 != strncmp( slash, old, oldlen ) )
    {
        return NULL;
    }

    scrapelen = strlen( announce ) - oldlen + newlen;
    scrape = calloc( scrapelen + 1, 1 );
    if( NULL == scrape )
    {
        return NULL;
    }
    assert( ( size_t )( slash - announce ) < scrapelen );
    memcpy( scrape, announce, slash - announce );
    used = slash - announce;
    strncat( scrape, new, scrapelen - used );
    used += newlen;
    assert( strlen( scrape ) == used );
    if( used < scrapelen )
    {
        assert( strlen( slash + oldlen ) == scrapelen - used );
        strncat( scrape, slash + oldlen, scrapelen - used );
    }

    return scrape;
}

int
tr_trackerInfoInit( tr_tracker_info  * info,
                    const char       * address,
                    int                address_len )
{
    int ret = tr_httpParseUrl( address, address_len,
                               &info->address,
                               &info->port,
                               &info->announce );
    if( !ret )
        info->scrape = announceToScrape( info->announce );

    return ret;
}

void
tr_trackerInfoClear( tr_tracker_info * info )
{
    tr_free( info->address );
    tr_free( info->announce );
    tr_free( info->scrape );
    memset( info, '\0', sizeof(tr_tracker_info) );
}

void
tr_metainfoRemoveSaved( const char * hashString, const char * tag )
{
    char file[MAX_PATH_LENGTH];
    savedname( file, sizeof file, hashString, tag );
    unlink( file );
}

/* Save a copy of the torrent file in the saved torrent directory */
int
tr_metainfoSave( const char * hash, const char * tag,
                 const uint8_t * buf, size_t buflen )
{
    char   path[MAX_PATH_LENGTH];
    FILE * file;

    savedname( path, sizeof path, hash, tag );
    file = fopen( path, "wb+" );
    if( !file )
    {
        tr_err( "Could not open file (%s) (%s)", path, strerror( errno ) );
        return TR_EINVALID;
    }
    fseek( file, 0, SEEK_SET );
    if( fwrite( buf, 1, buflen, file ) != buflen )
    {
        tr_err( "Could not write file (%s) (%s)", path, strerror( errno ) );
        fclose( file );
        return TR_EINVALID;
    }
    fclose( file );

    return TR_OK;
}

static int
parseFiles( tr_info * inf, benc_val_t * name,
            benc_val_t * files, benc_val_t * length )
{
    benc_val_t * item, * path;
    int ii;

    if( NULL == name || TYPE_STR != name->type )
    {
        tr_err( "%s \"name\" string", ( name ? "Invalid" : "Missing" ) );
        return TR_EINVALID;
    }

    strlcat_utf8( inf->name, name->val.s.s, sizeof( inf->name ),
                  TR_PATH_DELIMITER );
    if( '\0' == inf->name[0] )
    {
        tr_err( "Invalid \"name\" string" );
        return TR_EINVALID;
    }
    inf->totalSize = 0;

    if( files && TYPE_LIST == files->type )
    {
        /* Multi-file mode */
        inf->isMultifile = 1;
        inf->fileCount = files->val.l.count;
        inf->files     = calloc( inf->fileCount, sizeof( inf->files[0] ) );

        if( !inf->files )
            return TR_EINVALID;

        for( ii = 0; files->val.l.count > ii; ii++ )
        {
            item = &files->val.l.vals[ii];
            path = tr_bencDictFindFirst( item, "path.utf-8", "path", NULL );
            if( getfile( &inf->files[ii].name, inf->name, path ) )
            {
                tr_err( "%s \"path\" entry",
                        ( path ? "Invalid" : "Missing" ) );
                return TR_EINVALID;
            }
            length = tr_bencDictFind( item, "length" );
            if( NULL == length || TYPE_INT != length->type )
            {
                tr_err( "%s \"length\" entry",
                        ( length ? "Invalid" : "Missing" ) );
                return TR_EINVALID;
            }
            inf->files[ii].length = length->val.i;
            inf->totalSize         += length->val.i;
        }
    }
    else if( NULL != length && TYPE_INT == length->type )
    {
        char buf[4096];

        /* Single-file mode */
        inf->isMultifile = 0;
        inf->fileCount = 1;
        inf->files     = calloc( 1, sizeof( inf->files[0] ) );

        if( !inf->files )
            return TR_EINVALID;

        memset( buf, 0, sizeof( buf ) );
        strlcat_utf8( buf, name->val.s.s, sizeof(buf), TR_PATH_DELIMITER );
        tr_free( inf->files[0].name );
        inf->files[0].name = tr_strdup( buf );

        inf->files[0].length = length->val.i;
        inf->totalSize      += length->val.i;
    }
    else
    {
        tr_err( "%s \"files\" entry and %s \"length\" entry",
                ( files ? "Invalid" : "Missing" ),
                ( length ? "invalid" : "missing" ) );
    }

    return TR_OK;
}
