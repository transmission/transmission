/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Joshua Elsasser
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <assert.h>
#include <ctype.h>
#include <event.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bsdqueue.h"
#include "bsdtree.h"
#include "client.h"
#include "errors.h"
#include "ipcparse.h"
#include "misc.h"
#include "transmission.h"
#include "trcompat.h"

#define BESTDECIMAL(d)          (10.0 > (d) ? 2 : (100.0 > (d) ? 1 : 0))

struct opts
{
    int               proxy;
    char           ** proxycmd;
    enum confpathtype type;
    const char      * sock;
    struct strlist    files;
    int               sendquit;
    int               port;
    int               map;
    int               uplimit;
    int               up;
    int               downlimit;
    int               down;
    int               listquick;
    int               listfull;
    int               startall;
    struct strlist    start;
    int               stopall;
    struct strlist    stop;
    int               removeall;
    struct strlist    remove;
    char              dir[MAXPATHLEN];
    int               pex;
};

struct torinfo
{
    int     id;
    int     infogood;
    int     statgood;
    char  * name;
    int64_t size;
    char  * state;
    int64_t eta;
    int64_t done;
    int64_t ratedown;
    int64_t rateup;
    int64_t totaldown;
    int64_t totalup;
    char  * errorcode;
    char  * errormsg;
    RB_ENTRY( torinfo ) idlinks;
    RB_ENTRY( torinfo ) namelinks;
};

RB_HEAD( torlist, torinfo );
RB_HEAD( tornames, torinfo );

struct torhash
{
    char                hash[SHA_DIGEST_LENGTH*2+1];
    int                 id;
    RB_ENTRY( torhash ) link;
};

RB_HEAD( torhashes, torhash );

static void   usage        ( const char *, ... );
static int    readargs     ( int, char **, struct opts * );
static int    numarg       ( const char * );
static int    hasharg      ( const char *, struct strlist *, int * );
static int    fileargs     ( struct strlist *, int, char * const * );
static void   listmsg      ( const struct cl_info * );
static void   infomsg      ( const struct cl_info * );
static void   statmsg      ( const struct cl_stat * );
static void   hashmsg      ( const struct cl_info * );
static float  fmtsize      ( int64_t, const char ** );
static char * strdup_noctrl( const char * );
static void   print_eta    ( int64_t );
static void   printlisting ( void );
static int    sendidreqs   ( void );
static int    toridcmp     ( struct torinfo *, struct torinfo * );
static int    tornamecmp   ( struct torinfo *, struct torinfo * );
static int    torhashcmp   ( struct torhash *, struct torhash * );

static struct torlist   gl_torinfo      = RB_INITIALIZER( &gl_torinfo );
static struct strlist * gl_starthashes  = NULL;
static struct strlist * gl_stophashes   = NULL;
static struct strlist * gl_removehashes = NULL;
static struct torhashes gl_hashids      = RB_INITIALIZER( &gl_hashids );
static int              gl_gotlistinfo  = 0;
static int              gl_gotliststat  = 0;

RB_GENERATE_STATIC( torlist,   torinfo, idlinks,   toridcmp );
RB_GENERATE_STATIC( tornames,  torinfo, namelinks, tornamecmp );
RB_GENERATE_STATIC( torhashes, torhash, link,      torhashcmp );

