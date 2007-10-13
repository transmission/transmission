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
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <event.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libtransmission/transmission.h>
#include <libtransmission/trcompat.h>

#include "errors.h"
#include "misc.h"

static void              usage    ( const char *, ... );
static enum confpathtype readargs ( int, char **, char ** );
static int               makesock ( enum confpathtype, const char * );
static void              inread   ( struct bufferevent *, void * );
static void              noop     ( struct bufferevent *, void * );
static void              inerr    ( struct bufferevent *, short, void * );
static void              wtf      ( struct bufferevent *, void * );
static void              outerr   ( struct bufferevent *, short, void * );
static void              sockread ( struct bufferevent *, void * );
static void              sockwrite( struct bufferevent *, void * );
static void              sockerr  ( struct bufferevent *, short, void * );

static struct bufferevent * gl_in   = NULL;
static struct bufferevent * gl_out  = NULL;
static struct bufferevent * gl_sock = NULL;

int
main( int argc, char ** argv )
{
    struct event_base  * base;
    enum confpathtype    type;
    char               * sockpath;
    int                  sockfd;

    setmyname( argv[0] );
    type = readargs( argc, argv, &sockpath );
    base = event_init();

    sockfd = makesock( type, sockpath );
    if( 0 > sockfd )
    {
        return EXIT_FAILURE;
    }

    gl_in = bufferevent_new( STDIN_FILENO, inread, noop, inerr, NULL );
    if( NULL == gl_in )
    {
        errnomsg( "failed to set up event buffer for stdin" );
        return EXIT_FAILURE;
    }
    /* XXX bufferevent_base_set( base, gl_in ); */
    bufferevent_enable( gl_in, EV_READ );
    bufferevent_disable( gl_in, EV_WRITE );

    gl_out  = bufferevent_new( STDOUT_FILENO, wtf, noop, outerr,  NULL );
    if( NULL == gl_in )
    {
        errnomsg( "failed to set up event buffer for stdin" );
        return EXIT_FAILURE;
    }
    /* XXX bufferevent_base_set( base, gl_out ); */
    bufferevent_disable( gl_out, EV_READ );
    bufferevent_enable( gl_out, EV_WRITE );

    gl_sock = bufferevent_new( sockfd, sockread, sockwrite, sockerr, NULL );
    if( NULL == gl_in )
    {
        errnomsg( "failed to set up event buffer for stdin" );
        return EXIT_FAILURE;
    }
    /* XXX bufferevent_base_set( base, gl_sock ); */
    bufferevent_enable( gl_sock, EV_READ );
    bufferevent_enable( gl_sock, EV_WRITE );

    event_dispatch();
    /* XXX event_base_dispatch( base ); */

    return EXIT_FAILURE;
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
  "usage: %s [options] [files]...\n"
  "\n"
  "Transmission %s http://transmission.m0k.org/\n"
  "A fast and easy BitTorrent client\n"
  "\n"
  "  -h --help                 Display this message and exit\n"
  "  -t --type daemon          Use the daemon frontend, transmission-daemon\n"
  "  -t --type gtk             Use the GTK+ frontend, transmission\n"
  "  -t --type mac             Use the Mac OS X frontend\n",
            getmyname(), LONG_VERSION_STRING );
    exit( EXIT_SUCCESS );
}

enum confpathtype
readargs( int argc, char ** argv, char ** sockpath )
{
    char optstr[] = "ht:";
    struct option longopts[] =
    {
        { "help",               no_argument,       NULL, 'h' },
        { "type",               required_argument, NULL, 't' },
        { NULL, 0, NULL, 0 }
    };
    enum confpathtype type;
    int opt;

    type      = CONF_PATH_TYPE_DAEMON;
    *sockpath = NULL;

    while( 0 <= ( opt = getopt_long( argc, argv, optstr, longopts, NULL ) ) )
    {
        switch( opt )
        {
            case 't':
                if( 0 == strcasecmp( "daemon", optarg ) )
                {
                    type      = CONF_PATH_TYPE_DAEMON;
                    *sockpath = NULL;
                }
                else if( 0 == strcasecmp( "gtk", optarg ) )
                {
                    type      = CONF_PATH_TYPE_GTK;
                    *sockpath = NULL;
                }
                else if( 0 == strcasecmp( "mac", optarg ) ||
                         0 == strcasecmp( "osx", optarg ) ||
                         0 == strcasecmp( "macos", optarg ) ||
                         0 == strcasecmp( "macosx", optarg ) )
                {
                    type      = CONF_PATH_TYPE_OSX;
                    *sockpath = NULL;
                }
                else
                {
                    *sockpath = optarg;
                }
                break;
            default:
                usage( NULL );
                break;
        }
    }

    return type;
}

