/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
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
  #include <pthread.h>
#endif

#include <sys/types.h>
#include <dirent.h>
#include <unistd.h> /* getuid getpid */

#include "transmission.h"
#include "list.h"
#include "net.h"
#include "platform.h"
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

#ifdef __BEOS__
    thread_id        thread;
#elif defined(WIN32)
    HANDLE           thread;
    unsigned int     thread_id;
#else
    pthread_t        thread;
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
    t->thread = (HANDLE) _beginthreadex( NULL, 0, &ThreadFunc, t, 0, &t->thread_id );
#else
    pthread_create( &t->thread, NULL, (void * (*) (void *)) ThreadFunc, t );
#endif

    return t;
}
    
void
tr_threadJoin( tr_thread * t )
{
    if( t != NULL )
    {
#ifdef __BEOS__
        long exit;
        wait_for_thread( t->thread, &exit );
#elif defined(WIN32)
        WaitForSingleObject( t->thread, INFINITE );
        CloseHandle( t->thread );
#else
        pthread_join( t->thread, NULL );
#endif

        tr_dbg( "Thread '%s' joined", t->name );
        t->name = NULL;
        t->func = NULL;
        tr_free( t );
    }
}

/***
****  LOCKS
***/

struct tr_lock
{
    uint32_t depth;
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
    InitializeCriticalSection( &l->lock );
#else
    pthread_mutex_init( &l->lock, NULL );
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
    const tr_thread_id currentThread = tr_getCurrentThread( );

    if( l->lockThread == currentThread )
    {
        ++l->depth;
    }
    else
    {
#ifdef __BEOS__
        acquire_sem( l->lock );
#elif defined(WIN32)
        EnterCriticalSection( &l->lock );
#else
        pthread_mutex_lock( &l->lock );
#endif
        l->lockThread = currentThread;
        l->depth = 1;
    }
}

int
tr_lockHave( const tr_lock * l )
{
    return ( l->depth > 0 )
        && ( l->lockThread == tr_getCurrentThread() );
}

void
tr_lockUnlock( tr_lock * l )
{
    assert( tr_lockHave( l ) );

    if( !--l->depth )
    {
        l->lockThread = 0;
#ifdef __BEOS__
        release_sem( l->lock );
#elif defined(WIN32)
        LeaveCriticalSection( &l->lock );
#else
        pthread_mutex_unlock( &l->lock );
#endif
    }
}

/***
****  COND
***/

struct tr_cond
{
#ifdef __BEOS__
    sem_id sem;
    thread_id threads[BEOS_MAX_THREADS];
    int start, end;
#elif defined(WIN32)
    tr_list * events;
    tr_lock * lock;
#else
    pthread_cond_t cond;
#endif
};

#ifdef WIN32
static DWORD getContEventTLS( void )
{
    static int inited = FALSE;
    static DWORD event_tls;
    if( !inited ) {
        inited = TRUE;
        event_tls = TlsAlloc();
    }
    return event_tls;
}
#endif

tr_cond*
tr_condNew( void )
{
    tr_cond * c = tr_new0( tr_cond, 1 );
#ifdef __BEOS__
    c->sem = create_sem( 1, "" );
    c->start = 0;
    c->end = 0;
#elif defined(WIN32)
    c->events = NULL;
    c->lock = tr_lockNew( );
#else
    pthread_cond_init( &c->cond, NULL );
#endif
    return c;
}

void
tr_condWait( tr_cond * c, tr_lock * l )
{
#ifdef __BEOS__

    /* Keep track of that thread */
    acquire_sem( c->sem );
    c->threads[c->end] = find_thread( NULL );
    c->end = ( c->end + 1 ) % BEOS_MAX_THREADS;
    assert( c->end != c->start ); /* We hit BEOS_MAX_THREADS, arggh */
    release_sem( c->sem );

    release_sem( l->lock );
    suspend_thread( find_thread( NULL ) ); /* Wait for signal */
    acquire_sem( l->lock );

#elif defined(WIN32)

    /* get this thread's cond event */
    DWORD key = getContEventTLS ( );
    HANDLE hEvent = TlsGetValue( key );
    if( !hEvent ) {
        hEvent = CreateEvent( 0, FALSE, FALSE, 0 );
        TlsSetValue( key, hEvent );
    }

    /* add it to the list of events waiting to be signaled */
    tr_lockLock( c->lock );
    tr_list_append( &c->events, hEvent );
    tr_lockUnlock( c->lock );

    /* now wait for it to be signaled */
    tr_lockUnlock( l );
    WaitForSingleObject( hEvent, INFINITE );
    tr_lockLock( l );

    /* remove it from the list of events waiting to be signaled */
    tr_lockLock( c->lock );
    tr_list_remove_data( &c->events, hEvent );
    tr_lockUnlock( c->lock );

#else

    pthread_cond_wait( &c->cond, &l->lock );

#endif
}

#ifdef __BEOS__
static int condTrySignal( tr_cond * c )
{
    if( c->start == c->end )
        return 1;

    for( ;; )
    {
        thread_info info;
        get_thread_info( c->threads[c->start], &info );
        if( info.state == B_THREAD_SUSPENDED )
        {
            resume_thread( c->threads[c->start] );
            c->start = ( c->start + 1 ) % BEOS_MAX_THREADS;
            break;
        }
        /* The thread is not suspended yet, which can happen since
         * tr_condWait does not atomically suspends after releasing
         * the semaphore. Wait a bit and try again. */
        snooze( 5000 );
    }
    return 0;
}
#endif
void
tr_condSignal( tr_cond * c )
{
#ifdef __BEOS__

    acquire_sem( c->sem );
    condTrySignal( c );
    release_sem( c->sem );

#elif defined(WIN32)

    tr_lockLock( c->lock );
    if( c->events != NULL )
        SetEvent( (HANDLE)c->events->data );
    tr_lockUnlock( c->lock );

#else

    pthread_cond_signal( &c->cond );

#endif
}

