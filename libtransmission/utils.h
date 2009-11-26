/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_UTILS_H
#define TR_UTILS_H 1

#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h> /* size_t */
#include <stdio.h> /* FILE* */
#include <string.h> /* memcpy()* */
#include <stdlib.h> /* malloc() */
#include <time.h> /* time_t */

#include "transmission.h"

#ifdef __cplusplus
extern "C" {
#endif

/***
****
***/

/**
 * @addtogroup utils Utilities
 * @{
 */

#ifndef FALSE
 #define FALSE 0
#endif

#ifndef TRUE
 #define TRUE 1
#endif

#ifndef UNUSED
 #ifdef __GNUC__
  #define UNUSED __attribute__ ( ( unused ) )
 #else
  #define UNUSED
 #endif
#endif

#ifndef TR_GNUC_PRINTF
 #ifdef __GNUC__
  #define TR_GNUC_PRINTF( fmt, args ) __attribute__ ( ( format ( printf, fmt, args ) ) )
 #else
  #define TR_GNUC_PRINTF( fmt, args )
 #endif
#endif

#ifndef TR_GNUC_NONNULL
 #ifdef __GNUC__
  #define TR_GNUC_NONNULL( ... ) __attribute__((nonnull (__VA_ARGS__)))
 #else
  #define TR_GNUC_NONNULL( ... )
 #endif
#endif

#ifndef TR_GNUC_NULL_TERMINATED
 #if __GNUC__ > 4 || ( __GNUC__ == 4 && __GNUC_MINOR__ >= 3 )
  #define TR_GNUC_NULL_TERMINATED __attribute__ ( ( __sentinel__ ) )
  #define TR_GNUC_HOT __attribute ( ( hot ) )
 #else
  #define TR_GNUC_NULL_TERMINATED
  #define TR_GNUC_HOT
 #endif
#endif

#if __GNUC__ > 2 || ( __GNUC__ == 2 && __GNUC_MINOR__ >= 96 )
 #define TR_GNUC_PURE __attribute__ ( ( __pure__ ) )
 #define TR_GNUC_MALLOC __attribute__ ( ( __malloc__ ) )
#else
 #define TR_GNUC_PURE
 #define TR_GNUC_MALLOC
#endif


/***
****
***/

#if !defined( _ )
 #if defined( HAVE_LIBINTL_H ) && !defined( SYS_DARWIN )
  #include <libintl.h>
  #define _( a ) gettext ( a )
 #else
  #define _( a ) ( a )
 #endif
#endif

/* #define DISABLE_GETTEXT */
#if defined(TR_EMBEDDED) && !defined(DISABLE_GETTEXT)
 #define DISABLE_GETTEXT
#endif
#ifdef DISABLE_GETTEXT
 const char * tr_strip_positional_args( const char * fmt );
 #undef _
 #define _( a ) tr_strip_positional_args( a )
#endif

/****
*****
****/

void tr_assertImpl( const char * file, int line, const char * test, const char * fmt, ... ) TR_GNUC_PRINTF( 4, 5 );

#ifdef NDEBUG
 #define tr_assert( test, fmt, ... )
#else
 #define tr_assert( test, fmt, ... ) \
    do { if( ! ( test ) ) tr_assertImpl( __FILE__, __LINE__, #test, fmt, __VA_ARGS__ ); } while( 0 )
#endif

int            tr_msgLoggingIsActive( int level );

void           tr_msg( const char * file,
                       int          line,
                       int          level,
                       const char * torrent,
                       const char * fmt,
                       ... ) TR_GNUC_PRINTF( 5, 6 );

#define tr_nerr( n, ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_ERR ) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_ERR, n, __VA_ARGS__ ); \
    } while( 0 )

#define tr_ninf( n, ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_INF) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_INF, n, __VA_ARGS__ ); \
    } while( 0 )

#define tr_ndbg( n, ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_DBG) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_DBG, n, __VA_ARGS__ ); \
    } while( 0 )

#define tr_torerr( tor, ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_ERR ) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_ERR, tor->info.name, __VA_ARGS__ ); \
    } while( 0 )

#define tr_torinf( tor, ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_INF ) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_INF, tor->info.name, __VA_ARGS__ ); \
    } while( 0 )

#define tr_tordbg( tor, ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_DBG ) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_DBG, tor->info.name, __VA_ARGS__ ); \
    } while( 0 )

#define tr_err( ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_ERR ) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_ERR, NULL, __VA_ARGS__ ); \
    } while( 0 )

#define tr_inf( ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_INF ) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_INF, NULL, __VA_ARGS__ ); \
    } while( 0 )

