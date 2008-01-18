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

#ifndef WIN32
#define HAVE_GETRLIMIT
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_GETRLIMIT
#include <sys/time.h> /* getrlimit */
#include <sys/resource.h> /* getrlimit */
#endif
#include <unistd.h>
#include <libgen.h> /* basename, dirname */
#include <fcntl.h> /* O_LARGEFILE */

#include <event.h>
#include <evutil.h>

#include "transmission.h"
#include "trcompat.h"
#include "list.h"
#include "net.h"
#include "platform.h"
#include "utils.h"

#if SIZEOF_VOIDP==8
#define TR_UINT_TO_PTR(i) (void*)((uint64_t)i)
#else
#define TR_UINT_TO_PTR(i) ((void*)((uint32_t)i))
#endif

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
    TR_MAX_OPEN_FILES = 16, /* real files, not sockets */

    NOFILE_BUFFER = 512, /* the process' number of open files is
                            globalMaxPeers + NOFILE_BUFFER */
};

struct tr_openfile
{
    unsigned int  isCheckedOut : 1;
    unsigned int  isWritable : 1;
    unsigned int  closeWhenDone : 1;
    char          filename[MAX_PATH_LENGTH];
    int           fd;
    uint64_t      date;
};

struct tr_fd_s
{
    int                  reserved;
    int                  normal;
    int                  normalMax;
    tr_lock            * lock;
    struct tr_openfile   open[TR_MAX_OPEN_FILES];
};

static struct tr_fd_s * gFd = NULL;

/***
****
****  Local Files
****
***/

static tr_errno
TrOpenFile( int           i,
            const char  * folder,
            const char  * torrentFile,
            int           write )
{
    struct tr_openfile * file = &gFd->open[i];
    int flags;
    char filename[MAX_PATH_LENGTH];
    struct stat sb;

    /* confirm the parent folder exists */
    if( stat( folder, &sb ) || !S_ISDIR( sb.st_mode ) )
        return TR_ERROR_IO_PARENT;

    /* create subfolders, if any */
    tr_buildPath ( filename, sizeof(filename), folder, torrentFile, NULL );
    if( write ) {
        char * tmp = tr_strdup( filename );
        const int err = tr_mkdirp( dirname(tmp), 0777 ) ? errno : 0;
        tr_free( tmp );
        if( err )
            return tr_ioErrorFromErrno( err );
    }

    /* open the file */
    flags = write ? (O_RDWR | O_CREAT) : O_RDONLY;
#ifdef O_LARGEFILE
    flags |= O_LARGEFILE;
#endif
#ifdef WIN32
    flags |= O_BINARY;
#endif
    file->fd = open( filename, flags, 0666 );
    if( file->fd == -1 ) {
        const int err = errno;
        tr_err( "Couldn't open '%s': %s", filename, strerror(err) );
        return tr_ioErrorFromErrno( err );
    }

    return 0;
}

static int
fileIsOpen( const struct tr_openfile * o )
{
    return o->fd >= 0;
}

static void
TrCloseFile( int i )
{
    struct tr_openfile * o = &gFd->open[i];

    assert( i >= 0 );
    assert( i < TR_MAX_OPEN_FILES );
    assert( fileIsOpen( o ) );

    close( o->fd );
    o->fd = -1;
    o->isCheckedOut = 0;
}

static int
fileIsCheckedOut( const struct tr_openfile * o )
{
    return fileIsOpen(o) && o->isCheckedOut;
}

int
tr_fdFileCheckout( const char * folder,
                   const char * torrentFile, 
                   int          write )
{
    int i, winner = -1;
    struct tr_openfile * o;
    char filename[MAX_PATH_LENGTH];

    assert( folder && *folder );
    assert( torrentFile && *torrentFile );
    assert( write==0 || write==1 );

    tr_buildPath ( filename, sizeof(filename), folder, torrentFile, NULL );
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
            tr_lockUnlock( gFd->lock );
            tr_wait( 200 );
            tr_lockLock( gFd->lock );
            i = -1; /* reloop */
            continue;
        }

        if( write && !o->isWritable ) {
            dbgmsg( "found it!  it's open and available, but isn't writable. closing..." );
            TrCloseFile( i );
            break;
        }

        dbgmsg( "found it!  it's ready for use!" );
        winner = i;
        break;
    }

    dbgmsg( "it's not already open.  looking for an open slot or an old file." );
    while( winner < 0 )
    {
        uint64_t date = tr_date( ) + 1;

        /* look for the file that's been open longest */
        for( i=0; i<TR_MAX_OPEN_FILES; ++i )
        {
            o = &gFd->open[i];

            if( !fileIsOpen( o ) ) {
                winner = i;
                dbgmsg( "found an empty slot in %d", winner );
                break;
            }

            if( date > o->date ) {
                date = o->date;
                winner = i;
            }
        }

        if( winner >= 0 ) {
            if( fileIsOpen( &gFd->open[winner] ) ) {
                dbgmsg( "closing file '%s', slot #%d", gFd->open[winner].filename, winner );
                TrCloseFile( winner );
            }
        } else { 
            dbgmsg( "everything's full!  waiting for someone else to finish something" );
            tr_lockUnlock( gFd->lock );
            tr_wait( 200 );
            tr_lockLock( gFd->lock );
        }
    }

    assert( winner >= 0 );
    o = &gFd->open[winner];
    if( !fileIsOpen( o ) )
    {
        const tr_errno err = TrOpenFile( winner, folder, torrentFile, write );
        if( err ) {
            tr_lockUnlock( gFd->lock );
            return err;
        }

        dbgmsg( "opened '%s' in slot %d, write %c", filename, winner, write?'y':'n' );
        strlcpy( o->filename, filename, sizeof( o->filename ) );
        o->isWritable = write;
    }

    dbgmsg( "checking out '%s' in slot %d", filename, winner );
    o->isCheckedOut = 1;
    o->closeWhenDone = 0;
    o->date = tr_date( );
    tr_lockUnlock( gFd->lock );
    return o->fd;
}

