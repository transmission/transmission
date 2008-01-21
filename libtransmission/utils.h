/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#ifndef TR_UTILS_H
#define TR_UTILS_H 1

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h> /* for size_t */
#include <stdio.h> /* FILE* */

void tr_msgInit( void );

#define tr_err( a... ) tr_msg( __FILE__, __LINE__, TR_MSG_ERR, ## a )
#define tr_inf( a... ) tr_msg( __FILE__, __LINE__, TR_MSG_INF, ## a )
#define tr_dbg( a... ) tr_msg( __FILE__, __LINE__, TR_MSG_DBG, ## a )
void tr_msg  ( const char * file, int line, int level, const char * msg, ... );
FILE* tr_getLog( void );

char* tr_getLogTimeStr( char * buf, int buflen );

/** Returns a random number in the range of [0...n) */
int tr_rand ( int n );

/**
 * a portability wrapper around mkdir().
 * On WIN32, the `permissions' argument is unused.
 *
 * @return zero on success, or -1 if an error occurred
 * (in which case errno is set appropriately).
 */
int tr_mkdir( const char * path, int permissions );

/**
 * Like mkdir, but makes parent directories as needed.
 *
 * @return zero on success, or -1 if an error occurred
 * (in which case errno is set appropriately).
 */
int tr_mkdirp( const char * path, int permissions );


uint8_t* tr_loadFile( const char * filename, size_t * size );

int tr_vasprintf( char **strp, const char *fmt, va_list ap );

int tr_asprintf( char **strp, const char *fmt, ...);


/* creates a filename from a series of elements using the
   correct separator for filenames. */
void tr_buildPath( char* buf, size_t buflen,
                   const char * first_element, ... );

struct timeval timevalMsec( uint64_t milliseconds );


int tr_ioErrorFromErrno( void );

const char * tr_errorString( int code );

/* return the current date in milliseconds */
uint64_t tr_date( void );

/* wait the specified number of milliseconds */
void tr_wait( uint64_t delay_milliseconds );

/***
****
***/

/* Sometimes the system defines MAX/MIN, sometimes not. In the latter
   case, define those here since we will use them */
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)>(b)?(b):(a))
#endif

/***
****
***/

#define tr_new(struct_type, n_structs)           \
    ((struct_type *) tr_malloc (((size_t) sizeof (struct_type)) * ((size_t) (n_structs))))
#define tr_new0(struct_type, n_structs)          \
    ((struct_type *) tr_malloc0 (((size_t) sizeof (struct_type)) * ((size_t) (n_structs))))
#define tr_renew(struct_type, mem, n_structs)    \
    ((struct_type *) realloc ((mem), ((size_t) sizeof (struct_type)) * ((size_t) (n_structs))))

void* tr_malloc  ( size_t );
void* tr_malloc0 ( size_t );
void* tr_calloc  ( size_t nmemb, size_t size );
void  tr_free    ( void* );

char* tr_strdup( const char * str );
char* tr_strndup( const char * str, int len );

/***
****
***/

typedef void (tr_set_func)(void * element, void * userData );

void tr_set_compare( const void * a, size_t aCount,
                     const void * b, size_t bCount,
                     int compare( const void * a, const void * b ),
                     size_t elementSize,
                     tr_set_func in_a_cb,
                     tr_set_func in_b_cb,
                     tr_set_func in_both_cb,
                     void * userData );
                    
int tr_compareUint16( uint16_t a, uint16_t b );
int tr_compareUint32( uint32_t a, uint32_t b );

/***
****
***/

struct tr_bitfield
{
    uint8_t * bits;
    size_t len;
};

typedef struct tr_bitfield tr_bitfield;
typedef struct tr_bitfield tr_bitfield_t;

tr_bitfield* tr_bitfieldNew( size_t bitcount );
tr_bitfield* tr_bitfieldDup( const tr_bitfield* );
void tr_bitfieldFree( tr_bitfield*);

void tr_bitfieldClear( tr_bitfield* );
void tr_bitfieldAdd( tr_bitfield*, size_t bit );
void tr_bitfieldRem( tr_bitfield*, size_t bit );
void tr_bitfieldAddRange( tr_bitfield *, size_t begin, size_t end );
void tr_bitfieldRemRange ( tr_bitfield*, size_t begin, size_t end );

int    tr_bitfieldHas( const tr_bitfield*, size_t bit );
int    tr_bitfieldIsEmpty( const tr_bitfield* );
size_t tr_bitfieldCountTrueBits( const tr_bitfield* );

tr_bitfield* tr_bitfieldOr( tr_bitfield*, const tr_bitfield* );

double tr_getRatio( double numerator, double denominator );

#endif
