#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */

#include "transmission.h"
#include "bitfield.h"
#include "ConvertUTF.h" /* tr_utf8_validate*/
#include "platform.h"
#include "crypto.h"
#include "utils.h"
#include "web.h"

/* #define VERBOSE */
#undef VERBOSE
#define NUM_LOOPS 1
#define SPEED_TEST 0

#if SPEED_TEST
 #define VERBOSE
 #undef NUM_LOOPS
 #define NUM_LOOPS 200
#endif

static int test = 0;

#ifdef VERBOSE
  #define check( A ) \
    { \
        ++test; \
        if( A ){ \
            fprintf( stderr, "PASS test #%d (%s, %d)\n", test, __FILE__, __LINE__ ); \
        } else { \
            fprintf( stderr, "FAIL test #%d (%s, %d)\n", test, __FILE__, __LINE__ ); \
            return test; \
        } \
    }
#else
  #define check( A ) \
    { \
        ++test; \
        if( !( A ) ){ \
            fprintf( stderr, "FAIL test #%d (%s, %d)\n", test, __FILE__, __LINE__ ); \
            return test; \
        } \
    }
#endif

static int
test_bitfields( void )
{
    unsigned int  i;
    unsigned int  bitcount = 5000000;
    tr_bitfield * field = tr_bitfieldNew( bitcount );

    /* test tr_bitfieldAdd */
    for( i = 0; i < bitcount; ++i )
        if( !( i % 7 ) )
            tr_bitfieldAdd( field, i );
    for( i = 0; i < bitcount; ++i )
        check( tr_bitfieldHas( field, i ) == ( !( i % 7 ) ) );

    /* test tr_bitfieldAddRange */
    tr_bitfieldAddRange( field, 0, bitcount );
    for( i = 0; i < bitcount; ++i )
        check( tr_bitfieldHas( field, i ) );

    /* test tr_bitfieldRemRange in the middle of a boundary */
    tr_bitfieldRemRange( field, 4, 21 );
    for( i = 0; i < 64; ++i )
        check( tr_bitfieldHas( field, i ) == ( ( i < 4 ) || ( i >= 21 ) ) );

    /* test tr_bitfieldRemRange on the boundaries */
    tr_bitfieldAddRange( field, 0, 64 );
    tr_bitfieldRemRange( field, 8, 24 );
    for( i = 0; i < 64; ++i )
        check( tr_bitfieldHas( field, i ) == ( ( i < 8 ) || ( i >= 24 ) ) );

    /* test tr_bitfieldRemRange when begin & end is on the same word */
    tr_bitfieldAddRange( field, 0, 64 );
    tr_bitfieldRemRange( field, 4, 5 );
    for( i = 0; i < 64; ++i )
        check( tr_bitfieldHas( field, i ) == ( ( i < 4 ) || ( i >= 5 ) ) );

    /* test tr_bitfieldAddRange */
    tr_bitfieldRemRange( field, 0, 64 );
    tr_bitfieldAddRange( field, 4, 21 );
    for( i = 0; i < 64; ++i )
        check( tr_bitfieldHas( field, i ) == ( ( 4 <= i ) && ( i < 21 ) ) );

    /* test tr_bitfieldAddRange on the boundaries */
    tr_bitfieldRemRange( field, 0, 64 );
    tr_bitfieldAddRange( field, 8, 24 );
    for( i = 0; i < 64; ++i )
        check( tr_bitfieldHas( field, i ) == ( ( 8 <= i ) && ( i < 24 ) ) );

    /* test tr_bitfieldAddRange when begin & end is on the same word */
    tr_bitfieldRemRange( field, 0, 64 );
    tr_bitfieldAddRange( field, 4, 5 );
    for( i = 0; i < 64; ++i )
        check( tr_bitfieldHas( field, i ) == ( ( 4 <= i ) && ( i < 5 ) ) );

    tr_bitfieldFree( field );
    return 0;
}

static int
test_strstrip( void )
{
    char *in, *out;

    /* strstrip */
    in = tr_strdup( "   test    " );
    out = tr_strstrip( in );
    check( in == out );
    check( !strcmp( in, "test" ) );
    tr_free( in );

    /* strstrip */
    in = tr_strdup( " test test " );
    out = tr_strstrip( in );
    check( in == out );
    check( !strcmp( in, "test test" ) );
    tr_free( in );

    /* strstrip */
    in = tr_strdup( "test" );
    out = tr_strstrip( in );
    check( in == out );
    check( !strcmp( in, "test" ) );
    tr_free( in );

    return 0;
}

