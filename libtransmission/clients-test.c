#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "clients.h"

#define VERBOSE 0

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

#define TEST_CLIENT(A,B) \
  tr_clientForId( buf, sizeof( buf ), A ); \
  check( !strcmp( buf, B ) );

int
main( void )
{
    int test = 0;
    char buf[128];

    TEST_CLIENT( "-FC1013-", "FileCroc 1.0.1.3" );
    TEST_CLIENT( "-MR1100-", "Miro 1.1.0.0" );
    TEST_CLIENT( "-TR0006-", "Transmission 0.6" );
    TEST_CLIENT( "-TR0072-", "Transmission 0.72" );
    TEST_CLIENT( "-TR111Z-", "Transmission 1.11+" );

    /* cleanup */
    return 0;
}
