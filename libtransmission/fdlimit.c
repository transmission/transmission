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
#include "platform.h" /* MAX_PATH_LENGTH, TR_PATH_DELIMITER */
#include "session.h"
#include "torrent.h" /* tr_isTorrent() */
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
    tr_bool          isWritable;
    int              torrentId;
    tr_file_index_t  fileNum;
    char             filename[MAX_PATH_LENGTH];
    int              fd;
    uint64_t         date;
};

struct tr_fdInfo
{
    int                   socketCount;
    int                   socketLimit;
    int                   publicSocketLimit;
    int                   openFileLimit;
    struct tr_openfile  * openFiles;
};

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

/* Like pread and pwrite, except that the position is undefined afterwards.
   And of course they are not thread-safe. */

#ifdef SYS_DARWIN
 #define HAVE_PREAD
 #define HAVE_PWRITE
#endif

ssize_t
tr_pread( int fd, void *buf, size_t count, off_t offset )
{
#ifdef HAVE_PREAD
    return pread( fd, buf, count, offset );
#else
    const off_t lrc = lseek( fd, offset, SEEK_SET );
    if( lrc < 0 )
        return -1;
    return read( fd, buf, count );
#endif
}

ssize_t
tr_pwrite( int fd, void *buf, size_t count, off_t offset )
{
#ifdef HAVE_PWRITE
    return pwrite( fd, buf, count, offset );
#else
    const off_t lrc = lseek( fd, offset, SEEK_SET );
    if( lrc < 0 )
        return -1;
    return write( fd, buf, count );
#endif
}

int
tr_prefetch( int fd, off_t offset, size_t count )
{
#ifdef HAVE_POSIX_FADVISE
    return posix_fadvise( fd, offset, count, POSIX_FADV_WILLNEED );
#elif defined(SYS_DARWIN)
    int val;
    struct radvisory radv;
    radv.ra_offset = offset;
    radv.ra_count = count;
    return fcntl( fd, F_RDADVISE, &radv );
#else
    return 0;
#endif
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
TrOpenFile( tr_session             * session,
            int                      i,
            const char             * filename,
            tr_bool                  doWrite,
            tr_preallocation_mode    preallocationMode,
            uint64_t                 desiredFileSize )
{
    int flags;
    struct stat sb;
    tr_bool alreadyExisted;
    struct tr_openfile * file;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );

    file = &session->fdInfo->openFiles[i];

    /* create subfolders, if any */
    if( doWrite )
    {
        char * dir = tr_dirname( filename );
        const int err = tr_mkdirp( dir, 0777 ) ? errno : 0;
        if( err ) {
            tr_err( _( "Couldn't create \"%1$s\": %2$s" ), dir, tr_strerror( err ) );
            tr_free( dir );
            return err;
        }
        tr_free( dir );
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
    /* this doubles the OS level readahead buffer, which in practice
     * turns out to be a good thing, because many (most?) clients request
     * chunks of blocks in order.
     * It's okay for this to fail silently, so don't let it affect errno */
    {
        const int err = errno;
        posix_fadvise( file->fd, 0, 0, POSIX_FADV_SEQUENTIAL );
        errno = err;
    }
#endif

    return 0;
}

static TR_INLINE tr_bool
fileIsOpen( const struct tr_openfile * o )
{
    return o->fd >= 0;
}

static void
TrCloseFile( struct tr_openfile * o )
{
    assert( o != NULL );
    assert( fileIsOpen( o ) );

    tr_close_file( o->fd );
    o->fd = -1;
}

int
tr_fdFileGetCached( tr_session       * session,
                    int                torrentId,
                    tr_file_index_t    fileNum,
                    tr_bool            doWrite )
{
    struct tr_openfile * match = NULL;
    struct tr_fdInfo * gFd;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );
    assert( torrentId > 0 );
    assert( tr_isBool( doWrite ) );

    gFd = session->fdInfo;

    /* is it already open? */
    {
        int i;
        struct tr_openfile * o;
        for( i=0; i<gFd->openFileLimit; ++i )
        {
            o = &gFd->openFiles[i];

            if( torrentId != o->torrentId )
                continue;
            if( fileNum != o->fileNum )
                continue;
            if( !fileIsOpen( o ) )
                continue;

            match = o;
            break;
        }
    }

    if( ( match != NULL ) && ( !doWrite || match->isWritable ) )
    {
        match->date = tr_date( );
        return match->fd;
    }

    return -1;
}

