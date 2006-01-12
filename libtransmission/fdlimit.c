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

#include "transmission.h"

#define TR_MAX_OPEN_FILES 16 /* That is, real files, not sockets */
#define TR_RESERVED_FDS   16 /* Number of sockets reserved for
                                connections to trackers */

typedef struct tr_openFile_s
{
    char       path[MAX_PATH_LENGTH];
    FILE     * file;

#define STATUS_INVALID 1
#define STATUS_UNUSED  2
#define STATUS_USED    4
    int        status;

    uint64_t   date;

} tr_openFile_t;

struct tr_fd_s
{
    tr_lock_t       lock;
    
    int             reserved;

    int             normal;
    int             normalMax;

    tr_openFile_t   open[TR_MAX_OPEN_FILES];
};

/***********************************************************************
 * tr_fdInit
 **********************************************************************/
tr_fd_t * tr_fdInit()
{
    tr_fd_t * f;
    int i, j, s[4096];

    f = calloc( sizeof( tr_fd_t ), 1 );

    /* Init lock */
    tr_lockInit( &f->lock );

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

    f->reserved  = 0;
    f->normal    = 0;

    f->normalMax = i - TR_RESERVED_FDS - 10;
        /* To be safe, in case the UI needs to write a preferences file
           or something */

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        f->open[i].status = STATUS_INVALID;
    }

    return f;
}

/***********************************************************************
 * tr_fdFileOpen
 **********************************************************************/
FILE * tr_fdFileOpen( tr_fd_t * f, char * path )
{
    int i, winner;
    uint64_t date;

    tr_lockLock( f->lock );

    /* Is it already open? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].status > STATUS_INVALID &&
            !strcmp( path, f->open[i].path ) )
        {
            winner = i;
            goto done;
        }
    }

    /* Can we open one more file? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].status & STATUS_INVALID )
        {
            winner = i;
            goto open;
        }
    }

    for( ;; )
    {
        /* Close the oldest currently unused file */
        date   = tr_date() + 1;
        winner = -1;

        for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
        {
            if( f->open[i].status & STATUS_USED )
            {
                continue;
            }
            if( f->open[i].date < date )
            {
                winner = i;
                date   = f->open[i].date;
            }
        }

        if( winner >= 0 )
        {
            tr_dbg( "Closing %s", f->open[winner].path );
            fclose( f->open[winner].file );
            goto open;
        }

        /* All used! Wait a bit and try again */
        tr_lockUnlock( f->lock );
        tr_wait( 10 );
        tr_lockLock( f->lock );
    }

open:
    tr_dbg( "Opening %s", path );
    snprintf( f->open[winner].path, MAX_PATH_LENGTH, "%s", path );
    f->open[winner].file = fopen( path, "r+" );

done:
    f->open[winner].status = STATUS_USED;
    f->open[winner].date   = tr_date();
    tr_lockUnlock( f->lock );
    
    return f->open[winner].file;
}

/***********************************************************************
 * tr_fdFileRelease
 **********************************************************************/
void tr_fdFileRelease( tr_fd_t * f, FILE * file )
{
    int i;
    tr_lockLock( f->lock );

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].file == file )
        {
            f->open[i].status = STATUS_UNUSED;
            break;
        }
    }
    
    tr_lockUnlock( f->lock );
}

/***********************************************************************
 * tr_fdFileClose
 **********************************************************************/
void tr_fdFileClose( tr_fd_t * f, char * path )
{
    int i;

    tr_lockLock( f->lock );

    /* Is it already open? */
    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( f->open[i].status & STATUS_INVALID )
        {
            continue;
        }
        if( !strcmp( path, f->open[i].path ) )
        {
            tr_dbg( "Closing %s", path );
            fclose( f->open[i].file );
            f->open[i].status = STATUS_INVALID;
            break;
        }
    }

    tr_lockUnlock( f->lock );
}

int tr_fdSocketWillCreate( tr_fd_t * f, int reserved )
{
    int ret;

    tr_lockLock( f->lock );

    if( reserved )
    {
        if( f->reserved < TR_RESERVED_FDS )
        {
            ret = 0;
            (f->reserved)++;
        }
        else
        {
            ret = 1;
        }
    }
    else
    {
        if( f->normal < f->normalMax )
        {
            ret = 0;
            (f->normal)++;
        }
        else
        {
            ret = 1;
        }
    }

    tr_lockUnlock( f->lock );

    return ret;
}

void tr_fdSocketClosed( tr_fd_t * f, int reserved )
{
    tr_lockLock( f->lock );

    if( reserved )
    {
        (f->reserved)--;
    }
    else
    {
        (f->normal)--;
    }

    tr_lockUnlock( f->lock );
}

void tr_fdClose( tr_fd_t * f )
{
    tr_lockClose( f->lock );
    free( f );
}

