#include <stdio.h> /* fprintf */
#include <string.h> /* strcmp */
#include "transmission.h"
#include "utils.h"

#define VERBOSE 0

int test = 0;

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
/*fprintf( stderr, "in [%s] out [%s] should be [%s]\n", in, str, expected );*/
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
    check( testWildcard( "+192.*.*.*, -192.64.*.*", "+192.0.0.0/8, -192.64.0.0/16" ) );

    err = tr_rpcTestACL( NULL, "+192.*.*.*", &errmsg );
    check( !err );
    check( !errmsg );
    err = tr_rpcTestACL( NULL, "+192.*.8.*", &errmsg );
    check( err );
    check( errmsg );
    tr_free( errmsg );
    errmsg = NULL;
    err = tr_rpcTestACL( NULL, "+192.*.*.*, -192.168.*.*", &errmsg );
    check( !err );
    check( !errmsg );

    return 0;
}

int
main( void )
{
    int i;

    if(( i = test_acl( )))
        return i;

    return 0;
}
