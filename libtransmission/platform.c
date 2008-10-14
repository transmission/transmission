/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifdef __BEOS__
 #include <signal.h>
 #include <fs_info.h>
 #include <FindDirectory.h>
 #include <kernel/OS.h>
 #define BEOS_MAX_THREADS 256
#elif defined( WIN32 )
 #include <windows.h>
 #include <shlobj.h> /* for CSIDL_APPDATA, CSIDL_MYDOCUMENTS */
#else
 #ifdef SYS_DARWIN
  #include <CoreFoundation/CoreFoundation.h>
 #endif

 #define _XOPEN_SOURCE 500  /* needed for recursive locks. */
 #ifndef __USE_UNIX98
  #define __USE_UNIX98 /* some older Linuxes need it spelt out for them */
 #endif
 #include <pthread.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#ifdef WIN32
#include <libgen.h>
#endif
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h> /* getuid getpid close */

#include "transmission.h"
#include "list.h"
#include "platform.h"
#include "utils.h"

/***
****  THREADS
***/

#ifdef __BEOS__
typedef thread_id tr_thread_id;
#elif defined( WIN32 )
typedef DWORD tr_thread_id;
#else
typedef pthread_t tr_thread_id;
#endif

static tr_thread_id
tr_getCurrentThread( void )
{
#ifdef __BEOS__
    return find_thread( NULL );
#elif defined( WIN32 )
    return GetCurrentThreadId( );
#else
    return pthread_self( );
#endif
}

static int
tr_areThreadsEqual( tr_thread_id a,
                    tr_thread_id b )
{
#ifdef __BEOS__
    return a == b;
#elif defined( WIN32 )
    return a == b;
#else
    return pthread_equal( a, b );
#endif
}

struct tr_thread
{
    void            ( * func )( void * );
    void *          arg;
    tr_thread_id    thread;
#ifdef WIN32
    HANDLE          thread_handle;
#endif
};

int
tr_amInThread( const tr_thread * t )
{
    return tr_areThreadsEqual( tr_getCurrentThread( ), t->thread );
}

#ifdef WIN32
 #define ThreadFuncReturnType unsigned WINAPI
#else
 #define ThreadFuncReturnType void
#endif

static ThreadFuncReturnType
ThreadFunc( void * _t )
{
    tr_thread * t = _t;

#ifdef __BEOS__
    /* This is required because on BeOS, SIGINT is sent to each thread,
       which kills them not nicely */
    signal( SIGINT, SIG_IGN );
#endif

    t->func( t->arg );

#ifdef WIN32
    _endthreadex( 0 );
    return 0;
#endif
}

tr_thread *
tr_threadNew( void   ( *func )(void *),
              void * arg )
{
    tr_thread * t = tr_new0( tr_thread, 1 );

    t->func = func;
    t->arg  = arg;

#ifdef __BEOS__
    t->thread =
        spawn_thread( (void*)ThreadFunc, "beos thread", B_NORMAL_PRIORITY,
                     t );
    resume_thread( t->thread );
#elif defined( WIN32 )
    {
        unsigned int id;
        t->thread_handle =
            (HANDLE) _beginthreadex( NULL, 0, &ThreadFunc, t, 0,
                                     &id );
        t->thread = (DWORD) id;
    }
#else
    pthread_create( &t->thread, NULL, ( void * ( * )(
                                           void * ) )ThreadFunc, t );
#endif

    return t;
}

/***
****  LOCKS
***/

struct tr_lock
{
    int                 depth;
#ifdef __BEOS__
    sem_id              lock;
    thread_id           lockThread;
#elif defined( WIN32 )
    CRITICAL_SECTION    lock;
    DWORD               lockThread;
#else
    pthread_mutex_t     lock;
    pthread_t           lockThread;
#endif
};

tr_lock*
tr_lockNew( void )
{
    tr_lock *           l = tr_new0( tr_lock, 1 );

#ifdef __BEOS__
    l->lock = create_sem( 1, "" );
#elif defined( WIN32 )
    InitializeCriticalSection( &l->lock ); /* supports recursion */
#else
    pthread_mutexattr_t attr;
    pthread_mutexattr_init( &attr );
    pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
    pthread_mutex_init( &l->lock, &attr );
#endif

    return l;
}

void
tr_lockFree( tr_lock * l )
{
#ifdef __BEOS__
    delete_sem( l->lock );
#elif defined( WIN32 )
    DeleteCriticalSection( &l->lock );
#else
    pthread_mutex_destroy( &l->lock );
#endif
    tr_free( l );
}

