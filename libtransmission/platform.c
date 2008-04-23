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

#ifdef __BEOS__
  #include <signal.h> 
  #include <fs_info.h>
  #include <FindDirectory.h>
  #include <kernel/OS.h>
  #define BEOS_MAX_THREADS 256
#elif defined(WIN32)
  #include <windows.h>
  #include <shlobj.h> /* for CSIDL_APPDATA, CSIDL_PROFILE */
#else
  #define _XOPEN_SOURCE 500 /* needed for recursive locks. */
  #ifndef __USE_UNIX98
  #define __USE_UNIX98 /* some older Linuxes need it spelt out for them */
  #endif
  #include <pthread.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h> /* getuid getpid close */

#include "transmission.h"
#include "platform.h"
#include "trcompat.h"
#include "utils.h"

/***
****  THREADS
***/

#ifdef __BEOS__
typedef thread_id tr_thread_id;
#elif defined(WIN32)
typedef DWORD tr_thread_id;
#else
typedef pthread_t tr_thread_id;
#endif

static tr_thread_id
tr_getCurrentThread( void )
{
#ifdef __BEOS__
    return find_thread( NULL );
#elif defined(WIN32)
    return GetCurrentThreadId();
#else
    return pthread_self( );
#endif
}

static int
tr_areThreadsEqual( tr_thread_id a, tr_thread_id b )
{
#ifdef __BEOS__
    return a == b;
#elif defined(WIN32)
    return a == b;
#else
    return pthread_equal( a, b );
#endif
}

struct tr_thread
{
    void          (* func ) ( void * );
    void           * arg;
    const char     * name;
    tr_thread_id     thread;
#ifdef WIN32
    HANDLE           thread_handle;
#endif
};

int
tr_amInThread ( const tr_thread * t )
{
    return tr_areThreadsEqual( tr_getCurrentThread(), t->thread );
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
    const char * name = t->name;

#ifdef __BEOS__
    /* This is required because on BeOS, SIGINT is sent to each thread,
       which kills them not nicely */
    signal( SIGINT, SIG_IGN );
#endif

    tr_dbg( "Thread '%s' started", name );
    t->func( t->arg );
    tr_dbg( "Thread '%s' exited", name );

#ifdef WIN32
    _endthreadex( 0 );
    return 0;
#endif
}

tr_thread *
tr_threadNew( void (*func)(void *),
              void * arg,
              const char * name )
{
    tr_thread * t = tr_new0( tr_thread, 1 );
    t->func = func;
    t->arg  = arg;
    t->name = name;

#ifdef __BEOS__
    t->thread = spawn_thread( (void*)ThreadFunc, name, B_NORMAL_PRIORITY, t );
    resume_thread( t->thread );
#elif defined(WIN32)
    {
        unsigned int id;
        t->thread_handle = (HANDLE) _beginthreadex( NULL, 0, &ThreadFunc, t, 0, &id );
        t->thread = (DWORD) id;
    }
#else
    pthread_create( &t->thread, NULL, (void * (*) (void *)) ThreadFunc, t );
#endif

    return t;
}
    
/***
****  LOCKS
***/

struct tr_lock
{
    int depth;
#ifdef __BEOS__
    sem_id lock;
    thread_id lockThread;
#elif defined(WIN32)
    CRITICAL_SECTION lock;
    DWORD lockThread;
#else
    pthread_mutex_t lock;
    pthread_t lockThread;
#endif
};

tr_lock*
tr_lockNew( void )
{
    tr_lock * l = tr_new0( tr_lock, 1 );

#ifdef __BEOS__
    l->lock = create_sem( 1, "" );
#elif defined(WIN32)
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
#elif defined(WIN32)
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
#elif defined(WIN32)
    EnterCriticalSection( &l->lock );
#else
    pthread_mutex_lock( &l->lock );
#endif
    assert( l->depth >= 0 );
    if( l->depth )
        assert( tr_areThreadsEqual( l->lockThread, tr_getCurrentThread() ) );
    l->lockThread = tr_getCurrentThread( );
    ++l->depth;
}

int
tr_lockHave( const tr_lock * l )
{
    return ( l->depth > 0 )
        && ( tr_areThreadsEqual( l->lockThread, tr_getCurrentThread() ) );
}

void
tr_lockUnlock( tr_lock * l )
{
    assert( l->depth > 0 );
    assert( tr_areThreadsEqual( l->lockThread, tr_getCurrentThread() ));

    --l->depth;
    assert( l->depth >= 0 );
#ifdef __BEOS__
    release_sem( l->lock );
#elif defined(WIN32)
    LeaveCriticalSection( &l->lock );
#else
    pthread_mutex_unlock( &l->lock );
#endif
}

/***
****  PATHS
***/

#if !defined(WIN32) && !defined(__BEOS__) && !defined(__AMIGAOS4__)
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
            SHGetFolderPath( NULL, CSIDL_PROFILE, NULL, 0, home );
#elif defined(__BEOS__) || defined(__AMIGAOS4__)
            home = tr_strdup( "" );
#else
            struct passwd * pw = getpwuid( getuid() );
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
        char buf[MAX_PATH_LENGTH];
