#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "utils.h"

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

int
main( void )
{
    int i;
    int test = 0;

    for( i = 0; i < 100000; ++i )
    {
        int       j;
        int       val = 0;
        uint8_t * pch = tr_peerIdNew( );

        check( strlen( (char*)pch ) == 20 );
        check( !memcmp( pch, PEERID_PREFIX, 8 ) );

        for( j = 8; j < 20; ++j )
        {
            char tmp[2] = { pch[j], '\0' };
            val += strtoul( tmp, NULL, 36 );
        }

        check( ( val % 36 ) == 0 );

        tr_free( pch );
    }

    return 0;
}