int
main( int argc, char ** argv )
{
    struct event_base * evbase;
    struct opts         o;
    char                sockpath[MAXPATHLEN];

    setmyname( argv[0] );
    if( 0 > readargs( argc, argv, &o ) )
    {
        exit( 1 );
    }

    signal( SIGPIPE, SIG_IGN );

    evbase = event_init();
    client_init( evbase );

    if( o.proxy )
    {
        client_new_cmd( o.proxycmd );
    }
    else
    {
        if( NULL == o.sock )
        {
            confpath( sockpath, sizeof sockpath, CONF_FILE_SOCKET, o.type );
            client_new_sock( sockpath );
        }
        else
        {
            client_new_sock( o.sock );
        }
    }

    if( ( o.sendquit                &&   0 > client_quit     (           ) ) ||
        ( '\0' != o.dir[0]          &&   0 > client_dir      ( o.dir     ) ) ||
        ( !SLIST_EMPTY( &o.files )  &&   0 > client_addfiles ( &o.files  ) ) ||
        ( o.startall                &&   0 > client_start    ( 0, NULL   ) ) ||
        ( o.stopall                 &&   0 > client_stop     ( 0, NULL   ) ) ||
        ( o.removeall               &&   0 > client_remove   ( 0, NULL   ) ) ||
        ( o.port                    &&   0 > client_port     ( o.port    ) ) ||
        ( 0 <= o.map                &&   0 > client_automap  ( o.map     ) ) ||
        ( 0 <= o.pex                &&   0 > client_pex      ( o.pex     ) ) ||
        ( o.uplimit                 &&   0 > client_uplimit  ( o.up      ) ) ||
        ( o.downlimit               &&   0 > client_downlimit( o.down    ) ) ||
        ( o.listquick               &&   0 > client_list     ( listmsg   ) ) ||
        ( o.listfull                && ( 0 > client_info     ( infomsg     ) ||
                                         0 > client_status   ( statmsg ) ) ) )
    {
        exit( 1 );
    }

    if( ( !o.startall  && !SLIST_EMPTY( &o.start  ) ) ||
        ( !o.stopall   && !SLIST_EMPTY( &o.stop   ) ) ||
        ( !o.removeall && !SLIST_EMPTY( &o.remove ) ) )
    {
        if( 0 > client_hashids( hashmsg ) )
        {
            exit( 1 );
        }
        gl_starthashes  = ( o.startall  ? NULL : &o.start  );
        gl_stophashes   = ( o.stopall   ? NULL : &o.stop   );
        gl_removehashes = ( o.removeall ? NULL : &o.remove );
    }

    event_dispatch();
    /* event_base_dispatch( evbase ); */

    return 1;
}

void
usage( const char * msg, ... )
{
    va_list ap;

    if( NULL != msg )
    {
        printf( "%s: ", getmyname() );
        va_start( ap, msg );
        vprintf( msg, ap );
        va_end( ap );
        printf( "\n" );
    }

    printf(
  "usage: %s [options]\n"
  "\n"
  "Transmission %s (r%d) http://transmission.m0k.org/\n"
  "A free, lightweight BitTorrent client with a simple, intuitive interface.\n"
  "\n"
  "  -a --add <torrent>        Add a torrent\n"
  "  -d --download-limit <int> Max download rate in KiB/s\n"
  "  -D --download-unlimited   No download rate limit\n"
  "  -e --enable-pex           Enable peer exchange\n"
  "  -E --disable-pex          Disable peer exchange\n"
  "  -f --folder <path>        Folder to set for new torrents\n"
  "  -h --help                 Display this message and exit\n"
  "  -i --info                 List all torrents with info hashes\n"
  "  -l --list                 List all torrents with status\n"
  "  -m --port-mapping         Automatic port mapping via NAT-PMP or UPnP\n"
  "  -M --no-port-mapping      Disable automatic port mapping\n"
  "  -p --port <int>           Port to listen for incoming connections on\n"
  "  -q --quit                 Quit the daemon\n"
  "  -r --remove <hash>        Remove the torrent with the given hash\n"
  "  -r --remove all           Remove all torrents\n"
  "  -s --start <hash>         Start the torrent with the given hash\n"
  "  -s --start all            Start all stopped torrents\n"
  "  -S --stop <hash>          Stop the torrent with the given hash\n"
  "  -S --stop all             Stop all running torrents\n"
  "  -t --type daemon          Use the daemon frontend, transmission-daemon\n"
  "  -t --type gtk             Use the GTK+ frontend, transmission-gtk\n"
  "  -t --type mac             Use the MacOS X frontend\n"
  "  -u --upload-limit <int>   Max upload rate in KiB/s\n"
  "  -U --upload-unlimited     No upload rate limit\n"
  "  -x --proxy                Use proxy command to connect to frontend\n",
            getmyname(), VERSION_STRING, VERSION_REVISION );
    exit( 0 );
}