#ifdef __BEOS__
        find_directory( B_USER_SETTINGS_DIRECTORY,
                        dev_for_path("/boot"), true,
                        buf, sizeof( buf ) );
        strcat( buf, "/Transmission" );
#elif defined( SYS_DARWIN )
        tr_buildPath ( buf, sizeof( buf ), getHomeDir( ),
                       "Library", "Application Support",
                       "Transmission", NULL );
#elif defined(__AMIGAOS4__)
        strlcpy( buf, "PROGDIR:.transmission", sizeof( buf ) );
#elif defined(WIN32)
        char appdata[MAX_PATH_LENGTH];
        SHGetFolderPath( NULL, CSIDL_APPDATA, NULL, 0, appdata );
        tr_buildPath( buf, sizeof(buf),
                      appdata, "Transmission", NULL );
#else
        tr_buildPath ( buf, sizeof(buf),
                       getHomeDir( ), ".transmission", NULL );
#endif
        path = tr_strdup( buf );
    }

    return path;
}

static const char *
getOldTorrentsDir( void )
{
    static char * path = NULL;

    if( !path )
    {
        char buf[MAX_PATH_LENGTH];
        const char * p = getOldConfigDir();
#if defined(__BEOS__) || defined(WIN32) || defined(SYS_DARWIN)
        tr_buildPath( buf, sizeof( buf ), p, "Torrents", NULL );
#else
        tr_buildPath( buf, sizeof( buf ), p, "torrents", NULL );
#endif

        path = tr_strdup( buf );
    }

    return path;
}
static const char *
getOldCacheDir( void )
{
    static char * path = NULL;

    if( !path )
    {
        char buf[MAX_PATH_LENGTH];
#if defined(__BEOS__) || defined(WIN32)
        const char * p = getOldConfigDir( );
        tr_buildPath( buf, sizeof( buf ), p, "Cache", NULL );
#elif defined( SYS_DARWIN )
        tr_buildPath( buf, sizeof( buf ), getHomeDir(),
                      "Library", "Caches", "Transmission", NULL );
#else
        const char * p = getOldConfigDir( );
        tr_buildPath( buf, sizeof( buf ), p, "cache", NULL );
#endif
        path = tr_strdup( buf );
    }

    return path;
}

static void
moveFiles( const char * oldDir, const char * newDir )
{
    if( oldDir && newDir && strcmp( oldDir, newDir ) )
    {
        DIR * dirh = opendir( oldDir );
        if( dirh )
        {
            int count = 0;
            struct dirent * dirp;
            while(( dirp = readdir( dirh )))
            {
                if( strcmp( dirp->d_name, "." ) && strcmp( dirp->d_name, ".." ) )
                {
                    char o[MAX_PATH_LENGTH];
                    char n[MAX_PATH_LENGTH];
                    tr_buildPath( o, sizeof(o), oldDir, dirp->d_name, NULL );
                    tr_buildPath( n, sizeof(n), newDir, dirp->d_name, NULL );
                    rename( o, n );
                    ++count;
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

#ifdef SYS_DARWIN
#define RESUME_SUBDIR  "Resume"
#define TORRENT_SUBDIR "Torrents"
#else
#define RESUME_SUBDIR  "resume"
#define TORRENT_SUBDIR "torrents"
#endif

void
tr_setConfigDir( tr_handle * handle, const char * configDir )
{
    char buf[MAX_PATH_LENGTH];

    handle->configDir = tr_strdup( configDir );

    tr_buildPath( buf, sizeof( buf ), configDir, RESUME_SUBDIR, NULL );
    tr_mkdirp( buf, 0777 );
    handle->resumeDir = tr_strdup( buf );

    tr_buildPath( buf, sizeof( buf ), configDir, TORRENT_SUBDIR, NULL );
    tr_mkdirp( buf, 0777 );
    handle->torrentDir = tr_strdup( buf );

    migrateFiles( handle );
}

const char *
tr_getConfigDir( const tr_handle * handle )
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
        char path[MAX_PATH_LENGTH];

        if(( s = getenv( "TRANSMISSION_HOME" )))
        {
            snprintf( path, sizeof( path ), s );
        }
        else
        {
#ifdef SYS_DARWIN
            tr_buildPath( path, sizeof( path ),
                          getHomeDir( ), "Library", "Application Support",
                          "Transmission", NULL );
#elif defined(WIN32)
            char appdata[MAX_PATH_LENGTH];
            SHGetFolderPath( NULL, CSIDL_APPDATA, NULL, 0, appdata );
            tr_buildPath( path, sizeof( path ),
                          appdata, "Transmission", NULL );
#else
            if(( s = getenv( "XDG_CONFIG_HOME" )))
                tr_buildPath( path, sizeof( path ),
                              s, "transmission", NULL );
            else
                tr_buildPath( path, sizeof( path ),
                              getHomeDir(), ".config", "transmission", NULL );
#endif
        }

        s = tr_strdup( path );
    }

    return s;
}

/***
****
***/

int
tr_lockfile( const char * filename )
{
    int ret;

#ifdef WIN32

    HANDLE file = CreateFile( filename,
                              GENERIC_READ|GENERIC_WRITE,
                              FILE_SHARE_READ|FILE_SHARE_WRITE,
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
    else {
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
