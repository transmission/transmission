/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* usleep, stat */

#ifdef WIN32
    #include <windows.h> /* for Sleep */
#elif defined(__BEOS__)
    #include <kernel/OS.h>
    extern int vasprintf( char **, const char *, va_list );
#endif

#include "transmission.h"
#include "trcompat.h"
#include "utils.h"
#include "platform.h"

#define SPRINTF_BUFSIZE         100

static tr_lock      * messageLock = NULL;
static int            messageLevel = 0;
static int            messageQueuing = FALSE;
static tr_msg_list *  messageQueue = NULL;
static tr_msg_list ** messageQueueTail = &messageQueue;

void tr_msgInit( void )
{
    if( !messageLock )
         messageLock = tr_lockNew( );
}

FILE*
tr_getLog( void )
{
    static int initialized = FALSE;
    static FILE * file= NULL;

    if( !initialized )
    {
        const char * str = getenv( "TR_DEBUG_FD" );
        int fd;
        if( str && *str )
            fd = atoi( str );
        switch( fd ) {
            case 1: file = stdout; break;
            case 2: file = stderr; break;
            default: file = NULL; break;
        }
        initialized = TRUE;
    }

    return file;
}

char*
tr_getLogTimeStr( char * buf, int buflen )
{
    char tmp[64];
    time_t now;
    struct tm now_tm;
    struct timeval tv;
    int milliseconds;

    now = time( NULL );
    gettimeofday( &tv, NULL );

    localtime_r( &now, &now_tm );
    strftime( tmp, sizeof(tmp), "%H:%M:%S", &now_tm );
    milliseconds = (int)(tv.tv_usec / 1000);
    snprintf( buf, buflen, "%s.%03d", tmp, milliseconds );

    return buf;
}

void
tr_setMessageLevel( int level )
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

tr_msg_list * tr_getQueuedMessages( void )
{
    tr_msg_list * ret;

    assert( NULL != messageLock );
    tr_lockLock( messageLock );
    ret = messageQueue;
    messageQueue = NULL;
    messageQueueTail = &messageQueue;
    tr_lockUnlock( messageLock );

    return ret;
}

void tr_freeMessageList( tr_msg_list * list )
{
    tr_msg_list * next;

    while( NULL != list )
    {
        next = list->next;
        free( list->message );
        free( list );
        list = next;
    }
}

void tr_msg( int level, const char * msg, ... )
{
    va_list       args1, args2;
    tr_msg_list * newmsg;
    int           len1, len2;
    FILE        * fp;

    assert( NULL != messageLock );
    tr_lockLock( messageLock );

    fp = tr_getLog( );

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
                if( fp != NULL )
                    fprintf( fp, "%s\n", newmsg->message );
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
            if( fp == NULL )
                fp = stderr;
            vfprintf( fp, msg, args1 );
            fputc( '\n', fp );
            fflush( fp );
        }
        va_end( args1 );
    }

    tr_lockUnlock( messageLock );
}

int tr_rand( int sup )
{
    static int init = 0;

    assert( sup > 0 );

    if( !init )
    {
        srand( tr_date() );
        init = 1;
    }
    return rand() % sup;
}

/***
****
***/

void
tr_set_compare( const void * va, size_t aCount,
                const void * vb, size_t bCount,
                int compare( const void * a, const void * b ),
                size_t elementSize,
                tr_set_func in_a_cb,
                tr_set_func in_b_cb,
                tr_set_func in_both_cb,
                void * userData )
{
    const uint8_t * a = (const uint8_t *) va;
    const uint8_t * b = (const uint8_t *) vb;
    const uint8_t * aend = a + elementSize*aCount;
    const uint8_t * bend = b + elementSize*bCount;

    while( a!=aend || b!=bend )
    {
        if( a==aend )
        {
            (*in_b_cb)( (void*)b, userData );
            b += elementSize;
        }
        else if ( b==bend )
        {
            (*in_a_cb)( (void*)a, userData );
            a += elementSize;
        }
        else
        {
            const int val = (*compare)( a, b );

            if( !val )
            {
                (*in_both_cb)( (void*)a, userData );
                a += elementSize;
                b += elementSize;
            }
            else if( val < 0 )
            {
                (*in_a_cb)( (void*)a, userData );
                a += elementSize;
            }
            else if( val > 0 )
            {
                (*in_b_cb)( (void*)b, userData );
                b += elementSize;
            }
        }
    }
}

