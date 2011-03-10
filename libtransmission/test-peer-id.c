#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "session.h"
#include "utils.h"
#include "version.h"

#undef VERBOSE

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

int
main( void )
{
    int i;
    int test = 0;
    uint8_t peer_id[PEER_ID_LEN+1];

    for( i = 0; i < 100000; ++i )
    {
        int j;
        int val = 0;

        tr_peerIdInit( peer_id );

        check( strlen( (char*)peer_id ) == PEER_ID_LEN );
        check( !memcmp( peer_id, PEERID_PREFIX, 8 ) );

        for( j = 8; j < PEER_ID_LEN; ++j )
        {
            char tmp[2] = { peer_id[j], '\0' };
            val += strtoul( tmp, NULL, 36 );
        }

        check( ( val % 36 ) == 0 );
    }

    return 0;
}

