#include <stdio.h>
#include "transmission.h"
#include "request-list.h"

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
            fprintf( stderr, "FAIL test #%d (%s, %d)\n", test, __FILE__,\
                     __LINE__ ); \
            return test; \
        } \
    }

static int
testFoo( void )
{
    tr_bool success;
    struct request_list list = REQUEST_LIST_INIT;
    struct peer_request a, b, c, tmp;

    a.index = a.offset = a.length = 10;
    b.index = b.offset = b.length = 20;
    c.index = c.offset = c.length = 30;

    check( list.len == 0 );

    reqListAppend( &list, &a );
    reqListAppend( &list, &b );
    reqListAppend( &list, &c );

    check( list.len == 3 );
    check( list.fifo[0].index == 10 );
    check( list.fifo[1].index == 20 );
    check( list.fifo[2].index == 30 );
    check( reqListHas( &list, &a ) );
    check( reqListHas( &list, &b ) );
    check( reqListHas( &list, &c ) );

    success = reqListRemove( &list, &b );
    check( success );
    check( list.len == 2 );
    check( list.fifo[0].index == 10 );
    check( list.fifo[1].index == 30 );
    check( reqListHas( &list, &a ) );
    check( !reqListHas( &list, &b ) );
    check( reqListHas( &list, &c ) );

    success = reqListPop( &list, &tmp );
    check( success );
    check( list.len == 1 );
    check( tmp.index == 10 );
    check( list.fifo[0].index == 30 );
    check( !reqListHas( &list, &a ) );
    check( !reqListHas( &list, &b ) );
    check( reqListHas( &list, &c ) );

    success = reqListPop( &list, &tmp );
    check( success );
    check( list.len == 0 );
    check( tmp.index == 30 );
    check( !reqListHas( &list, &a ) );
    check( !reqListHas( &list, &b ) );
    check( !reqListHas( &list, &c ) );

    success = reqListPop( &list, &tmp );
    check( !success );

    reqListAppend( &list, &a );
    reqListAppend( &list, &b );
    reqListAppend( &list, &c );

    /* remove from middle, front, end */

    success = reqListRemove( &list, &b );
    check( success );
    check( list.len == 2 );
    check( reqListHas( &list, &a ) );
    check( !reqListHas( &list, &b ) );
    check( reqListHas( &list, &c ) );

    success = reqListRemove( &list, &c );
    check( success );
    check( list.len == 1 );
    check( reqListHas( &list, &a ) );
    check( !reqListHas( &list, &b ) );
    check( !reqListHas( &list, &c ) );

    success = reqListRemove( &list, &c );
    check( !success );
    check( list.len == 1 );

    success = reqListRemove( &list, &a );
    check( success );
    check( list.len == 0 );
    check( !reqListHas( &list, &a ) );
    check( !reqListHas( &list, &b ) );
    check( !reqListHas( &list, &c ) );

    return 0;
}

int
main( void )
{
    int i;

    if(( i = testFoo( )))
        return i;

    return 0;
}

