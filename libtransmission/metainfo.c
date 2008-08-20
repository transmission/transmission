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
static int parseFiles( tr_info * inf, tr_benc * files, tr_benc * length );

/***
****
***/

static char*
getTorrentFilename( const tr_handle  * handle,
                    const tr_info    * inf )
{
    return tr_strdup_printf( "%s%c%s.%16.16s.torrent",
                             tr_getTorrentDir( handle ),
                             TR_PATH_DELIMITER,
                             inf->name,
                             inf->hashString );
}

static char*
getTorrentOldFilename( const tr_handle * handle,
                       const tr_info   * inf )
{
    char * ret;
    struct evbuffer * buf = evbuffer_new( );
    evbuffer_add_printf( buf, "%s%c%s", 
                         tr_getTorrentDir( handle ),
                         TR_PATH_DELIMITER,
                         inf->hashString );
    if( handle->tag )
        evbuffer_add_printf( buf, "-%s", handle->tag );
    ret = tr_strndup( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
    evbuffer_free( buf );
    return ret;
}

void
tr_metainfoMigrate( tr_handle * handle,
                    tr_info   * inf )
{
    struct stat new_sb;
    char * new_name = getTorrentFilename( handle, inf );

    if( stat( new_name, &new_sb ) || ( ( new_sb.st_mode & S_IFMT ) != S_IFREG ) )
    {
        char * old_name = getTorrentOldFilename( handle, inf );
        size_t contentLen;
        uint8_t * content;

        tr_mkdirp( tr_getTorrentDir( handle ), 0777 );
        if(( content = tr_loadFile( old_name, &contentLen )))
        {
            FILE * out;
            errno = 0;
            out = fopen( new_name, "wb+" );
            if( !out )
            {
                tr_nerr( inf->name, _( "Couldn't create \"%1$s\": %2$s" ), new_name, tr_strerror( errno ) );
            }
            else
            {
                if( fwrite( content, sizeof( uint8_t ), contentLen, out ) == contentLen )
                {
                    tr_free( inf->torrent );
                    inf->torrent = tr_strdup( new_name );
                    tr_sessionSetTorrentFile( handle, inf->hashString, new_name );
                    unlink( old_name );
                }
                fclose( out );
            }
        }

        tr_free( content );
        tr_free( old_name );
    }

    tr_free( new_name );
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

static void
geturllist( tr_info * inf, tr_benc * meta )
{
    benc_val_t * urls;

    if( tr_bencDictFindList( meta, "url-list", &urls ) )
    {
        int i;
        const char * url;
        const int n = tr_bencListSize( urls );

        inf->webseedCount = 0;
        inf->webseeds = tr_new0( char*, n );

        for( i=0; i<n; ++i )
            if( tr_bencGetStr( tr_bencListChild( urls, i ), &url ) )
                inf->webseeds[inf->webseedCount++] = tr_strdup( url );
    }
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
            tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), "announce-list" );
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

    if( !inf->trackerCount )
        tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), "announce" );

    return inf->trackerCount ? TR_OK : TR_ERROR;
}

int
tr_metainfoParse( const tr_handle  * handle,
                  tr_info          * inf,
                  const tr_benc    * meta_in )
{
    int64_t i;
    const char * str;
    const uint8_t * raw;
    size_t rawlen;
    tr_piece_index_t pi;
    tr_benc * beInfo;
    tr_benc * meta = (tr_benc *) meta_in;

    /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
     * from the Metainfo file. Note that the value will be a bencoded 
     * dictionary, given the definition of the info key above. */
    if( tr_bencDictFindDict( meta, "info", &beInfo ) )
    {
        int len;
        char * str = tr_bencSave( beInfo, &len );
        tr_sha1( inf->hash, str, len, NULL );
        tr_sha1_to_hex( inf->hashString, inf->hash );
        tr_free( str );
    }
    else
    {
        tr_err( _( "Missing metadata entry \"%s\"" ), "info" );
        return TR_EINVALID;
    }

    /* name */
    if( !tr_bencDictFindStr( beInfo, "name.utf-8", &str ) )
        if( !tr_bencDictFindStr( beInfo, "name", &str ) )
            str = NULL;
    tr_free( inf->name );
    inf->name = tr_strdup( str );
    if( !str || !*str ) {
        tr_err( _( "Invalid metadata entry \"%s\"" ), "name" );
        return TR_EINVALID;
    }

    /* comment */
    if( !tr_bencDictFindStr( meta, "comment.utf-8", &str ) )
        if( !tr_bencDictFindStr( meta, "comment", &str ) )
            str = "";
    tr_free( inf->comment );
    inf->comment = tr_strdup( str );
    
    /* creator */
    if( !tr_bencDictFindStr( meta, "created by.utf-8", &str ) )
        if( !tr_bencDictFindStr( meta, "created by", &str ) )
            str = "";
    tr_free( inf->creator );
    inf->creator = tr_strdup( str );
    
    /* Date created */
    if( !tr_bencDictFindInt( meta, "creation date", &i ) )
        i = 0;
    inf->dateCreated = i;
    
    /* Private torrent */
    if( !tr_bencDictFindInt( beInfo, "private", &i ) )
        if( !tr_bencDictFindInt( meta, "private", &i ) )
            i = 0;
    inf->isPrivate = i != 0;
    
    /* Piece length */
    if( tr_bencDictFindInt( beInfo, "piece length", &i ) && ( i > 0 ) )
        inf->pieceSize = i;
    else {
        tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), "piece length" );
        goto fail;
    }

    /* Hashes */
    if( !tr_bencDictFindRaw( beInfo, "pieces", &raw, &rawlen ) || ( rawlen % SHA_DIGEST_LENGTH ) ) {
        tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), "pieces" );
        goto fail;
    }
    inf->pieceCount = rawlen / SHA_DIGEST_LENGTH;
    inf->pieces = tr_new0( tr_piece, inf->pieceCount );
    for ( pi=0; pi<inf->pieceCount; ++pi )
        memcpy( inf->pieces[pi].hash, &raw[pi*SHA_DIGEST_LENGTH], SHA_DIGEST_LENGTH );

    /* files */
    if( parseFiles( inf, tr_bencDictFind( beInfo, "files" ),
                         tr_bencDictFind( beInfo, "length" ) ) )
    {
        goto fail;
    }

    if( !inf->fileCount || !inf->totalSize )
    {
        tr_nerr( inf->name, _( "Torrent is corrupt" ) ); /* the content is missing! */
        goto fail;
    }

    /* TODO add more tests so we don't crash on weird files */

    if( (uint64_t) inf->pieceCount !=
        ( inf->totalSize + inf->pieceSize - 1 ) / inf->pieceSize )
    {
        tr_nerr( inf->name, _( "Torrent is corrupt" ) ); /* size of hashes and files don't match */
        goto fail;
    }

    /* get announce or announce-list */
    if( getannounce( inf, meta ) )
        goto fail;

    /* get the url-list */
    geturllist( inf, meta );

    /* filename of Transmission's copy */
    tr_free( inf->torrent );
    inf->torrent = getTorrentFilename( handle, inf );

    return TR_OK;

  fail:
    tr_metainfoFree( inf );
    return TR_EINVALID;
}