static int
test_buildpath( void )
{
    char * out;

    out = tr_buildPath( "foo", "bar", NULL );
    check( !strcmp( out, "foo" TR_PATH_DELIMITER_STR "bar" ) );
    tr_free( out );

    out = tr_buildPath( "", "foo", "bar", NULL );
    check( !strcmp( out, TR_PATH_DELIMITER_STR "foo" TR_PATH_DELIMITER_STR "bar" ) );
    tr_free( out );

    return 0;
}

static int
test_utf8( void )
{
    const char * in;
    char * out;
    tr_bool err;

    in = "hello world";
    out = tr_utf8clean( in, -1, &err );
    check( err == FALSE )
    check( out != NULL )
    check( !strcmp( out, in ) )
    tr_free( out );

    in = "hello world";
    out = tr_utf8clean( in, 5, &err );
    check( err == FALSE )
    check( out != NULL )
    check( !strcmp( out, "hello" ) )
    tr_free( out );

    /* this version is not utf-8 */
    in = "Òðóäíî áûòü Áîãîì";
    out = tr_utf8clean( in, 17, &err );
    check( out != NULL )
    check( err != 0 )
    check( strlen( out ) == 17 )
    check( tr_utf8_validate( out, -1, NULL ) )
    tr_free( out );

    /* same string, but utf-8 clean */
    in = "Ã’Ã°Ã³Ã¤Ã­Ã® Ã¡Ã»Ã²Ã¼ ÃÃ®Ã£Ã®Ã¬";
    out = tr_utf8clean( in, -1, &err );
    check( out != NULL )
    check( !err );
    check( tr_utf8_validate( out, -1, NULL ) )
    check ( !strcmp( in, out ) )
    tr_free( out );

    return 0;
}

static int
test_numbers( void )
{
    int i;
    int count;
    int * numbers;

    numbers = tr_parseNumberRange( "1-10,13,16-19", -1, &count );
    check( count == 15 );
    check( numbers != NULL );
    check( numbers[0] == 1 );
    check( numbers[5] == 6 );
    check( numbers[9] == 10 );
    check( numbers[10] == 13 );
    check( numbers[11] == 16 );
    check( numbers[14] == 19 );
    tr_free( numbers );

    numbers = tr_parseNumberRange( "1-5,3-7,2-6", -1, &count );
    check( count == 7 );
    check( numbers != NULL );
    for( i=0; i<count; ++i )
        check( numbers[i] == i+1 );
    tr_free( numbers );

    numbers = tr_parseNumberRange( "1-Hello", -1, &count );
    check( count == 0 );
    check( numbers == NULL );

    numbers = tr_parseNumberRange( "1-", -1, &count );
    check( count == 0 );
    check( numbers == NULL );

    numbers = tr_parseNumberRange( "Hello", -1, &count );
    check( count == 0 );
    check( numbers == NULL );

    return 0;
}

static int
compareInts( const void * va, const void * vb )
{
    const int a = *(const int *)va;
    const int b = *(const int*)vb;
    return a - b;
}

static int
test_lowerbound( void )
{
    int i;
    const int A[] = { 1, 2, 3, 3, 3, 5, 8 };
    const int expected_pos[] = { 0, 1, 2, 5, 5, 6, 6, 6, 7, 7 };
    const int expected_exact[] = { TRUE, TRUE, TRUE, FALSE, TRUE, FALSE, FALSE, TRUE, FALSE, FALSE };
    const int N = sizeof(A) / sizeof(A[0]);

    for( i=1; i<=10; ++i )
    {
        tr_bool exact;
        const int pos = tr_lowerBound( &i, A, N, sizeof(int), compareInts, &exact );

#if 0
        fprintf( stderr, "searching for %d.  ", i );
        fprintf( stderr, "result: index = %d, ", pos );
        if( pos != N )
            fprintf( stderr, "A[%d] == %d\n", pos, A[pos] );
        else
            fprintf( stderr, "which is off the end.\n" );
#endif
        check( pos == expected_pos[i-1] )
        check( exact == expected_exact[i-1] )
    }

    return 0;
}