int
readargs( int argc, char ** argv, struct opts * opts )
{
    char optstr[] = "a:d:DeEf:hilmMp:qr:s:S:t:u:Ux";
    struct option longopts[] =
    {
        { "add",                required_argument, NULL, 'a' },
        { "download-limit",     required_argument, NULL, 'd' },
        { "download-unlimited", no_argument,       NULL, 'D' },
        { "enable-pex",         no_argument,       NULL, 'e' },
        { "disable-pex",        no_argument,       NULL, 'E' },
        { "folder",             required_argument, NULL, 'f' },
        { "help",               no_argument,       NULL, 'h' },
        { "info",               no_argument,       NULL, 'i' },
        { "list",               no_argument,       NULL, 'l' },
        { "port-mapping",       no_argument,       NULL, 'm' },
        { "no-port-mapping",    no_argument,       NULL, 'M' },
        { "port",               required_argument, NULL, 'p' },
        { "quit",               no_argument,       NULL, 'q' },
        { "remove",             required_argument, NULL, 'r' },
        { "start",              required_argument, NULL, 's' },
        { "stop",               required_argument, NULL, 'S' },
        { "type",               required_argument, NULL, 't' },
        { "upload-limit",       required_argument, NULL, 'u' },
        { "upload-unlimited",   no_argument,       NULL, 'U' },
        { "proxy",              no_argument,       NULL, 'U' },
        { NULL, 0, NULL, 0 }
    };
    int opt, gotmsg;

    gotmsg = 0;
    bzero( opts, sizeof *opts );
    opts->type = CONF_PATH_TYPE_DAEMON;
    SLIST_INIT( &opts->files );
    opts->map = -1;
    opts->pex = -1;
    SLIST_INIT( &opts->start );
    SLIST_INIT( &opts->stop );
    SLIST_INIT( &opts->remove );

    while( 0 <= ( opt = getopt_long( argc, argv, optstr, longopts, NULL ) ) )
    {
        switch( opt )
        {
            case 'a':
                if( 0 > fileargs( &opts->files, 1, &optarg ) )
                {
                    return -1;
                }
                break;
            case 'd':
                opts->downlimit = 1;
                opts->down      = numarg( optarg );
                break;
            case 'D':
                opts->downlimit = 1;
                opts->down      = -1;
                break;
            case 'e':
                opts->pex       = 1;
                break;
            case 'E':
                opts->pex       = 0;
                break;
            case 'f':
                absolutify( opts->dir, sizeof opts->dir, optarg );
                break;
            case 'i':
                opts->listquick = 1;
                break;
            case 'l':
                opts->listfull  = 1;
                break;
            case 'm':
                opts->map       = 1;
                break;
            case 'M':
                opts->map       = 0;
                break;
            case 'p':
                opts->port      = numarg( optarg );
                if( 0 >= opts->port || 0xffff <= opts->port )
                {
                    usage( "invalid port: %i", opts->port );
                }
                break;
            case 'q':
                opts->sendquit  = 1;
                break;
            case 'r':
                if( 0 > hasharg( optarg, &opts->remove, &opts->removeall ) )
                {
                    return -1;
                }
                break;
            case 's':
                if( 0 > hasharg( optarg, &opts->start, &opts->startall ) )
                {
                    return -1;
                }
                break;
            case 'S':
                if( 0 > hasharg( optarg, &opts->stop, &opts->stopall ) )
                {
                    return -1;
                }
                break;
            case 't':
                if( 0 == strcasecmp( "daemon", optarg ) )
                {
                    opts->type  = CONF_PATH_TYPE_DAEMON;
                    opts->sock  = NULL;
                }
                else if( 0 == strcasecmp( "gtk", optarg ) )
                {
                    opts->type  = CONF_PATH_TYPE_GTK;
                    opts->sock  = NULL;
                }
                else if( 0 == strcasecmp( "mac", optarg ) ||
                         0 == strcasecmp( "osx", optarg ) ||
                         0 == strcasecmp( "macos", optarg ) ||
                         0 == strcasecmp( "macosx", optarg ) )
                {
                    opts->type  = CONF_PATH_TYPE_OSX;
                    opts->sock  = NULL;
                }
                else
                {
                    opts->sock  = optarg;
                }
                break;
            case 'u':
                opts->uplimit   = 1;
                opts->up        = numarg( optarg );
                break;
            case 'U':
                opts->uplimit   = 1;
                opts->up        = -1;
                break;
            case 'x':
                opts->proxy     = 1;
                break;
            default:
                usage( NULL );
                break;
        }
        gotmsg = 1;
    }

    if( !gotmsg && argc == optind )
    {
        usage( NULL );
    }

    if( opts->proxy )
    {
        opts->proxycmd = argv + optind;
    }
    else if( 0 > fileargs( &opts->files, argc - optind, argv + optind ) )
    {
        return -1;
    }

    return 0;
}

