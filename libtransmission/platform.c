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

#ifdef SYS_BEOS
  #include <fs_info.h>
  #include <FindDirectory.h>
#endif
#include <sys/types.h>
#include <dirent.h>

#include "transmission.h"

static void
tr_migrateResume( const char *oldDirectory, const char *newDirectory )
{
    DIR * dirh;
    struct dirent * dirp;
    char oldFile[MAX_PATH_LENGTH];
    char newFile[MAX_PATH_LENGTH];

    if( ( dirh = opendir( oldDirectory ) ) )
    {
        while( ( dirp = readdir( dirh ) ) )
        {
            if( strncmp( "resume.", dirp->d_name, 7 ) )
            {
                continue;
            }
            snprintf( oldFile, MAX_PATH_LENGTH, "%s/%s",
                      oldDirectory, dirp->d_name );
            snprintf( newFile, MAX_PATH_LENGTH, "%s/%s",
                      newDirectory, dirp->d_name );
            rename( oldFile, newFile );
        }

        closedir( dirh );
    }
}

char * tr_getPrefsDirectory()
{
    static char prefsDirectory[MAX_PATH_LENGTH];
    static int  init = 0;

    if( init )
    {
        return prefsDirectory;
    }

#ifdef SYS_BEOS
	find_directory( B_USER_SETTINGS_DIRECTORY, dev_for_path("/boot"),
	                true, prefsDirectory, MAX_PATH_LENGTH );
	strcat( prefsDirectory, "/Transmission" );
#elif defined( SYS_DARWIN )
    snprintf( prefsDirectory, MAX_PATH_LENGTH,
              "%s/Library/Caches/Transmission", getenv( "HOME" ) );
#elif defined(__AMIGAOS4__)
    snprintf( prefsDirectory, MAX_PATH_LENGTH, "PROGDIR:.transmission" );
#else
    snprintf( prefsDirectory, MAX_PATH_LENGTH, "%s/.transmission",
              getenv( "HOME" ) );
#endif

    tr_mkdir( prefsDirectory );
    init = 1;

#ifdef SYS_DARWIN
    char oldDirectory[MAX_PATH_LENGTH];
    snprintf( oldDirectory, MAX_PATH_LENGTH, "%s/.transmission",
              getenv( "HOME" ) );
    tr_migrateResume( oldDirectory, prefsDirectory );
    rmdir( oldDirectory );
#endif

    return prefsDirectory;
}

char * tr_getCacheDirectory()
{
    static char cacheDirectory[MAX_PATH_LENGTH];
    static int  init = 0;

    if( init )
    {
        return cacheDirectory;
    }

#ifdef SYS_BEOS
    /* XXX hey Bryan, is this fine with you? */
    snprintf( cacheDirectory, MAX_PATH_LENGTH, "%s/Cache",
              tr_getPrefsDirectory() );
#elif defined( SYS_DARWIN )
    snprintf( cacheDirectory, MAX_PATH_LENGTH, "%s",
              tr_getPrefsDirectory() );
#else
    snprintf( cacheDirectory, MAX_PATH_LENGTH, "%s/cache",
              tr_getPrefsDirectory() );
#endif

    tr_mkdir( cacheDirectory );
    init = 1;

    if( strcmp( tr_getPrefsDirectory(), cacheDirectory ) )
    {
        tr_migrateResume( tr_getPrefsDirectory(), cacheDirectory );
    }

    return cacheDirectory;
}

char * tr_getTorrentsDirectory()
{
    static char torrentsDirectory[MAX_PATH_LENGTH];
    static int  init = 0;

    if( init )
    {
        return torrentsDirectory;
    }

#ifdef SYS_BEOS
    /* XXX hey Bryan, is this fine with you? */
    snprintf( torrentsDirectory, MAX_PATH_LENGTH, "%s/Torrents",
              tr_getPrefsDirectory() );
#elif defined( SYS_DARWIN )
    snprintf( torrentsDirectory, MAX_PATH_LENGTH,
              "%s/Library/Application Support/Transmission/Torrents",
              getenv( "HOME" ) );
#else
    snprintf( torrentsDirectory, MAX_PATH_LENGTH, "%s/torrents",
              tr_getPrefsDirectory() );
#endif

    tr_mkdir( torrentsDirectory );
    init = 1;

    return torrentsDirectory;
}

void tr_threadCreate( tr_thread_t * t, void (*func)(void *), void * arg )
{
#ifdef SYS_BEOS
    *t = spawn_thread( (void *) func, "torrent-tx", B_NORMAL_PRIORITY, arg );
    resume_thread( *t );
#else
    pthread_create( t, NULL, (void *) func, arg );
#endif
}

void tr_threadJoin( tr_thread_t * t )
{
#ifdef SYS_BEOS
    long exit;
    wait_for_thread( *t, &exit );
#else
    pthread_join( *t, NULL );
#endif
}

void tr_lockInit( tr_lock_t * l )
{
#ifdef SYS_BEOS
    *l = create_sem( 1, "" );
#else
    pthread_mutex_init( l, NULL );
#endif
}

void tr_lockClose( tr_lock_t * l )
{
#ifdef SYS_BEOS
    delete_sem( *l );
#else
    pthread_mutex_destroy( l );
#endif
}