static int
test_memmem( void )
{
    char const haystack[12] = "abcabcabcabc";
    char const needle[3] = "cab";

    check( tr_memmem( haystack, sizeof haystack, haystack, sizeof haystack) == haystack )
    check( tr_memmem( haystack, sizeof haystack, needle, sizeof needle) == haystack + 2 )
    check( tr_memmem( needle, sizeof needle, haystack, sizeof haystack) == NULL )
    check( tr_memmem( haystack, sizeof haystack, "", 0) == haystack )
    check( tr_memmem( haystack, sizeof haystack, NULL, 0) == haystack )
    check( tr_memmem( haystack, 0, "", 0) == haystack )

    return 0;
}

static int
test_hex( void )
{
    char hex1[41];
    char hex2[41];
    uint8_t sha1[20];
    /*uint8_t sha2[20];*/

    memcpy( hex1, "fb5ef5507427b17e04b69cef31fa3379b456735a", 41 );
    tr_hex_to_sha1( sha1, hex1 );
    tr_sha1_to_hex( hex2, sha1 );
    check( !strcmp( hex1, hex2 ) )

    return 0;
}

static int
test_array( void )
{
    int i;
    int array[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    int n = sizeof( array ) / sizeof( array[0] );

    tr_removeElementFromArray( array, 5, sizeof( int ), n-- );
    for( i=0; i<n; ++i )
        check( array[i] == ( i<5 ? i : i+1 ) );

    tr_removeElementFromArray( array, 0, sizeof( int ), n-- );
    for( i=0; i<n; ++i )
        check( array[i] == ( i<4 ? i+1 : i+2 ) );

    tr_removeElementFromArray( array, n-1, sizeof( int ), n ); n--;
    for( i=0; i<n; ++i )
        check( array[i] == ( i<4 ? i+1 : i+2 ) );

    return 0;
}

static int
test_url( void )
{
    int port;
    char * host;
    char * path;
    char * str;
    const char * url;

    url = "http://www.some-tracker.org/some/path";
    check( !tr_httpParseURL( url, -1, &host, &port, &path ) )
    check( !strcmp( host, "www.some-tracker.org" ) )
    check( !strcmp( path, "/some/path" ) )
    check( port == 80 )
    tr_free( path );
    tr_free( host );

    url = "http://www.some-tracker.org:80/some/path";
    check( !tr_httpParseURL( url, -1, &host, &port, &path ) )
    check( !strcmp( host, "www.some-tracker.org" ) )
    check( !strcmp( path, "/some/path" ) )
    check( port == 80 )
    tr_free( path );
    tr_free( host );

    url = "http%3A%2F%2Fwww.example.com%2F~user%2F%3Ftest%3D1%26test1%3D2";
    str = tr_http_unescape( url, strlen( url ) );
    check( !strcmp( str, "http://www.example.com/~user/?test=1&test1=2" ) )
    tr_free( str );

    return 0;
}

int
main( void )
{
    char buf[32];
    char *in, *out;
    int   len;
    int   i;
    int   l;

    /* tr_truncd */
    tr_snprintf( buf, sizeof( buf ), "%.2f%%", 99.999 );
    check( !strcmp( buf, "100.00%" ) );
    tr_snprintf( buf, sizeof( buf ), "%.2f%%", tr_truncd( 99.999, 2 ) );
    check( !strcmp( buf, "99.99%" ) );

    /* base64 */
    out = tr_base64_encode( "YOYO!", -1, &len );
    check( out );
    check( !strcmp( out, "WU9ZTyE=\n" ) );
    check( len == 9 );
    in = tr_base64_decode( out, -1, &len );
    check( in );
    check( !strcmp( in, "YOYO!" ) );
    check( len == 5 );
    tr_free( in );
    tr_free( out );
    out = tr_base64_encode( NULL, 0, &len );
    check( out == NULL );
    check( len == 0 );

    if( ( i = test_hex( ) ) )
        return i;
    if( ( i = test_lowerbound( ) ) )
        return i;
    if( ( i = test_strstrip( ) ) )
        return i;
    if( ( i = test_buildpath( ) ) )
        return i;
    if( ( i = test_utf8( ) ) )
        return i;
    if( ( i = test_numbers( ) ) )
        return i;
    if( ( i = test_memmem( ) ) )
        return i;
    if( ( i = test_array( ) ) )
        return i;
    if( ( i = test_url( ) ) )
        return i;

    /* test that tr_cryptoRandInt() stays in-bounds */
    for( i = 0; i < 100000; ++i )
    {
        const int val = tr_cryptoRandInt( 100 );
        check( val >= 0 );
        check( val < 100 );
    }

    /* simple bitfield tests */
    for( l = 0; l < NUM_LOOPS; ++l )
        if( ( i = test_bitfields( ) ) )
            return i;

    return 0;
}

