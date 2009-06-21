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

#ifdef HAVE_POSIX_FADVISE
 #ifdef _XOPEN_SOURCE
  #undef _XOPEN_SOURCE
 #endif
 #define _XOPEN_SOURCE 600
#endif

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SYS_DARWIN
 #include <fcntl.h>
#endif

#ifdef HAVE_XFS_XFS_H
 #include <xfs/xfs.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_GETRLIMIT
 #include <sys/time.h> /* getrlimit */
 #include <sys/resource.h> /* getrlimit */
#endif
#include <unistd.h>
#include <fcntl.h> /* O_LARGEFILE posix_fadvise */

#include <evutil.h>

#include "transmission.h"
#include "fdlimit.h"
#include "list.h"
#include "net.h"
#include "platform.h" /* tr_lock */
#include "utils.h"

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

/**
***
**/

enum
{
    NOFILE_BUFFER = 512, /* the process' number of open files is
                            globalMaxPeers + NOFILE_BUFFER */
};

struct tr_openfile
{
    tr_bool    isCheckedOut;
    tr_bool    isWritable;
    tr_bool    closeWhenDone;
    int        torrentId;
    char       filename[MAX_PATH_LENGTH];
    int        fd;
    uint64_t   date;
};

struct tr_fd_s
{
    int                   socketCount;
    int                   socketLimit;

    struct tr_openfile  * openFiles;
    int                   openFileLimit;

    tr_lock             * lock;
};

static struct tr_fd_s * gFd = NULL;

/***
****
****  Local Files
****
***/

#ifndef O_LARGEFILE
 #define O_LARGEFILE 0
#endif

static tr_bool
preallocateFileSparse( int fd, uint64_t length )
{
    const char zero = '\0';

    if( length == 0 )
        return TRUE;

    if( lseek( fd, length-1, SEEK_SET ) == -1 )
        return FALSE;
    if( write( fd, &zero, 1 ) == -1 )
        return FALSE;
    if( ftruncate( fd, length ) == -1 )
        return FALSE;

    return TRUE;
}

static tr_bool
preallocateFileFull( const char * filename, uint64_t length )
{
    tr_bool success = 0;

#ifdef WIN32

    HANDLE hFile = CreateFile( filename, GENERIC_WRITE, 0, 0, CREATE_NEW, 0, 0 );
    if( hFile != INVALID_HANDLE_VALUE )
    {
        LARGE_INTEGER li;
        li.QuadPart = length;
        success = SetFilePointerEx( hFile, li, NULL, FILE_BEGIN ) && SetEndOfFile( hFile );
        CloseHandle( hFile );
    }

#else

    int flags = O_RDWR | O_CREAT | O_LARGEFILE;
    int fd = open( filename, flags, 0666 );
    if( fd >= 0 )
    {
# ifdef HAVE_XFS_XFS_H
        if( !success && platform_test_xfs_fd( fd ) )
        {
            xfs_flock64_t fl;
            fl.l_whence = 0;
            fl.l_start = 0;
            fl.l_len = length;
            success = !xfsctl( NULL, fd, XFS_IOC_RESVSP64, &fl );
        }
# endif
# ifdef SYS_DARWIN
        if( !success )
        {
            fstore_t fst;
            fst.fst_flags = F_ALLOCATECONTIG;
            fst.fst_posmode = F_PEOFPOSMODE;
            fst.fst_offset = 0;
            fst.fst_length = length;
            fst.fst_bytesalloc = 0;
            success = !fcntl( fd, F_PREALLOCATE, &fst );
        }
# endif
# ifdef HAVE_POSIX_FALLOCATE
        if( !success )
        {
            success = !posix_fallocate( fd, 0, length );
        }
# endif

        if( !success ) /* if nothing else works, do it the old-fashioned way */
        {
            uint8_t buf[ 4096 ]; 
            memset( buf, 0, sizeof( buf ) ); 
            success = TRUE; 
            while ( success && ( length > 0 ) ) 
            { 
                const int thisPass = MIN( length, sizeof( buf ) ); 
                success = write( fd, buf, thisPass ) == thisPass; 
                length -= thisPass; 
            } 
        }

        close( fd );
    }

#endif

    return success;
}

tr_bool
tr_preallocate_file( const char * filename, uint64_t length )
{
    return preallocateFileFull( filename, length );
}

int
tr_open_file_for_writing( const char * filename )
{
    int flags = O_WRONLY | O_CREAT;
#ifdef O_BINARY
    flags |= O_BINARY;
#endif
#ifdef O_LARGEFILE
    flags |= O_LARGEFILE;
#endif
    return open( filename, flags, 0666 );
}

