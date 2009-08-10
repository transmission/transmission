/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
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

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h> /* for int64_t */

struct evbuffer;

/**
 * @addtogroup tr_benc Variant
 *
 * An object that acts like a union for
 * integers, strings, lists, dictionaries, booleans, and floating-point numbers.
 * The structure is named tr_benc due to the historical reason that it was
 * originally tightly coupled with bencoded data.  It currently supports
 * being parsed from, and serialized to, both bencoded notation and json notation.
 *
 * @{
 */

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
enum
{
    TR_TYPE_INT  = 1,
    TR_TYPE_STR  = 2,
    TR_TYPE_LIST = 4,
    TR_TYPE_DICT = 8,
    TR_TYPE_BOOL = 16,
    TR_TYPE_REAL = 32
};

/* These are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
typedef struct tr_benc
{
    union
    {
        uint8_t b; /* bool type */

        double d;  /* double type */

        int64_t i; /* int type */

        struct /* string type */
        {
            size_t len; /* the string length */
            union {
                char buf[16]; /* local buffer for short strings */
                char * ptr; /* alloc'ed pointer for long strings */
            } str;
        } s;

        struct /* list & dict types */
        {
            struct tr_benc * vals; /* nodes */
            size_t alloc; /* nodes allocated */
            size_t count; /* nodes used */
        } l;
    } val;

    char type;
} tr_benc;

/***
****
***/

int       tr_bencParse( const void     * buf,
                        const void     * bufend,
                        tr_benc        * setme_benc,
                        const uint8_t ** setme_end );

int       tr_bencLoad( const void   * buf,
                       size_t         buflen,
                       tr_benc      * setme_benc,
                       char        ** setme_end );

void      tr_bencFree( tr_benc * );

void      tr_bencInitStr( tr_benc *, const void * str, int str_len );

void      tr_bencInitRaw( tr_benc *, const void * raw, size_t raw_len );

void      tr_bencInitInt( tr_benc *, int64_t num );

int       tr_bencInitDict( tr_benc *, size_t reserveCount );

int       tr_bencInitList( tr_benc *, size_t reserveCount );

void      tr_bencInitBool( tr_benc *, int value );

void      tr_bencInitReal( tr_benc *, double value );

/***
****  Serialization / Deserialization
***/

typedef enum
{
    TR_FMT_BENC,
    TR_FMT_JSON,
    TR_FMT_JSON_LEAN /* saves bandwidth by omitting all whitespace. */
}
tr_fmt_mode;

int tr_bencToFile( const tr_benc *, tr_fmt_mode, const char * filename );

char* tr_bencToStr( const tr_benc *, tr_fmt_mode, int * len );

void tr_bencToBuf( const tr_benc *, tr_fmt_mode, struct evbuffer * );

/* TR_FMT_JSON_LEAN and TR_FMT_JSON are equivalent in this function. */
int tr_bencLoadFile( tr_benc * setme, tr_fmt_mode, const char * filename );

/***
****
***/

int tr_bencListReserve( tr_benc *, size_t reserveCount );

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

tr_benc * tr_bencDictAddReal( tr_benc *, const char * key, double );

tr_benc * tr_bencDictAddInt( tr_benc *, const char * key, int64_t );

tr_benc * tr_bencDictAddBool( tr_benc *, const char * key, tr_bool );

tr_benc * tr_bencDictAddStr( tr_benc *, const char * key, const char * );

tr_benc * tr_bencDictAddList( tr_benc *, const char * key, size_t reserve );

tr_benc * tr_bencDictAddDict( tr_benc *, const char * key, size_t reserve );

tr_benc * tr_bencDictAddRaw( tr_benc *, const char * key,
                             const void * raw, size_t rawlen );

tr_bool   tr_bencDictChild( tr_benc *, size_t i, const char ** key, tr_benc ** val );

tr_benc*  tr_bencDictFind( tr_benc *, const char * key );

tr_bool   tr_bencDictFindList( tr_benc *, const char * key, tr_benc ** setme );

tr_bool   tr_bencDictFindDict( tr_benc *, const char * key, tr_benc ** setme );

tr_bool   tr_bencDictFindInt( tr_benc *, const char * key, int64_t * setme );

tr_bool   tr_bencDictFindReal( tr_benc *, const char * key, double * setme );

tr_bool   tr_bencDictFindBool( tr_benc *, const char * key, tr_bool * setme );

tr_bool   tr_bencDictFindStr( tr_benc *, const char * key, const char ** setme );

tr_bool   tr_bencDictFindRaw( tr_benc *, const char * key,
                              const uint8_t ** setme_raw, size_t * setme_len );

/***
****
***/

tr_bool   tr_bencGetInt( const tr_benc * val, int64_t * setme );
tr_bool   tr_bencGetStr( const tr_benc * val, const char ** setme );
tr_bool   tr_bencGetBool( const tr_benc * val, tr_bool * setme );
tr_bool   tr_bencGetReal( const tr_benc * val, double * setme );

static TR_INLINE tr_bool tr_bencIsType  ( const tr_benc * b, int type ) { return ( b != NULL ) && ( b->type == type ); }
static TR_INLINE tr_bool tr_bencIsInt   ( const tr_benc * b ) { return tr_bencIsType( b, TR_TYPE_INT ); }
static TR_INLINE tr_bool tr_bencIsDict  ( const tr_benc * b ) { return tr_bencIsType( b, TR_TYPE_DICT ); }
static TR_INLINE tr_bool tr_bencIsList  ( const tr_benc * b ) { return tr_bencIsType( b, TR_TYPE_LIST ); }
static TR_INLINE tr_bool tr_bencIsString( const tr_benc * b ) { return tr_bencIsType( b, TR_TYPE_STR ); }
static TR_INLINE tr_bool tr_bencIsBool  ( const tr_benc * b ) { return tr_bencIsType( b, TR_TYPE_BOOL ); }
static TR_INLINE tr_bool tr_bencIsReal  ( const tr_benc * b ) { return tr_bencIsType( b, TR_TYPE_REAL ); }

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

/* this is only quasi-supported.  don't rely on it too heavily outside of libT */
void  tr_bencMergeDicts( tr_benc * target, const tr_benc * source );

/* @} */

#ifdef __cplusplus
}
#endif

#endif
