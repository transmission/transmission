#include <stdio.h>
#include <string.h>
#include "transmission.h"
#include "bencode.h"
#include "json.h"
#include "utils.h" /* tr_free */

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

#include "ConvertUTF.h"

static int
test_utf8( void )
{
    const char      * in = "{ \"key\": \"Letöltések\" }";
    tr_benc           top;
    const char      * str;
    char            * json;
    int               err;

    err = tr_jsonParse( NULL, in, strlen( in ), &top, NULL );
    check( !err );
    check( tr_bencIsDict( &top ) );
    check( tr_bencDictFindStr( &top, "key", &str ) );
    check( !strcmp( str, "Letöltések" ) );
    if( !err )
        tr_bencFree( &top );

    in = "{ \"key\": \"\\u005C\" }";
    err = tr_jsonParse( NULL, in, strlen( in ), &top, NULL );
    check( !err );
    check( tr_bencIsDict( &top ) );
    check( tr_bencDictFindStr( &top, "key", &str ) );
    check( !strcmp( str, "\\" ) );
    if( !err )
        tr_bencFree( &top );

    /**
     * 1. Feed it JSON-escaped nonascii to the JSON decoder.
     * 2. Confirm that the result is UTF-8.
     * 3. Feed the same UTF-8 back into the JSON encoder.
     * 4. Confirm that the result is JSON-escaped.
     * 5. Dogfood that result back into the parser.
     * 6. Confirm that the result is UTF-8.
     */
    in = "{ \"key\": \"Let\\u00f6lt\\u00e9sek\" }";
    err = tr_jsonParse( NULL, in, strlen( in ), &top, NULL );
    check( !err );
    check( tr_bencIsDict( &top ) );
    check( tr_bencDictFindStr( &top, "key", &str ) );
    check( !strcmp( str, "Letöltések" ) );
    json = tr_bencToStr( &top, TR_FMT_JSON, NULL );
    if( !err )
        tr_bencFree( &top );
    check( json );
    check( strstr( json, "\\u00f6" ) != NULL );
    check( strstr( json, "\\u00e9" ) != NULL );
    err = tr_jsonParse( NULL, json, strlen( json ), &top, NULL );
    check( !err );
    check( tr_bencIsDict( &top ) );
    check( tr_bencDictFindStr( &top, "key", &str ) );
    check( !strcmp( str, "Letöltések" ) );
    if( !err )
        tr_bencFree( &top );
    tr_free( json );

    return 0;
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
    tr_benc      top, *headers, *body, *args, *ids;
    const char * str;
    int64_t      i;
    const int    err = tr_jsonParse( NULL, in, strlen( in ), &top, NULL );

    check( !err );
    check( tr_bencIsDict( &top ) );
    check( ( headers = tr_bencDictFind( &top, "headers" ) ) );
    check( tr_bencIsDict( headers ) );
    check( tr_bencDictFindStr( headers, "type", &str ) );
    check( !strcmp( str, "request" ) );
    check( tr_bencDictFindInt( headers, "tag", &i ) );
    check( i == 666 );
    check( ( body = tr_bencDictFind( &top, "body" ) ) );
    check( tr_bencDictFindStr( body, "name", &str ) );
    check( !strcmp( str, "torrent-info" ) );
    check( ( args = tr_bencDictFind( body, "arguments" ) ) );
    check( tr_bencIsDict( args ) );
    check( ( ids = tr_bencDictFind( args, "ids" ) ) );
    check( tr_bencIsList( ids ) );
    check( tr_bencListSize( ids ) == 2 );
    check( tr_bencGetInt( tr_bencListChild( ids, 0 ), &i ) );
    check( i == 7 );
    check( tr_bencGetInt( tr_bencListChild( ids, 1 ), &i ) );
    check( i == 10 );

    tr_bencFree( &top );
    return 0;
}

static int
test2( void )
{
    tr_benc top;
    const char * in = " ";
    int err;

    top.type = 0;
    err = tr_jsonParse( NULL, in, strlen( in ), &top, NULL );

    check( err );
    check( !tr_bencIsDict( &top ) );

    return 0;
}

static int
test3( void )
{
    const char * in = "{ \"error\": 2,"
                      "  \"errorString\": \"torrent not registered with this tracker 6UHsVW'*C\","
                      "  \"eta\": 262792,"
                      "  \"id\": 25,"
                      "  \"leftUntilDone\": 2275655680 }";
    tr_benc top;
    const char * str;

    const int err = tr_jsonParse( NULL, in, strlen( in ), &top, NULL );
    check( !err );
    check( tr_bencDictFindStr( &top, "errorString", &str ) );
    check( !strcmp( str, "torrent not registered with this tracker 6UHsVW'*C" ) );

    tr_bencFree( &top );
    return 0;
}

int
main( void )
{
    int i;

    if( ( i = test_utf8( ) ) )
        return i;

    if( ( i = test1( ) ) )
        return i;

    if( ( i = test2( ) ) )
        return i;

    if( ( i = test3( ) ) )
        return i;

    return 0;
}