/* returns an fd on success, or a -1 on failure and sets errno */
int
tr_fdFileCheckout( tr_session             * session,
                   int                      torrentId,
                   tr_file_index_t          fileNum,
                   const char             * filename,
                   tr_bool                  doWrite,
                   tr_preallocation_mode    preallocationMode,
                   uint64_t                 desiredFileSize )
{
    int i, winner = -1;
    struct tr_fdInfo * gFd;
    struct tr_openfile * o;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );
    assert( torrentId > 0 );
    assert( filename && *filename );
    assert( tr_isBool( doWrite ) );

    gFd = session->fdInfo;

    dbgmsg( "looking for file '%s', writable %c", filename, doWrite ? 'y' : 'n' );

    /* is it already open? */
    for( i=0; i<gFd->openFileLimit; ++i )
    {
        o = &gFd->openFiles[i];

        if( torrentId != o->torrentId )
            continue;
        if( fileNum != o->fileNum )
            continue;
        if( !fileIsOpen( o ) )
            continue;

        if( doWrite && !o->isWritable )
        {
            dbgmsg( "found it!  it's open and available, but isn't writable. closing..." );
            TrCloseFile( o );
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

        assert( winner >= 0 );

        if( fileIsOpen( &gFd->openFiles[winner] ) )
        {
            dbgmsg( "closing file \"%s\"", gFd->openFiles[winner].filename );
            TrCloseFile( &gFd->openFiles[winner] );
        }
    }

    assert( winner >= 0 );
    o = &gFd->openFiles[winner];
    if( !fileIsOpen( o ) )
    {
        const int err = TrOpenFile( session, winner, filename, doWrite,
                                    preallocationMode, desiredFileSize );
        if( err ) {
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
    o->fileNum = fileNum;
    o->date = tr_date( );
    return o->fd;
}

void
tr_fdFileClose( tr_session        * session,
                const tr_torrent  * tor,
                tr_file_index_t     fileNum )
{
    struct tr_openfile * o;
    struct tr_fdInfo * gFd;
    const struct tr_openfile * end;
    const int torrentId = tr_torrentId( tor );

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );
    assert( tr_isTorrent( tor ) );
    assert( fileNum < tor->info.fileCount );

    gFd = session->fdInfo;

    for( o=gFd->openFiles, end=o+gFd->openFileLimit; o!=end; ++o )
    {
        if( torrentId != o->torrentId )
            continue;
        if( fileNum != o->fileNum )
            continue;
        if( !fileIsOpen( o ) )
            continue;

        dbgmsg( "tr_fdFileClose closing \"%s\"", o->filename );
        TrCloseFile( o );
    }
}

void
tr_fdTorrentClose( tr_session * session, int torrentId )
{
    struct tr_openfile * o;
    struct tr_fdInfo * gFd;
    const struct tr_openfile * end;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );

    gFd = session->fdInfo;

    for( o=gFd->openFiles, end=o+gFd->openFileLimit; o!=end; ++o )
        if( fileIsOpen( o ) && ( o->torrentId == torrentId ) )
            TrCloseFile( o );
}

/***
****
****  Sockets
****
***/

int
tr_fdSocketCreate( tr_session * session, int domain, int type )
{
    int s = -1;
    struct tr_fdInfo * gFd;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );

    gFd = session->fdInfo;

    if( gFd->socketCount < gFd->socketLimit )
        if( ( s = socket( domain, type, 0 ) ) < 0 )
        {
            if( sockerrno != EAFNOSUPPORT )
                tr_err( _( "Couldn't create socket: %s" ),
                        tr_strerror( sockerrno ) );
        }

    if( s > -1 )
        ++gFd->socketCount;

    assert( gFd->socketCount >= 0 );
    return s;
}