int
numarg( const char * arg )
{
    char * end;
    long   num;

    end = NULL;
    num = strtol( arg, &end, 10 );
    if( NULL != end && '\0' != *end )
    {
        usage( "not a number: %s", arg );
        return -1;
    }

    return num;
}

int
hasharg( const char * arg, struct strlist * list, int * all )
{
    struct stritem * listitem;
    struct torhash * treeitem, key, * foo;
    size_t           len, ii;

    /* check for special "all" value */
    if( 0 == strcasecmp( "all", arg ) )
    {
        *all = 1;
        return 0;
    }

    /* check hash length */
    len = strlen( arg );
    if( SHA_DIGEST_LENGTH * 2 != len )
    {
        usage( "torrent info hash length is not %i: %s",
               SHA_DIGEST_LENGTH * 2, arg );
        return -1;
    }

    /* allocate list item */
    listitem = calloc( 1, sizeof *listitem );
    if( NULL == listitem )
    {
        mallocmsg( sizeof *listitem );
        return -1;
    }
    listitem->str = calloc( len + 1, 1 );
    if( NULL == listitem->str )
    {
        mallocmsg( len + 1 );
        free( listitem );
        return -1;
    }

    /* check that the hash is all hex and copy it in lowercase */
    for( ii = 0; len > ii; ii++ )
    {
        if( !isxdigit( arg[ii] ) )
        {
            usage( "torrent info hash is not hex: %s", arg );
            free( listitem->str );
            free( listitem );
            return -1;
        }
        listitem->str[ii] = tolower( arg[ii] );
    }

    /* try to look up the hash in the hash tree */
    bzero( &key, sizeof key );
    strlcpy( key.hash, listitem->str, sizeof key.hash );
    treeitem = RB_FIND( torhashes, &gl_hashids, &key );
    if( NULL == treeitem )
    {
        /* the hash isn't in the tree, allocate a tree item and insert it */
        treeitem = calloc( 1, sizeof *treeitem );
        if( NULL == treeitem )
        {
            mallocmsg( sizeof *treeitem );
            free( listitem->str );
            free( listitem );
            return -1;
        }
        treeitem->id = -1;
        strlcpy( treeitem->hash, listitem->str, sizeof treeitem->hash );
        foo = RB_INSERT( torhashes, &gl_hashids, treeitem );
        assert( NULL == foo );
    }

    /* finally, add the list item to the list */
    SLIST_INSERT_HEAD( list, listitem, next );

    return 0;
}

int
fileargs( struct strlist * list, int argc, char * const * argv )
{
    struct stritem * item;
    int              ii;
    char             path[MAXPATHLEN];

    for( ii = 0; argc > ii; ii++ )
    {
        item = calloc( 1, sizeof *item );
        if( NULL == item )
        {
            mallocmsg( sizeof *item );
            return -1;
        }
        if( '/' == argv[ii][0] )
        {
            item->str = strdup( argv[ii] );
            if( NULL == item->str )
            {
                mallocmsg( strlen( argv[ii] ) + 1 );
                free( item );
                return -1;
            }
        }
        else
        {
            absolutify( path, sizeof path, argv[ii] );
            item->str = strdup( path );
            if( NULL == item->str )
            {
                mallocmsg( strlen( path ) + 1 );
                free( item );
                return -1;
            }
        }
        SLIST_INSERT_HEAD( list, item, next );
    }

    return 0;
}