int
tr_open_file_for_scanning( const char * filename )
{
    int fd;
    int flags;

    /* build the flags */
    flags = O_RDONLY;
#ifdef O_SEQUENTIAL
    flags |= O_SEQUENTIAL;
#endif
#ifdef O_BINARY
    flags |= O_BINARY;
#endif
#ifdef O_LARGEFILE
    flags |= O_LARGEFILE;
#endif

    /* open the file */
    fd = open( filename, flags, 0666 );
    if( fd >= 0 )
    {
        /* Set hints about the lookahead buffer and caching. It's okay
           for these to fail silently, so don't let them affect errno */
        const int err = errno;
#ifdef HAVE_POSIX_FADVISE
        posix_fadvise( fd, 0, 0, POSIX_FADV_SEQUENTIAL );
#endif
#ifdef SYS_DARWIN
        fcntl( fd, F_NOCACHE, 1 );
        fcntl( fd, F_RDAHEAD, 1 );
#endif
        errno = err;
    }

    return fd;
}

void
tr_close_file( int fd )
{
#if defined(HAVE_POSIX_FADVISE)
    /* Set hint about not caching this file.
       It's okay for this to fail silently, so don't let it affect errno */
    const int err = errno;
    posix_fadvise( fd, 0, 0, POSIX_FADV_DONTNEED );
    errno = err;
#endif
    close( fd );
}

/**
 * returns 0 on success, or an errno value on failure.
 * errno values include ENOENT if the parent folder doesn't exist,
 * plus the errno values set by tr_mkdirp() and open().
 */
static int
TrOpenFile( int                      i,
            const char             * folder,
            const char             * torrentFile,
            tr_bool                  doWrite,
            tr_preallocation_mode    preallocationMode,
            uint64_t                 desiredFileSize )
{
    struct tr_openfile * file = &gFd->openFiles[i];
    int                  flags;
    char               * filename;
    struct stat          sb;
    int                  alreadyExisted;

    /* confirm the parent folder exists */
    if( stat( folder, &sb ) || !S_ISDIR( sb.st_mode ) )
    {
        tr_err( _( "Couldn't create \"%1$s\": parent folder \"%2$s\" does not exist" ), torrentFile, folder );
        return ENOENT;
    }

    /* create subfolders, if any */
    filename = tr_buildPath( folder, torrentFile, NULL );
    if( doWrite )
    {
        char * tmp = tr_dirname( filename );
        const int err = tr_mkdirp( tmp, 0777 ) ? errno : 0;
        if( err ) {
            tr_err( _( "Couldn't create \"%1$s\": %2$s" ), tmp, tr_strerror( err ) );
            tr_free( tmp );
            tr_free( filename );
            return err;
        }
        tr_free( tmp );
    }

    alreadyExisted = !stat( filename, &sb ) && S_ISREG( sb.st_mode );

    if( doWrite && !alreadyExisted && ( preallocationMode == TR_PREALLOCATE_FULL ) )
        if( preallocateFileFull( filename, desiredFileSize ) )
            tr_inf( _( "Preallocated file \"%s\"" ), filename );
    
    /* open the file */
    flags = doWrite ? ( O_RDWR | O_CREAT ) : O_RDONLY;
#ifdef O_SEQUENTIAL
    flags |= O_SEQUENTIAL;
#endif
#ifdef O_LARGEFILE
    flags |= O_LARGEFILE;
#endif
#ifdef WIN32
    flags |= O_BINARY;
#endif
    file->fd = open( filename, flags, 0666 );
    if( file->fd == -1 )
    {
        const int err = errno;
        tr_err( _( "Couldn't open \"%1$s\": %2$s" ), filename, tr_strerror( err ) );
        tr_free( filename );
        return err;
    }

    /* If the file already exists and it's too large, truncate it.
     * This is a fringe case that happens if a torrent's been updated
     * and one of the updated torrent's files is smaller.
     * http://trac.transmissionbt.com/ticket/2228
     * https://bugs.launchpad.net/ubuntu/+source/transmission/+bug/318249
     */
    if( alreadyExisted && ( desiredFileSize < (uint64_t)sb.st_size ) )
        ftruncate( file->fd, desiredFileSize );

    if( doWrite && !alreadyExisted && ( preallocationMode == TR_PREALLOCATE_SPARSE ) )
        preallocateFileSparse( file->fd, desiredFileSize );

#ifdef HAVE_POSIX_FADVISE
    posix_fadvise( file->fd, 0, 0, POSIX_FADV_SEQUENTIAL );
#endif

    tr_free( filename );
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
    struct tr_openfile * o = &gFd->openFiles[i];

    assert( i >= 0 );
    assert( i < gFd->openFileLimit );
    assert( fileIsOpen( o ) );

    tr_close_file( o->fd );
    o->fd = -1;
    o->isCheckedOut = 0;
}

