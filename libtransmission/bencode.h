/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef TR_BENCODE_H
#define TR_BENCODE_H 1

#include <inttypes.h> /* for int64_t */
#include <string.h> /* for memset */

typedef struct tr_benc
{
#define TYPE_INT  1
#define TYPE_STR  2
#define TYPE_LIST 4
#define TYPE_DICT 8
    char   type;
    union
    {
        int64_t i;
        struct
        {
            int    i;
            int    nofree;
            char * s;
        } s;
        struct
        {
            int alloc;
            int count;
            struct tr_benc * vals;
        } l;
    } val;
} tr_benc;

/* backwards compatability */
typedef tr_benc benc_val_t;

int tr_bencParse( const void      * buf,
                  const void      * bufend,
                  tr_benc         * setme_benc,
                  const uint8_t  ** setme_end );

int tr_bencLoad( const void  * buf,
                 int           buflen,
                 tr_benc     * setme_benc,
                 char       ** setme_end );

void      tr_bencPrint( const tr_benc * );
void      tr_bencFree( tr_benc * );
int       tr_bencDictFindInt( tr_benc * dict, const char * key, int64_t * setme );
int       tr_bencDictFindDouble( tr_benc * dict, const char * key, double * setme );
int       tr_bencDictFindStr( tr_benc * dict, const char * key, const char ** setme );
int       tr_bencDictFindList( tr_benc * dict, const char * key, tr_benc ** setme );
int       tr_bencDictFindDict( tr_benc * dict, const char * key, tr_benc ** setme );
tr_benc * tr_bencDictFind( tr_benc * dict, const char * key );
tr_benc * tr_bencDictFindType( tr_benc * dict, const char * key, int type );
tr_benc * tr_bencDictFindFirst( tr_benc * dict, ... );

/* convenience functions for building tr_benc    structures */

static inline void tr_bencInit( tr_benc    * val, int type )
{
    memset( val, 0, sizeof( *val ) );
    val->type = type;
}

#define tr_bencInitStr( a, b, c, d ) \
    _tr_bencInitStr( (a), ( char * )(b), (c), (d) )
void   _tr_bencInitStr( tr_benc * val, char * str, int len, int nofree );
int    tr_bencInitStrDup( tr_benc * val, const char * str );
void   tr_bencInitRaw( tr_benc * val, const void * src, size_t byteCount );
void   tr_bencInitInt( tr_benc * val, int64_t num );
int   tr_bencInitDict( tr_benc * val, int reserveCount );
int   tr_bencInitList( tr_benc * val, int reserveCount );
int   tr_bencListReserve( tr_benc * list, int count );
/* note that for one key-value pair, count should be 1, not 2 */
int   tr_bencDictReserve( tr_benc * dict, int count );
tr_benc    * tr_bencListAdd( tr_benc  * list );
tr_benc    * tr_bencListAddInt( tr_benc  * list, int64_t val );
tr_benc    * tr_bencListAddStr( tr_benc  * list, const char * val );
tr_benc    * tr_bencListAddList( tr_benc  * list, int reserveCount );
tr_benc    * tr_bencListAddDict( tr_benc  * list, int reserveCount );
tr_benc    * tr_bencDictAdd( tr_benc * dict, const char * key );
tr_benc    * tr_bencDictAddDouble( tr_benc * dict, const char * key, double d );
tr_benc    * tr_bencDictAddInt( tr_benc * dict, const char * key, int64_t val );
tr_benc    * tr_bencDictAddStr( tr_benc * dict, const char * key, const char * val );
tr_benc    * tr_bencDictAddList( tr_benc * dict, const char * key, int reserveCount );
tr_benc    * tr_bencDictAddDict( tr_benc * dict, const char * key, int reserveCount );
tr_benc    * tr_bencDictAddRaw( tr_benc * dict, const char * key, const void *, size_t len );

char*  tr_bencSave( const tr_benc * val, int * len );
char*  tr_bencSaveAsJSON( const tr_benc * top, int * len );
int    tr_bencSaveFile( const char * filename, const tr_benc * );
int    tr_bencLoadFile( const char * filename, tr_benc * );

int tr_bencGetInt( const tr_benc * val, int64_t * setme );
int tr_bencGetStr( const tr_benc * val, const char ** setme );

int tr_bencIsType( const tr_benc *, int type );
#define tr_bencIsInt(b) (tr_bencIsType(b,TYPE_INT))
#define tr_bencIsDict(b) (tr_bencIsType(b,TYPE_DICT))
#define tr_bencIsList(b) (tr_bencIsType(b,TYPE_LIST))
#define tr_bencIsString(b) (tr_bencIsType(b,TYPE_STR))

/**
***  Treat these as private -- they're only made public here
***  so that the unit tests can find them
**/

int  tr_bencParseInt( const uint8_t  * buf,
                      const uint8_t  * bufend,
                      const uint8_t ** setme_end, 
                      int64_t        * setme_val );

int  tr_bencParseStr( const uint8_t  * buf,
                      const uint8_t  * bufend,
                      const uint8_t ** setme_end, 
                      uint8_t       ** setme_str,
                      size_t         * setme_strlen );

/**
***
**/

int       tr_bencListSize( const tr_benc * list );
tr_benc * tr_bencListChild( tr_benc * list, int n );


#endif
