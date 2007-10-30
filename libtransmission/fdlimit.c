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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h> /* basename, dirname */
#include <fcntl.h>

#include <sys/queue.h> /* libevent needs this */
#include <sys/types.h> /* libevent needs this */
#include <event.h>
#include <evhttp.h>
#include <evutil.h>

#include "transmission.h"
#include "trcompat.h"
#include "net.h"
#include "platform.h"
#include "utils.h"

/**
***
**/

static void
myDebug( const char * file, int line, const char * fmt, ... )
{
    FILE * fp = tr_getLog( );
    if( fp != NULL )
    {
        va_list args;
        char s[64];
        struct evbuffer * buf = evbuffer_new( );
        char * myfile = tr_strdup( file );

        evbuffer_add_printf( buf, "[%s] ", tr_getLogTimeStr( s, sizeof(s) ) );
        va_start( args, fmt );
        evbuffer_add_vprintf( buf, fmt, args );
        va_end( args );
        evbuffer_add_printf( buf, " (%s:%d)\n", basename(myfile), line );
        fwrite( EVBUFFER_DATA(buf), 1, EVBUFFER_LENGTH(buf), fp );

        tr_free( myfile );
        evbuffer_free( buf );
    }
}

#define dbgmsg(fmt...) myDebug(__FILE__, __LINE__, ##fmt )

/**
***
**/

enum
{
    TR_MAX_OPEN_FILES = 16, /* That is, real files, not sockets */

    TR_RESERVED_FDS   = 16 /* sockets reserved for tracker connections */
};

struct tr_openfile
{
    unsigned int  isCheckedOut : 1;
    unsigned int  isWritable : 1;
    char          filename[MAX_PATH_LENGTH];
    int           file;
    uint64_t      date;
};

struct tr_fd_s
{
    int                  reserved;
    int                  normal;
    int                  normalMax;
    tr_lock            * lock;
    tr_cond            * cond;
    struct tr_openfile   open[TR_MAX_OPEN_FILES];
};

static struct tr_fd_s * gFd = NULL;

/***
****
****  Local Files
****
***/

static int
TrOpenFile( int i, const char * filename, int write )
{
    struct tr_openfile * file = &gFd->open[i];
    struct stat sb;
    char * dir;
    int flags;

    tr_dbg( "Opening '%s' (%d)", filename, write );

    /* create subfolders, if any */
    dir = dirname( tr_strdup( filename ) );
    if( write && tr_mkdirp( dir, 0700 ) ) {
        free( dir );
        return tr_ioErrorFromErrno( );
    }

    /* Make sure the parent folder exists */
    if( stat( dir, &sb ) || !S_ISDIR( sb.st_mode ) ) {
        free( dir );
        return TR_ERROR_IO_PARENT;
    }

    errno = 0;
    flags = 0;
#ifdef WIN32
    flags |= O_BINARY;
#endif
    flags |= write ? (O_RDWR | O_CREAT) : O_RDONLY;
    file->file = open( filename, flags, 0666 );
    free( dir );
    if( write && ( file->file < 0 ) )
    {
        const int ret = tr_ioErrorFromErrno();
        if( errno )
            tr_err( "Couldn't open '%s': %s", filename, strerror(errno) );
        else
            tr_err( "Couldn't open '%s'", filename );
        return ret;
    }

    return TR_OK;
}

static int
fileIsOpen( const struct tr_openfile * o )
{
    return o->file >= 0;
}

static void
TrCloseFile( int i )
{
    struct tr_openfile * file = &gFd->open[i];

    assert( i >= 0 );
    assert( i < TR_MAX_OPEN_FILES );
    assert( fileIsOpen( file ) );

    dbgmsg( "closing %s in slot %d writable %c",
            file->filename, i, file->isWritable?'y':'n' );
    close( file->file );
    file->file = -1;
    file->isCheckedOut = 0;
    tr_condSignal( gFd->cond );
}

static int
fileIsCheckedOut( const struct tr_openfile * o )
{
    return fileIsOpen(o) && o->isCheckedOut;
}

int
tr_fdFileOpen( const char * filename, int write )
{
    int i, winner;
    struct tr_openfile * o;

    dbgmsg( "looking for file '%s', writable %c", filename, write?'y':'n' );

    tr_lockLock( gFd->lock );

    /* Is it already open? */
    for( i=0; i<TR_MAX_OPEN_FILES; ++i )
    {
        o = &gFd->open[i];

        if( !fileIsOpen( o ) )
            continue;

        if( strcmp( filename, o->filename ) )
            continue;

        if( fileIsCheckedOut( o ) ) {
            dbgmsg( "found it!  it's open, but checked out.  waiting..." );
            tr_condWait( gFd->cond, gFd->lock );
            i = -1;
            continue;
        }

        if( write && !o->isWritable ) {
            dbgmsg( "found it!  it's open and available, but isn't writable. closing..." );
            TrCloseFile( i );
            continue;
        }

        dbgmsg( "found it!  it's ready for use!" );
        winner = i;
        goto done;
    }


    dbgmsg( "it's not already open.  looking for an open slot or an old file." );
    for( ;; )
    {
        uint64_t date = tr_date() + 1;
        winner = -1;

        for( i=0; i<TR_MAX_OPEN_FILES; ++i )
        {
            o = &gFd->open[i];

            if( !fileIsOpen( o ) ) {
                winner = i;
                dbgmsg( "found an empty slot in %d", winner );
                goto done;
            }

            if( o->date < date ) {
                winner = i;
                date = o->date;
            }
        }

        if( winner >= 0 ) {
            dbgmsg( "closing file '%s', slot #%d", gFd->open[winner].filename, winner );
            TrCloseFile( winner );
            goto done;
        }

        /* All used! Wait a bit and try again */
        dbgmsg( "everything's full!  waiting for someone else to finish something" );
        tr_condWait( gFd->cond, gFd->lock );
    }

done:

    o = &gFd->open[winner];
    if( !fileIsOpen( o ) )
    {
        const int ret = TrOpenFile( winner, filename, write );
        if( ret ) {
            tr_lockUnlock( gFd->lock );
            return ret;
        }

        dbgmsg( "opened '%s' in slot %d, write %c", filename, winner, write?'y':'n' );
        strlcpy( gFd->open[winner].filename, filename, MAX_PATH_LENGTH );
        gFd->open[winner].isWritable = write;
    }

    dbgmsg( "checking out '%s' in slot %d", filename, winner );
    gFd->open[winner].isCheckedOut = 1;
    gFd->open[winner].date = tr_date();
    tr_lockUnlock( gFd->lock );
    return gFd->open[winner].file;
}

