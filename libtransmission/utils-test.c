#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */
#include "transmission.h"
#include "platform.h"
#include "utils.h"
#include "crypto.h"

#define VERBOSE 0
#define NUM_LOOPS 1
#define SPEED_TEST 0

#if SPEED_TEST
 #undef VERBOSE
 #define VERBOSE 0
 #undef NUM_LOOPS
 #define NUM_LOOPS 200
#endif

static int test = 0;

#define check( A ) \
    { \
        ++test; \
        if( A ){ \
            if( VERBOSE ) \
                fprintf( stderr, "PASS test #%d (%s, %d)\n", test, __FILE__,\
                         __LINE__ );\
        } else { \
            fprintf( stderr, "FAIL test #%d (%s, %d)\n", test, __FILE__,\
                     __LINE__ ); \
            return test; \
        } \
    }

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

int
main( void )
{
    char *in, *out;
    int   len;
    int   i;
    int   l;

    /* base64 */
    in = "YOYO!";
    out = tr_base64_encode( in, -1, &len );
    check( out );
    check( !strcmp( out, "WU9ZTyE=\n" ) );
    check( len == 9 );
    in = tr_base64_decode( out, -1, &len );
    check( in );
    check( !strcmp( in, "YOYO!" ) );
    check( len == 5 );
    tr_free( in );
    tr_free( out );

    if( ( i = test_strstrip( ) ) )
        return i;
    if( ( i = test_buildpath( ) ) )
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