/***
****
***/

void * tr_memmem ( const void *vbig, size_t big_len,
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

/**
***
**/

int
tr_compareUint16( uint16_t a, uint16_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

int
tr_compareUint32( uint32_t a, uint32_t b )
{
    if( a < b ) return -1;
    if( a > b ) return 1;
    return 0;
}

/**
***
**/

struct timeval
timevalMsec( uint64_t milliseconds )
{
    struct timeval ret;
    const uint64_t microseconds = milliseconds * 1000;
    ret.tv_sec  = microseconds / 1000000;
    ret.tv_usec = microseconds % 1000000;
    return ret;
}

uint8_t *
tr_loadFile( const char * path, size_t * size )
{
    uint8_t    * buf;
    struct stat  sb;
    FILE       * file;

    /* try to stat the file */
    errno = 0;
    if( stat( path, &sb ) )
    {
        tr_err( "Couldn't get information for file \"%s\" %s", path, strerror(errno) );
        return NULL;
    }

    if( ( sb.st_mode & S_IFMT ) != S_IFREG )
    {
        tr_err( "Not a regular file (%s)", path );
        return NULL;
    }

    /* Load the torrent file into our buffer */
    file = fopen( path, "rb" );
    if( !file )
    {
        tr_err( "Couldn't open file \"%s\" %s", path, strerror(errno) );
        return NULL;
    }
    buf = malloc( sb.st_size );
    if( NULL == buf )
    {
        tr_err( "Couldn't allocate memory (%"PRIu64" bytes)",
                ( uint64_t )sb.st_size );
        fclose( file );
    }
    fseek( file, 0, SEEK_SET );
    if( fread( buf, sb.st_size, 1, file ) != 1 )
    {
        tr_err( "Error reading \"%s\" %s", path, strerror(errno) );
        free( buf );
        fclose( file );
        return NULL;
    }
    fclose( file );

    *size = sb.st_size;

    return buf;
}

int
tr_mkdir( const char * path, int permissions 
#ifdef WIN32
                                             UNUSED
#endif
                                                    )
{
#ifdef WIN32
    return mkdir( path );
#else
    return mkdir( path, permissions );
#endif
}

int
tr_mkdirp( const char * path_in, int permissions )
{
    char * path = tr_strdup( path_in );
    char * p, * pp;
    struct stat sb;
    int done;

    /* walk past the root */
    p = path;
    while( *p == TR_PATH_DELIMITER )
        ++p;

    pp = p;
    done = 0;
    while( ( p = strchr( pp, TR_PATH_DELIMITER ) ) || ( p = strchr( pp, '\0' ) ) )
    {
        if( !*p )
            done = 1;
        else
            *p = '\0';

        if( stat( path, &sb ) )
        {
            /* Folder doesn't exist yet */
            if( tr_mkdir( path, permissions ) )
            {
                tr_err( "Could not create directory %s (%s)", path,
                        strerror( errno ) );
                tr_free( path );
                return 1;
            }
        }
        else if( ( sb.st_mode & S_IFMT ) != S_IFDIR )
        {
            /* Node exists but isn't a folder */
            tr_err( "Remove %s, it's in the way.", path );
            tr_free( path );
            return 1;
        }

        if( done )
            break;

        *p = TR_PATH_DELIMITER;
        p++;
        pp = p;
    }

    tr_free( path );
    return 0;
}

int
tr_strncasecmp( const char * s1, const char * s2, size_t n )
{
    if ( !n )
        return 0;

    while( n-- != 0 && tolower( *s1 ) == tolower( *s2 ) ) {
        if( !n || !*s1 || !*s2 )
	    break;
        ++s1;
        ++s2;
    }

    return tolower(*(unsigned char *) s1) - tolower(*(unsigned char *) s2);
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

    want = vsnprintf( NULL, 0, fmt, ap1 );

    if( tr_concat( buf, used, max, NULL, want ) )
    {
        return 1;
    }
    assert( *used + want + 1 <= *max );

    *used += vsnprintf( *buf + *used, *max - *used, fmt, ap2 );

    return 0;
}

#ifndef HAVE_ASPRINTF

int
asprintf( char ** buf, const char * format, ... )
{
    va_list ap;
    int     ret;

    va_start( ap, format );
    ret = vasprintf( buf, format, ap );
    va_end( ap );

    return ret;
}

int
vasprintf( char ** buf, const char * format, va_list ap )
{
    va_list ap2;
    int     used, max;

    va_copy( ap2, ap );

    *buf = NULL;
    used = 0;
    max  = 0;

    if( tr_vsprintf( buf, &used, &max, format, ap, ap2 ) )
    {
        free( *buf );
        return -1;
    }

    return used;
}

#endif /* HAVE_ASPRINTF */

int tr_concat( char ** buf, int * used, int * max, const char * data, int len )
{
    int     newmax;
    char  * newbuf;

    newmax = *max;
    while( *used + len + 1 > newmax )
    {
        newmax += SPRINTF_BUFSIZE;
    }
    if( newmax > *max )
    {
        newbuf = realloc( *buf, newmax );
        if( NULL == newbuf )
        {
            return 1;
        }
        *buf = newbuf;
        *max = newmax;
    }

    if( NULL != data )
    {
        memcpy( *buf + *used, data, len );
        *used += len;
    }

    return 0;
}

void
tr_buildPath ( char *buf, size_t buflen, const char *first_element, ... )
{
    va_list vl;
    char* walk = buf;
    const char * element = first_element;

    if( first_element == NULL )
        return;

    va_start( vl, first_element );
    for( ;; ) {
        const size_t n = strlen( element );
        memcpy( walk, element, n );
        walk += n;
        element = (const char*) va_arg( vl, const char* );
        if( element == NULL )
            break;
        *walk++ = TR_PATH_DELIMITER;
    }
    *walk = '\0';
    assert( walk-buf <= (int)buflen );
}

int
tr_ioErrorFromErrno( void )
{
    switch( errno )
    {
        case EACCES:
        case EROFS:
            return TR_ERROR_IO_PERMISSIONS;
        case ENOSPC:
            return TR_ERROR_IO_SPACE;
        case EMFILE:
            return TR_ERROR_IO_OPEN_FILES;
        case EFBIG:
            return TR_ERROR_IO_FILE_TOO_BIG;
        default:
            tr_dbg( "generic i/o errno from errno: %s", strerror( errno ) );
            return TR_ERROR_IO_OTHER;
    }
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
        case TR_ERROR_IO_PERMISSIONS:
            return "Insufficient permissions";
        case TR_ERROR_IO_SPACE:
            return "Insufficient free space";
        case TR_ERROR_IO_DUP_DOWNLOAD:
            return "Already active transfer with same name and download folder";
        case TR_ERROR_IO_FILE_TOO_BIG:
            return "File too large";
        case TR_ERROR_IO_OPEN_FILES:
            return "Too many open files";
        case TR_ERROR_IO_OTHER:
            return "Generic I/O error";
    }
    return "Unknown error";
}

/****
*****
****/

char*
tr_strdup( const char * in )
{
    return tr_strndup( in, in ? strlen(in) : 0 );
}

char*
tr_strndup( const char * in, int len )
{
    char * out = NULL;

    if( in != NULL )
    {
        out = tr_malloc( len+1 );
        memcpy( out, in, len );
        out[len] = '\0';
    }
    return out;
}

void*
tr_calloc( size_t nmemb, size_t size )
{
    return nmemb && size ? calloc( nmemb, size ) : NULL;
}

void*
tr_malloc( size_t size )
{
    return size ? malloc( size ) : NULL;
}

void*
tr_malloc0( size_t size )
{
    void * ret = tr_malloc( size );
    memset( ret, 0, size );
    return ret;
}

void
tr_free( void * p )
{
    if( p )
        free( p );
}

/****
*****
****/

/* note that the argument is how many bits are needed, not bytes */
tr_bitfield*
tr_bitfieldNew( size_t bitcount )
{
    tr_bitfield * ret = calloc( 1, sizeof(tr_bitfield) );
    if( NULL == ret )
        return NULL;

    ret->len = (bitcount+7u) / 8u;
    ret->bits = calloc( ret->len, 1 );
    if( NULL == ret->bits ) {
        free( ret );
        return NULL;
    }

    return ret;
}

tr_bitfield*
tr_bitfieldDup( const tr_bitfield * in )
{
    tr_bitfield * ret = calloc( 1, sizeof(tr_bitfield) );
    ret->len = in->len;
    ret->bits = malloc( ret->len );
    memcpy( ret->bits, in->bits, ret->len );
    return ret;
}

void tr_bitfieldFree( tr_bitfield * bitfield )
{
    if( bitfield )
    {
        free( bitfield->bits );
        free( bitfield );
    }
}

void
tr_bitfieldClear( tr_bitfield * bitfield )
{
    memset( bitfield->bits, 0, bitfield->len );
}

int
tr_bitfieldIsEmpty( const tr_bitfield * bitfield )
{
    unsigned int i;

    for( i=0; i<bitfield->len; ++i )
        if( bitfield->bits[i] )
            return 0;

    return 1;
}

int
tr_bitfieldHas( const tr_bitfield * bitfield, size_t nth )
{
    static const uint8_t ands[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
    return bitfield!=NULL && (bitfield->bits[nth>>3u] & ands[nth&7u] );
}

void
tr_bitfieldAdd( tr_bitfield  * bitfield, size_t nth )
{
    static const uint8_t ands[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
    bitfield->bits[nth>>3u] |= ands[nth&7u];
    assert( tr_bitfieldHas( bitfield, nth ) );
}

void
tr_bitfieldAddRange( tr_bitfield  * bitfield,
                     size_t         begin,
                     size_t         end )
{
    /* TODO: there are faster ways to do this */
    unsigned int i;
    for( i=begin; i<end; ++i )
        tr_bitfieldAdd( bitfield, i );
}

void
tr_bitfieldRem( tr_bitfield   * bitfield,
                size_t          nth )
{
    static const uint8_t rems[8] = { 127, 191, 223, 239, 247, 251, 253, 254 };

    if( bitfield != NULL )
        bitfield->bits[nth>>3u] &= rems[nth&7u];

    assert( !tr_bitfieldHas( bitfield, nth ) );
}

void
tr_bitfieldRemRange ( tr_bitfield  * b,
                      size_t         begin,
                      size_t         end )
{
    /* TODO: there are faster ways to do this */
    unsigned int i;
    for( i=begin; i<end; ++i )
        tr_bitfieldRem( b, i );
}

tr_bitfield*
tr_bitfieldOr( tr_bitfield * a, const tr_bitfield * b )
{
    uint8_t *ait;
    const uint8_t *aend, *bit;

    assert( a->len == b->len );

    for( ait=a->bits, bit=b->bits, aend=ait+a->len; ait!=aend; ++ait, ++bit )
        *ait |= *bit;

    return a;
}

size_t
tr_bitfieldCountTrueBits( const tr_bitfield* b )
{
    size_t ret = 0;
    const uint8_t *it, *end;
    static const int trueBitCount[512] = {
        0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,
        4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8,5,6,6,7,6,7,7,8,6,7,7,8,7,8,8,9
    };

    if( !b )
        return 0;

    for( it=b->bits, end=it+b->len; it!=end; ++it )
        ret += trueBitCount[*it];

    return ret;
}

/***
****
***/

uint64_t
tr_date( void )
{
    struct timeval tv;
    gettimeofday( &tv, NULL );
    return (uint64_t) tv.tv_sec * 1000 + ( tv.tv_usec / 1000 );
}

void
tr_wait( uint64_t delay_milliseconds )
{
#ifdef __BEOS__
    snooze( 1000 * delay_milliseconds );
#elif defined(WIN32)
    Sleep( (DWORD)delay_milliseconds );
#else
    usleep( 1000 * delay_milliseconds );
#endif
}

/***
****
***/


#ifndef HAVE_STRLCPY

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0')
				break;
		}
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

#endif /* HAVE_STRLCPY */


#ifndef HAVE_STRLCAT

/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
size_t
strlcat(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0 && *d != '\0')
		d++;
	dlen = d - dst;
	n = siz - dlen;

	if (n == 0)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	return(dlen + (s - src));	/* count does not include NUL */
}

#endif /* HAVE_STRLCAT */