void
tr_fdFileRelease( int file )
{
    int i;
    tr_lockLock( gFd->lock );

    for( i=0; i<TR_MAX_OPEN_FILES; ++i ) {
        struct tr_openfile * o = &gFd->open[i];
        if( o->file == file ) {
            dbgmsg( "releasing file '%s' in slot #%d", o->filename, i );
            if( o->isWritable )
                fsync( o->file ); /* fflush */
            o->isCheckedOut = 0;
            break;
        }
    }
    
    tr_condSignal( gFd->cond );
    tr_lockUnlock( gFd->lock );
}

/***
****
****  Sockets
****
***/

struct tr_socket
{
    int socket;
    int priority;
};

/* Remember the priority of every socket we open, so that we can keep
 * track of how many reserved file descriptors we are using */
static struct tr_socket * gSockets = NULL;
static int gSocketsSize = 0;
static int gSocketsCount = 0;

static void
SocketSetPriority( int s, int priority )
{
    if( gSocketsSize <= gSocketsCount ) {
        gSocketsSize += 64;
        gSockets = tr_renew( struct tr_socket, gSockets, gSocketsSize );
    }

    gSockets[gSocketsCount].socket = s;
    gSockets[gSocketsCount].priority = priority;
    ++gSocketsCount;
}

static int
SocketGetPriority( int s )
{
    int i, ret;

    for( i=0; i<gSocketsCount; ++i )
        if( gSockets[i].socket == s )
            break;

    if( i >= gSocketsCount ) {
        tr_err( "could not find that socket (%d)!", s );
        return -1;
    }

    ret = gSockets[i].priority;
    gSocketsCount--;
    memmove( &gSockets[i], &gSockets[i+1],
            ( gSocketsCount - i ) * sizeof( struct tr_socket ) );
    return ret;
}

int
tr_fdSocketCreate( int type, int priority )
{
    int s = -1;

    tr_lockLock( gFd->lock );

    if( priority && gFd->reserved >= TR_RESERVED_FDS )
        priority = FALSE;

    if( priority || ( gFd->normal < gFd->normalMax ) )
       if( ( s = socket( AF_INET, type, 0 ) ) < 0 )
           tr_err( "Couldn't create socket (%s)", strerror( sockerrno ) );

    if( s > -1 )
    {
        SocketSetPriority( s, priority );
        if( priority )
            gFd->reserved++;
        else
            gFd->normal++;
    }
    tr_lockUnlock( gFd->lock );

    return s;
}

int
tr_fdSocketAccept( int b, struct in_addr * addr, tr_port_t * port )
{
    int s = -1;
    unsigned len;
    struct sockaddr_in sock;

    assert( addr != NULL );
    assert( port != NULL );

    tr_lockLock( gFd->lock );
    if( gFd->normal < gFd->normalMax )
    {
        len = sizeof( sock );
        s = accept( b, (struct sockaddr *) &sock, &len );
    }
    if( s > -1 )
    {
        SocketSetPriority( s, 0 );
        *addr = sock.sin_addr;
        *port = sock.sin_port;
        gFd->normal++;
    }
    tr_lockUnlock( gFd->lock );

    return s;
}

static void
socketClose( int fd )
{
#ifdef BEOS_NETSERVER
    closesocket( fd );
#else
    EVUTIL_CLOSESOCKET( fd );
#endif
}

void
tr_fdSocketClose( int s )
{
    if( s >= 0 )
    {
        tr_lockLock( gFd->lock );
        socketClose( s );
        if( SocketGetPriority( s ) )
            gFd->reserved--;
        else
            gFd->normal--;
        tr_lockUnlock( gFd->lock );
    }
}

/***
****
****  Startup / Shutdown
****
***/

void
tr_fdInit( void )
{
    int i, j, s[4096];

    assert( gFd == NULL );

    gFd = tr_new0( struct tr_fd_s, 1 );
    gFd->lock = tr_lockNew( );
    gFd->cond = tr_condNew( );

    /* count the max number of sockets we can use */
    for( i=0; i<4096; ++i )
        if( ( s[i] = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
            break;
    for( j=0; j<i; ++j )
        socketClose( s[j] );
    tr_dbg( "%d usable file descriptors", i );

    /* set some fds aside for the UI or daemon to use */
    gFd->normalMax = i - TR_RESERVED_FDS - 10;

    for( i=0; i<TR_MAX_OPEN_FILES; ++i )
        gFd->open[i].file = -1;
          
}

void
tr_fdClose( void )
{
    int i = 0;

    for( i=0; i<TR_MAX_OPEN_FILES; ++i )
        if( fileIsOpen( &gFd->open[i] ) )
            TrCloseFile( i );

    tr_lockFree( gFd->lock );
    tr_condFree( gFd->cond );

    tr_free( gSockets );
    tr_free( gFd );
}
