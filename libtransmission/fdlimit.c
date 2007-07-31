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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "transmission.h"
#include "net.h"
#include "platform.h"
#include "utils.h"

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
    tr_lock_t     * lock;
    tr_cond_t     * cond;
    
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
static int  OpenFile( int i, const char * folder, const char * name, int write );
static void CloseFile( int i );


/***********************************************************************
 * tr_fdInit
 **********************************************************************/
void tr_fdInit( void )
{
    int i, j, s[4096];

    if( gFd )
    {
        tr_err( "tr_fdInit was called before!" );
        return;
    }

    gFd = calloc( 1, sizeof( tr_fd_t ) );

    /* Init lock and cond */
    gFd->lock = tr_lockNew( );
    gFd->cond = tr_condNew( );

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
#ifdef BEOS_NETSERVER
	    closesocket( s[j] );
#else
	    close( s[j] );
#endif
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
int tr_fdFileOpen( const char * folder, const char * name, int write )
{
    int i, winner, ret;
    uint64_t date;

    tr_lockLock( gFd->lock );

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
            tr_condWait( gFd->cond, gFd->lock );
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
        tr_condWait( gFd->cond, gFd->lock );
    }

open:
    if( ( ret = OpenFile( winner, folder, name, write ) ) )
    {
        tr_lockUnlock( gFd->lock );
        return ret;
    }
    snprintf( gFd->open[winner].folder, MAX_PATH_LENGTH, "%s", folder );
    snprintf( gFd->open[winner].name, MAX_PATH_LENGTH, "%s", name );
    gFd->open[winner].write = write;

done:
    gFd->open[winner].status = STATUS_USED;
    gFd->open[winner].date   = tr_date();
    tr_lockUnlock( gFd->lock );
    
    return gFd->open[winner].file;
}

/***********************************************************************
 * tr_fdFileRelease
 **********************************************************************/
void tr_fdFileRelease( int file )
{
    int i;
    tr_lockLock( gFd->lock );

    for( i = 0; i < TR_MAX_OPEN_FILES; i++ )
    {
        if( gFd->open[i].file == file )
        {
            gFd->open[i].status = STATUS_UNUSED;
            break;
        }
    }
    
    tr_condSignal( gFd->cond );
    tr_lockUnlock( gFd->lock );
}

/***********************************************************************
 * tr_fdFileClose
 **********************************************************************/
void tr_fdFileClose( const char * folder, const char * name )
{
    int i;

    tr_lockLock( gFd->lock );

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

    tr_lockUnlock( gFd->lock );
}


/***********************************************************************
 * Sockets
 **********************************************************************/
typedef struct
{
    int socket;
    int priority;
}
tr_socket_t;

/* Remember the priority of every socket we open, so that we can keep
 * track of how many reserved file descriptors we are using */
static tr_socket_t * gSockets = NULL;
static int gSocketsSize = 0;
static int gSocketsCount = 0;
static void SocketSetPriority( int s, int priority )
{
    if( gSocketsSize < 1 )
    {
        gSocketsSize = 256;
        gSockets = malloc( gSocketsSize * sizeof( tr_socket_t ) );
    }
    if( gSocketsSize <= gSocketsCount )
    {
        gSocketsSize *= 2;
        gSockets = realloc( gSockets, gSocketsSize * sizeof( tr_socket_t ) );
    }
    gSockets[gSocketsCount].socket = s;
    gSockets[gSocketsCount].priority = priority;
    gSocketsCount++;
}
static int SocketGetPriority( int s )
{
    int i, ret;
    for( i = 0; i < gSocketsCount; i++ )
        if( gSockets[i].socket == s )
            break;
    if( i >= gSocketsCount )
    {
        tr_err( "could not find that socket (%d)!", s );
        return -1;
    }
    ret = gSockets[i].priority;
    gSocketsCount--;
    memmove( &gSockets[i], &gSockets[i+1],
            ( gSocketsCount - i ) * sizeof( tr_socket_t ) );
    return ret;
}

