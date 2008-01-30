#include <stdio.h>
#include "transmission.h"
#include "bencode.h"
#include "utils.h" /* tr_free */

#define VERBOSE 1

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
testInt( void )
{
    uint8_t buf[128];
    int64_t val;
    int err;
    const uint8_t * end;

    /* good int string */
    snprintf( (char*)buf, sizeof( buf ), "i64e" );
    err = tr_bencParseInt( buf, 4, &end, &val );
    check( err == 0 );
    check( val == 64 );
    check( end == buf + 4 );

    /* missing 'e' */
    end = NULL;
    val = 888;
    err = tr_bencParseInt( buf, 3, &end, &val );
    check( err == TR_ERROR ); 
    check( val == 888 );
    check( end == NULL );

    /* empty buffer */
    err = tr_bencParseInt( buf, 0, &end, &val );
    check( err == TR_ERROR ); 
    check( val == 888 );
    check( end == NULL );

    /* bad number */
    snprintf( (char*)buf, sizeof( buf ), "i6z4e" );
    err = tr_bencParseInt( buf, 4, &end, &val );
    check( err == TR_ERROR );
    check( val == 888 );
    check( end == NULL );

    /* negative number */
    snprintf( (char*)buf, sizeof( buf ), "i-3e" );
    err = tr_bencParseInt( buf, 4, &end, &val );
    check( err == TR_OK );
    check( val == -3 );
    check( end == buf + 4 );

    /* zero */
    snprintf( (char*)buf, sizeof( buf ), "i0e" );
    err = tr_bencParseInt( buf, 4, &end, &val );
    check( err == TR_OK );
    check( val == 0 );
    check( end == buf + 3 );

    /* no leading zeroes allowed */
    val = 0;
    end = NULL;
    snprintf( (char*)buf, sizeof( buf ), "i04e" );
    err = tr_bencParseInt( buf, 4, &end, &val );
    check( err == TR_ERROR );
    check( val == 0 );
    check( end == NULL );

    return 0;
}

static int
testStr( void )
{
    uint8_t buf[128];
    int err;
    const uint8_t * end;
    uint8_t * str;
    size_t len;

    /* good string */
    snprintf( (char*)buf, sizeof( buf ), "4:boat" );
    err = tr_bencParseStr( buf, 6, &end, &str, &len );
    check( err == TR_OK );
    check( !strcmp( (char*)str, "boat" ) );
    check( len == 4 );
    check( end == buf + 6 );
    tr_free( str );
    str = NULL;
    end = NULL;
    len = 0;

    /* string goes past end of buffer */
    err = tr_bencParseStr( buf, 5, &end, &str, &len );
    check( err == TR_ERROR );
    check( str == NULL );
    check( end == NULL );
    check( !len );

    /* empty string */
    snprintf( (char*)buf, sizeof( buf ), "0:" );
    err = tr_bencParseStr( buf, 2, &end, &str, &len );
    check( err == TR_OK );
    check( !*str );
    check( !len );
    check( end == buf + 2 );
    tr_free( str );
    str = NULL;
    end = NULL;
    len = 0;

    /* short string */
    snprintf( (char*)buf, sizeof( buf ), "3:boat" );
    err = tr_bencParseStr( buf, 6, &end, &str, &len );
    check( err == TR_OK );
    check( !strcmp( (char*)str, "boa" ) );
    check( len == 3 );
    check( end == buf + 5 );
    tr_free( str );
    str = NULL;
    end = NULL;
    len = 0;

    return 0;
}
 
int
main( void )
{
    int i;

    if(( i = testInt( ) ))
        return i;

    if(( i = testStr( ) ))
        return i;

    return 0;
}