int
makesock( enum confpathtype type, const char * path )
{
    struct sockaddr_un sa;
    int                fd;

    memset( &sa, 0, sizeof sa );
    sa.sun_family = AF_LOCAL;
    if( NULL == path )
    {
        confpath( sa.sun_path, sizeof sa.sun_path, CONF_FILE_SOCKET, type );
    }
    else
    {
        strlcpy( sa.sun_path, path, sizeof sa.sun_path );
    }

    fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( 0 > fd )
    {
        errnomsg( "failed to create socket" );
        return -1;
    }

    if( 0 > connect( fd, ( struct sockaddr * )&sa, SUN_LEN( &sa ) ) )
    {
        errnomsg( "failed to connect to socket file: %s", sa.sun_path );
        close( fd );
        return -1;
    }

    return fd;
}

void
inread( struct bufferevent * ev UNUSED, void * arg UNUSED )
{
    bufferevent_write_buffer( gl_sock, EVBUFFER_INPUT( gl_in ) );
}

void
noop( struct bufferevent * ev UNUSED, void * arg UNUSED )
{
}

void
inerr( struct bufferevent * ev UNUSED, short what, void * arg UNUSED )
{
    if( EVBUFFER_EOF & what )
    {
        bufferevent_free( gl_in );
        gl_in = NULL;
        sockwrite( NULL, NULL );
        return;
    }

    if( EVBUFFER_TIMEOUT & what )
    {
        errmsg( "timed out reading from stdin" );
    }
    else if( EVBUFFER_READ & what )
    {
        errmsg( "read error on stdin" );
    }
    else if( EVBUFFER_ERROR & what )
    {
        errmsg( "error on stdin" );
    }
    else
    {
        errmsg( "unknown error on stdin: 0x%x", what );
    }

    exit( EXIT_FAILURE );
}

void
wtf( struct bufferevent * ev, void * arg UNUSED )
{
    /* this shouldn't happen, but let's drain the buffer anyway */
    evbuffer_drain( EVBUFFER_INPUT( ev ),
                    EVBUFFER_LENGTH( EVBUFFER_INPUT( ev ) ) );
}

void
outerr( struct bufferevent * ev UNUSED, short what, void * arg UNUSED )
{
    if( EVBUFFER_TIMEOUT & what )
    {
        errmsg( "timed out writing to stdout" );
    }
    else if( EVBUFFER_WRITE & what )
    {
        errmsg( "write error on stdout" );
    }
    else if( EVBUFFER_ERROR & what )
    {
        errmsg( "error on client stdout" );
    }
    else
    {
        errmsg( "unknown error on stdout connection: 0x%x", what );
    }

    exit( EXIT_FAILURE );
}

void
sockread( struct bufferevent * ev UNUSED, void * arg UNUSED )
{
    bufferevent_write_buffer( gl_out, EVBUFFER_INPUT( gl_sock ) );
}

void
sockwrite( struct bufferevent * ev UNUSED, void * arg UNUSED )
{
    if( NULL == gl_in && 0 == EVBUFFER_LENGTH( EVBUFFER_OUTPUT( gl_sock ) ) )
    {
        exit( EXIT_SUCCESS );
    }
}

void
sockerr( struct bufferevent * ev UNUSED, short what, void * arg UNUSED )
{
    if( EVBUFFER_EOF & what )
    {
        errmsg( "server closed connection" );
    }
    else if( EVBUFFER_TIMEOUT & what )
    {
        errmsg( "server connection timed out" );
    }
    else if( EVBUFFER_READ & what )
    {
        errmsg( "read error on server connection" );
    }
    else if( EVBUFFER_WRITE & what )
    {
        errmsg( "write error on server connection" );
    }
    else if( EVBUFFER_ERROR & what )
    {
        errmsg( "error on server connection" );
    }
    else
    {
        errmsg( "unknown error on server connection: 0x%x", what );
    }

    exit( EXIT_FAILURE );
}
