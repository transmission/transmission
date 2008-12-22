#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "transmission.h"
#include "bencode.h"
#include "json.h"
#include "utils.h" /* tr_free */

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
testInt( void )
{
    uint8_t         buf[128];
    int64_t         val;
    int             err;
    const uint8_t * end;

    /* good int string */
    tr_snprintf( (char*)buf, sizeof( buf ), "i64e" );
    err = tr_bencParseInt( buf, buf + 4, &end, &val );
    check( err == 0 );
    check( val == 64 );
    check( end == buf + 4 );

    /* missing 'e' */
    end = NULL;
    val = 888;
    err = tr_bencParseInt( buf, buf + 3, &end, &val );
    check( err == EILSEQ );
    check( val == 888 );
    check( end == NULL );

    /* empty buffer */
    err = tr_bencParseInt( buf, buf + 0, &end, &val );
    check( err == EILSEQ );
    check( val == 888 );
    check( end == NULL );

    /* bad number */
    tr_snprintf( (char*)buf, sizeof( buf ), "i6z4e" );
    err = tr_bencParseInt( buf, buf + 5, &end, &val );
    check( err == EILSEQ );
    check( val == 888 );
    check( end == NULL );

    /* negative number */
    tr_snprintf( (char*)buf, sizeof( buf ), "i-3e" );
    err = tr_bencParseInt( buf, buf + 4, &end, &val );
    check( err == 0 );
    check( val == -3 );
    check( end == buf + 4 );

    /* zero */
    tr_snprintf( (char*)buf, sizeof( buf ), "i0e" );
    err = tr_bencParseInt( buf, buf + 4, &end, &val );
    check( err == 0 );
    check( val == 0 );
    check( end == buf + 3 );

    /* no leading zeroes allowed */
    val = 0;
    end = NULL;
    tr_snprintf( (char*)buf, sizeof( buf ), "i04e" );
    err = tr_bencParseInt( buf, buf + 4, &end, &val );
    check( err == EILSEQ );
    check( val == 0 );
    check( end == NULL );

    return 0;
}

static int
testStr( void )
{
    uint8_t         buf[128];
    int             err;
    const uint8_t * end;
    const uint8_t * str;
    size_t          len;

    /* good string */
    tr_snprintf( (char*)buf, sizeof( buf ), "4:boat" );
    err = tr_bencParseStr( buf, buf + 6, &end, &str, &len );
    check( err == 0 );
    check( !strncmp( (char*)str, "boat", len ) );
    check( len == 4 );
    check( end == buf + 6 );
    str = NULL;
    end = NULL;
    len = 0;

    /* string goes past end of buffer */
    err = tr_bencParseStr( buf, buf + 5, &end, &str, &len );
    check( err == EILSEQ );
    check( str == NULL );
    check( end == NULL );
    check( !len );

    /* empty string */
    tr_snprintf( (char*)buf, sizeof( buf ), "0:" );
    err = tr_bencParseStr( buf, buf + 2, &end, &str, &len );
    check( err == 0 );
    check( !*str );
    check( !len );
    check( end == buf + 2 );
    str = NULL;
    end = NULL;
    len = 0;

    /* short string */
    tr_snprintf( (char*)buf, sizeof( buf ), "3:boat" );
    err = tr_bencParseStr( buf, buf + 6, &end, &str, &len );
    check( err == 0 );
    check( !strncmp( (char*)str, "boa", len ) );
    check( len == 3 );
    check( end == buf + 5 );
    str = NULL;
    end = NULL;
    len = 0;

    return 0;
}

static int
testString( const char * str,
            int          isGood )
{
    tr_benc         val;
    const uint8_t * end = NULL;
    char *          saved;
    const size_t    len = strlen( str );
    int             savedLen;
    int             err = tr_bencParse( str, str + len, &val, &end );

    if( !isGood )
    {
        check( err );
    }
    else
    {
        check( !err );
#if 0
        fprintf( stderr, "in: [%s]\n", str );
        fprintf( stderr, "out:\n%s", tr_bencSaveAsJSON( &val, NULL ) );
#endif
        check( end == (const uint8_t*)str + len );
        saved = tr_bencSave( &val, &savedLen );
        check( !strcmp( saved, str ) );
        check( len == (size_t)savedLen );
        tr_free( saved );
        tr_bencFree( &val );
    }
    return 0;
}

