#include <stdio.h>
#include <string.h>
#include "transmission.h"
#include "magnet.h"
#include "utils.h"

/* #define VERBOSE */
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

static int
test1( void )
{
    int i;
    const char * uri;
    tr_magnet_info * info;
    const int dec[] = { 210, 53, 64, 16, 163, 202, 74, 222, 91, 116,
                        39, 187, 9, 58, 98, 163, 137, 159, 243, 129 };

    uri = "magnet:?xt=urn:btih:"
          "d2354010a3ca4ade5b7427bb093a62a3899ff381"
          "&dn=Display%20Name"
          "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
          "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
          "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile";
    info = tr_magnetParse( uri );
    check( info != NULL )
    check( info->trackerCount == 2 );
    check( !strcmp( info->trackers[0], "http://tracker.openbittorrent.com/announce" ) )
    check( !strcmp( info->trackers[1], "http://tracker.opentracker.org/announce" ) )
    check( info->webseedCount == 1 );
    check( !strcmp( info->webseeds[0], "http://server.webseed.org/path/to/file" ) )
    check( !strcmp( info->displayName, "Display Name" ) )
    for( i=0; i<20; ++i )
        check( info->hash[i] == dec[i] );
    tr_magnetFree( info );
    info = NULL;

    /* same thing but in base32 encoding */
    uri = "magnet:?xt=urn:btih:"
          "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"
          "&dn=Display%20Name"
          "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
          "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
          "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce";
    info = tr_magnetParse( uri );
    check( info != NULL )
    check( info->trackerCount == 2 );
    check( !strcmp( info->trackers[0], "http://tracker.openbittorrent.com/announce" ) )
    check( !strcmp( info->trackers[1], "http://tracker.opentracker.org/announce" ) )
    check( info->webseedCount == 1 );
    check( !strcmp( info->webseeds[0], "http://server.webseed.org/path/to/file" ) )
    check( !strcmp( info->displayName, "Display Name" ) )
    for( i=0; i<20; ++i )
        check( info->hash[i] == dec[i] );
    tr_magnetFree( info );
    info = NULL;

    return 0;
}

int
main( void )
{
    int i;

    if( ( i = test1( ) ) )
        return i;

#ifdef VERBOSE
    fprintf( stderr, "magnet-test passed\n" );
#endif
    return 0;
}

