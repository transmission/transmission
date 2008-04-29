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

int
main( void )
{
    int test = 0;
    char buf[128];

    tr_clientForId( buf, sizeof( buf ), "-FC1013-" ); check( !strcmp( buf, "FileCroc 1.0.1.3" ) );
    tr_clientForId( buf, sizeof( buf ), "-MR1100-" ); check( !strcmp( buf, "Miro 1.1.0.0" ) );
    tr_clientForId( buf, sizeof( buf ), "-TR0006-" ); check( !strcmp( buf, "Transmission 0.6" ) );
    tr_clientForId( buf, sizeof( buf ), "-TR0072-" ); check( !strcmp( buf, "Transmission 0.72" ) );
    tr_clientForId( buf, sizeof( buf ), "-TR111Z-" ); check( !strcmp( buf, "Transmission 1.11 (Dev)" ) );

    /* cleanup */
    return 0;
}
