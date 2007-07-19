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

#ifndef TR_DAEMON_MISC_H
#define TR_DAEMON_MISC_H

#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>

#include <libtransmission/bsdqueue.h>

#define CONF_FILE_LOCK          "lock"
#define CONF_FILE_SOCKET        "socket"
#define CONF_FILE_STATE         "state"

enum confpathtype
{
    CONF_PATH_TYPE_DAEMON,
    CONF_PATH_TYPE_GTK,
    CONF_PATH_TYPE_OSX,
};

struct bufferevent;

#ifdef __GNUC__
#  define UNUSED __attribute__((unused))
#else
#  define UNUSED
#endif

#ifdef __GNUC__
#  define PRINTF( fmt, args ) __attribute__((format (printf, fmt, args)))
#else
#  define PRINTF( fmt, args )
#endif

#define ARRAYLEN( ary )         ( sizeof( ary ) / sizeof( (ary)[0] ) )

#ifndef MIN
#define MIN( aa, bb )           ( (aa) < (bb) ? (aa) : (bb) )
#endif
#ifndef MAX
#define MAX( aa, bb )           ( (aa) > (bb) ? (aa) : (bb) )
#endif

#undef NULL
#define NULL                    ( ( void * )0 )

#ifndef AF_LOCAL
#define AF_LOCAL AF_UNIX
#endif

#ifndef PF_LOCAL
#define PF_LOCAL PF_UNIX
#endif

#ifndef SUN_LEN
#define SUN_LEN( sun )                                                       \
  ( sizeof( *(sun) ) - sizeof( (sun)->sun_path ) + strlen( (sun)->sun_path ) )
#endif

#define SAFEFREE( ptr )                                                       \
    do                                                                        \
    {                                                                         \
        int saved = errno;                                                    \
        free( ptr );                                                          \
        errno = saved;                                                        \
    }                                                                         \
    while( 0 )
#define SAFEFREESTRLIST( ptr )                                                \
    do                                                                        \
    {                                                                         \
        int saved = errno;                                                    \
        FREESTRLIST( ptr );                                                   \
        errno = saved;                                                        \
    }                                                                         \
    while( 0 )
#define SAFEBENCFREE( val )                                                   \
    do                                                                        \
    {                                                                         \
        int saved = errno;                                                    \
        tr_bencFree( val );                                                   \
        errno = saved;                                                        \
    }                                                                         \
    while( 0 )

#define INTCMP_FUNC( name, type, id )                                         \
int                                                                           \
name( struct type * _icf_first, struct type * _icf_second )                   \
{                                                                             \
    if( _icf_first->id < _icf_second->id )                                    \
    {                                                                         \
        return -1;                                                            \
    }                                                                         \
    else if( _icf_first->id > _icf_second->id )                               \
    {                                                                         \
        return 1;                                                             \
    }                                                                         \
    else                                                                      \
    {                                                                         \
        return 0;                                                             \
    }                                                                         \
}

struct stritem
{
    char                 * str;
    SLIST_ENTRY( stritem ) next;
};

SLIST_HEAD( strlist, stritem );

#define FREESTRLIST( _fl_head )                                               \
    while( !SLIST_EMPTY( _fl_head ) )                                         \
    {                                                                         \
        struct stritem * _fl_dead = SLIST_FIRST( _fl_head );                  \
        SLIST_REMOVE_HEAD( _fl_head, next );                                  \
        free( _fl_dead->str );                                                \
        free( _fl_dead );                                                     \
    }

void         setmyname ( const char * );
const char * getmyname ( void );
void         confpath  ( char *, size_t, const char *, enum confpathtype );
void         absolutify( char *, size_t, const char * );
int          writefile ( const char *, uint8_t *, ssize_t );
uint8_t *    readfile  ( const char *, size_t * );

#endif /* TR_DAEMON_MISC_H */
