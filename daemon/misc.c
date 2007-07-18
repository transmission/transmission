/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Joshua Elsasser
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

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libtransmission/transmission.h>
#include <libtransmission/trcompat.h>

#include "errors.h"
#include "misc.h"

static void pushdir ( char *, const char *, size_t );

static char gl_myname[256];

void
setmyname( const char * argv0 )
{
    const char * name;

    name = strrchr( argv0, '/' );
    if( NULL == name || '\0' == *(++name) )
    {
        name = argv0;
    }
    strlcpy( gl_myname, name, sizeof gl_myname );
}

const char *
getmyname( void )
{
    return gl_myname;
}

void
pushdir( char * path, const char * file, size_t size )
{
    size_t off;

    off = strlen( path );
    if( 0 < off && off + 1 < size && '/' != path[off-1] )
    {
        path[off]   = '/';
        path[off+1] = '\0';
    }
    strlcat( path, file, size );
}

void
confpath( char * buf, size_t len, const char * file, enum confpathtype type )
{
    strlcpy( buf, tr_getPrefsDirectory(), len );

    switch( type )
    {
        case CONF_PATH_TYPE_DAEMON:
            pushdir( buf, "daemon", len );
            break;
        case CONF_PATH_TYPE_GTK:
            pushdir( buf, "gtk", len );
            break;
        case CONF_PATH_TYPE_OSX:
            break;
        default:
            assert( 0 );
            break;
    }

    if( NULL != file )
    {
        pushdir( buf, file, len );
    }
}

void
absolutify( char * buf, size_t len, const char * path )
{
    size_t off;

    if( '/' == path[0] )
    {
        strlcpy( buf, path, len );
        return;
    }

    getcwd( buf, len );
    off = strlen( buf );
    if( 0 < off && len > off + 1 && '/' != buf[off] )
    {
        buf[off] = '/';
        off++;
        buf[off] = '\0';
    }
    strlcat( buf, path, len );
}

int
writefile( const char * name, uint8_t * buf, ssize_t len )
{
    int     fd;
    ssize_t res;

    fd = open( name, O_WRONLY | O_CREAT | O_TRUNC, 0666 );
    if( 0 > fd )
    {
        errnomsg( "failed to open %s for writing", name );
        return -1;
    }

    res = write( fd, buf, len );
    if( 0 > res )
    {
        errnomsg( "failed to write to %s", name );
        return -1;
    }
    if( len > res )
    {
        errmsg( "failed to write all data to %s", name );
        return -1;
    }

    close( fd );

    return 0;
}

uint8_t *
readfile( const char * name, size_t * len )
{
    struct stat sb;
    int         fd;
    uint8_t   * buf;
    ssize_t     res;

    fd = open( name, O_RDONLY );
    if( 0 > fd )
    {
        if( ENOENT != errno )
        {
            errnomsg( "failed to open %s for reading", name );
        }
        return NULL;
    }

    if( 0 > fstat( fd, &sb ) )
    {
        errnomsg( "failed to stat %s", name );
        return NULL;
    }

    buf = malloc( sb.st_size );
    if( NULL == buf )
    {
        mallocmsg( sb.st_size );
        return NULL;
    }

    res = read( fd, buf, sb.st_size );
    if( 0 > res )
    {
        errnomsg( "failed to read from %s", name );
        free( buf );
        return NULL;
    }
    if( res < sb.st_size )
    {
        errmsg( "failed to read all data from %s", name );
        free( buf );
        return NULL;
    }

    close( fd );
    *len = res;

    return buf;
}
