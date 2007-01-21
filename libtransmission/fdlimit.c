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

#include "transmission.h"

#define TR_MAX_OPEN_FILES 16 /* That is, real files, not sockets */
#define TR_RESERVED_FDS   16 /* Number of sockets reserved for
                                connections to trackers */

/***********************************************************************
 * Structures
 **********************************************************************/
typedef struct tr_openFile_s
{
    char       folder[MAX_PATH_LENGTH];
    char       name[MAX_PATH_LENGTH];
    int        file;
    int        write;

#define STATUS_INVALID 1
#define STATUS_UNUSED  2
#define STATUS_USED    4
#define STATUS_CLOSING 8
    int        status;

    uint64_t   date;
}
tr_openFile_t;

typedef struct tr_fd_s
{
    tr_lock_t       lock;
    tr_cond_t       cond;
    
    int             reserved;

    int             normal;
    int             normalMax;

    tr_openFile_t   open[TR_MAX_OPEN_FILES];
}
tr_fd_t;

static tr_fd_t * gFd = NULL;

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static int  ErrorFromErrno();
static int  OpenFile( int i, char * folder, char * name, int write );
static void CloseFile( int i );


/***********************************************************************
 * tr_fdInit
 **********************************************************************/
void tr_fdInit()
{
    int i, j, s[4096];

    if( gFd )
    {
        tr_err( "tr_fdInit was called before!" );
        return;
    }

    gFd = calloc( sizeof( tr_fd_t ), 1 );

    /* Init lock and cond */
    tr_lockInit( &gFd->lock );
    tr_condInit( &gFd->cond );

    /* Detect the maximum number of open files or sockets */
    for( i = 0; i < 4096; i++ )
    {
        if( ( s[i] = socket( AF_INET, SOCK_STREAM, 0 ) ) < 0 )
        {
            break;
        }
    }
    for( j = 0; j < i; j++ )
    {
        tr_netClose( s[j] );
    }

    tr_dbg( "%d usable file descriptors", i );

    gFd->reserved  = 0;
    gFd->normal    = 0;

    gFd->normalMax = i - TR_RESERVED_FDS - 10;
        /* To be safe, in case the UI needs to write a preferences file
           or something */

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        gFd->open[i].status = STATUS_INVALID;
    }
}

/***********************************************************************
 * tr_fdFileOpen
 **********************************************************************/
int tr_fdFileOpen( char * folder, char * name, int write )
{
    int i, winner, ret;
    uint64_t date;

    tr_lockLock( &gFd->lock );

    /* Is it already open? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( gFd->open[i].status & STATUS_INVALID ||
            strcmp( folder, gFd->open[i].folder ) ||
            strcmp( name, gFd->open[i].name ) )
        {
            continue;
        }
        if( gFd->open[i].status & STATUS_CLOSING )
        {
            /* File is being closed by another thread, wait until
             * it's done before we reopen it */
            tr_condWait( &gFd->cond, &gFd->lock );
            i = -1;
            continue;
        }
        if( gFd->open[i].write < write )
        {
            /* File is open read-only and needs to be closed then
             * re-opened read-write */
            CloseFile( i );
            continue;
        }
        winner = i;
        goto done;
    }

    /* Can we open one more file? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( gFd->open[i].status & STATUS_INVALID )
        {
            winner = i;
            goto open;
        }
    }

    /* All slots taken - close the oldest currently unused file */
    for( ;; )
    {
        date   = tr_date() + 1;
        winner = -1;

        for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
        {
            if( !( gFd->open[i].status & STATUS_UNUSED ) )
            {
                continue;
            }
            if( gFd->open[i].date < date )
            {
                winner = i;
                date   = gFd->open[i].date;
            }
        }

        if( winner >= 0 )
        {
            CloseFile( winner );
            goto open;
        }

        /* All used! Wait a bit and try again */
        tr_condWait( &gFd->cond, &gFd->lock );
    }

open:
    if( ( ret = OpenFile( winner, folder, name, write ) ) )
    {
        tr_lockUnlock( &gFd->lock );
        return ret;
    }
    snprintf( gFd->open[winner].folder, MAX_PATH_LENGTH, "%s", folder );
    snprintf( gFd->open[winner].name, MAX_PATH_LENGTH, "%s", name );
    gFd->open[winner].write = write;

done:
    gFd->open[winner].status = STATUS_USED;
    gFd->open[winner].date   = tr_date();
    tr_lockUnlock( &gFd->lock );
    
    return gFd->open[winner].file;
}

/***********************************************************************
 * tr_fdFileRelease
 **********************************************************************/
void tr_fdFileRelease( int file )
{
    int i;
    tr_lockLock( &gFd->lock );

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( gFd->open[i].file == file )
        {
            gFd->open[i].status = STATUS_UNUSED;
            break;
        }
    }
    
    tr_condSignal( &gFd->cond );
    tr_lockUnlock( &gFd->lock );
}