void
tr_condBroadcast( tr_cond * c )
{
#ifdef __BEOS__

    acquire_sem( c->sem );
    while( !condTrySignal( c ) );
    release_sem( c->sem );

#elif defined(WIN32)

    tr_list * l;
    tr_lockLock( c->lock );
    for( l=c->events; l!=NULL; l=l->next )
        SetEvent( (HANDLE)l->data );
    tr_lockUnlock( c->lock );

#else

    pthread_cond_broadcast( &c->cond );

#endif
}

void
tr_condFree( tr_cond * c )
{
#ifdef __BEOS__
    delete_sem( c->sem );
#elif defined(WIN32)
    tr_list_free( &c->events, NULL );
    tr_lockFree( c->lock );
#else
    pthread_cond_destroy( &c->cond );
#endif
    tr_free( c );
}


/***
****  PATHS
***/

#if !defined(WIN32) && !defined(__BEOS__) && !defined(__AMIGAOS4__)
#include <pwd.h>
#endif

const char *
tr_getHomeDirectory( void )
{
    static char buf[MAX_PATH_LENGTH];
    static int init = 0;
    const char * envHome;

    if( init )
        return buf;

    envHome = getenv( "HOME" );
    if( envHome )
        snprintf( buf, sizeof(buf), "%s", envHome );
    else {
#ifdef WIN32
        SHGetFolderPath( NULL, CSIDL_PROFILE, NULL, 0, buf );
#elif defined(__BEOS__) || defined(__AMIGAOS4__)
        *buf = '\0';
#else
        struct passwd * pw = getpwuid( getuid() );
        endpwent();
        if( pw != NULL )
            snprintf( buf, sizeof(buf), "%s", pw->pw_dir );
#endif
    }

    init = 1;
    return buf;
}


static void
tr_migrateResume( const char *oldDirectory, const char *newDirectory )
{
    DIR * dirh = opendir( oldDirectory );

    if( dirh != NULL )
    {
        struct dirent * dirp;

        while( ( dirp = readdir( dirh ) ) )
        {
            if( !strncmp( "resume.", dirp->d_name, 7 ) )
            {
                char o[MAX_PATH_LENGTH];
                char n[MAX_PATH_LENGTH];
                tr_buildPath( o, sizeof(o), oldDirectory, dirp->d_name, NULL );
                tr_buildPath( n, sizeof(n), newDirectory, dirp->d_name, NULL );
                rename( o, n );
            }
        }

        closedir( dirh );
    }
}

const char *
tr_getPrefsDirectory( void )
{
    static char   buf[MAX_PATH_LENGTH];
    static int    init = 0;
    static size_t buflen = sizeof(buf);
    const char* h;

    if( init )
        return buf;

    h = tr_getHomeDirectory();
#ifdef __BEOS__
    find_directory( B_USER_SETTINGS_DIRECTORY,
                    dev_for_path("/boot"), true, buf, buflen );
    strcat( buf, "/Transmission" );
#elif defined( SYS_DARWIN )
    tr_buildPath ( buf, buflen, h,
                  "Library", "Application Support", "Transmission", NULL );
#elif defined(__AMIGAOS4__)
    snprintf( buf, buflen, "PROGDIR:.transmission" );
#elif defined(WIN32)
    {
        char tmp[MAX_PATH_LENGTH];
        SHGetFolderPath( NULL, CSIDL_APPDATA, NULL, 0, tmp );
        tr_buildPath( buf, sizeof(buf), tmp, "Transmission", NULL );
        buflen = strlen( buf );
    }
#else
    tr_buildPath ( buf, buflen, h, ".transmission", NULL );
#endif

    tr_mkdirp( buf, 0700 );
    init = 1;

#ifdef SYS_DARWIN
    char old[MAX_PATH_LENGTH];
    tr_buildPath ( old, sizeof(old), h, ".transmission", NULL );
    tr_migrateResume( old, buf );
    rmdir( old );
#endif

    return buf;
}

const char *
tr_getCacheDirectory( void )
{
    static char buf[MAX_PATH_LENGTH];
    static int  init = 0;
    static const size_t buflen = sizeof(buf);
    const char * p;

    if( init )
        return buf;

    p = tr_getPrefsDirectory();
#if defined(__BEOS__) || defined(WIN32)
    tr_buildPath( buf, buflen, p, "Cache", NULL );
#elif defined( SYS_DARWIN )
    tr_buildPath( buf, buflen, tr_getHomeDirectory(),
                  "Library", "Caches", "Transmission", NULL );
#else
    tr_buildPath( buf, buflen, p, "cache", NULL );
#endif

    tr_mkdirp( buf, 0700 );
    init = 1;

    if( strcmp( p, buf ) )
        tr_migrateResume( p, buf );

    return buf;
}

const char *
tr_getTorrentsDirectory( void )
{
    static char buf[MAX_PATH_LENGTH];
    static int  init = 0;
    static const size_t buflen = sizeof(buf);
    const char * p;

    if( init )
        return buf;

    p = tr_getPrefsDirectory ();

#if defined(__BEOS__) || defined(WIN32)
    tr_buildPath( buf, buflen, p, "Torrents", NULL );
#elif defined( SYS_DARWIN )
    tr_buildPath( buf, buflen, p, "Torrents", NULL );
#else
    tr_buildPath( buf, buflen, p, "torrents", NULL );
#endif

    tr_mkdirp( buf, 0700 );
    init = 1;
    return buf;
}
