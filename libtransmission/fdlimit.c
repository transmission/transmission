/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

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

#ifdef HAVE_FALLOCATE64
  /* FIXME can't find the right #include voodoo to pick up the declaration.. */
  extern int fallocate64( int fd, int mode, uint64_t offset, uint64_t len );
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
#include <fcntl.h> /* O_LARGEFILE posix_fadvise */
#include <unistd.h>

#include "transmission.h"
#include "fdlimit.h"
#include "net.h"
#include "session.h"
#include "torrent.h" /* tr_isTorrent() */

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

/***
****
****  Local Files
****
***/

#ifndef O_LARGEFILE
 #define O_LARGEFILE 0
#endif

#ifndef O_BINARY
 #define O_BINARY 0
#endif

#ifndef O_SEQUENTIAL
 #define O_SEQUENTIAL 0
#endif


static bool
preallocate_file_sparse( int fd, uint64_t length )
{
    const char zero = '\0';
    bool success = 0;

    if( !length )
        success = true;

#ifdef HAVE_FALLOCATE64
    if( !success ) /* fallocate64 is always preferred, so try it first */
        success = !fallocate64( fd, 0, 0, length );
#endif

    if( !success ) /* fallback: the old-style seek-and-write */
        success = ( lseek( fd, length-1, SEEK_SET ) != -1 )
               && ( write( fd, &zero, 1 ) != -1 )
               && ( ftruncate( fd, length ) != -1 );

    return success;
}

