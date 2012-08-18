#include <stdio.h>
#include <string.h> /* memset() */

#include "transmission.h"
#include "history.h"

#undef VERBOSE
#include "libtransmission-test.h"

static int
test1( void )
{
    tr_recentHistory h;

    memset( &h, 0, sizeof( tr_recentHistory ) );

    tr_historyAdd( &h, 10000, 1 );
    check( (int)tr_historyGet( &h, 12000, 1000 ) == 0 );
    check( (int)tr_historyGet( &h, 12000, 3000 ) == 1 );
    check( (int)tr_historyGet( &h, 12000, 5000 ) == 1 );
    tr_historyAdd( &h, 20000, 1 );
    check( (int)tr_historyGet( &h, 22000,  1000 ) == 0 );
    check( (int)tr_historyGet( &h, 22000,  3000 ) == 1 );
    check( (int)tr_historyGet( &h, 22000, 15000 ) == 2 );
    check( (int)tr_historyGet( &h, 22000, 20000 ) == 2 );

    return 0;
}

MAIN_SINGLE_TEST(test1)