/***********************************************************************
 * tr_fdSocketCreate
 **********************************************************************/
int tr_fdSocketCreate( int type, int priority )
{
    int s = -1;

    tr_lockLock( gFd->lock );

    if( priority && gFd->reserved >= TR_RESERVED_FDS )
        priority = FALSE;

    if( priority || ( gFd->normal < gFd->normalMax ) )
       if( ( s = socket( AF_INET, type, 0 ) ) < 0 )
           tr_err( "Could not create socket (%s)", strerror( errno ) );

    if( s > -1 )
    {
        SocketSetPriority( s, priority );
        if( priority )
            gFd->reserved++;
        else
            gFd->normal++;
    }
    tr_lockUnlock( gFd->lock );

    return s;
}

int tr_fdSocketAccept( int b, struct in_addr * addr, tr_port_t * port )
{
    int s = -1;
    unsigned len;
    struct sockaddr_in sock;

    tr_lockLock( gFd->lock );
    if( gFd->normal < gFd->normalMax )
    {
        len = sizeof( sock );
        s = accept( b, (struct sockaddr *) &sock, &len );
    }
    if( s > -1 )
    {
        SocketSetPriority( s, 0 );
        if( NULL != addr )
        {
            *addr = sock.sin_addr;
        }
        if( NULL != port )
        {
            *port = sock.sin_port;
        }
        gFd->normal++;
    }
    tr_lockUnlock( gFd->lock );

    return s;
}

/***********************************************************************
 * tr_fdSocketClose
 **********************************************************************/
void tr_fdSocketClose( int s )
{
    tr_lockLock( gFd->lock );
#ifdef BEOS_NETSERVER
    closesocket( s );
#else
    close( s );
#endif
    if( SocketGetPriority( s ) )
        gFd->reserved--;
    else
        gFd->normal--;
    tr_lockUnlock( gFd->lock );
}

/***********************************************************************
 * tr_fdClose
 **********************************************************************/
void tr_fdClose( void )
{
    tr_lockFree( gFd->lock );
    tr_condFree( gFd->cond );
    free( gFd );
}


/***********************************************************************
 * Local functions
 **********************************************************************/

/***********************************************************************
 * CheckFolder
 ***********************************************************************
 *
 **********************************************************************/
static int OpenFile( int i, const char * folder, const char * name, int write )
{
    tr_openFile_t * file = &gFd->open[i];
    struct stat sb;
    char path[MAX_PATH_LENGTH];
    int ret;

    tr_dbg( "Opening %s in %s (%d)", name, folder, write );

    /* Make sure the parent folder exists */
    if( stat( folder, &sb ) || !S_ISDIR( sb.st_mode ) )
    {
        return TR_ERROR_IO_PARENT;
    }

    snprintf( path, sizeof(path), "%s" TR_PATH_DELIMITER_STR "%s",
              folder,
              name );

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
                    ret = tr_ioErrorFromErrno();
                    tr_err( "Could not create folder '%s'", path );
                    return ret;
                }
            }
            else
            {
                if( !S_ISDIR( sb.st_mode ) )
                {
                    tr_err( "Is not a folder: '%s'", path );
                    return TR_ERROR_IO_OTHER;
                }
            }
            *s = '/';
            p = s + 1;
        }
    }

    /* Now try to really open the file */
    errno = 0;
    file->file = open( path, write ? ( O_RDWR | O_CREAT ) : O_RDONLY, 0666 );
    if( write && ( file->file < 0 ) )
    {
        ret = tr_ioErrorFromErrno();
        if( errno )
            tr_err( "Couldn't open %s in %s: %s", name, folder, strerror(errno) );
        else
            tr_err( "Couldn't open %s in %s", name, folder );
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
        tr_condWait( gFd->cond, gFd->lock );
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
    tr_lockUnlock( gFd->lock );
    close( file->file );
    tr_lockLock( gFd->lock );
    file->status = STATUS_INVALID;
    tr_condSignal( gFd->cond );
}

