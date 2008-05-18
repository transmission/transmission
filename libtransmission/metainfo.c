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
#include <ctype.h> /* isspace */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlink, stat */

#include <event.h> /* struct evbuffer */

#include "transmission.h"
#include "bencode.h"
#include "crypto.h" /* tr_sha1 */
#include "metainfo.h"
#include "platform.h"
#include "utils.h"

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static int parseFiles( tr_info * inf, tr_benc * name,
                       tr_benc * files, tr_benc * length );

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
getTorrentFilename( const tr_handle  * handle,
                    const tr_info    * inf,
                    char             * buf,
                    size_t             buflen )
{
    const char * dir = tr_getTorrentDir( handle );
    char base[MAX_PATH_LENGTH];
    snprintf( base, sizeof( base ), "%s.%16.16s.torrent", inf->name, inf->hashString );
    tr_buildPath( buf, buflen, dir, base, NULL );
}

static void
getTorrentOldFilename( const tr_handle * handle,
                       const tr_info   * info,
                       char            * name,
                       size_t            len )
{
    const char * torDir = tr_getTorrentDir( handle );

    if( !handle->tag )
    {
        tr_buildPath( name, len, torDir, info->hashString, NULL );
    }
    else
    {
        char base[1024];
        snprintf( base, sizeof(base), "%s-%s", info->hashString, handle->tag );
        tr_buildPath( name, len, torDir, base, NULL );
    }
}

void
tr_metainfoMigrate( tr_handle * handle,
                    tr_info   * inf )
{
    struct stat new_sb;
    char new_name[MAX_PATH_LENGTH];

    getTorrentFilename( handle, inf, new_name, sizeof( new_name ) );

    if( stat( new_name, &new_sb ) || ( ( new_sb.st_mode & S_IFMT ) != S_IFREG ) )
    {
        char old_name[MAX_PATH_LENGTH];
        size_t contentLen;
        uint8_t * content;

        getTorrentOldFilename( handle, inf, old_name, sizeof( old_name ) );
        if(( content = tr_loadFile( old_name, &contentLen )))
        {
            FILE * out = fopen( new_name, "wb+" );
            if( fwrite( content, sizeof( uint8_t ), contentLen, out ) == contentLen )
            {
                tr_free( inf->torrent );
                inf->torrent = tr_strdup( new_name );
                tr_sessionSetTorrentFile( handle, inf->hashString, new_name );
                unlink( old_name );
            }
            fclose( out );
        }

        tr_free( content );
    }
}

static char *
announceToScrape( const char * announce )
{
    char * scrape = NULL;
    const char * s;

    /* To derive the scrape URL use the following steps:
     * Begin with the announce URL. Find the last '/' in it.
     * If the text immediately following that '/' isn't 'announce'
     * it will be taken as a sign that that tracker doesn't support
     * the scrape convention. If it does, substitute 'scrape' for
     * 'announce' to find the scrape page.  */
    if((( s = strrchr( announce, '/' ))) && !strncmp( ++s, "announce", 8 ))
    {
        struct evbuffer * buf = evbuffer_new( );
        evbuffer_add( buf, announce, s-announce );
        evbuffer_add( buf, "scrape", 6 );
        evbuffer_add_printf( buf, "%s", s+8 );
        scrape = tr_strdup( ( char * ) EVBUFFER_DATA( buf ) );
        evbuffer_free( buf );
    }

    return scrape;
}

