#include <stdio.h>
#include "transmission.h"
#include "net.h"
#include "peer-mgr.h"
#include "utils.h"

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
    uint32_t i;
    int test = 0;
    tr_bitfield * bitfield;
    uint8_t infohash[SHA_DIGEST_LENGTH];
    struct in_addr addr;
    uint32_t sz;
    uint32_t k;
    int pieces[] = { 1059,431,808,1217,287,376,1188,353,508 };

    for( i=0; i<SHA_DIGEST_LENGTH; ++i )
        infohash[i] = 0xaa;
    tr_netResolve( "80.4.4.200", &addr );
    sz = 1313;

    k = 7;
    bitfield = tr_peerMgrGenerateAllowedSet( k, sz, infohash, &addr );
    check( tr_bitfieldCountTrueBits( bitfield ) == k );
    for( i=0; i<k; ++i )
        check( tr_bitfieldHas( bitfield, pieces[i] ) );
    tr_bitfieldFree( bitfield );

    k = 9;
    bitfield = tr_peerMgrGenerateAllowedSet( k, sz, infohash, &addr );
    check( tr_bitfieldCountTrueBits( bitfield ) == k );
    for( i=0; i<k; ++i )
        check( tr_bitfieldHas( bitfield, pieces[i] ) );
    tr_bitfieldFree( bitfield );


    return 0;
}
