#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "clients.h"

#define VERBOSE 0

#define check( A ) \
    { \
        ++test; \
        if( A ){ \
            if( VERBOSE ) \
                fprintf( stderr, "PASS test #%d (%s, %d)\n", test, __FILE__,\
                         __LINE__ );\
        } else { \
            if( VERBOSE ) \
                fprintf( stderr, "FAIL test #%d (%s, %d)\n", test, __FILE__,\
                         __LINE__ );\
            return test; \
        } \
    }

#define TEST_CLIENT( A, B ) \
    tr_clientForId( buf, sizeof( buf ), A ); \
    check( !strcmp( buf, B ) );

int
main( void )
{
    int  test = 0;
    char buf[128];

    TEST_CLIENT( "-FC1013-", "FileCroc 1.0.1.3" );
    TEST_CLIENT( "-MR1100-", "Miro 1.1.0.0" );
    TEST_CLIENT( "-TR0006-", "Transmission 0.6" );
    TEST_CLIENT( "-TR0072-", "Transmission 0.72" );
    TEST_CLIENT( "-TR111Z-", "Transmission 1.11+" );
    TEST_CLIENT( "O1008132", "Osprey 1.0.0" );

    TEST_CLIENT(
        "\x65\x78\x62\x63\x00\x38\x7A\x44\x63\x10\x2D\x6E\x9A\xD6\x72\x3B\x33\x9F\x35\xA9",
        "BitComet 0.56" );
    TEST_CLIENT(
        "\x65\x78\x62\x63\x00\x38\x4C\x4F\x52\x44\x32\x00\x04\x8E\xCE\xD5\x7B\xD7\x10\x28",
        "BitLord 0.56" );

    /* cleanup */
    return 0;
}