void
tr_lockLock( tr_lock * l )
{
#ifdef __BEOS__
    acquire_sem( l->lock );
#elif defined( WIN32 )
    EnterCriticalSection( &l->lock );
#else
    pthread_mutex_lock( &l->lock );
#endif
    assert( l->depth >= 0 );
    if( l->depth )
        assert( tr_areThreadsEqual( l->lockThread, tr_getCurrentThread( ) ) );
    l->lockThread = tr_getCurrentThread( );
    ++l->depth;
}

int
tr_lockHave( const tr_lock * l )
{
    return ( l->depth > 0 )
           && ( tr_areThreadsEqual( l->lockThread, tr_getCurrentThread( ) ) );
}

void
tr_lockUnlock( tr_lock * l )
{
    assert( l->depth > 0 );
    assert( tr_areThreadsEqual( l->lockThread, tr_getCurrentThread( ) ) );

    --l->depth;
    assert( l->depth >= 0 );
#ifdef __BEOS__
    release_sem( l->lock );
#elif defined( WIN32 )
    LeaveCriticalSection( &l->lock );
#else
    pthread_mutex_unlock( &l->lock );
#endif
}

/***
****  PATHS
***/

#if !defined( WIN32 ) && !defined( __BEOS__ ) && !defined( __AMIGAOS4__ )
 #include <pwd.h>
#endif

static const char *
getHomeDir( void )
{
    static char * home = NULL;

    if( !home )
    {
        home = tr_strdup( getenv( "HOME" ) );

        if( !home )
        {
#ifdef WIN32
            SHGetFolderPath( NULL, CSIDL_MYDOCUMENTS, NULL, 0, home );
#elif defined( __BEOS__ ) || defined( __AMIGAOS4__ )
            home = tr_strdup( "" );
#else
            struct passwd * pw = getpwuid( getuid( ) );
            if( pw )
                home = tr_strdup( pw->pw_dir );
            endpwent( );
#endif
        }

        if( !home )
            home = tr_strdup( "" );
    }

    return home;
}

static const char *
getOldConfigDir( void )
{
    static char * path = NULL;

    if( !path )
    {
#ifdef __BEOS__
        char buf[MAX_PATH_LENGTH];
        find_directory( B_USER_SETTINGS_DIRECTORY,
                       dev_for_path( "/boot" ), true,
                       buf, sizeof( buf ) );
        path = tr_buildPath( buf, "Transmission", NULL );
#elif defined( SYS_DARWIN )
        path = tr_buildPath( getHomeDir( ), "Library",
                              "Application Support",
                              "Transmission", NULL );
#elif defined( __AMIGAOS4__ )
        path = tr_strdup( "PROGDIR:.transmission" );
#elif defined( WIN32 )
        char appdata[MAX_PATH_LENGTH];
        SHGetFolderPath( NULL, CSIDL_APPDATA, NULL, 0, appdata );
        path = tr_buildPath( appdata, "Transmission", NULL );
#else
        path = tr_buildPath( getHomeDir( ), ".transmission", NULL );
#endif
    }

    return path;
}

#if defined(SYS_DARWIN) || defined(WIN32)
 #define RESUME_SUBDIR  "Resume"
 #define TORRENT_SUBDIR "Torrents"
#else
 #define RESUME_SUBDIR  "resume"
 #define TORRENT_SUBDIR "torrents"
#endif

static const char *
getOldTorrentsDir( void )
{
    static char * path = NULL;

    if( !path )
        path = tr_buildPath( getOldConfigDir( ), TORRENT_SUBDIR, NULL );

    return path;
}

static const char *
getOldCacheDir( void )
{
    static char * path = NULL;

    if( !path )
    {
#if defined( __BEOS__ ) || defined( WIN32 )
        path = tr_buildPath( getOldConfigDir( ), "Cache", NULL );
#elif defined( SYS_DARWIN )
        path = tr_buildPath( getHomeDir( ), "Library", "Caches", "Transmission", NULL );
#else
        path = tr_buildPath( getOldConfigDir( ), "cache", NULL );
#endif
    }

    return path;
}

static void
moveFiles( const char * oldDir,
           const char * newDir )
{
    if( oldDir && newDir && strcmp( oldDir, newDir ) )
    {
        DIR * dirh = opendir( oldDir );
        if( dirh )
        {
            int             count = 0;
            struct dirent * dirp;
            while( ( dirp = readdir( dirh ) ) )
            {
                if( strcmp( dirp->d_name,
                            "." ) && strcmp( dirp->d_name, ".." ) )
                {
                    char * o = tr_buildPath( oldDir, dirp->d_name, NULL );
                    char * n = tr_buildPath( newDir, dirp->d_name, NULL );
                    rename( o, n );
                    ++count;
                    tr_free( n );
                    tr_free( o );
                }
            }

            if( count )
                tr_inf( _( "Migrated %1$d files from \"%2$s\" to \"%3$s\"" ),
                        count, oldDir, newDir );
            closedir( dirh );
        }
    }
}