void
listmsg( const struct cl_info * inf )
{
    char * newname;
    size_t ii;

    if( NULL == inf )
    {
        return;
    }

    if( NULL == inf->name )
    {
        errmsg( "missing torrent name from server" );
        return;
    }
    else if( NULL == inf->hash )
    {
        errmsg( "missing torrent hash from server" );
        return;
    }

    newname = strdup_noctrl( inf->name );
    if( NULL == newname )
    {
        return;
    }

    for( ii = 0; '\0' != inf->hash[ii]; ii++ )
    {
        if( isxdigit( inf->hash[ii] ) )
        {
            putchar( tolower( inf->hash[ii] ) );
        }
    }
    putchar( ' ' );
    printf( "%s\n", newname );
    free( newname );
}

void
infomsg( const struct cl_info * cinfo )
{
    struct torinfo * tinfo, key;

    gl_gotlistinfo = 1;

    if( NULL == cinfo )
    {
        if( gl_gotliststat )
        {
            printlisting();
        }
        return;
    }

    if( NULL == cinfo->name || 0 >= cinfo->size )
    {
        errmsg( "missing torrent info from server" );
        return;
    }

    bzero( &key, sizeof key );
    key.id = cinfo->id;
    tinfo = RB_FIND( torlist, &gl_torinfo, &key );
    if( NULL == tinfo )
    {
        tinfo = calloc( 1, sizeof *tinfo );
        if( NULL == tinfo )
        {
            mallocmsg( sizeof *tinfo );
            return;
        }
        tinfo->id = cinfo->id;
        RB_INSERT( torlist, &gl_torinfo, tinfo );
    }

    tinfo->infogood = 1;
    free( tinfo->name );
    tinfo->name = strdup_noctrl( cinfo->name );
    tinfo->size = cinfo->size;
}

void
statmsg( const struct cl_stat * st )
{
    struct torinfo * info, key;

    gl_gotliststat = 1;

    if( NULL == st )
    {
        if( gl_gotlistinfo )
        {
            printlisting();
        }
        return;
    }

    if( NULL == st->state || 0 > st->done ||
        0 > st->ratedown  || 0 > st->rateup ||
        0 > st->totaldown || 0 > st->totalup )
    {
        errmsg( "missing torrent status from server" );
        return;
    }

    bzero( &key, sizeof key );
    key.id = st->id;
    info = RB_FIND( torlist, &gl_torinfo, &key );
    if( NULL == info )
    {
        info = calloc( 1, sizeof *info );
        if( NULL == info )
        {
            mallocmsg( sizeof *info );
            return;
        }
        info->id = st->id;
        RB_INSERT( torlist, &gl_torinfo, info );
    }

    info->statgood = 1;
    free( info->state );
    info->state     = strdup_noctrl( st->state );
    info->eta       = st->eta;
    info->done      = st->done;
    info->ratedown  = st->ratedown;
    info->rateup    = st->rateup;
    info->totaldown = st->totaldown;
    info->totalup   = st->totalup;
    if( NULL != st->error && '\0' != st->error[0] )
    {
        info->errorcode = strdup_noctrl( st->error );
    }
    if( NULL != st->errmsg && '\0' != st->errmsg[0] )
    {
        info->errormsg = strdup_noctrl( st->errmsg );
    }
}

void
hashmsg( const struct cl_info * inf )
{
    struct torhash key, * found;

    if( NULL == inf )
    {
        sendidreqs();
        return;
    }

    if( NULL == inf->hash )
    {
        errmsg( "missing torrent hash from server" );
        return;
    }

    bzero( &key, sizeof key );
    strlcpy( key.hash, inf->hash, sizeof key.hash );
    found = RB_FIND( torhashes, &gl_hashids, &key );
    if( NULL != found )
    {
        found->id = inf->id;
    }
}

float
fmtsize( int64_t num, const char ** units )
{
    static const char * sizes[] =
    {
        "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB",
    };
    float  ret;
    size_t ii;

    ret = num;
    for( ii = 0; ARRAYLEN( sizes ) > ii && 1000.0 < ret; ii++ )
    {
        ret /= 1024.0;
    }

    if( NULL != units )
    {
        *units = sizes[ii];
    }

    return ret;
}

