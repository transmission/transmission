/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
            trackers[trackerCount].id = 0;
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlink, stat */

#include <event.h> /* struct evbuffer */

#include "transmission.h"
#include "session.h"
#include "bencode.h"
#include "crypto.h" /* tr_sha1 */
#include "metainfo.h"
#include "platform.h"
#include "utils.h"

/***
****
***/

char*
tr_metainfoGetBasename( const tr_info * inf )
{
    char *ret, *pch, *name;

    name = tr_strdup( inf->name );
    for( pch=name; pch && *pch; ++pch )
        if( *pch == '/' )
            *pch = '_';
   
    ret = tr_strdup_printf( "%s.%16.16s", name, inf->hashString );

    tr_free( name );
    return ret;
}

static char*
getTorrentFilename( const tr_session * session,
                    const tr_info *   inf )
{
    char * base = tr_metainfoGetBasename( inf );
    char * filename = tr_strdup_printf( "%s" TR_PATH_DELIMITER_STR "%s.torrent",
                                        tr_getTorrentDir( session ), base );
    tr_free( base );
    return filename;
}

static char*
getOldTorrentFilename( const tr_session * session, const tr_info * inf )
{
    int i;
    char * path;
    struct stat sb;
    const int tagCount = 5;
    const char * tags[] = { "beos", "cli", "daemon", "macosx", "wx" };

    /* test the beos, cli, daemon, macosx, wx tags */
    for( i=0; i<tagCount; ++i ) {
        path = tr_strdup_printf( "%s%c%s-%s", tr_getTorrentDir( session ), '/', inf->hashString, tags[i] );
        if( !stat( path, &sb ) && ( ( sb.st_mode & S_IFMT ) == S_IFREG ) )
            return path;
        tr_free( path );
    }

    /* test a non-tagged file */
    path = tr_buildPath( tr_getTorrentDir( session ), inf->hashString, NULL );
    if( !stat( path, &sb ) && ( ( sb.st_mode & S_IFMT ) == S_IFREG ) )
        return path;
    tr_free( path );

    /* return the -gtk form by default, since that's the most common case.
       don't bother testing stat() on it since this is the last candidate
       and we don't want to return NULL anyway */
    return tr_strdup_printf( "%s%c%s-%s", tr_getTorrentDir( session ), '/', inf->hashString, "gtk" );
}

/* this is for really old versions of T and will probably be removed someday */
void
tr_metainfoMigrate( tr_session * session,
                    tr_info *   inf )
{
    struct stat new_sb;
    char *      name = getTorrentFilename( session, inf );

    if( stat( name, &new_sb ) || ( ( new_sb.st_mode & S_IFMT ) != S_IFREG ) )
    {
        char *    old_name = getOldTorrentFilename( session, inf );
        size_t    contentLen;
        uint8_t * content;

        tr_mkdirp( tr_getTorrentDir( session ), 0777 );
        if( ( content = tr_loadFile( old_name, &contentLen ) ) )
        {
            FILE * out;
            errno = 0;
            out = fopen( name, "wb+" );
            if( !out )
            {
                tr_nerr( inf->name, _( "Couldn't create \"%1$s\": %2$s" ),
                        name, tr_strerror( errno ) );
            }
            else
            {
                if( fwrite( content, sizeof( uint8_t ), contentLen, out )
                    == contentLen )
                {
                    tr_free( inf->torrent );
                    inf->torrent = tr_strdup( name );
                    tr_sessionSetTorrentFile( session, inf->hashString, name );
                    unlink( old_name );
                }
                fclose( out );
            }
        }

        tr_free( content );
        tr_free( old_name );
    }

    tr_free( name );
}

/***
****
***/

static tr_bool
path_is_suspicious( const char * path )
{
    return ( path == NULL )
        || ( strstr( path, "../" ) != NULL );
}