static void
migrateFiles( const tr_handle * handle )
{
    static int migrated = FALSE;

    if( !migrated )
    {
        const char * oldDir;
        const char * newDir;
        migrated = TRUE;

        oldDir = getOldTorrentsDir( );
        newDir = tr_getTorrentDir( handle );
        moveFiles( oldDir, newDir );

        oldDir = getOldCacheDir( );
        newDir = tr_getResumeDir( handle );
        moveFiles( oldDir, newDir );
    }
}

void
tr_setConfigDir( tr_handle *  handle,
                 const char * configDir )
{
    char * path;

    handle->configDir = tr_strdup( configDir );

    path = tr_buildPath( configDir, RESUME_SUBDIR, NULL );
    tr_mkdirp( path, 0777 );
    handle->resumeDir = path;

    path = tr_buildPath( configDir, TORRENT_SUBDIR, NULL );
    tr_mkdirp( path, 0777 );
    handle->torrentDir = path;

    migrateFiles( handle );
}

const char *
tr_sessionGetConfigDir( const tr_handle * handle )
{
    return handle->configDir;
}

const char *
tr_getTorrentDir( const tr_handle * handle )
{
    return handle->torrentDir;
}

const char *
tr_getResumeDir( const tr_handle * handle )
{
    return handle->resumeDir;
}

const char*
tr_getDefaultConfigDir( void )
{
    static char * s = NULL;

    if( !s )
    {
        if( ( s = getenv( "TRANSMISSION_HOME" ) ) )
        {
            s = tr_strdup( s );
        }
        else
        {
#ifdef SYS_DARWIN
            s = tr_buildPath( getHomeDir( ), "Library",
                              "Application Support", "Transmission", NULL );
#elif defined( WIN32 )
            char appdata[MAX_PATH_LENGTH];
            SHGetFolderPath( NULL, CSIDL_APPDATA, NULL, 0, appdata );
            s = tr_buildPath( appdata, "Transmission", NULL );
#else
            if( ( s = getenv( "XDG_CONFIG_HOME" ) ) )
                s = tr_buildPath( s, "transmission", NULL );
            else
                s = tr_buildPath( getHomeDir( ), ".config", "transmission", NULL );
#endif
        }
    }

    return s;
}

/***
****
***/

static int
isClutchDir( const char * path )
{
    struct stat sb;
    char * tmp = tr_buildPath( path, "javascript", "transmission.js", NULL );
    const int ret = !stat( tmp, &sb );
    tr_inf( _( "Searching for web interface file \"%s\"" ), tmp );
    tr_free( tmp );
    return ret;
    
}

const char *
tr_getClutchDir( const tr_session * session UNUSED )
{
    static char * s = NULL;

    if( !s )
    {
        if( ( s = getenv( "CLUTCH_HOME" ) ) )
        {
            s = tr_strdup( s );
        }
        else if( ( s = getenv( "TRANSMISSION_WEB_HOME" ) ) )
        {
            s = tr_strdup( s );
        }
        else
        {
#ifdef SYS_DARWIN
            CFURLRef     appURL = CFBundleCopyBundleURL(
                 CFBundleGetMainBundle( ) );
            CFStringRef  appRef = CFURLCopyFileSystemPath(
                appURL, kCFURLPOSIXPathStyle );
            const char * appString = CFStringGetCStringPtr(
                 appRef, CFStringGetFastestEncoding( appRef ) );
            CFRelease( appURL );
            CFRelease( appRef );

            s = tr_buildPath( appString, "Contents", "Resources", "web", NULL );
#elif defined( WIN32 )

 #warning hey win32 people is this good or is there a better implementation of the next four lines
            char appdata[MAX_PATH_LENGTH];
            SHGetFolderPath( NULL, CSIDL_APPDATA, NULL, 0, appdata );
            s = tr_buildPath( appdata, "Transmission", NULL );
#else
            tr_list *candidates = NULL, *l;

            /* XDG_DATA_HOME should be the first in the list of candidates */
            s = getenv( "XDG_DATA_HOME" );
            if( s && *s )
                tr_list_append( &candidates, tr_strdup( s ) );
            else {
                char * dhome = tr_buildPath( getHomeDir( ), ".local", "share", NULL );
                tr_list_append( &candidates, dhome );
            }

            /* XDG_DATA_DIRS are the backup directories */
            s = getenv( "XDG_DATA_DIRS" );
            if( !s || !*s )
                s =  PACKAGE_DATA_DIR ":/usr/local/share/:/usr/share/";
            while( s && *s )
            {
                char * end = strchr( s, ':' );
                if( end )
                {
                    tr_list_append( &candidates, tr_strndup( s, end - s ) );
                    s = end + 1;
                }
                else
                {
                    tr_list_append( &candidates, tr_strdup( s ) );
                    break;
                }
            }

            for( l = candidates; l; l = l->next )
            {
                char * path = tr_buildPath( l->data, "transmission", "web", NULL );
                const int found = isClutchDir( path );
                if( found ) {
                    s = path;
                    break;
                }
                tr_free( path );
            }

            tr_list_free( &candidates, tr_free );
#endif
        }
    }

    return s;
}

