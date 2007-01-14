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

#define SPRINTF_BUFSIZE         100

static tr_lock_t      * messageLock = NULL;
static int              messageLevel = 0;
static int              messageQueuing = 0;
static tr_msg_list_t *  messageQueue = NULL;
static tr_msg_list_t ** messageQueueTail = &messageQueue;

void tr_msgInit( void )
{
    if( NULL == messageLock )
    {
        messageLock = calloc( 1, sizeof( *messageLock ) );
        tr_lockInit( messageLock );
    }
}

void tr_setMessageLevel( int level )
{
    tr_msgInit();
    tr_lockLock( messageLock );
    messageLevel = MAX( 0, level );
    tr_lockUnlock( messageLock );
}

int tr_getMessageLevel( void )
{
    int ret;

    tr_msgInit();
    tr_lockLock( messageLock );
    ret = messageLevel;
    tr_lockUnlock( messageLock );

    return ret;
}

void tr_setMessageQueuing( int enabled )
{
    tr_msgInit();
    tr_lockLock( messageLock );
    messageQueuing = enabled;
    tr_lockUnlock( messageLock );
}

tr_msg_list_t * tr_getQueuedMessages( void )
{
    tr_msg_list_t * ret;

    assert( NULL != messageLock );
    tr_lockLock( messageLock );
    ret = messageQueue;
    messageQueue = NULL;
    messageQueueTail = &messageQueue;
    tr_lockUnlock( messageLock );

    return ret;
}

void tr_freeMessageList( tr_msg_list_t * list )
{
    tr_msg_list_t * next;

    while( NULL != list )
    {
        next = list->next;
        free( list->message );
        free( list );
        list = next;
    }
}

void tr_msg( int level, char * msg, ... )
{
    va_list         args1, args2;
    tr_msg_list_t * newmsg;
    int             len1, len2;

    assert( NULL != messageLock );
    tr_lockLock( messageLock );

    if( !messageLevel )
    {
        char * env;
        env          = getenv( "TR_DEBUG" );
        messageLevel = ( env ? atoi( env ) : 0 ) + 1;
        messageLevel = MAX( 1, messageLevel );
    }

    if( messageLevel >= level )
    {
        va_start( args1, msg );
        if( messageQueuing )
        {
            newmsg = calloc( 1, sizeof( *newmsg ) );
            if( NULL != newmsg )
            {
                newmsg->level = level;
                newmsg->when = time( NULL );
                len1 = len2 = 0;
                va_start( args2, msg );
                tr_vsprintf( &newmsg->message, &len1, &len2, msg,
                             args1, args2 );
                va_end( args2 );
                if( NULL == newmsg->message )
                {
                    free( newmsg );
                }
                else
                {
                    *messageQueueTail = newmsg;
                    messageQueueTail = &newmsg->next;
                }
            }
        }
        else
        {
            vfprintf( stderr, msg, args1 );
            fputc( '\n', stderr );
        }
        va_end( args1 );
    }

    tr_lockUnlock( messageLock );
}

int tr_rand( int sup )
{
    static int init = 0;
    if( !init )
    {
        srand( tr_date() );
        init = 1;
    }
    return rand() % sup;
}

void * tr_memmem( const void *vbig, size_t big_len,
                  const void *vlittle, size_t little_len )
{
    const char *big = vbig;
    const char *little = vlittle;
    size_t ii, jj;

    if( 0 == big_len || 0 == little_len )
    {
        return NULL;
    }

    for( ii = 0; ii + little_len <= big_len; ii++ )
    {
        for( jj = 0; jj < little_len; jj++ )
        {
            if( big[ii + jj] != little[jj] )
            {
                break;
            }
        }
        if( jj == little_len )
        {
            return (char*)big + ii;
        }
    }

    return NULL;
}

int tr_mkdir( char * path )
{
    char      * p, * pp;
    struct stat sb;
    int done;

    p = path;
    while( '/' == *p )
      p++;
    pp = p;
    done = 0;
    while( ( p = strchr( pp, '/' ) ) || ( p = strchr( pp, '\0' ) ) )
    {
        if( '\0' == *p)
        {
            done = 1;
        }
        else
        {
            *p = '\0';
        }
        if( stat( path, &sb ) )
        {
            /* Folder doesn't exist yet */
            if( mkdir( path, 0777 ) )
            {
                tr_err( "Could not create directory %s (%s)", path,
                        strerror( errno ) );
                *p = '/';
                return 1;
            }
        }
        else if( ( sb.st_mode & S_IFMT ) != S_IFDIR )
        {
            /* Node exists but isn't a folder */
            tr_err( "Remove %s, it's in the way.", path );
            *p = '/';
            return 1;
        }
        if( done )
        {
            break;
        }
        *p = '/';
        p++;
        pp = p;
    }

    return 0;
}

#define UPPER( cc ) \
    ( 'a' <= (cc) && 'z' >= (cc) ? (cc) - ( 'a' - 'A' ) : (cc) )

int tr_strncasecmp( const char * first, const char * second, int len )
{
    int ii;
    char firstchar, secondchar;

    if( 0 > len )
    {
        len = strlen( first );
        ii = strlen( second );
        len = MIN( len, ii );
    }

    for( ii = 0; ii < len; ii++ )
    {
        if( first[ii] != second[ii] )
        {
            firstchar = UPPER( first[ii] );
            secondchar = UPPER( second[ii] );
            if( firstchar > secondchar )
            {
                return 1;
            }
            else if( firstchar < secondchar )
            {
                return -1;
            }
        }
        if( '\0' == first[ii] )
        {
            /* if first[ii] is '\0' then second[ii] is too */
            return 0;
        }
    }

    return 0;
}

int tr_sprintf( char ** buf, int * used, int * max, const char * format, ... )
{
    va_list ap1, ap2;
    int     ret;

    va_start( ap1, format );
    va_start( ap2, format );
    ret = tr_vsprintf( buf, used, max, format, ap1, ap2 );
    va_end( ap2 );
    va_end( ap1 );

    return ret;
}

int tr_vsprintf( char ** buf, int * used, int * max, const char * fmt,
                 va_list ap1, va_list ap2 )
{
    int     want;
    char  * newbuf;

    want = vsnprintf( NULL, 0, fmt, ap1 );

    while( *used + want + 1 > *max )
    {
        *max += SPRINTF_BUFSIZE;
        newbuf = realloc( *buf, *max );
        if( NULL == newbuf )
        {
            return 1;
        }
        *buf = newbuf;
    }

    *used += vsnprintf( *buf + *used, *max - *used, fmt, ap2 );

    return 0;
}

char *
tr_dupstr( const char * base, int len )
{
    char * ret;

    ret = malloc( len + 1 );
    if( NULL != ret )
    {
        memcpy( ret, base, len );
        ret[len] = '\0';
    }

    return ret;
}

char *
tr_errorString( int code )
{
    switch( code )
    {
        case TR_OK:
            return "No error";
        case TR_ERROR:
            return "Generic error";
        case TR_ERROR_ASSERT:
            return "Assert error";
        case TR_ERROR_IO_PARENT:
            return "Download folder does not exist";
        case TR_ERROR_IO_PERMISSIONS:
            return "Insufficient permissions";
        case TR_ERROR_IO_OTHER:
            return "Generic I/O error";
    }
    return "Unknown error";
}

