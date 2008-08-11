#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */
#include "transmission.h"
#include "bencode.h"
#include "rpc.h"
#include "utils.h"

#define VERBOSE 0

static int test = 0;

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

extern char* cidrize( const char * in );

extern int tr_rpcTestACL( const void           * unused,
                          const char           * acl,
                          char                ** setme_errmsg );

static int
testWildcard( const char * in, const char * expected )
{
    int ok;
    char * str = cidrize( in );
/* fprintf( stderr, "in [%s] out [%s] expected [%s]\n", in, str, expected ); */
    ok = expected ? !strcmp( expected, str ) : !str;
    tr_free( str );
    return ok;
}

static int
test_acl( void )
{
    int err;
    char * errmsg = NULL;

    check( testWildcard( "192.*.*.*", "192.0.0.0/8" ) );
    check( testWildcard( "192.64.*.*", "192.64.0.0/16" ) );
    check( testWildcard( "192.64.0.*", "192.64.0.0/24" ) );
    check( testWildcard( "192.64.0.1", "192.64.0.1/32" ) );
    check( testWildcard( "+192.*.*.*,-192.64.*.*",
                         "+192.0.0.0/8,-192.64.0.0/16" ) );

    err = tr_rpcTestACL( NULL, "+192.*.*.*", &errmsg );
    check( !err );
    check( !errmsg );
    err = tr_rpcTestACL( NULL, "+192.*.8.*", &errmsg );
    check( err );
    check( errmsg );
    tr_free( errmsg );
    errmsg = NULL;
    err = tr_rpcTestACL( NULL, "+192.*.*.*,-192.168.*.*", &errmsg );
    check( !err );
    check( !errmsg );

    return 0;
}

static int
test_list( void )
{
    int64_t i;
    const char * str;
    tr_benc top;

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

    if(( i = test_acl( )))
        return i;
    if(( i = test_list( )))
        return i;

    return 0;
}