char *
strdup_noctrl( const char * str )
{
    char * ret;
    size_t ii;

    ii = strlen( str );
    ret = malloc( ii + 1 );
    if( NULL == ret )
    {
        mallocmsg( ii + 1 );
        return NULL;
    }

    for( ii = 0; '\0' != str[ii]; ii++ )
    {
        ret[ii] = ( iscntrl( str[ii] ) ? ' ' : str[ii] );
    }
    ret[ii] = '\0';

    return ret;
}

void
print_eta( int64_t secs )
{
    static const struct
    {
        const char * label;
        int64_t      div;
    }
    units[] =
    {
        { "second", 60 },
        { "minute", 60 },
        { "hour",   24 },
        { "day",    7  },
        { "week",   1 }
    };
    int    readable[ ARRAYLEN( units ) ];
    int    printed;
    size_t ii;

    if( 0 > secs )
    {
        printf( "stalled" );
        return;
    }

    for( ii = 0; ARRAYLEN( units ) > ii; ii++ )
    {
        readable[ii] = secs % units[ii].div;
        secs /= units[ii].div;
    }
    readable[ii-1] = MIN( INT_MAX, secs );

    printf( "done in" );
    for( printed = 0; 0 < ii && 2 > printed; ii-- )
    {
        if( 0 != readable[ii-1] ||
            ( 0 == printed && 1 == ii ) )
        {
            printf( " %i %s%s", readable[ii-1], units[ii-1].label,
                    ( 1 == readable[ii-1] ? "" : "s" ) );
            printed++;
        }
    }
}

int
sendidreqs( void )
{
    struct
    {
        struct strlist * list;
        int ( * func )( size_t, const int * );
    }
    reqs[] =
    {
        { gl_starthashes,  client_start  },
        { gl_stophashes,   client_stop   },
        { gl_removehashes, client_remove },
    };
    struct stritem * jj;
    size_t           ii;
    int            * ids, count, ret;
    struct torhash   key, * found;

    ret = -1;
    bzero( &key, sizeof key );

    for( ii = 0; ARRAYLEN( reqs ) > ii; ii++)
    {
        if( SLIST_EMPTY( reqs[ii].list ) )
        {
            continue;
        }

        count = 0;
        SLIST_FOREACH( jj, reqs[ii].list, next )
        {
            count++;
        }
        count++;

        ids = calloc( count, sizeof ids[0] );
        if( NULL == ids )
        {
            mallocmsg( count * sizeof ids[0] );
            return -1;
        }

        count = 0;
        SLIST_FOREACH( jj, reqs[ii].list, next )
        {
            strlcpy( key.hash, jj->str, sizeof key.hash );
            found = RB_FIND( torhashes, &gl_hashids, &key );
            if( NULL != found && TORRENT_ID_VALID( found->id ) )
            {
                ids[count] = found->id;
                count++;
            }
        }

        if( 0 < count )
        {
            if( 0 > reqs[ii].func( count, ids ) )
            {
                free( ids );
                return -1;
            }
            ret = 0;
        }

        free( ids );
    }

    gl_starthashes  = NULL;
    gl_stophashes   = NULL;
    gl_removehashes = NULL;

    return ret;
}