static int
fileIsCheckedOut( const struct tr_openfile * o )
{
    return fileIsOpen( o ) && o->isCheckedOut;
}

/* returns an fd on success, or a -1 on failure and sets errno */
int
tr_fdFileCheckout( int                      torrentId,
                   const char             * folder,
                   const char             * torrentFile,
                   tr_bool                  doWrite,
                   tr_preallocation_mode    preallocationMode,
                   uint64_t                 desiredFileSize )
{
    int i, winner = -1;
    struct tr_openfile * o;
    char filename[MAX_PATH_LENGTH];

    assert( torrentId > 0 );
    assert( folder && *folder );
    assert( torrentFile && *torrentFile );
    assert( doWrite == 0 || doWrite == 1 );

    tr_snprintf( filename, sizeof( filename ), "%s%c%s", folder, TR_PATH_DELIMITER, torrentFile );
    dbgmsg( "looking for file '%s', writable %c", filename, doWrite ? 'y' : 'n' );

    tr_lockLock( gFd->lock );

    /* is it already open? */
    for( i=0; i<gFd->openFileLimit; ++i )
    {
        o = &gFd->openFiles[i];

        if( !fileIsOpen( o ) )
            continue;
        if( torrentId != o->torrentId )
            continue;
        if( strcmp( filename, o->filename ) )
            continue;

        if( fileIsCheckedOut( o ) )
        {
            dbgmsg( "found it!  it's open, but checked out.  waiting..." );
            tr_lockUnlock( gFd->lock );
            tr_wait( 200 );
            tr_lockLock( gFd->lock );
            i = -1; /* reloop */
            continue;
        }

        if( doWrite && !o->isWritable )
        {
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
        for( i=0; i<gFd->openFileLimit; ++i )
        {
            o = &gFd->openFiles[i];

            if( !fileIsOpen( o ) )
            {
                winner = i;
                dbgmsg( "found an empty slot in %d", winner );
                break;
            }

            if( date > o->date )
            {
                date = o->date;
                winner = i;
            }
        }

        if( winner >= 0 )
        {
            if( fileIsOpen( &gFd->openFiles[winner] ) )
            {
                dbgmsg( "closing file '%s', slot #%d",
                        gFd->openFiles[winner].filename,
                        winner );
                TrCloseFile( winner );
            }
        }
        else
        {
            dbgmsg( "everything's full!  waiting for someone else to finish something" );
            tr_lockUnlock( gFd->lock );
            tr_wait( 200 );
            tr_lockLock( gFd->lock );
        }
    }

    assert( winner >= 0 );
    o = &gFd->openFiles[winner];
    if( !fileIsOpen( o ) )
    {
        const int err = TrOpenFile( winner, folder, torrentFile, doWrite, preallocationMode, desiredFileSize );
        if( err ) {
            tr_lockUnlock( gFd->lock );
            errno = err;
            return -1;
        }

        dbgmsg( "opened '%s' in slot %d, doWrite %c", filename, winner,
                doWrite ? 'y' : 'n' );
        tr_strlcpy( o->filename, filename, sizeof( o->filename ) );
        o->isWritable = doWrite;
    }

    dbgmsg( "checking out '%s' in slot %d", filename, winner );
    o->torrentId = torrentId;
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

    for( i = 0; i < gFd->openFileLimit; ++i )
    {
        struct tr_openfile * o = &gFd->openFiles[i];
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

    for( i=0; i<gFd->openFileLimit; ++i )
    {
        struct tr_openfile * o = &gFd->openFiles[i];
        if( !fileIsOpen( o ) || strcmp( filename, o->filename ) )
            continue;

        dbgmsg( "tr_fdFileClose closing '%s'", filename );

        assert( !o->isCheckedOut && "this is a test assertion... I *think* this is always true now" );

        TrCloseFile( i );
    }

    tr_lockUnlock( gFd->lock );
}

void
tr_fdTorrentClose( int torrentId )
{
    int i;
    tr_lockLock( gFd->lock );

    for( i=0; i<gFd->openFileLimit; ++i )
    {
        struct tr_openfile * o = &gFd->openFiles[i];

        assert( !o->isCheckedOut && "this is a test assertion... I *think* this is always true now" );

        if( fileIsOpen( o ) && o->torrentId == torrentId )
            TrCloseFile( i );
    }

    tr_lockUnlock( gFd->lock );
}


/***
****
****  Sockets
****
***/

static int
getSocketMax( struct tr_fd_s * gFd )
{
    return gFd->socketLimit;
}

int
tr_fdSocketCreate( int domain, int type )
{
    int s = -1;

    tr_lockLock( gFd->lock );

    if( gFd->socketCount < getSocketMax( gFd ) )
        if( ( s = socket( domain, type, 0 ) ) < 0 )
        {
#ifdef SYS_DARWIN
            if( sockerrno != EAFNOSUPPORT )
#endif
            tr_err( _( "Couldn't create socket: %s" ),
                   tr_strerror( sockerrno ) );
            s = -sockerrno;
        }

    if( s > -1 )
        ++gFd->socketCount;

    assert( gFd->socketCount >= 0 );

    tr_lockUnlock( gFd->lock );
    return s;
}

int
tr_fdSocketAccept( int           b,
                   tr_address  * addr,
                   tr_port     * port )
{
    int s;
    unsigned int len;
    struct sockaddr_storage sock;
    tr_lockLock( gFd->lock );

    assert( addr );
    assert( port );

    len = sizeof( struct sockaddr_storage );
    s = accept( b, (struct sockaddr *) &sock, &len );

    if( ( s >= 0 ) && gFd->socketCount > getSocketMax( gFd ) )
    {
        EVUTIL_CLOSESOCKET( s );
        s = -1;
    }

    if( s >= 0 )
    {
        /* "The ss_family field of the sockaddr_storage structure will always 
         * align with the family field of any protocol-specific structure." */ 
        if( sock.ss_family == AF_INET ) 
        {
            struct sockaddr_in *si;
            union { struct sockaddr_storage dummy; struct sockaddr_in si; } s;
            s.dummy = sock;
            si = &s.si;
            addr->type = TR_AF_INET; 
            addr->addr.addr4.s_addr = si->sin_addr.s_addr; 
            *port = si->sin_port; 
        } 
        else 
        { 
            struct sockaddr_in6 *si;
            union { struct sockaddr_storage dummy; struct sockaddr_in6 si; } s;
            s.dummy = sock;
            si = &s.si;
            addr->type = TR_AF_INET6; 
            addr->addr.addr6 = si->sin6_addr;
            *port = si->sin6_port; 
        } 
        ++gFd->socketCount;
    }

    tr_lockUnlock( gFd->lock );
    return s;
}

static void
socketClose( int fd )
{
    EVUTIL_CLOSESOCKET( fd );
}

void
tr_fdSocketClose( int s )
{
    tr_lockLock( gFd->lock );

    if( s >= 0 )
    {
        socketClose( s );
        --gFd->socketCount;
    }

    assert( gFd->socketCount >= 0 );

    tr_lockUnlock( gFd->lock );
}

/***
****
****  Startup / Shutdown
****
***/

void
tr_fdInit( size_t openFileLimit, size_t socketLimit )
{
    int i;

    assert( gFd == NULL );
    gFd = tr_new0( struct tr_fd_s, 1 );
    gFd->openFiles = tr_new0( struct tr_openfile, openFileLimit );
    gFd->openFileLimit = openFileLimit;
    gFd->lock = tr_lockNew( );

#ifdef HAVE_GETRLIMIT
    {
        struct rlimit rlim;
        getrlimit( RLIMIT_NOFILE, &rlim );
        rlim.rlim_cur = MIN( rlim.rlim_max,
                            (rlim_t)( socketLimit + NOFILE_BUFFER ) );
        setrlimit( RLIMIT_NOFILE, &rlim );
        gFd->socketLimit = rlim.rlim_cur - NOFILE_BUFFER;
        tr_dbg( "setrlimit( RLIMIT_NOFILE, %d )", (int)rlim.rlim_cur );
    }
#else
    gFd->socketLimit = socketLimit;
#endif
    tr_dbg( "%zu usable file descriptors", socketLimit );

    for( i = 0; i < gFd->openFileLimit; ++i )
        gFd->openFiles[i].fd = -1;
}

void
tr_fdClose( void )
{
    int i = 0;

    for( i = 0; i < gFd->openFileLimit; ++i )
        if( fileIsOpen( &gFd->openFiles[i] ) )
            TrCloseFile( i );

    tr_lockFree( gFd->lock );

    tr_free( gFd->openFiles );
    tr_free( gFd );
    gFd = NULL;
}

void
tr_fdSetPeerLimit( uint16_t n )
{
    assert( gFd != NULL && "tr_fdInit() must be called first!" );
    gFd->socketLimit = n;
}

uint16_t
tr_fdGetPeerLimit( void )
{
    return gFd ? gFd->socketLimit : -1;
}