static bool
preallocate_file_full( const char * filename, uint64_t length )
{
    bool success = 0;

#ifdef WIN32

    HANDLE hFile = CreateFile( filename, GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_FLAG_RANDOM_ACCESS, 0 );
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
# ifdef HAVE_FALLOCATE64
       if( !success )
       {
           success = !fallocate64( fd, 0, 0, length );
       }
# endif
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
            success = true;
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


/* portability wrapper for fsync(). */
int
tr_fsync( int fd )
{
#ifdef WIN32
    return _commit( fd );
#else
    return fsync( fd );
#endif
}


/* Like pread and pwrite, except that the position is undefined afterwards.
   And of course they are not thread-safe. */

/* don't use pread/pwrite on old versions of uClibc because they're buggy.
 * https://trac.transmissionbt.com/ticket/3826 */
#ifdef __UCLIBC__
#define TR_UCLIBC_CHECK_VERSION(major,minor,micro) \
    (__UCLIBC_MAJOR__ > (major) || \
     (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ > (minor)) || \
     (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ == (minor) && \
      __UCLIBC_SUBLEVEL__ >= (micro)))
#if !TR_UCLIBC_CHECK_VERSION(0,9,28)
 #undef HAVE_PREAD
 #undef HAVE_PWRITE
#endif
#endif

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
tr_pwrite( int fd, const void *buf, size_t count, off_t offset )
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
tr_prefetch( int fd UNUSED, off_t offset UNUSED, size_t count UNUSED )
{
#ifdef HAVE_POSIX_FADVISE
    return posix_fadvise( fd, offset, count, POSIX_FADV_WILLNEED );
#elif defined(SYS_DARWIN)
    struct radvisory radv;
    radv.ra_offset = offset;
    radv.ra_count = count;
    return fcntl( fd, F_RDADVISE, &radv );
#else
    return 0;
#endif
}

void
tr_set_file_for_single_pass( int fd )
{
    if( fd >= 0 )
    {
        /* Set hints about the lookahead buffer and caching. It's okay
           for these to fail silently, so don't let them affect errno */
        const int err = errno;
#ifdef HAVE_POSIX_FADVISE
        posix_fadvise( fd, 0, 0, POSIX_FADV_SEQUENTIAL );
#endif
#ifdef SYS_DARWIN
        fcntl( fd, F_RDAHEAD, 1 );
        fcntl( fd, F_NOCACHE, 1 );
#endif
        errno = err;
    }
}

static int
open_local_file( const char * filename, int flags )
{
    const int fd = open( filename, flags, 0666 );
    tr_set_file_for_single_pass( fd );
    return fd;
}
int
tr_open_file_for_writing( const char * filename )
{
    return open_local_file( filename, O_LARGEFILE|O_BINARY|O_CREAT|O_WRONLY );
}
int
tr_open_file_for_scanning( const char * filename )
{
    return open_local_file( filename, O_LARGEFILE|O_BINARY|O_SEQUENTIAL|O_RDONLY );
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
#ifdef SYS_DARWIN
    /* it's unclear to me from the man pages if this actually flushes out the cache,
     * but it couldn't hurt... */
    fcntl( fd, F_NOCACHE, 1 );
#endif
    close( fd );
}

/*****
******
******
******
*****/

struct tr_cached_file
{
    bool             is_writable;
    int              fd;
    int              torrent_id;
    tr_file_index_t  file_index;
    time_t           used_at;
};

static inline bool
cached_file_is_open( const struct tr_cached_file * o )
{
    assert( o != NULL );

    return o->fd >= 0;
}

static void
cached_file_close( struct tr_cached_file * o )
{
    assert( cached_file_is_open( o ) );

    tr_close_file( o->fd );
    o->fd = -1;
}

/**
 * returns 0 on success, or an errno value on failure.
 * errno values include ENOENT if the parent folder doesn't exist,
 * plus the errno values set by tr_mkdirp() and open().
 */
static int
cached_file_open( struct tr_cached_file  * o,
                  const char             * existing_dir,
                  const char             * filename,
                  bool                     writable,
                  tr_preallocation_mode    allocation,
                  uint64_t                 file_size )
{
    int flags;
    struct stat sb;
    bool alreadyExisted;

    /* confirm that existing_dir, if specified, exists on the disk */
    if( existing_dir && *existing_dir && stat( existing_dir, &sb ) )
    {
        const int err = errno;
        tr_err( _( "Couldn't open \"%1$s\": %2$s" ), existing_dir, tr_strerror( err ) );
        return err;
    }

    /* create subfolders, if any */
    if( writable )
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

    if( writable && !alreadyExisted && ( allocation == TR_PREALLOCATE_FULL ) )
        if( preallocate_file_full( filename, file_size ) )
            tr_dbg( "Preallocated file \"%s\"", filename );

    /* open the file */
    flags = writable ? ( O_RDWR | O_CREAT ) : O_RDONLY;
    flags |= O_LARGEFILE | O_BINARY | O_SEQUENTIAL;
    o->fd = open( filename, flags, 0666 );

    if( o->fd == -1 )
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
    if( alreadyExisted && ( file_size < (uint64_t)sb.st_size ) )
    {
        if( ftruncate( o->fd, file_size ) == -1 )
        {
            const int err = errno;
            tr_err( _( "Couldn't truncate \"%1$s\": %2$s" ), filename, tr_strerror( err ) );
            return err;
        }
    }

    if( writable && !alreadyExisted && ( allocation == TR_PREALLOCATE_SPARSE ) )
        preallocate_file_sparse( o->fd, file_size );

    /* Many (most?) clients request blocks in ascending order,
     * so increase the readahead buffer.
     * Also, disable OS-level caching because "inactive memory" angers users. */
    tr_set_file_for_single_pass( o->fd );

    return 0;
}

/***
****
***/

struct tr_fileset
{
    struct tr_cached_file * begin;
    const struct tr_cached_file * end;
};

static void
fileset_construct( struct tr_fileset * set, int n )
{
    struct tr_cached_file * o;
    const struct tr_cached_file TR_CACHED_FILE_INIT = { 0, -1, 0, 0, 0 };

    set->begin = tr_new( struct tr_cached_file, n );
    set->end = set->begin + n;

    for( o=set->begin; o!=set->end; ++o )
        *o = TR_CACHED_FILE_INIT;
}

static void
fileset_close_all( struct tr_fileset * set )
{
    struct tr_cached_file * o;

    if( set != NULL )
        for( o=set->begin; o!=set->end; ++o )
            if( cached_file_is_open( o ) )
                cached_file_close( o );
}

static void
fileset_destruct( struct tr_fileset * set )
{
    fileset_close_all( set );
    tr_free( set->begin );
    set->end = set->begin = NULL;
}

static void
fileset_close_torrent( struct tr_fileset * set, int torrent_id )
{
    struct tr_cached_file * o;

    if( set != NULL )
        for( o=set->begin; o!=set->end; ++o )
            if( ( o->torrent_id == torrent_id ) && cached_file_is_open( o ) )
                cached_file_close( o );
}

static struct tr_cached_file *
fileset_lookup( struct tr_fileset * set, int torrent_id, tr_file_index_t i )
{
    struct tr_cached_file * o;

    if( set != NULL )
        for( o=set->begin; o!=set->end; ++o )
            if( ( torrent_id == o->torrent_id ) && ( i == o->file_index ) && cached_file_is_open( o ) )
                return o;

    return NULL;
}

static struct tr_cached_file *
fileset_get_empty_slot( struct tr_fileset * set )
{
    struct tr_cached_file * o;
    struct tr_cached_file * cull;

    /* try to find an unused slot */
    for( o=set->begin; o!=set->end; ++o )
        if( !cached_file_is_open( o ) )
            return o;

    /* all slots are full... recycle the least recently used */
    for( cull=NULL, o=set->begin; o!=set->end; ++o )
        if( !cull || o->used_at < cull->used_at )
            cull = o;
    cached_file_close( cull );
    return cull;
}

static int
fileset_get_size( const struct tr_fileset * set )
{
    return set ? set->end - set->begin : 0;
}

/***
****
***/

struct tr_fdInfo
{
    int socket_count;
    int socket_limit;
    int public_socket_limit;
    struct tr_fileset fileset;
};

static struct tr_fileset*
get_fileset( tr_session * session )
{
    return session && session->fdInfo ? &session->fdInfo->fileset : NULL;
}

void
tr_fdFileClose( tr_session * s, const tr_torrent * tor, tr_file_index_t i )
{
    struct tr_cached_file * o;

    if(( o = fileset_lookup( get_fileset( s ), tr_torrentId( tor ), i )))
    {
        /* flush writable files so that their mtimes will be
         * up-to-date when this function returns to the caller... */
        if( o->is_writable )
            tr_fsync( o->fd );

        cached_file_close( o );
    }
}

int
tr_fdFileGetCached( tr_session * s, int torrent_id, tr_file_index_t i, bool writable )
{
    struct tr_cached_file * o = fileset_lookup( get_fileset( s ), torrent_id, i );

    if( !o || ( writable && !o->is_writable ) )
        return -1;

    o->used_at = tr_time( );
    return o->fd;
}

void
tr_fdTorrentClose( tr_session * session, int torrent_id )
{
    fileset_close_torrent( get_fileset( session ), torrent_id );
}

/* returns an fd on success, or a -1 on failure and sets errno */
int
tr_fdFileCheckout( tr_session             * session,
                   int                      torrent_id,
                   tr_file_index_t          i,
                   const char             * existing_dir,
                   const char             * filename,
                   bool                     writable,
                   tr_preallocation_mode    allocation,
                   uint64_t                 file_size )
{
    struct tr_fileset * set = get_fileset( session );
    struct tr_cached_file * o = fileset_lookup( set, torrent_id, i );

    if( o && writable && !o->is_writable )
        cached_file_close( o ); /* close it so we can reopen in rw mode */
    else if( !o )
        o = fileset_get_empty_slot( set );

    if( !cached_file_is_open( o ) )
    {
        const int err = cached_file_open( o, existing_dir, filename, writable, allocation, file_size );
        if( err ) {
            errno = err;
            return -1;
        }

        dbgmsg( "opened '%s' writable %c", filename, writable?'y':'n' );
        o->is_writable = writable;
    }

    dbgmsg( "checking out '%s'", filename );
    o->torrent_id = torrent_id;
    o->file_index = i;
    o->used_at = tr_time( );
    return o->fd;
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

    if( gFd->socket_count < gFd->socket_limit )
        if(( s = socket( domain, type, 0 )) < 0 )
            if( sockerrno != EAFNOSUPPORT )
                tr_err( _( "Couldn't create socket: %s" ), tr_strerror( sockerrno ) );

    if( s > -1 )
        ++gFd->socket_count;

    assert( gFd->socket_count >= 0 );

    if( s >= 0 )
    {
        static bool buf_logged = false;
        if( !buf_logged )
        {
            int i;
            socklen_t size = sizeof( int );
            buf_logged = true;
            getsockopt( s, SOL_SOCKET, SO_SNDBUF, &i, &size );
            tr_dbg( "SO_SNDBUF size is %d", i );
            getsockopt( s, SOL_SOCKET, SO_RCVBUF, &i, &size );
            tr_dbg( "SO_RCVBUF size is %d", i );
        }
    }

    return s;
}

int
tr_fdSocketAccept( tr_session * s, int sockfd, tr_address * addr, tr_port * port )
{
    int fd;
    unsigned int len;
    struct tr_fdInfo * gFd;
    struct sockaddr_storage sock;

    assert( tr_isSession( s ) );
    assert( s->fdInfo != NULL );
    assert( addr );
    assert( port );

    gFd = s->fdInfo;

    len = sizeof( struct sockaddr_storage );
    fd = accept( sockfd, (struct sockaddr *) &sock, &len );

    if( fd >= 0 )
    {
        if( ( gFd->socket_count < gFd->socket_limit ) && tr_ssToAddr( addr, port, &sock ) )
        {
            ++gFd->socket_count;
        }
        else
        {
            tr_netCloseSocket( fd );
            fd = -1;
        }
    }

    return fd;
}

void
tr_fdSocketClose( tr_session * session, int fd )
{
    assert( tr_isSession( session ) );

    if( session->fdInfo != NULL )
    {
        struct tr_fdInfo * gFd = session->fdInfo;

        if( fd >= 0 )
        {
            tr_netCloseSocket( fd );
            --gFd->socket_count;
        }

        assert( gFd->socket_count >= 0 );
    }
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
    struct tr_fdInfo * gFd = session->fdInfo;

    if( gFd != NULL )
    {
        fileset_destruct( &gFd->fileset );
        tr_free( gFd );
    }

    session->fdInfo = NULL;
}

/***
****
***/

int
tr_fdGetFileLimit( tr_session * session )
{
    return fileset_get_size( get_fileset( session ) );
}

void
tr_fdSetFileLimit( tr_session * session, int limit )
{
    ensureSessionFdInfoExists( session );

    if( limit != tr_fdGetFileLimit( session ) )
    {
        struct tr_fileset * set = get_fileset( session );
        fileset_destruct( set );
        fileset_construct( set, limit );
    }
}

void
tr_fdSetPeerLimit( tr_session * session, int socket_limit )
{
    struct tr_fdInfo * gFd;

    ensureSessionFdInfoExists( session );

    gFd = session->fdInfo;

#ifdef HAVE_GETRLIMIT
    {
        struct rlimit rlim;
        const int NOFILE_BUFFER = 512;
        const int open_max = sysconf( _SC_OPEN_MAX );
        getrlimit( RLIMIT_NOFILE, &rlim );
        rlim.rlim_cur = MAX( 1024, open_max );
        rlim.rlim_cur = MIN( rlim.rlim_cur, rlim.rlim_max );
        setrlimit( RLIMIT_NOFILE, &rlim );
        tr_dbg( "setrlimit( RLIMIT_NOFILE, %d )", (int)rlim.rlim_cur );
        gFd->socket_limit = MIN( socket_limit, (int)rlim.rlim_cur - NOFILE_BUFFER );
    }
#else
    gFd->socket_limit = socket_limit;
#endif
    gFd->public_socket_limit = socket_limit;

    tr_dbg( "socket limit is %d", gFd->socket_limit );
}

int
tr_fdGetPeerLimit( const tr_session * session )
{
    return session && session->fdInfo ? session->fdInfo->public_socket_limit : -1;
}
