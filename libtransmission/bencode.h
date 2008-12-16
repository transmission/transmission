/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_BENCODE_H
#define TR_BENCODE_H 1

#include <inttypes.h> /* for int64_t */

enum
{
    TYPE_INT  = 1,
    TYPE_STR  = 2,
    TYPE_LIST = 4,
    TYPE_DICT = 8
};

typedef struct tr_benc
{
    char    type;
    union
    {
        int64_t i;
        struct
        {
            size_t i;
            char * s;
        } s;
        struct
        {
            size_t alloc;
            size_t count;
            struct tr_benc * vals;
        } l;
    } val;
} tr_benc;

/***
****
***/

int       tr_bencParse( const void *     buf,
                        const void *     bufend,
                        tr_benc *        setme_benc,
                        const uint8_t ** setme_end );

int       tr_bencLoad( const void * buf,
                       size_t       buflen,
                       tr_benc *    setme_benc,
                       char **      setme_end );

int       tr_bencLoadFile( const char * filename,
                                        tr_benc * );

int       tr_bencLoadJSONFile( const char * filename,
                                            tr_benc * );

#if 0
void      tr_bencPrint( const tr_benc * );

#endif

void      tr_bencFree( tr_benc * );

char*     tr_bencSave( const tr_benc * val, int * len );

char*     tr_bencSaveAsJSON( const tr_benc * top, int * len );

int       tr_bencSaveFile( const char * filename, const tr_benc * );

int       tr_bencSaveJSONFile( const char * filename, const tr_benc * );

void      tr_bencInitStr( tr_benc *, const void * str, int str_len );

void      tr_bencInitRaw( tr_benc *, const void * raw, size_t raw_len );

void      tr_bencInitInt( tr_benc *, int64_t num );

int       tr_bencInitDict( tr_benc *, size_t reserveCount );

int       tr_bencInitList( tr_benc *, size_t reserveCount );

/***
****
***/

int       tr_bencListReserve( tr_benc *, size_t reserveCount );

tr_benc * tr_bencListAdd( tr_benc * );

tr_benc * tr_bencListAddInt( tr_benc *, int64_t val );

tr_benc * tr_bencListAddStr( tr_benc *, const char * val );

tr_benc * tr_bencListAddList( tr_benc *, size_t reserveCount );

tr_benc * tr_bencListAddDict( tr_benc *, size_t reserveCount );

size_t    tr_bencListSize( const tr_benc * list );

tr_benc * tr_bencListChild( tr_benc * list, size_t n );

/***
****
***/

int       tr_bencDictReserve( tr_benc *, size_t reserveCount );

int       tr_bencDictRemove( tr_benc *, const char * key );

tr_benc * tr_bencDictAdd( tr_benc *, const char * key );

tr_benc * tr_bencDictAddDouble( tr_benc *, const char * key, double );

tr_benc * tr_bencDictAddInt( tr_benc *, const char * key, int64_t );

tr_benc * tr_bencDictAddStr( tr_benc *, const char * key, const char * );

tr_benc * tr_bencDictAddList( tr_benc *, const char * key, size_t reserve );

tr_benc * tr_bencDictAddDict( tr_benc *, const char * key, size_t reserve );

tr_benc * tr_bencDictAddRaw( tr_benc *, const char * key,
                             const void * raw, size_t rawlen );

tr_benc*  tr_bencDictFind( tr_benc *, const char * key );

tr_bool   tr_bencDictFindList( tr_benc *, const char * key, tr_benc ** setme );

tr_bool   tr_bencDictFindDict( tr_benc *, const char * key, tr_benc ** setme );

tr_bool   tr_bencDictFindInt( tr_benc *, const char * key, int64_t * setme );

tr_bool   tr_bencDictFindDouble( tr_benc *, const char * key, double * setme );

tr_bool   tr_bencDictFindStr( tr_benc *, const char * key, const char ** setme );

tr_bool   tr_bencDictFindRaw( tr_benc *, const char * key,
                              const uint8_t ** setme_raw, size_t * setme_len );

/***
****
***/

tr_bool   tr_bencGetInt( const tr_benc * val, int64_t * setme );

tr_bool   tr_bencGetStr( const tr_benc * val, const char ** setme );

tr_bool   tr_bencIsType( const tr_benc *, int type );


#define tr_bencIsInt( b )    tr_bencIsType( ( b ), TYPE_INT )
#define tr_bencIsDict( b )   tr_bencIsType( ( b ), TYPE_DICT )
#define tr_bencIsList( b )   tr_bencIsType( ( b ), TYPE_LIST )
#define tr_bencIsString( b ) tr_bencIsType( ( b ), TYPE_STR )

/**
***  Treat these as private -- they're only made public here
***  so that the unit tests can find them
**/

int tr_bencParseInt( const uint8_t *  buf,
                     const uint8_t *  bufend,
                     const uint8_t ** setme_end,
                     int64_t *        setme_val );

int tr_bencParseStr( const uint8_t *  buf,
                     const uint8_t *  bufend,
                     const uint8_t ** setme_end,
                     const uint8_t ** setme_str,
                     size_t *         setme_strlen );

/**
***
**/

void  tr_bencMergeDicts( tr_benc * target, const tr_benc * source );

#endif