#define tr_dbg( ... ) \
    do { \
        if( tr_msgLoggingIsActive( TR_MSG_DBG ) ) \
            tr_msg( __FILE__, __LINE__, TR_MSG_DBG, NULL, __VA_ARGS__ ); \
    } while( 0 )



FILE*          tr_getLog( void );

tr_bool        tr_deepLoggingIsActive( void );

void           tr_deepLog( const char * file,
                           int          line,
                           const char * name,
                           const char * fmt,
                           ... ) TR_GNUC_PRINTF( 4, 5 ) TR_GNUC_NONNULL(1,4);

char*          tr_getLogTimeStr( char * buf,
                                 int    buflen ) TR_GNUC_NONNULL(1);


int            tr_wildmat( const char * text,
                           const char * pattern ) TR_GNUC_NONNULL(1,2);

/** @brief Portability wrapper for basename() that uses the system implementation if available */
char* tr_basename( const char * path ) TR_GNUC_MALLOC;

/** @brief Portability wrapper for dirname() that uses the system implementation if available */
char* tr_dirname( const char * path ) TR_GNUC_MALLOC;

/**
 * @brief Portability wrapper for mkdir()
 *
 * a portability wrapper around mkdir().
 * On WIN32, the `permissions' argument is unused.
 *
 * @return zero on success, or -1 if an error occurred
 * (in which case errno is set appropriately).
 */
int            tr_mkdir( const char * path,
                         int          permissions ) TR_GNUC_NONNULL(1);

/**
 * Like mkdir, but makes parent directories as needed.
 *
 * @return zero on success, or -1 if an error occurred
 * (in which case errno is set appropriately).
 */
int tr_mkdirp( const char * path, int permissions ) TR_GNUC_NONNULL(1);


/**
 * @brief Loads a file and returns its contents.
 * On failure, NULL is returned and errno is set.
 */
uint8_t* tr_loadFile( const char * filename, size_t * size ) TR_GNUC_MALLOC TR_GNUC_NONNULL(1);


/** @brief build a filename from a series of elements using the
           platform's correct directory separator. */
char* tr_buildPath( const char * first_element, ... ) TR_GNUC_NULL_TERMINATED
                                                      TR_GNUC_MALLOC;

struct timeval;

tr_bool tr_isTimeval( const struct timeval * tv );

void tr_timevalMsec( uint64_t milliseconds, struct timeval * setme );

void tr_timevalSet( struct timeval * setme, int seconds, int microseconds );

struct event;

void tr_timerAdd( struct event * timer, int seconds, int microseconds );


/** @brief return the current date in milliseconds */
uint64_t       tr_date( void );

/** @brief wait the specified number of milliseconds */
void           tr_wait( uint64_t delay_milliseconds );

/**
 * @brief make a copy of 'str' whose non-utf8 content has been corrected or stripped
 * @param str the string to make a clean copy of
 * @param len the length of the string to copy.  If -1, the entire string is used.
 * @param err if an error occurs and err is non-NULL, it's set to TRUE.
 */
char* tr_utf8clean( const char * str, int len, tr_bool * err ) TR_GNUC_MALLOC;


/***
****
***/

/* Sometimes the system defines MAX/MIN, sometimes not.
   In the latter case, define those here since we will use them */
#ifndef MAX
 #define MAX( a, b ) ( ( a ) > ( b ) ? ( a ) : ( b ) )
#endif
#ifndef MIN
 #define MIN( a, b ) ( ( a ) > ( b ) ? ( b ) : ( a ) )
#endif

/***
****
***/

static TR_INLINE void* tr_malloc( size_t size )
{
    return size ? malloc( size ) : NULL;
}
static TR_INLINE void* tr_malloc0( size_t size )
{
    return size ? calloc( 1, size ) : NULL;
}
static TR_INLINE void tr_free( void * p )
{
    if( p != NULL )
        free( p );
}
static TR_INLINE void* tr_memdup( const void * src, int byteCount )
{
    return memcpy( tr_malloc( byteCount ), src, byteCount );
}

#define tr_new( struct_type, n_structs )           \
    ( (struct_type *) tr_malloc ( ( (size_t) sizeof ( struct_type ) ) * ( ( size_t) ( n_structs ) ) ) )

#define tr_new0( struct_type, n_structs )          \
    ( (struct_type *) tr_malloc0 ( ( (size_t) sizeof ( struct_type ) ) * ( ( size_t) ( n_structs ) ) ) )

#define tr_renew( struct_type, mem, n_structs )    \
    ( (struct_type *) realloc ( ( mem ), ( (size_t) sizeof ( struct_type ) ) * ( ( size_t) ( n_structs ) ) ) )

