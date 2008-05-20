#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */
#include "transmission.h"
#include "utils.h"

#define VERBOSE 0

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


int
main( void )
{
    char *in, *out;
    int len;

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

    return 0;
}