static int
testParse( void )
{
    tr_benc         val;
    tr_benc *       child;
    tr_benc *       child2;
    uint8_t         buf[512];
    const uint8_t * end;
    int             err;
    int             len;
    int64_t         i;
    char *          saved;

    tr_snprintf( (char*)buf, sizeof( buf ), "i64e" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( tr_bencGetInt( &val, &i ) );
    check( i == 64 );
    check( end == buf + 4 );
    tr_bencFree( &val );

    tr_snprintf( (char*)buf, sizeof( buf ), "li64ei32ei16ee" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( end == buf + strlen( (char*)buf ) );
    check( val.val.l.count == 3 );
    check( tr_bencGetInt( &val.val.l.vals[0], &i ) );
    check( i == 64 );
    check( tr_bencGetInt( &val.val.l.vals[1], &i ) );
    check( i == 32 );
    check( tr_bencGetInt( &val.val.l.vals[2], &i ) );
    check( i == 16 );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, (char*)buf ) );
    tr_free( saved );
    tr_bencFree( &val );

    end = NULL;
    tr_snprintf( (char*)buf, sizeof( buf ), "lllee" );
    err = tr_bencParse( buf, buf + strlen( (char*)buf ), &val, &end );
    check( err );
    check( end == NULL );

    end = NULL;
    tr_snprintf( (char*)buf, sizeof( buf ), "le" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( end == buf + 2 );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, "le" ) );
    tr_free( saved );
    tr_bencFree( &val );

    if( ( err = testString( "llleee", TRUE ) ) )
        return err;
    if( ( err = testString( "d3:cow3:moo4:spam4:eggse", TRUE ) ) )
        return err;
    if( ( err = testString( "d4:spaml1:a1:bee", TRUE ) ) )
        return err;
    if( ( err =
             testString( "d5:greenli1ei2ei3ee4:spamd1:ai123e3:keyi214eee",
                         TRUE ) ) )
        return err;
    if( ( err =
             testString(
                 "d9:publisher3:bob17:publisher-webpage15:www.example.com18:publisher.location4:homee",
                 TRUE ) ) )
        return err;
    if( ( err =
             testString(
                 "d8:completei1e8:intervali1800e12:min intervali1800e5:peers0:e",
                 TRUE ) ) )
        return err;
    if( ( err = testString( "d1:ai0e1:be", FALSE ) ) ) /* odd number of children
                                                         */
        return err;
    if( ( err = testString( "", FALSE ) ) )
        return err;
    if( ( err = testString( " ", FALSE ) ) )
        return err;

    /* nested containers
     * parse an unsorted dict
     * save as a sorted dict */
    end = NULL;
    tr_snprintf( (char*)buf, sizeof( buf ), "lld1:bi32e1:ai64eeee" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( end == buf + strlen( (const char*)buf ) );
    check( ( child = tr_bencListChild( &val, 0 ) ) );
    check( ( child2 = tr_bencListChild( child, 0 ) ) );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, "lld1:ai64e1:bi32eeee" ) );
    tr_free( saved );
    tr_bencFree( &val );

    /* too many endings */
    end = NULL;
    tr_snprintf( (char*)buf, sizeof( buf ), "leee" );
    err = tr_bencParse( buf, buf + sizeof( buf ), &val, &end );
    check( !err );
    check( end == buf + 2 );
    saved = tr_bencSave( &val, &len );
    check( !strcmp( saved, "le" ) );
    tr_free( saved );
    tr_bencFree( &val );

    /* no ending */
    end = NULL;
    tr_snprintf( (char*)buf, sizeof( buf ), "l1:a1:b1:c" );
    err = tr_bencParse( buf, buf + strlen( (char*)buf ), &val, &end );
    check( err );

    /* incomplete string */
    end = NULL;
    tr_snprintf( (char*)buf, sizeof( buf ), "1:" );
    err = tr_bencParse( buf, buf + strlen( (char*)buf ), &val, &end );
    check( err );

    return 0;
}

static void
stripWhitespace( char * in )
{
    char * out;

    for( out = in; *in; ++in )
        if( !isspace( *in ) )
            *out++ = *in;
    *out = '\0';
}

static int
testJSONSnippet( const char * benc_str,
                 const char * expected )
{
    tr_benc top;
    char *  serialized;

    tr_bencLoad( benc_str, strlen( benc_str ), &top, NULL );
    serialized = tr_bencSaveAsJSON( &top, NULL );
    stripWhitespace( serialized );
#if 0
    fprintf( stderr, "benc: %s\n", benc_str );
    fprintf( stderr, "json: %s\n", serialized );
    fprintf( stderr, "want: %s\n", expected );
#endif
    check( !strcmp( serialized, expected ) );
    tr_free( serialized );
    tr_bencFree( &top );
    return 0;
}

static int
testJSON( void )
{
    int          val;
    const char * benc_str;
    const char * expected;

    benc_str = "i6e";
    expected = "6";
    if( ( val = testJSONSnippet( benc_str, expected ) ) )
        return val;

    benc_str = "d5:helloi1e5:worldi2ee";
    expected = "{\"hello\":1,\"world\":2}";
    if( ( val = testJSONSnippet( benc_str, expected ) ) )
        return val;

    benc_str = "d5:helloi1e5:worldi2e3:fooli1ei2ei3eee";
    expected = "{\"foo\":[1,2,3],\"hello\":1,\"world\":2}";
    if( ( val = testJSONSnippet( benc_str, expected ) ) )
        return val;

    benc_str = "d5:helloi1e5:worldi2e3:fooli1ei2ei3ed1:ai0eeee";
    expected = "{\"foo\":[1,2,3,{\"a\":0}],\"hello\":1,\"world\":2}";
    if( ( val = testJSONSnippet( benc_str, expected ) ) )
        return val;

    benc_str = "d4:argsd6:statusle7:status2lee6:result7:successe";
    expected =
        "{\"args\":{\"status\":[],\"status2\":[]},\"result\":\"success\"}";
    if( ( val = testJSONSnippet( benc_str, expected ) ) )
        return val;

    return 0;
}

static int
testStackSmash( int depth )
{
    int             i;
    int             len;
    int             err;
    uint8_t *       in;
    const uint8_t * end;
    tr_benc         val;
    char *          saved;

    in = tr_new( uint8_t, depth * 2 + 1 );
    for( i = 0; i < depth; ++i )
    {
        in[i] = 'l';
        in[depth + i] = 'e';
    }
    in[depth * 2] = '\0';
    err = tr_bencParse( in, in + ( depth * 2 ), &val, &end );
    check( !err );
    check( end == in + ( depth * 2 ) );
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

    if(( i = testJSON( )))
        return i;

#ifndef WIN32
    i = testStackSmash( 1000000 );
#else
    i = testStackSmash( 100000 );
#endif
    if( i )
        return i;

    return 0;
}

