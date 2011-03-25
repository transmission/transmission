#include <stdio.h>
#include "transmission.h"
#include "peer-msgs.h"
#include "utils.h"

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
#if 0
    uint32_t           i;
    int                test = 0;
    uint8_t            infohash[SHA_DIGEST_LENGTH];
    struct tr_address  addr;
    tr_piece_index_t   pieceCount = 1313;
    size_t             numwant;
    size_t             numgot;
    tr_piece_index_t pieces[] = { 1059, 431, 808, 1217, 287, 376, 1188, 353, 508 };
    tr_piece_index_t buf[16];

    for( i = 0; i < SHA_DIGEST_LENGTH; ++i )
        infohash[i] = 0xaa;
    tr_address_from_string( &addr, "80.4.4.200" );

    numwant = 7;
    numgot = tr_generateAllowedSet( buf, numwant, pieceCount, infohash, &addr );
    check( numgot == numwant );
    for( i=0; i<numgot; ++i )
        check( buf[i] == pieces[i] );

    numwant = 9;
    numgot = tr_generateAllowedSet( buf, numwant, pieceCount, infohash, &addr );
    check( numgot == numwant );
    for( i=0; i<numgot; ++i )
        check( buf[i] == pieces[i] );
#endif

    return 0;
}