void
printlisting( void )
{
    struct tornames  names;
    struct torinfo * ii, * next;
    const char     * units, * name, * upunits, * downunits;
    float            size, progress, upspeed, downspeed, ratio;

    /* sort the torrents by name */
    RB_INIT( &names );
    RB_FOREACH( ii, torlist, &gl_torinfo )
    {
        RB_INSERT( tornames, &names, ii );
    }

    /* print the torrent info while freeing the tree */
    for( ii = RB_MIN( tornames, &names ); NULL != ii; ii = next )
    {
        next = RB_NEXT( tornames, &names, ii );
        RB_REMOVE( tornames, &names, ii );
        RB_REMOVE( torlist, &gl_torinfo, ii );

        if( !ii->infogood || !ii->statgood )
        {
            goto free;
        }

        /* massage some numbers into a better format for printing */
        size      = fmtsize( ii->size, &units );
        upspeed   = fmtsize( ii->rateup, &upunits );
        downspeed = fmtsize( ii->ratedown, &downunits );
        name      = ( NULL == ii->name ? "???" : ii->name );
        progress  = ( float )ii->done / ( float )ii->size * 100.0;
        progress  = MIN( 100.0, progress );
        progress  = MAX( 0.0, progress );

        /* print name and size */
        printf( "%s (%.*f %s) - ", name, BESTDECIMAL( size ), size, units );

        /* print hash check progress */
        if( 0 == strcasecmp( "checking", ii->state ) )
        {
            printf( "%.*f%% checking files",
                    BESTDECIMAL( progress ), progress );
        }
        /* print download progress, speeds, and eta */
        else if( 0 == strcasecmp( "downloading", ii->state ) )
        {
            progress = MIN( 99.0, progress );
            printf( "%.*f%% downloading at %.*f %s/s (UL at %.*f %s/s), ",
                    BESTDECIMAL( progress ), progress,
                    BESTDECIMAL( downspeed ), downspeed, downunits,
                    BESTDECIMAL( upspeed ), upspeed, upunits );
            print_eta( ii->eta );
        }
        /* print seeding speed */
        else if( 0 == strcasecmp( "seeding", ii->state ) )
        {
            if( 0 == ii->totalup && 0 == ii->totaldown )
            {
                printf( "100%% seeding at %.*f %s/s [N/A]",
                        BESTDECIMAL( upspeed ), upspeed, upunits );
            }
            else if( 0 == ii->totaldown )
            {
                printf( "100%% seeding at %.*f %s/s [INF]",
                        BESTDECIMAL( upspeed ), upspeed, upunits );
            }
            else
            {
                ratio = ( float )ii->totalup / ( float )ii->totaldown;
                printf( "100%% seeding at %.*f %s/s [%.*f]",
                        BESTDECIMAL( upspeed ), upspeed, upunits,
                        BESTDECIMAL( ratio ), ratio );
            }
        }
        /* print stopping message */
        else if( 0 == strcasecmp( "stopping", ii->state ) )
        {
            printf( "%.*f%% stopping...", BESTDECIMAL( progress ), progress );
        }
        /* print stopped message with progress */
        else if( 0 == strcasecmp( "paused", ii->state ) )
        {
            printf( "%.*f%% stopped", BESTDECIMAL( progress ), progress );
        }
        /* unknown status, just print it with progress */
        else
        {
            printf( "%.*f%% %s",
                    BESTDECIMAL( progress ), progress, ii->state );
        }

        /* print any error */
        if( NULL != ii->errorcode || NULL != ii->errormsg )
        {
            if( NULL == ii->errorcode )
            {
                printf( " [error: %s]", ii->errormsg );
            }
            else
            {
                printf( " [%s error]", ii->errorcode );
            }
        }

        /* don't forget the newline */
        putchar( '\n' );

        /* free the stuff for this torrent */
      free:
        free( ii->name );
        free( ii->state );
        free( ii->errorcode );
        free( ii->errormsg );
        free( ii );
    }
}

int
tornamecmp( struct torinfo * left, struct torinfo * right )
{
    int ret;

    /* we know they're equal if they're the same struct */
    if( left == right )
    {
        return 0;
    }
    /* we might be missing a name, fall back on torrent ID */
    else if( NULL == left->name && NULL == right->name )
    {
        return toridcmp( left, right );
    }
    else if( NULL == left->name )
    {
        return -1;
    }
    else if( NULL == right->name )
    {
        return 1;
    }

    /* if we have two names, compare them */
    ret = strcasecmp( left->name, right->name );
    if( 0 != ret )
    {
        return ret;
    }
    /* if the names are the same then fall back on the torrent ID,
       this is to handle different torrents with the same name */
    else
    {
        return toridcmp( left, right );
    }
}

int
torhashcmp( struct torhash * left, struct torhash * right )
{
    return strcasecmp( left->hash, right->hash );
}

INTCMP_FUNC( toridcmp, torinfo, id )