int
tr_fdSocketAccept( tr_session  * session,
                   int           b,
                   tr_address  * addr,
                   tr_port     * port )
{
    int s;
    unsigned int len;
    struct tr_fdInfo * gFd;
    struct sockaddr_storage sock;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );
    assert( addr );
    assert( port );

    gFd = session->fdInfo;

    len = sizeof( struct sockaddr_storage );
    s = accept( b, (struct sockaddr *) &sock, &len );

    if( ( s >= 0 ) && gFd->socketCount > gFd->socketLimit )
    {
        tr_netCloseSocket( s );
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

    return s;
}

void
tr_fdSocketClose( tr_session * session, int fd )
{
    struct tr_fdInfo * gFd;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );

    gFd = session->fdInfo;

    if( fd >= 0 )
    {
        tr_netCloseSocket( fd );
        --gFd->socketCount;
    }

    assert( gFd->socketCount >= 0 );
}

/***
****
****  Startup / Shutdown
****
***/

static void
ensureSessionFdInfoExists( tr_session * session )
{
    assert( tr_isSession( session ) );

    if( session->fdInfo == NULL )
        session->fdInfo = tr_new0( struct tr_fdInfo, 1 );
}

void
tr_fdClose( tr_session * session )
{
    struct tr_fdInfo * gFd;
    struct tr_openfile * o;
    const struct tr_openfile * end;

    assert( tr_isSession( session ) );
    assert( session->fdInfo != NULL );

    gFd = session->fdInfo;

    for( o=gFd->openFiles, end=o+gFd->openFileLimit; o!=end; ++o )
        if( fileIsOpen( o ) )
            TrCloseFile( o );

    tr_free( gFd->openFiles );
    tr_free( gFd );
    session->fdInfo = NULL;
}

/***
****
***/

void
tr_fdSetFileLimit( tr_session * session, int limit )
{
    struct tr_fdInfo * gFd;

    ensureSessionFdInfoExists( session );

    gFd = session->fdInfo;

    if( gFd->openFileLimit != limit )
    {
        int i;
        struct tr_openfile * o;
        const struct tr_openfile * end;

        /* close any files we've got open  */
        for( o=gFd->openFiles, end=o+gFd->openFileLimit; o!=end; ++o )
            if( fileIsOpen( o ) )
                TrCloseFile( o );

        /* rebuild the openFiles array */
        tr_free( gFd->openFiles );
        gFd->openFiles = tr_new0( struct tr_openfile, limit );
        gFd->openFileLimit = limit;
        for( i=0; i<gFd->openFileLimit; ++i )
            gFd->openFiles[i].fd = -1;
    }
}

int
tr_fdGetFileLimit( const tr_session * session )
{
    return session && session->fdInfo ? session->fdInfo->openFileLimit : -1;
}

void
tr_fdSetPeerLimit( tr_session * session, int limit )
{
    struct tr_fdInfo * gFd;

    ensureSessionFdInfoExists( session );

    gFd = session->fdInfo;

#ifdef HAVE_GETRLIMIT
    {
        struct rlimit rlim;
        getrlimit( RLIMIT_NOFILE, &rlim );
        rlim.rlim_cur = MIN( rlim.rlim_max, (rlim_t)( limit + NOFILE_BUFFER ) );
        setrlimit( RLIMIT_NOFILE, &rlim );
        gFd->socketLimit = rlim.rlim_cur - NOFILE_BUFFER;
        tr_dbg( "setrlimit( RLIMIT_NOFILE, %d )", (int)rlim.rlim_cur );
    }
#else
    gFd->socketLimit = limit;
#endif
    gFd->publicSocketLimit = limit;

    tr_dbg( "%d usable file descriptors", limit );
}

int
tr_fdGetPeerLimit( const tr_session * session )
{
    return session && session->fdInfo ? session->fdInfo->publicSocketLimit : -1;
}
