/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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
#ifdef SYS_DARWIN
  #include <sys/types.h>
  #include <dirent.h>
#endif

#include "transmission.h"

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
#else
    snprintf( prefsDirectory, MAX_PATH_LENGTH, "%s/.transmission",
              getenv( "HOME" ) );
#endif

	mkdir( prefsDirectory, 0755 );
    init = 1;

#ifdef SYS_DARWIN
    DIR * dirh;
    struct dirent * dirp;
    char oldDirectory[MAX_PATH_LENGTH];
    char oldFile[MAX_PATH_LENGTH];
    char newFile[MAX_PATH_LENGTH];
    snprintf( oldDirectory, MAX_PATH_LENGTH, "%s/.transmission",
              getenv( "HOME" ) );
    if( ( dirh = opendir( oldDirectory ) ) )
    {
        while( ( dirp = readdir( dirh ) ) )
        {
            if( !strcmp( ".", dirp->d_name ) ||
                !strcmp( "..", dirp->d_name ) )
            {
                continue;
            }
            snprintf( oldFile, MAX_PATH_LENGTH, "%s/%s",
                      oldDirectory, dirp->d_name );
            snprintf( newFile, MAX_PATH_LENGTH, "%s/%s",
                      prefsDirectory, dirp->d_name );
            rename( oldFile, newFile );
        }

        closedir( dirh );
        rmdir( oldDirectory );
    }
#endif

    return prefsDirectory;
}

void tr_threadCreate( tr_thread_t * t, void (*func)(void *), void * arg )
{
#ifdef SYS_BEOS
    *t = spawn_thread( (void *) func, "torrent-tx", arg );
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