/***
****
***/

tr_lockfile_state_t
tr_lockfile( const char * filename )
{
    tr_lockfile_state_t ret;

#ifdef WIN32

    HANDLE              file = CreateFile(
        filename,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ |
        FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL );
    if( file == INVALID_HANDLE_VALUE )
        ret = TR_LOCKFILE_EOPEN;
    else if( !LockFile( file, 0, 0, 1, 1 ) )
        ret = TR_LOCKFILE_ELOCK;
    else
        ret = TR_LOCKFILE_SUCCESS;

#else

    int fd = open( filename, O_RDWR | O_CREAT, 0666 );
    if( fd < 0 )
        ret = TR_LOCKFILE_EOPEN;
    else
    {
        struct flock lk;
        memset( &lk, 0,  sizeof( lk ) );
        lk.l_start = 0;
        lk.l_len = 0;
        lk.l_type = F_WRLCK;
        lk.l_whence = SEEK_SET;
        if( -1 == fcntl( fd, F_SETLK, &lk ) )
            ret = TR_LOCKFILE_ELOCK;
        else
            ret = TR_LOCKFILE_SUCCESS;
    }

#endif

    return ret;
}

#ifdef WIN32

/* The following mmap functions are by Joerg Walter, and were taken from
 * his paper at: http://www.genesys-e.de/jwalter/mix4win.htm
 */

#if defined(_MSC_VER)
__declspec( align( 4 ) ) static LONG volatile g_sl;
#else
static LONG volatile g_sl __attribute__ ( ( aligned ( 4 ) ) );
#endif

/* Wait for spin lock */
static int
slwait( LONG volatile *sl )
{
    while( InterlockedCompareExchange ( sl, 1, 0 ) != 0 )
        Sleep ( 0 );

    return 0;
}

/* Release spin lock */
static int
slrelease( LONG volatile *sl )
{
    InterlockedExchange ( sl, 0 );
    return 0;
}

/* getpagesize for windows */
static long
getpagesize( void )
{
    static long g_pagesize = 0;

    if( !g_pagesize )
    {
        SYSTEM_INFO system_info;
        GetSystemInfo ( &system_info );
        g_pagesize = system_info.dwPageSize;
    }
    return g_pagesize;
}

static long
getregionsize( void )
{
    static long g_regionsize = 0;

    if( !g_regionsize )
    {
        SYSTEM_INFO system_info;
        GetSystemInfo ( &system_info );
        g_regionsize = system_info.dwAllocationGranularity;
    }
    return g_regionsize;
}

void *
mmap( void *ptr,
      long  size,
      long  prot,
      long  type,
      long  handle,
      long  arg )
{
    static long g_pagesize;
    static long g_regionsize;

    /* Wait for spin lock */
    slwait ( &g_sl );
    /* First time initialization */
    if( !g_pagesize )
        g_pagesize = getpagesize ( );
    if( !g_regionsize )
        g_regionsize = getregionsize ( );
    /* Allocate this */
    ptr = VirtualAlloc ( ptr, size,
                         MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN,
                         PAGE_READWRITE );
    if( !ptr )
    {
        ptr = (void *) -1;
        goto mmap_exit;
    }
mmap_exit:
    /* Release spin lock */
    slrelease ( &g_sl );
    return ptr;
}

long
munmap( void *ptr,
        long  size )
{
    static long g_pagesize;
    static long g_regionsize;
    int         rc = -1;

    /* Wait for spin lock */
    slwait ( &g_sl );
    /* First time initialization */
    if( !g_pagesize )
        g_pagesize = getpagesize ( );
    if( !g_regionsize )
        g_regionsize = getregionsize ( );
    /* Free this */
    if( !VirtualFree ( ptr, 0,
                       MEM_RELEASE ) )
        goto munmap_exit;
    rc = 0;
munmap_exit:
    /* Release spin lock */
    slrelease ( &g_sl );
    return rc;
}

#endif

