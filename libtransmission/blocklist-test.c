#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "blocklist.h"
#include "net.h"
#include "utils.h"

#define VERBOSE 1

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

extern void tr_getBlocklistFilename( char * buf, size_t buflen );

static void
createTestBlocklist( const char * tmpfile )
{
    const char * lines[] = { "Austin Law Firm:216.16.1.144-216.16.1.151",
                             "Sargent Controls and Aerospace:216.19.18.0-216.19.18.255",
                             "Corel Corporation:216.21.157.192-216.21.157.223",
                             "Fox Speed Channel:216.79.131.192-216.79.131.223" };
    FILE * out;
    int i;
    const int lineCount = sizeof(lines) / sizeof(lines[0]);

    /* create the ascii file to feed to libtransmission */
    out = fopen( tmpfile, "w+" );
    for( i=0; i<lineCount; ++i )
        fprintf( out, "%s\n", lines[i] );
    fclose( out );
}

int
main( void )
{
    tr_handle * handle;
    char fname[MAX_PATH_LENGTH];
    char bak[MAX_PATH_LENGTH];
    char * tmpfile = "/tmp/transmission-blocklist-test.txt";
    struct in_addr addr;
    int test = 0;

    handle = tr_init( "unit-tests" );

    /* backup the real blocklist */
    tr_getBlocklistFilename( fname, sizeof( fname ) );
    snprintf( bak, sizeof( bak ), "%s.bak", fname );
    rename( fname, bak );

    /* create our own dummy blocklist */
    createTestBlocklist( tmpfile );
    tr_blocklistSet( handle, tmpfile );

    /* now run some tests */
    check( !tr_netResolve( "216.16.1.143", &addr ) );
    check( !tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.144", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.145", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.146", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.147", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.148", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.149", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.150", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.151", &addr ) );
    check(  tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.152", &addr ) );
    check( !tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "216.16.1.153", &addr ) );
    check( !tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "217.0.0.1", &addr ) );
    check( !tr_peerIsBlocked( handle, &addr ) );
    check( !tr_netResolve( "255.0.0.1", &addr ) );

    /* restore the real blocklist */
    remove( tmpfile );
    remove( fname );
    rename( bak, fname );
    return 0;
}
