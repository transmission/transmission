#include <stdio.h>
#include "transmission.h"
#include "bencode.h"
#include "json.h"
#include "utils.h" /* tr_free */

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

static int
test1( void )
{
    const char * in =
        "{\n"
        "    \"headers\": {\n"
        "        \"type\": \"request\",\n"
        "        \"tag\": 666\n"
        "    },\n"
        "    \"body\": {\n"
        "        \"name\": \"torrent-info\",\n"
        "        \"arguments\": {\n"
        "            \"ids\": [ 7, 10 ]\n"
        "        }\n"
        "    }\n"
        "}\n";
    tr_benc top, *headers, *body, *args, *ids;
    const char * str;
    int64_t i;
    const uint8_t * end = NULL;
    const int err = tr_jsonParse( in, strlen(in), &top, &end );

    check( !err );
    check( tr_bencIsDict( &top ) );
    check(( headers = tr_bencDictFind( &top, "headers" )));
    check( tr_bencIsDict( headers ) );
    check( tr_bencDictFindStr( headers, "type", &str ) );
    check( !strcmp( str, "request" ) );
    check( tr_bencDictFindInt( headers, "tag", &i ) );
    check( i == 666 );
    check(( body = tr_bencDictFind( &top, "body" )));
    check( tr_bencDictFindStr( body, "name", &str ) );
    check( !strcmp( str, "torrent-info" ) );
    check(( args = tr_bencDictFind( body, "arguments" )));
    check( tr_bencIsDict( args ) );
    check(( ids = tr_bencDictFind( args, "ids" )));
    check( tr_bencIsList( ids ) );
    check( tr_bencListSize( ids ) == 2 );
    check( tr_bencGetInt( tr_bencListChild( ids, 0 ), &i ) );
    check( i == 7 );
    check( tr_bencGetInt( tr_bencListChild( ids, 1 ), &i ) );
    check( i == 10 );
    
    tr_bencFree( &top );
    return 0;
}

int
main( void )
{
    int i;

    if(( i = test1( )))
        return i;

    return 0;
}