/***********************************************************************
 * tr_fdFileClose
 **********************************************************************/
void tr_fdFileClose( char * folder, char * name )
{
    int i;

    tr_lockLock( &gFd->lock );

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( gFd->open[i].status & STATUS_INVALID )
        {
            continue;
        }
        if( !strcmp( folder, gFd->open[i].folder ) &&
            !strcmp( name, gFd->open[i].name ) )
        {
            CloseFile( i );
        }
    }

    tr_lockUnlock( &gFd->lock );
}

/***********************************************************************
 * tr_fdSocketWillCreate
 **********************************************************************/
int tr_fdSocketWillCreate( int reserved )
{
    int ret;

    tr_lockLock( &gFd->lock );

    if( reserved )
    {
        if( gFd->reserved < TR_RESERVED_FDS )
        {
            ret = 0;
            (gFd->reserved)++;
        }
        else
        {
            ret = 1;
        }
    }
    else
    {
        if( gFd->normal < gFd->normalMax )
        {
            ret = 0;
            (gFd->normal)++;
        }
        else
        {
            ret = 1;
        }
    }

    tr_lockUnlock( &gFd->lock );

    return ret;
}

/***********************************************************************
 * tr_fdSocketClosed
 **********************************************************************/
void tr_fdSocketClosed( int reserved )
{
    tr_lockLock( &gFd->lock );

    if( reserved )
    {
        (gFd->reserved)--;
    }
    else
    {
        (gFd->normal)--;
    }

    tr_lockUnlock( &gFd->lock );
}

/***********************************************************************
 * tr_fdClose
 **********************************************************************/
void tr_fdClose()
{
    tr_lockClose( &gFd->lock );
    tr_condClose( &gFd->cond );
    free( gFd );
}


/***********************************************************************
 * Local functions
 **********************************************************************/

/***********************************************************************
 * ErrorFromErrno
 **********************************************************************/
static int ErrorFromErrno()
{
    if( errno == EACCES || errno == EROFS )
        return TR_ERROR_IO_PERMISSIONS;
    return TR_ERROR_IO_OTHER;
}

/***********************************************************************
 * CheckFolder
 ***********************************************************************
 *
 **********************************************************************/
static int OpenFile( int i, char * folder, char * name, int write )
{
    tr_openFile_t * file = &gFd->open[i];
    struct stat sb;
    char * path;

    tr_dbg( "Opening %s in %s (%d)", name, folder, write );

    /* Make sure the parent folder exists */
    if( stat( folder, &sb ) || !S_ISDIR( sb.st_mode ) )
    {
        return TR_ERROR_IO_PARENT;
    }

    asprintf( &path, "%s/%s", folder, name );

    /* Create subfolders, if any */
    if( write )
    {
        char * p = path + strlen( folder ) + 1;
        char * s;

        while( ( s = strchr( p, '/' ) ) )
        {
            *s = '\0';
            if( stat( path, &sb ) )
            {
                if( mkdir( path, 0777 ) )
                {
                    tr_err( "Could not create folder '%s'", path );
                    free( path );
                    return ErrorFromErrno();
                }
            }
            else
            {
                if( !S_ISDIR( sb.st_mode ) )
                {
                    tr_err( "Is not a folder: '%s'", path );
                    free( path );
                    return TR_ERROR_IO_OTHER;
                }
            }
            *s = '/';
            p = s + 1;
        }
    }

    /* Now try to really open the file */
    file->file = open( path, write ? ( O_RDWR | O_CREAT ) : O_RDONLY, 0666 );
    free( path );

    if( file->file < 0 )
    {
        int ret = ErrorFromErrno();
        tr_err( "Could not open %s in %s (%d, %d)", name, folder, write, ret );
        return ret;
    }

    return TR_OK;
}

/***********************************************************************
 * CloseFile
 ***********************************************************************
 * We first mark it as closing then release the lock while doing so,
 * because close() may take same time and we don't want to block other
 * threads.
 **********************************************************************/
static void CloseFile( int i )
{
    tr_openFile_t * file = &gFd->open[i];

    /* If it's already being closed by another thread, just wait till
     * it is done */
    while( file->status & STATUS_CLOSING )
    {
        tr_condWait( &gFd->cond, &gFd->lock );
    }
    if( file->status & STATUS_INVALID )
    {
        return;
    }

    /* Nobody is closing it already, so let's do it */
    if( file->status & STATUS_USED )
    {
        tr_err( "CloseFile: closing a file that's being used!" );
    }
    tr_dbg( "Closing %s in %s (%d)", file->name, file->folder, file->write );
    file->status = STATUS_CLOSING;
    tr_lockUnlock( &gFd->lock );
    close( file->file );
    tr_lockLock( &gFd->lock );
    file->status = STATUS_INVALID;
    tr_condSignal( &gFd->cond );
}

