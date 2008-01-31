#include <stdio.h>
#include "transmission.h"
#include "bencode.h"
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
testInt( void )
{
    uint8_t buf[128];
    int64_t val;
    unsigned int err;
    const uint8_t * end;

    /* good int string */
    snprintf( (char*)buf, sizeof( buf ), "i64e" );
    err = tr_bencParseInt( buf, buf+4, &end, &val );
    check( err == 0 );
    check( val == 64 );
    check( end == buf + 4 );

    /* missing 'e' */
    end = NULL;
    val = 888;
    err = tr_bencParseInt( buf, buf+3, &end, &val );
    check( err == TR_ERROR ); 
    check( val == 888 );
    check( end == NULL );

    /* empty buffer */
    err = tr_bencParseInt( buf, buf+0, &end, &val );
    check( err == TR_ERROR ); 
    check( val == 888 );
    check( end == NULL );

    /* bad number */
    snprintf( (char*)buf, sizeof( buf ), "i6z4e" );
    err = tr_bencParseInt( buf, buf+5, &end, &val );
    check( err == TR_ERROR );
    check( val == 888 );
    check( end == NULL );

    /* negative number */
    snprintf( (char*)buf, sizeof( buf ), "i-3e" );
    err = tr_bencParseInt( buf, buf+4, &end, &val );
    check( err == TR_OK );
    check( val == -3 );
    check( end == buf + 4 );

    /* zero */
    snprintf( (char*)buf, sizeof( buf ), "i0e" );
    err = tr_bencParseInt( buf, buf+4, &end, &val );
    check( err == TR_OK );
    check( val == 0 );
    check( end == buf + 3 );

    /* no leading zeroes allowed */
    val = 0;
    end = NULL;
    snprintf( (char*)buf, sizeof( buf ), "i04e" );
    err = tr_bencParseInt( buf, buf+4, &end, &val );
    check( err == TR_ERROR );
    check( val == 0 );
    check( end == NULL );

    return 0;
}

static int
testStr( void )
{
    uint8_t buf[128];
    unsigned int err;
    const uint8_t * end;
    uint8_t * str;
    size_t len;

    /* good string */
    snprintf( (char*)buf, sizeof( buf ), "4:boat" );
    err = tr_bencParseStr( buf, buf+6, &end, &str, &len );
    check( err == TR_OK );
    check( !strcmp( (char*)str, "boat" ) );
    check( len == 4 );
    check( end == buf + 6 );
    tr_free( str );
    str = NULL;
    end = NULL;
    len = 0;

    /* string goes past end of buffer */
    err = tr_bencParseStr( buf, buf+5, &end, &str, &len );
    check( err == TR_ERROR );
    check( str == NULL );
    check( end == NULL );
    check( !len );

    /* empty string */
    snprintf( (char*)buf, sizeof( buf ), "0:" );
    err = tr_bencParseStr( buf, buf+2, &end, &str, &len );
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
    err = tr_bencParseStr( buf, buf+6, &end, &str, &len );
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

static int
testParse( void )
{
    benc_val_t val;
    benc_val_t * child;
    benc_val_t * child2;
    uint8_t buf[512];
    const uint8_t * end;
    int err;
    int len;
    char * saved;

    snprintf( (char*)buf, sizeof( buf ), "i64e" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( tr_bencGetInt( &val ) == 64 );
    check( end == buf + 4 );
    tr_bencFree( &val );

    snprintf( (char*)buf, sizeof( buf ), "li64ei32ei16ee" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( end == buf + strlen( (char*)buf ) );
    check( val.val.l.count == 3 );
    check( tr_bencGetInt( &val.val.l.vals[0] ) == 64 );
    check( tr_bencGetInt( &val.val.l.vals[1] ) == 32 );
    check( tr_bencGetInt( &val.val.l.vals[2] ) == 16 );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, (char*)buf ) );
    tr_free( saved );
    tr_bencFree( &val );

    end = NULL;
    snprintf( (char*)buf, sizeof( buf ), "lllee" );
    err = tr_bencParse( buf, buf + strlen( (char*)buf ), &val , &end );
    check( err );
    check( end == NULL );

    end = NULL;
    snprintf( (char*)buf, sizeof( buf ), "le" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val , &end );
    check( !err );
    check( end == buf + 2 );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, "le" ) );
    tr_free( saved );
    tr_bencFree( &val );

    end = NULL;
    snprintf( (char*)buf, sizeof( buf ), "llleee" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val , &end );
    check( !err );
    check( end == buf + 6 );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, "llleee" ) );
    tr_free( saved );
    tr_bencFree( &val );

    /* nested containers
     * parse an unsorted dict
     * save as a sorted dict */
    end = NULL;
    snprintf( (char*)buf, sizeof( buf ), "lld1:bi32e1:ai64eeee" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( end == buf + strlen( (const char*)buf ) );
    check(( child = tr_bencListGetNthChild( &val, 0 )));
    check(( child2 = tr_bencListGetNthChild( child, 0 )));
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, "lld1:ai64e1:bi32eeee" ) );
    tr_free( saved );
    tr_bencFree( &val );

    end = NULL;
    snprintf( (char*)buf, sizeof( buf ), "d8:completei1e8:intervali1800e12:min intervali1800e5:peers0:e" );
    err = tr_bencParse( buf, buf+sizeof( buf ), &val, &end );
    check( !err );
    check( end == buf + strlen( (const char*)buf ) );
    tr_bencFree( &val );

    return 0;
}

static int
testStackSmash( void )
{
    int i;
    int len;
    int depth;
    int err;
    uint8_t * in;
    const uint8_t * end;
    benc_val_t val;
    char * saved;

    depth = 1000000;
    in = tr_new( uint8_t, depth*2 + 1 );
    for( i=0; i<depth; ++i ) {
        in[i] = 'l';
        in[depth+i] = 'e';
    }
    in[depth*2] = '\0';
    err = tr_bencParse( in, in+(depth*2), &val, &end );
    check( !err );
    check( end == in+(depth*2) );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, (char*)in ) );
    tr_free( in );
    tr_free( saved );
    tr_bencFree( &val );

    return 0;
}


int
main( void )
{
    int i;

    if(( i = testInt( )))
        return i;

    if(( i = testStr( )))
        return i;

    if(( i = testParse( )))
        return i;

    if(( i = testStackSmash( )))
        return i;

    return 0;
}