/** @param in is a void* so that callers can pass in both signed & unsigned without a cast */
char* tr_strndup( const void * in, int len ) TR_GNUC_MALLOC;

/** @param in is a void* so that callers can pass in both signed & unsigned without a cast */
static TR_INLINE char* tr_strdup( const void * in )
{
    return tr_strndup( in, in ? strlen( (const char *) in ) : 0 );
}

/* @brief same argument list as bsearch() */
int tr_lowerBound( const void * key,
                   const void * base,
                   size_t       nmemb,
                   size_t       size,
                   int       (* compar)(const void* key, const void* arrayMember),
                   tr_bool    * exact_match ) TR_GNUC_HOT TR_GNUC_NONNULL(1,5,6);


char*       tr_strdup_printf( const char * fmt,
                              ... )  TR_GNUC_PRINTF( 1, 2 ) TR_GNUC_MALLOC;

char*       tr_base64_encode( const void * input,
                              int          inlen,
                              int *        outlen ) TR_GNUC_MALLOC;

char*       tr_base64_decode( const void * input,
                              int          inlen,
                              int *        outlen ) TR_GNUC_MALLOC;

/** @brief Portability wrapper for strlcpy() that uses the system implementation if available */
size_t tr_strlcpy( char * dst, const void * src, size_t siz );

/** @brief Portability wrapper for snprintf() that uses the system implementation if available */
int tr_snprintf( char * buf, size_t buflen,
                 const char * fmt, ... ) TR_GNUC_PRINTF( 3, 4 ) TR_GNUC_NONNULL(1,3);

const char* tr_strerror( int );

/** @brief strips leading and trailing whitspace from a string
    @return the stripped string */
char* tr_strstrip( char * str );

/** @brief Portability wrapper for memmem() that uses the system implementation if available */
const char* tr_memmem( const char * haystack, size_t haystack_len,
                       const char * needle, size_t needle_len );

/***
****
***/

typedef void ( tr_set_func )( void * element, void * userData );

void        tr_set_compare( const void * a, size_t aCount,
                            const void * b, size_t bCount,
                            int compare( const void * a, const void * b ),
                            size_t elementSize,
                            tr_set_func in_a_cb,
                            tr_set_func in_b_cb,
                            tr_set_func in_both_cb,
                            void * userData );

void tr_sha1_to_hex( char * out,
                     const uint8_t * sha1 ) TR_GNUC_NONNULL(1,2);

void tr_hex_to_sha1( uint8_t * out,
                     const char * hex ) TR_GNUC_NONNULL(1,2);


tr_bool tr_httpIsValidURL( const char * url ) TR_GNUC_NONNULL(1);

int  tr_httpParseURL( const char * url,
                      int          url_len,
                      char **      setme_host,
                      int *        setme_port,
                      char **      setme_path ) TR_GNUC_NONNULL(1);

double tr_getRatio( double numerator, double denominator );

int* tr_parseNumberRange( const char * str, int str_len, int * setmeCount ) TR_GNUC_MALLOC TR_GNUC_NONNULL(1);


/* truncate a double value at a given number of decimal places.
   this can be used to prevent a printf() call from rounding up:
   call with the decimal_places argument equal to the number of
   decimal places in the printf()'s precision:

   - printf("%.2f%%",           99.999    ) ==> "100.00%"

   - printf("%.2f%%", tr_truncd(99.999, 2)) ==>  "99.99%"
               ^                        ^
               |   These should match   |
               +------------------------+
*/
double tr_truncd( double x, int decimal_places );

/**
 * @param buf the buffer to write the string to
 * @param buflef buf's size
 * @param ratio the ratio to convert to a string
 * @param the string represntation of "infinity"
 */
char* tr_strratio( char * buf, size_t buflen, double ratio, const char * infinity ) TR_GNUC_NONNULL(1,4);

/** @brief Portability wrapper for localtime_r() that uses the system implementation if available */
struct tm * tr_localtime_r( const time_t *_clock, struct tm *_result );


/** on success, return 0.  on failure, return -1 and set errno */
int tr_moveFile( const char * oldpath, const char * newpath,
                 tr_bool * renamed ) TR_GNUC_NONNULL(1,2);

void tr_removeElementFromArray( void * array, int index_to_remove,
                                size_t sizeof_element, size_t nmemb );

/***
****
***/

extern time_t transmission_now;

static TR_INLINE time_t tr_time( void ) { return transmission_now; }

void tr_timeUpdate( time_t now );

/***
****
***/

#ifdef __cplusplus
}
#endif

/** @} */

#endif