void
tr_fdFileReturn( int fd )
{
    int i;
    tr_lockLock( gFd->lock );

    for( i=0; i<TR_MAX_OPEN_FILES; ++i )
    {
        struct tr_openfile * o = &gFd->open[i];
        if( o->fd != fd )
            continue;

        dbgmsg( "releasing file '%s' in slot #%d", o->filename, i );
        o->isCheckedOut = 0;
        if( o->closeWhenDone )
            TrCloseFile( i );
        
        break;
    }
    
    tr_lockUnlock( gFd->lock );
}

void
tr_fdFileClose( const char * filename )
{
    int i;
    tr_lockLock( gFd->lock );

    for( i=0; i<TR_MAX_OPEN_FILES; ++i )
    {
        struct tr_openfile * o = &gFd->open[i];
        if( !fileIsOpen(o) || strcmp(filename,o->filename) )
            continue;

        dbgmsg( "tr_fdFileClose closing '%s'", filename );

        if( !o->isCheckedOut ) {
            dbgmsg( "not checked out, so closing it now... '%s'", filename );
            TrCloseFile( i );
        } else {
            dbgmsg( "flagging file '%s', slot #%d to be closed when checked in", gFd->open[i].filename, i );
            o->closeWhenDone = 1;
        }
    }
    
    tr_lockUnlock( gFd->lock );
}

/***
****
****  Sockets
****
***/

static tr_list * reservedSockets = NULL;

static void
setSocketPriority( int fd, int isReserved )
{
    if( isReserved )
        tr_list_append( &reservedSockets, TR_UINT_TO_PTR(fd) );
}

static int
socketWasReserved( int fd )
{
    return tr_list_remove_data( &reservedSockets, TR_UINT_TO_PTR(fd) ) != NULL;
}

int
tr_fdSocketCreate( int type, int isReserved )
{
    int s = -1;
    tr_lockLock( gFd->lock );

    if( isReserved || ( gFd->normal < gFd->normalMax ) )
        if( ( s = socket( AF_INET, type, 0 ) ) < 0 )
            tr_err( "Couldn't create socket (%s)", strerror( sockerrno ) );

    if( s > -1 )
    {
        setSocketPriority( s, isReserved );

        if( isReserved )
            ++gFd->reserved;
        else
            ++gFd->normal;
    }

    assert( gFd->reserved >= 0 );
    assert( gFd->normal >= 0 );

    tr_lockUnlock( gFd->lock );
    return s;
}

int
tr_fdSocketAccept( int b, struct in_addr * addr, tr_port_t * port )
{
    int s = -1;
    unsigned int len;
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
        setSocketPriority( s, FALSE );
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
    tr_lockLock( gFd->lock );

    if( s >= 0 ) {
        socketClose( s );
        if( socketWasReserved( s ) )
            --gFd->reserved;
        else
            --gFd->normal;
    }

    assert( gFd->reserved >= 0 );
    assert( gFd->normal >= 0 );

    tr_lockUnlock( gFd->lock );
}

/***
****
****  Startup / Shutdown
****
***/

void
tr_fdInit( int globalPeerLimit )
{
    int i;

    assert( gFd == NULL );
    gFd = tr_new0( struct tr_fd_s, 1 );
    gFd->lock = tr_lockNew( );

#ifdef HAVE_GETRLIMIT
    {
        struct rlimit rlim;
        getrlimit( RLIMIT_NOFILE, &rlim );
        rlim.rlim_cur = MIN( rlim.rlim_max,
               (rlim_t)(globalPeerLimit + NOFILE_BUFFER) );
        setrlimit( RLIMIT_NOFILE, &rlim );
        gFd->normalMax = rlim.rlim_cur - NOFILE_BUFFER;
        tr_dbg( "setrlimit( RLIMIT_NOFILE, %d )", rlim.rlim_cur );
    }
#else
    gFd->normalMax = globalPeerLimit;
#endif
    tr_dbg( "%d usable file descriptors", globalPeerLimit );

    for( i=0; i<TR_MAX_OPEN_FILES; ++i ) 
        gFd->open[i].fd = -1;
}

void
tr_fdClose( void )
{
    int i = 0;

    for( i=0; i<TR_MAX_OPEN_FILES; ++i )
        if( fileIsOpen( &gFd->open[i] ) )
            TrCloseFile( i );

    tr_lockFree( gFd->lock );

    tr_list_free( &reservedSockets, NULL );
    tr_free( gFd );
}

void
tr_fdSetPeerLimit( uint16_t n )
{
    assert( gFd!=NULL && "tr_fdInit() must be called first!" );
    gFd->normalMax = n;
}

uint16_t
tr_fdGetPeerLimit( void )
{
    return gFd ? gFd->normalMax : -1;
}
