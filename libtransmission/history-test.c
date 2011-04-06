#include <stdio.h>
#include <string.h> /* memset() */

#include "transmission.h"
#include "history.h"

#undef VERBOSE

static int test = 0;

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

static int
test1( void )
{
    tr_recentHistory h;

    memset( &h, 0, sizeof( tr_recentHistory ) );

    tr_historyAdd( &h, 10000, 1 );
    check( (int)tr_historyGet( &h, 12000, 1000 ) == 0 )
    check( (int)tr_historyGet( &h, 12000, 3000 ) == 1 )
    check( (int)tr_historyGet( &h, 12000, 5000 ) == 1 )
    tr_historyAdd( &h, 20000, 1 );
    check( (int)tr_historyGet( &h, 22000,  1000 ) == 0 )
    check( (int)tr_historyGet( &h, 22000,  3000 ) == 1 )
    check( (int)tr_historyGet( &h, 22000, 15000 ) == 2 )
    check( (int)tr_historyGet( &h, 22000, 20000 ) == 2 )

    return 0;
}

int
main( void )
{
    int i;

    if( ( i = test1( ) ) )
        return i;

    return 0;
}