static tr_bool
getfile( char ** setme, const char * root, tr_benc * path )
{
    tr_bool success = FALSE;

    if( tr_bencIsList( path ) )
    {
        int i;
        const int n = tr_bencListSize( path );
        struct evbuffer * buf = evbuffer_new( );

        evbuffer_add( buf, root, strlen( root ) );
        for( i = 0; i < n; ++i )
        {
            const char * str;
            if( tr_bencGetStr( tr_bencListChild( path, i ), &str ) )
            {
                evbuffer_add( buf, TR_PATH_DELIMITER_STR, 1 );
                evbuffer_add( buf, str, strlen( str ) );
            }
        }

        *setme = tr_utf8clean( (char*)EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
        /* fprintf( stderr, "[%s]\n", *setme ); */
        evbuffer_free( buf );
        success = TRUE;
    }

    if( ( *setme != NULL ) && path_is_suspicious( *setme ) )
    {
        tr_free( *setme );
        *setme = NULL;
        success = FALSE;
    }

    return success;
}

static const char*
parseFiles( tr_info * inf, tr_benc * files, const tr_benc * length )
{
    int64_t len;

    inf->totalSize = 0;

    if( tr_bencIsList( files ) ) /* multi-file mode */
    {
        tr_file_index_t i;

        inf->isMultifile = 1;
        inf->fileCount   = tr_bencListSize( files );
        inf->files       = tr_new0( tr_file, inf->fileCount );

        for( i = 0; i < inf->fileCount; ++i )
        {
            tr_benc * file;
            tr_benc * path;

            file = tr_bencListChild( files, i );
            if( !tr_bencIsDict( file ) )
                return "files";

            if( !tr_bencDictFindList( file, "path.utf-8", &path ) )
                if( !tr_bencDictFindList( file, "path", &path ) )
                    return "path";

            if( !getfile( &inf->files[i].name, inf->name, path ) )
                return "path";

            if( !tr_bencDictFindInt( file, "length", &len ) )
                return "length";

            inf->files[i].length = len;
            inf->totalSize      += len;
        }
    }
    else if( tr_bencGetInt( length, &len ) ) /* single-file mode */
    {
        if( path_is_suspicious( inf->name ) )
            return "path";

        inf->isMultifile      = 0;
        inf->fileCount        = 1;
        inf->files            = tr_new0( tr_file, 1 );
        inf->files[0].name    = tr_strdup( inf->name );
        inf->files[0].length  = len;
        inf->totalSize       += len;
    }
    else
    {
        return "length";
    }

    return NULL;
}

static char *
tr_convertAnnounceToScrape( const char * announce )
{
    char *       scrape = NULL;
    const char * s;

    /* To derive the scrape URL use the following steps:
     * Begin with the announce URL. Find the last '/' in it.
     * If the text immediately following that '/' isn't 'announce'
     * it will be taken as a sign that that tracker doesn't support
     * the scrape convention. If it does, substitute 'scrape' for
     * 'announce' to find the scrape page.  */
    if( ( ( s = strrchr( announce, '/' ) ) ) && !strncmp( ++s, "announce", 8 ) )
    {
        const char * prefix = announce;
        const size_t prefix_len = s - announce;
        const char * suffix = s + 8;
        const size_t suffix_len = strlen( suffix );
        const size_t alloc_len = prefix_len + 6 + suffix_len + 1;
        char * walk = scrape = tr_new( char, alloc_len );
        memcpy( walk, prefix, prefix_len ); walk += prefix_len;
        memcpy( walk, "scrape", 6 );        walk += 6;
        memcpy( walk, suffix, suffix_len ); walk += suffix_len;
        *walk++ = '\0';
        assert( walk - scrape == (int)alloc_len );
    }

    return scrape;
}

static const char*
getannounce( tr_info * inf, tr_benc * meta )
{
    const char *      str;
    tr_tracker_info * trackers = NULL;
    int               trackerCount = 0;
    tr_benc *         tiers;

    /* Announce-list */
    if( tr_bencDictFindList( meta, "announce-list", &tiers ) )
    {
        int       n;
        int       i, j, validTiers;
        const int numTiers = tr_bencListSize( tiers );

        n = 0;
        for( i = 0; i < numTiers; ++i )
            n += tr_bencListSize( tr_bencListChild( tiers, i ) );

        trackers = tr_new0( tr_tracker_info, n );

        for( i = 0, validTiers = 0; i < numTiers; ++i )
        {
            tr_benc * tier = tr_bencListChild( tiers, i );
            const int tierSize = tr_bencListSize( tier );
            tr_bool anyAdded = FALSE;
            for( j = 0; j < tierSize; ++j )
            {
                if( tr_bencGetStr( tr_bencListChild( tier, j ), &str ) )
                {
                    char * url = tr_strstrip( tr_strdup( str ) );
                    if( tr_urlIsValidTracker( url ) )
                    {
                        tr_tracker_info * t = trackers + trackerCount;
                        t->tier = validTiers;
                        t->announce = tr_strdup( url );
                        t->scrape = tr_convertAnnounceToScrape( url );
                        t->id = trackerCount;

                        anyAdded = TRUE;
                        ++trackerCount;
                    }
                    tr_free( url );
                }
            }

            if( anyAdded )
                ++validTiers;
        }

        /* did we use any of the tiers? */
        if( !trackerCount )
        {
            tr_free( trackers );
            trackers = NULL;
        }
    }

    /* Regular announce value */
    if( !trackerCount
      && tr_bencDictFindStr( meta, "announce", &str ) )
    {
        char * url = tr_strstrip( tr_strdup( str ) );
        if( tr_urlIsValidTracker( url ) )
        {
            trackers = tr_new0( tr_tracker_info, 1 );
            trackers[trackerCount].tier = 0;
            trackers[trackerCount].announce = tr_strdup( url );
            trackers[trackerCount].scrape = tr_convertAnnounceToScrape( url );
            trackers[trackerCount].id = 0;
            trackerCount++;
            /*fprintf( stderr, "single announce: [%s]\n", url );*/
        }
        tr_free( url );
    }

    inf->trackers = trackers;
    inf->trackerCount = trackerCount;

    return NULL;
}

static void
geturllist( tr_info * inf,
            tr_benc * meta )
{
    tr_benc * urls;
    const char * url;

    if( tr_bencDictFindList( meta, "url-list", &urls ) )
    {
        int          i;
        const int    n = tr_bencListSize( urls );

        inf->webseedCount = 0;
        inf->webseeds = tr_new0( char*, n );

        for( i = 0; i < n; ++i )
            if( tr_bencGetStr( tr_bencListChild( urls, i ), &url ) )
                inf->webseeds[inf->webseedCount++] = tr_strdup( url );
    }
    else if( tr_bencDictFindStr( meta, "url-list", &url ) ) /* handle single items in webseeds */
    {
        inf->webseedCount = 1;
        inf->webseeds = tr_new0( char*, 1 );
        inf->webseeds[0] = tr_strdup( url );
    }
}

static int
is_rfc2396_alnum( char ch )
{
    return ( '0' <= ch && ch <= '9' )
        || ( 'A' <= ch && ch <= 'Z' )
        || ( 'a' <= ch && ch <= 'z' );
}

static void
escape( char * out, const uint8_t * in, size_t in_len ) /* rfc2396 */
{
    const uint8_t *end = in + in_len;

    while( in != end )
        if( is_rfc2396_alnum( *in ) )
            *out++ = (char) *in++;
        else
            out += tr_snprintf( out, 4, "%%%02X", (unsigned int)*in++ );

    *out = '\0';
}

static const char*
tr_metainfoParseImpl( const tr_session  * session,
                      tr_info           * inf,
                      tr_bool           * hasInfoDict,
                      int               * infoDictLength,
                      const tr_benc     * meta_in )
{
    int64_t         i;
    size_t          raw_len;
    const char *    str;
    const uint8_t * raw;
    tr_benc *       d;
    tr_benc *       infoDict = NULL;
    tr_benc *       meta = (tr_benc *) meta_in;
    tr_bool         b;
    tr_bool         isMagnet = FALSE;

    /* info_hash: urlencoded 20-byte SHA1 hash of the value of the info key
     * from the Metainfo file. Note that the value will be a bencoded
     * dictionary, given the definition of the info key above. */
    b = tr_bencDictFindDict( meta, "info", &infoDict );
    if( hasInfoDict != NULL )
        *hasInfoDict = b;
    if( !b )
    {
        /* no info dictionary... is this a magnet link? */
        if( tr_bencDictFindDict( meta, "magnet-info", &d ) )
        {
            isMagnet = TRUE;

            /* get the info-hash */
            if( !tr_bencDictFindRaw( d, "info_hash", &raw, &raw_len ) )
                return "info_hash";
            if( raw_len != SHA_DIGEST_LENGTH )
                return "info_hash";
            memcpy( inf->hash, raw, raw_len );
            tr_sha1_to_hex( inf->hashString, inf->hash );
            escape( inf->hashEscaped, inf->hash, SHA_DIGEST_LENGTH );

            /* maybe get the display name */
          
            if( tr_bencDictFindStr( d, "display-name", &str ) ) {
                tr_free( inf->name );
                inf->name = tr_strdup( str );
            }

            if( !inf->name )
                inf->name = tr_strdup( inf->hashString );
        }
        else /* not a magnet link and has no info dict... */
        {
            return "info";
        }
    }
    else
    {
        int len;
        char * bstr = tr_bencToStr( infoDict, TR_FMT_BENC, &len );
        tr_sha1( inf->hash, bstr, len, NULL );
        tr_sha1_to_hex( inf->hashString, inf->hash );
        escape( inf->hashEscaped, inf->hash, SHA_DIGEST_LENGTH );

        if( infoDictLength != NULL )
            *infoDictLength = len;

        tr_free( bstr );
    }

    /* name */
    if( !isMagnet ) {
        if( !tr_bencDictFindStr( infoDict, "name.utf-8", &str ) )
            if( !tr_bencDictFindStr( infoDict, "name", &str ) )
                str = "";
        if( !str || !*str )
            return "name";
        tr_free( inf->name );
        inf->name = tr_utf8clean( str, -1 );
    }

    /* comment */
    if( !tr_bencDictFindStr( meta, "comment.utf-8", &str ) )
        if( !tr_bencDictFindStr( meta, "comment", &str ) )
            str = "";
    tr_free( inf->comment );
    inf->comment = tr_utf8clean( str, -1 );

    /* created by */
    if( !tr_bencDictFindStr( meta, "created by.utf-8", &str ) )
        if( !tr_bencDictFindStr( meta, "created by", &str ) )
            str = "";
    tr_free( inf->creator );
    inf->creator = tr_utf8clean( str, -1 );

    /* creation date */
    if( !tr_bencDictFindInt( meta, "creation date", &i ) )
        i = 0;
    inf->dateCreated = i;

    /* private */
    if( !tr_bencDictFindInt( infoDict, "private", &i ) )
        if( !tr_bencDictFindInt( meta, "private", &i ) )
            i = 0;
    inf->isPrivate = i != 0;

    /* piece length */
    if( !isMagnet ) {
        if( !tr_bencDictFindInt( infoDict, "piece length", &i ) || ( i < 1 ) )
            return "piece length";
        inf->pieceSize = i;
    }

    /* pieces */
    if( !isMagnet ) {
        if( !tr_bencDictFindRaw( infoDict, "pieces", &raw, &raw_len ) )
            return "pieces";
        if( raw_len % SHA_DIGEST_LENGTH )
            return "pieces";
        inf->pieceCount = raw_len / SHA_DIGEST_LENGTH;
        inf->pieces = tr_new0( tr_piece, inf->pieceCount );
        for( i = 0; i < inf->pieceCount; ++i )
            memcpy( inf->pieces[i].hash, &raw[i * SHA_DIGEST_LENGTH],
                    SHA_DIGEST_LENGTH );
    }

    /* files */
    if( !isMagnet ) {
        if( ( str = parseFiles( inf, tr_bencDictFind( infoDict, "files" ),
                                     tr_bencDictFind( infoDict, "length" ) ) ) )
            return str;
        if( !inf->fileCount || !inf->totalSize )
            return "files";
        if( (uint64_t) inf->pieceCount !=
           ( inf->totalSize + inf->pieceSize - 1 ) / inf->pieceSize )
            return "files";
    }

    /* get announce or announce-list */
    if( ( str = getannounce( inf, meta ) ) )
        return str;

    /* get the url-list */
    geturllist( inf, meta );

    /* filename of Transmission's copy */
    tr_free( inf->torrent );
    inf->torrent = session ?  getTorrentFilename( session, inf ) : NULL;

    return NULL;
}

tr_bool
tr_metainfoParse( const tr_session * session,
                  const tr_benc    * meta_in,
                  tr_info          * inf,
                  tr_bool          * hasInfoDict,
                  int              * infoDictLength )
{
    const char * badTag = tr_metainfoParseImpl( session,
                                                inf,
                                                hasInfoDict,
                                                infoDictLength,
                                                meta_in );
    const tr_bool success = badTag == NULL;

    if( badTag )
    {
        tr_nerr( inf->name, _( "Invalid metadata entry \"%s\"" ), badTag );
        tr_metainfoFree( inf );
    }

    return success;
}

void
tr_metainfoFree( tr_info * inf )
{
    int i;
    tr_file_index_t ff;

    for( i = 0; i < inf->webseedCount; ++i )
        tr_free( inf->webseeds[i] );

    for( ff = 0; ff < inf->fileCount; ++ff )
        tr_free( inf->files[ff].name );

    tr_free( inf->webseeds );
    tr_free( inf->pieces );
    tr_free( inf->files );
    tr_free( inf->comment );
    tr_free( inf->creator );
    tr_free( inf->torrent );
    tr_free( inf->name );

    for( i = 0; i < inf->trackerCount; ++i )
    {
        tr_free( inf->trackers[i].announce );
        tr_free( inf->trackers[i].scrape );
    }
    tr_free( inf->trackers );

    memset( inf, '\0', sizeof( tr_info ) );
}

void
tr_metainfoRemoveSaved( const tr_session * session,
                        const tr_info *   inf )
{
    char * filename;

    filename = getTorrentFilename( session, inf );
    unlink( filename );
    tr_free( filename );

    filename = getOldTorrentFilename( session, inf );
    unlink( filename );
    tr_free( filename );
}
