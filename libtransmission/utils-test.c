#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */
#include "transmission.h"
#include "utils.h"

#define VERBOSE 0
#define NUM_LOOPS 1
#define SPEED_TEST 0

#if SPEED_TEST
#undef VERBOSE
#define VERBOSE 0
#undef NUM_LOOPS
#define NUM_LOOPS 200
#endif

int test = 0;

#define check(A) { \
    ++test; \
    if (A) { \
        if( VERBOSE ) \
            fprintf( stderr, "PASS test #%d (%s, %d)\n", test, __FILE__, __LINE__ ); \
    } else { \
        if( VERBOSE ) \
            fprintf( stderr, "FAIL test #%d (%s, %d)\n", test, __FILE__, __LINE__ ); \
        return test; \
    } \
}

static int
test_bitfields( void )
{ 
    int i;
    int bitcount = 5000000;
    size_t pos;
    tr_bitfield * field = tr_bitfieldNew( bitcount );

    /* make every seventh one true */
    for( i=0; i<bitcount; ++i )
        if( !( i % 7 ) )
            tr_bitfieldAdd( field, i );

    /* check to see if `has' has the right bits */
    for( i=0; i<bitcount; ++i )
        check( tr_bitfieldHas( field, i ) == (!(i%7)) );

    /* testing the "find next" function */
    check( tr_bitfieldFindTrue( field, 0, &pos ) );
    check( pos == 0 );
    check( tr_bitfieldFindTrue( field, 1, &pos ) );
    check( pos == 7 );
    check( tr_bitfieldFindTrue( field, 2, &pos ) );
    check( pos == 7 );
    check( tr_bitfieldFindTrue( field, 7, &pos ) );
    check( pos == 7 );
    check( tr_bitfieldFindTrue( field, 8, &pos ) );
    check( pos == 14 );
    check( tr_bitfieldFindTrue( field, 13, &pos ) );
    check( pos == 14 );
    check( tr_bitfieldFindTrue( field, 14, &pos ) );
    check( pos == 14 );
    check( tr_bitfieldFindTrue( field, 15, &pos ) );
    check( pos == 21 );
    check( tr_bitfieldFindTrue( field, 16, &pos ) );
    check( pos == 21 );


    tr_bitfieldFree( field );
    return 0;
}



int
main( void )
{
    char *in, *out;
    int len;
    int i;
    int l;

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

    /* simple bitfield tests */
    for( l=0; l<NUM_LOOPS; ++l )
        if(( i = test_bitfields( )))
            return i;

    return 0;
}
