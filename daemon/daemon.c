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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <assert.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "errors.h"
#include "misc.h"
#include "server.h"
#include "torrents.h"
#include "trcompat.h"
#include "version.h"

static void usage       ( const char *, ... );
static void readargs    ( int, char **, int *, int *, char ** );
static int  trylocksock ( const char * );
static int  getlock     ( const char * );
static int  getsock     ( const char * );
static void exitcleanup ( void );
static void setupsigs   ( struct event_base * );
static void gotsig      ( int, short, void * );

static int  gl_lockfd               = -1;
static char gl_lockpath[MAXPATHLEN] = "";
static int  gl_sockfd               = -1;
static char gl_sockpath[MAXPATHLEN] = "";

int
main( int argc, char ** argv )
{
    struct event_base * evbase;
    int                 nofork, debug, sockfd;
    char              * sockpath;

    setmyname( argv[0] );
    readargs( argc, argv, &nofork, &debug, &sockpath );

    if( !nofork )
    {
        if( 0 > daemon( 1, 0 ) )
        {
            errnomsg( "failed to daemonize" );
            exit( 1 );
        }
        errsyslog( 1 );
    }

    atexit( exitcleanup );
    sockfd = trylocksock( sockpath );
    if( 0 > sockfd )
    {
        exit( 1 );
    }

    evbase = event_init();
    setupsigs( evbase );
    torrent_init( evbase );
    server_init( evbase );
    server_debug( debug );
    server_listen( sockfd );

    event_dispatch();
    /* XXX event_base_dispatch( evbase ); */

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
  "usage: %s [-dfh]\n"
  "\n"
  "Transmission %s (r%d) http://transmission.m0k.org/\n"
  "A free, lightweight BitTorrent client with a simple, intuitive interface\n"
  "\n"
  "  -d --debug                Print data send and received, implies -f\n"
  "  -f --foreground           Run in the foreground and log to stderr\n"
  "  -h --help                 Display this message and exit\n"
  "  -s --socket <path>        Place the socket file at <path>\n"
  "\n"
  "To add torrents or set options, use the transmission-remote program.\n",
            getmyname(), VERSION_STRING, VERSION_REVISION );
    exit( 0 );
}

void
readargs( int argc, char ** argv, int * nofork, int * debug, char ** sock )
{
    char optstr[] = "dfhs:";
    struct option longopts[] =
    {
        { "debug",              no_argument,       NULL, 'd' },
        { "foreground",         no_argument,       NULL, 'f' },
        { "help",               no_argument,       NULL, 'h' },
        { "socket",             required_argument, NULL, 's' },
        { NULL, 0, NULL, 0 }
    };
    int opt;

    *nofork = 0;
    *debug  = 0;
    *sock   = NULL;

    while( 0 <= ( opt = getopt_long( argc, argv, optstr, longopts, NULL ) ) )
    {
        switch( opt )
        {
            case 'd':
                *debug = 1;
                /* FALLTHROUGH */
            case 'f':
                *nofork = 1;
                break;
            case 's':
                *sock   = optarg;
                break;
            default:
                usage( NULL );
                break;
        }
    }
}

int
trylocksock( const char * sockpath )
{
    char path[MAXPATHLEN];
    int  fd;

    confpath( path, sizeof path, NULL, CONF_PATH_TYPE_DAEMON );
    if( 0 > mkdir( path, 0777 ) && EEXIST != errno )
    {
        errnomsg( "failed to create directory: %s", path );
        return -1;
    }

    confpath( path, sizeof path, CONF_FILE_LOCK, 0 );
    fd = getlock( path );
    if( 0 > fd )
    {
        return -1;
    }
    gl_lockfd = fd;
    strlcpy( gl_lockpath, path, sizeof gl_lockpath );

    if( NULL == sockpath )
    {
        confpath( path, sizeof path, CONF_FILE_SOCKET, 0 );
        sockpath = path;
    }
    fd = getsock( sockpath );
    if( 0 > fd )
    {
        return -1;
    }
    gl_sockfd = fd;
    strlcpy( gl_sockpath, sockpath, sizeof gl_sockpath );

    return fd;
}

int
getlock( const char * path )
{
    struct flock lk;
    int          fd;
    char         pid[64];

    fd = open( path, O_RDWR | O_CREAT, 0666 );
    if( 0 > fd )
    {
        errnomsg( "failed to open file: %s", path );
        return -1;
    }

    bzero( &lk, sizeof lk );
    lk.l_start  = 0;
    lk.l_len    = 0;
    lk.l_type   = F_WRLCK;
    lk.l_whence = SEEK_SET;
    if( 0 > fcntl( fd, F_SETLK, &lk ) )
    {
        if( EAGAIN == errno )
        {
            errmsg( "another copy of %s is already running", getmyname() );
        }
        else
        {
            errnomsg( "failed to obtain lock on file: %s", path );
        }
        close( fd );
        return -1;
    }

    ftruncate( fd, 0 );
    snprintf( pid, sizeof pid, "%i\n", getpid() );
    write( fd, pid, strlen( pid ) );

    return fd;
}

int
getsock( const char * path )
{
    struct sockaddr_un sun;
    int                fd;

    fd = socket( PF_LOCAL, SOCK_STREAM, 0 );
    if( 0 > fd )
    {
        errnomsg( "failed to create socket file: %s", path );
        return -1;
    }

    bzero( &sun, sizeof sun );
    sun.sun_family = AF_LOCAL;
    strlcpy( sun.sun_path, path, sizeof sun.sun_path );
    unlink( path );
    if( 0 > bind( fd, ( struct sockaddr * )&sun, SUN_LEN( &sun ) ) )
    {
        /* bind can sometimes fail on the first call */
        unlink( path );
        if( 0 > bind( fd, ( struct sockaddr * )&sun, SUN_LEN( &sun ) ) )
        {
            errnomsg( "failed to bind socket file: %s", path );
            close( fd );
            return -1;
        }
    }

    return fd;
}

void
exitcleanup( void )
{
    if( 0 <= gl_sockfd )
    {
        unlink( gl_sockpath );
        close( gl_sockfd );
    }
    if( 0 <= gl_lockfd )
    {
        unlink( gl_lockpath );
        close( gl_lockfd );
    }
}

void
setupsigs( struct event_base * base /* XXX */ UNUSED )
{
    static struct event ev_int;
    static struct event ev_quit;
    static struct event ev_term;

    signal_set( &ev_int, SIGINT, gotsig, NULL );
    /* XXX event_base_set( base, &ev_int ); */
    signal_add( &ev_int, NULL );

    signal_set( &ev_quit, SIGINT, gotsig, NULL );
    /* XXX event_base_set( base, &ev_quit ); */
    signal_add( &ev_quit, NULL );

    signal_set( &ev_term, SIGINT, gotsig, NULL );
    /* XXX event_base_set( base, &ev_term ); */
    signal_add( &ev_term, NULL );

    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, SIG_IGN );
}

void
gotsig( int sig, short what UNUSED, void * arg UNUSED )
{
    static int exiting = 0;

    if( !exiting )
    {
        exiting = 1;
        errmsg( "received fatal signal %i, attempting to exit cleanly", sig );
        torrent_exit( 0 );
    }
    else
    {
        errmsg( "received fatal signal %i while exiting, exiting immediately",
                sig );
        signal( sig, SIG_DFL );
        raise( sig );
    }
}