static int
getannounce( tr_info * inf, tr_benc * meta )
{
    const char * str;
    tr_tracker_info * trackers = NULL;
    int trackerCount = 0;
    tr_benc * tiers;

    /* Announce-list */
    if( tr_bencDictFindList( meta, "announce-list", &tiers ) )
    {
        int n;
        int i, j;

        n = 0;
        for( i=0; i<tiers->val.l.count; ++i )
            n += tiers->val.l.vals[i].val.l.count;

        trackers = tr_new0( tr_tracker_info, n );
        trackerCount = 0;

        for( i=0; i<tiers->val.l.count; ++i ) {
            const tr_benc * tier = &tiers->val.l.vals[i];
            for( j=0; tr_bencIsList(tier) && j<tier->val.l.count; ++j ) {
                const tr_benc * a = &tier->val.l.vals[j];
                if( tr_bencIsString( a ) && tr_httpIsValidURL( a->val.s.s ) ) {
                    tr_tracker_info * t = trackers + trackerCount++;
                    t->tier = i;
                    t->announce = tr_strndup( a->val.s.s, a->val.s.i );
                    t->scrape = announceToScrape( a->val.s.s );
                    /*fprintf( stderr, "tier %d: %s\n", i, a->val.s.s );*/
                }
            }
        }

        /* did we use any of the tiers? */
        if( !trackerCount ) {
            tr_inf( _( "Invalid metadata entry \"%s\"" ), "announce-list" );
            tr_free( trackers );
            trackers = NULL;
        }
    }

    /* Regular announce value */
    if( !trackerCount
        && tr_bencDictFindStr( meta, "announce", &str )
        && tr_httpIsValidURL( str ) )
    {
        trackers = tr_new0( tr_tracker_info, 1 );
        trackers[trackerCount].tier = 0;
        trackers[trackerCount].announce = tr_strdup( str );
        trackers[trackerCount++].scrape = announceToScrape( str );
        /*fprintf( stderr, "single announce: [%s]\n", str );*/
    }

    inf->trackers = trackers;
    inf->trackerCount = trackerCount;

    return inf->trackerCount ? TR_OK : TR_ERROR;
}

int
tr_metainfoParse( const tr_handle  * handle,
                  tr_info          * inf,
                  const tr_benc    * meta_in )
{
    tr_piece_index_t i;
    tr_benc * beInfo, * val, * val2;
    tr_benc * meta = (tr_benc *) meta_in;
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
        tr_err( _( "Missing metadata entry \"%s\"" ), "info" );
        return TR_EINVALID;
    }

    tr_sha1_to_hex( inf->hashString, inf->hash );

    /* comment */
    memset( buf, '\0', sizeof( buf ) );
    val = tr_bencDictFindFirst( meta, "comment.utf-8", "comment", NULL );
    if( tr_bencIsString( val ) )
        strlcat_utf8( buf, val->val.s.s, sizeof( buf ), 0 );
    tr_free( inf->comment );
    inf->comment = tr_strdup( buf );
    
    /* creator */
    memset( buf, '\0', sizeof( buf ) );
    val = tr_bencDictFindFirst( meta, "created by.utf-8", "created by", NULL );
    if( tr_bencIsString( val ) )
        strlcat_utf8( buf, val->val.s.s, sizeof( buf ), 0 );
    tr_free( inf->creator );
    inf->creator = tr_strdup( buf );
    
    /* Date created */
    inf->dateCreated = 0;
    val = tr_bencDictFind( meta, "creation date" );
    if( tr_bencIsInt( val ) )
        inf->dateCreated = val->val.i;
    
    /* Private torrent */
    val  = tr_bencDictFind( beInfo, "private" );
    val2 = tr_bencDictFind( meta,  "private" );
    if( ( tr_bencIsInt(val) && val->val.i ) ||
        ( tr_bencIsInt(val2) && val2->val.i ) )
    {
        inf->isPrivate = 1;
    }
    
    /* Piece length */
    val = tr_bencDictFind( beInfo, "piece length" );
    if( !tr_bencIsInt( val ) )
    {
        if( val )
            tr_err( _( "Invalid metadata entry \"%s\"" ), "piece length" );
        else
            tr_err( _( "Missing metadata entry \"%s\"" ), "piece length" );
        goto fail;
    }
    inf->pieceSize = val->val.i;

    /* Hashes */
    val = tr_bencDictFind( beInfo, "pieces" );
    if( !tr_bencIsString( val ) )
    {
        if( val )
            tr_err( _( "Invalid metadata entry \"%s\"" ), "pieces" );
        else
            tr_err( _( "Missing metadata entry \"%s\"" ), "pieces" );
        goto fail;
    }
    if( val->val.s.i % SHA_DIGEST_LENGTH )
    {
        tr_err( _( "Invalid metadata entry \"%s\"" ), "pieces" );
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

    if( !inf->fileCount || !inf->totalSize )
    {
        tr_err( _( "Torrent is corrupt" ) ); /* the content is missing! */
        goto fail;
    }

    /* TODO add more tests so we don't crash on weird files */

    if( (uint64_t) inf->pieceCount !=
        ( inf->totalSize + inf->pieceSize - 1 ) / inf->pieceSize )
    {
        tr_err( _( "Torrent is corrupt" ) ); /* size of hashes and files don't match */
        goto fail;
    }

    /* get announce or announce-list */
    if( getannounce( inf, meta ) )
        goto fail;

    /* filename of Transmission's copy */
    getTorrentFilename( handle, inf, buf, sizeof( buf ) );
    tr_free( inf->torrent );
    inf->torrent = tr_strdup( buf );

    return TR_OK;

  fail:
    tr_metainfoFree( inf );
    return TR_EINVALID;
}