void tr_metainfoFree( tr_info * inf )
{
    tr_file_index_t ff;
    int i;

    for( i=0; i<inf->webseedCount; ++i )
        tr_free( inf->webseeds[i] );

    for( ff=0; ff<inf->fileCount; ++ff )
        tr_free( inf->files[ff].name );

    tr_free( inf->webseeds );
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
getfile( char ** setme, const char * root, tr_benc * path )
{
    int err;

    if( !tr_bencIsList( path ) )
    {
        err = TR_EINVALID;
    }
    else
    {
        struct evbuffer * buf = evbuffer_new( );
        int n = tr_bencListSize( path );
        int i;

        evbuffer_add( buf, root, strlen( root ) );
        for( i=0; i<n; ++i ) {
            const char * str;
            if( tr_bencGetStr( tr_bencListChild( path, i ), &str ) && strcmp( str, ".." ) ) {
                evbuffer_add( buf, TR_PATH_DELIMITER_STR, 1 );
                evbuffer_add( buf, str, strlen( str ) );
            }
        }

        *setme = tr_strndup( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
        /* fprintf( stderr, "[%s]\n", *setme ); */
        evbuffer_free( buf );
        err = TR_OK;
    }

    return err;
}

void
tr_metainfoRemoveSaved( const tr_handle * handle,
                        const tr_info   * inf )
{
    char * filename;

    filename = getTorrentFilename( handle, inf );
    unlink( filename );
    tr_free( filename );

    filename = getTorrentOldFilename( handle, inf );
    unlink( filename );
    tr_free( filename );
}

static int
parseFiles( tr_info * inf, tr_benc * files, tr_benc * length )
{
    tr_benc * item, * path;
    int ii;
    inf->totalSize = 0;

    if( tr_bencIsList( files ) )
    {
        /* Multi-file mode */
        inf->isMultifile = 1;
        inf->fileCount   = files->val.l.count;
        inf->files       = tr_new0( tr_file, inf->fileCount );

        if( !inf->files )
            return TR_EINVALID;

        for( ii = 0; files->val.l.count > ii; ii++ )
        {
            item = &files->val.l.vals[ii];
            path = tr_bencDictFindFirst( item, "path.utf-8", "path", NULL );
            if( getfile( &inf->files[ii].name, inf->name, path ) )
            {
                if( path )
                    tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), "path" );
                else
                    tr_nerr( inf->name, _( "Missing metadata entry \"%s\"" ), "path" );
                return TR_EINVALID;
            }
            length = tr_bencDictFind( item, "length" );
            if( !tr_bencIsInt( length ) )
            {
                if( length )
                    tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), "length" );
                else
                    tr_nerr( inf->name, _( "Missing metadata entry \"%s\"" ), "length" );
                return TR_EINVALID;
            }
            inf->files[ii].length = length->val.i;
            inf->totalSize         += length->val.i;
        }
    }
    else if( tr_bencIsInt( length ) )
    {
        /* Single-file mode */
        inf->isMultifile = 0;
        inf->fileCount = 1;
        inf->files = tr_new0( tr_file, 1 );

        if( !inf->files )
            return TR_EINVALID;

        tr_free( inf->files[0].name );
        inf->files[0].name   = tr_strdup( inf->name );
        inf->files[0].length = length->val.i;
        inf->totalSize      += length->val.i;
    }
    else
    {
        tr_nerr( inf->name, _( "Invalid or missing metadata entries \"length\" and \"files\"" ) );
    }

    return TR_OK;
}
