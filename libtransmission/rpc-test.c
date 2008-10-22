#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */
#include "transmission.h"
#include "bencode.h"
#include "rpcimpl.h"
#include "utils.h"

#define VERBOSE 0

static int test = 0;

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

static int
test_list( void )
{
    int64_t      i;
    const char * str;
    tr_benc      top;

    tr_rpc_parse_list_str( &top, "12", -1 );
    check( tr_bencIsInt( &top ) );
    check( tr_bencGetInt( &top, &i ) );
    check( i == 12 );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "12", 1 );
    check( tr_bencIsInt( &top ) );
    check( tr_bencGetInt( &top, &i ) );
    check( i == 1 );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "6,7", -1 );
    check( tr_bencIsList( &top ) );
    check( tr_bencListSize( &top ) == 2 );
    check( tr_bencGetInt( tr_bencListChild( &top, 0 ), &i ) );
    check( i == 6 );
    check( tr_bencGetInt( tr_bencListChild( &top, 1 ), &i ) );
    check( i == 7 );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "asdf", -1 );
    check( tr_bencIsString( &top ) );
    check( tr_bencGetStr( &top, &str ) );
    check( !strcmp( str, "asdf" ) );
    tr_bencFree( &top );

    tr_rpc_parse_list_str( &top, "1,3-5", -1 );
    check( tr_bencIsList( &top ) );
    check( tr_bencListSize( &top ) == 4 );
    check( tr_bencGetInt( tr_bencListChild( &top, 0 ), &i ) );
    check( i == 1 );
    check( tr_bencGetInt( tr_bencListChild( &top, 1 ), &i ) );
    check( i == 3 );
    check( tr_bencGetInt( tr_bencListChild( &top, 2 ), &i ) );
    check( i == 4 );
    check( tr_bencGetInt( tr_bencListChild( &top, 3 ), &i ) );
    check( i == 5 );
    tr_bencFree( &top );

    return 0;
}

int
main( void )
{
    int i;

    if( ( i = test_list( ) ) )
        return i;

    return 0;
}