void tr_metainfoFree( tr_info * inf )
{
    tr_file_index_t ff;
    int i;

    for( ff=0; ff<inf->fileCount; ++ff )
        tr_free( inf->files[ff].name );

    tr_free( inf->pieces );
    tr_free( inf->files );
    tr_free( inf->comment );
    tr_free( inf->creator );
    tr_free( inf->torrent );
    tr_free( inf->name );
    
    for( i=0; i<inf->trackerCount; ++i ) {
        tr_free( inf->trackers[i].announce );
        tr_free( inf->trackers[i].scrape );
    }
    tr_free( inf->trackers );

    memset( inf, '\0', sizeof(tr_info) );
}

static int
getfile( char ** setme, const char * prefix, tr_benc * name )
{
    const char ** list;
    int           ii, jj;
    char          buf[4096];

    if( !tr_bencIsList( name ) )
        return TR_EINVALID;

    list = calloc( name->val.l.count, sizeof( list[0] ) );
    if( !list )
        return TR_EINVALID;

    for( ii = jj = 0; name->val.l.count > ii; ii++ )
    {
        tr_benc * dir = &name->val.l.vals[ii];

        if( !tr_bencIsString( dir ) )
            continue;

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

void
tr_metainfoRemoveSaved( const tr_handle * handle,
                        const tr_info   * inf )
{
    char filename[MAX_PATH_LENGTH];

    getTorrentFilename( handle, inf, filename, sizeof( filename ) );
    unlink( filename );

    getTorrentOldFilename( handle, inf, filename, sizeof( filename ) );
    unlink( filename );
}

static int
parseFiles( tr_info * inf, tr_benc * name,
            tr_benc * files, tr_benc * length )
{
    tr_benc * item, * path;
    int ii;
    char buf[4096];

    if( !tr_bencIsString( name ) )
    {
        if( name )
            tr_err( _( "Invalid metadata entry \"%s\"" ), "name" );
        else
            tr_err( _( "Missing metadata entry \"%s\"" ), "name" );
        return TR_EINVALID;
    }

    memset( buf, 0, sizeof( buf ) );
    strlcat_utf8( buf, name->val.s.s, sizeof( buf ), 0 );
    tr_free( inf->name );
    inf->name = tr_strdup( buf );
    if( !inf->name || !*inf->name )
    {
        tr_err( _( "Invalid metadata entry \"%s\"" ), "name" );
        return TR_EINVALID;
    }
    inf->totalSize = 0;

    if( tr_bencIsList( files ) )
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
                if( path )
                    tr_err( _( "Invalid metadata entry \"%s\"" ), "path" );
                else
                    tr_err( _( "Missing metadata entry \"%s\"" ), "path" );
                return TR_EINVALID;
            }
            length = tr_bencDictFind( item, "length" );
            if( !tr_bencIsInt( length ) )
            {
                if( length )
                    tr_err( _( "Invalid metadata entry \"%s\"" ), "length" );
                else
                    tr_err( _( "Missing metadata entry \"%s\"" ), "length" );
                return TR_EINVALID;
            }
            inf->files[ii].length = length->val.i;
            inf->totalSize         += length->val.i;
        }
    }
    else if( tr_bencIsInt( length ) )
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
        tr_err( _( "Invalid or missing metadata entries \"length\" and \"files\"" ) );
    }

    return TR_OK;
}
