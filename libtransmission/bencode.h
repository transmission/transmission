/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

typedef struct benc_val_s
{
    char * begin;
    char * end;
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
            char * s;
            int    nofree;
        } s;
        struct
        {
            int                 alloc;
            int                 count;
            struct benc_val_s * vals;
        } l;
    } val;
} benc_val_t;

#define tr_bencLoad(b,l,v,e) _tr_bencLoad((char*)(b),(l),(v),(char**)(e))
int          _tr_bencLoad( char * buf, int len, benc_val_t * val,
                           char ** end );
void         tr_bencPrint( benc_val_t * val );
void         tr_bencFree( benc_val_t * val );
benc_val_t * tr_bencDictFind( benc_val_t * val, const char * key );
benc_val_t * tr_bencDictFindFirst( benc_val_t * val, ... );

/* marks a string as 'do not free' and returns it */
char *       tr_bencStealStr( benc_val_t * val );

/* convenience functions for building benc_val_t structures */

static inline void tr_bencInit( benc_val_t * val, int type )
{
    memset( val, 0, sizeof( *val ) );
    val->type = type;
}

#define tr_bencInitStr( a, b, c, d ) \
    _tr_bencInitStr( (a), ( char * )(b), (c), (d) )
void   _tr_bencInitStr( benc_val_t * val, char * str, int len, int nofree );
int    tr_bencInitStrDup( benc_val_t * val, const char * str );
void   tr_bencInitInt( benc_val_t * val, int64_t num );
/* args are a NULL terminated list of benc_val_t** */
int    tr_bencListAppend( benc_val_t * val, ... );
/* args are a NULL terminated list of pairs of char * key, benc_val_t ** val */
int    tr_bencDictAppend( benc_val_t * val, ... );
/* like tr_bencDictAppend but char * key args are marked nofree */
int    tr_bencDictAppendNofree( benc_val_t * val, ... );

/* insure val has room for at least count more items */
int tr_bencListExtend( benc_val_t * val, int count );

char * tr_bencSaveMalloc( benc_val_t * val, int * len );
int    tr_bencSave( benc_val_t * val, char ** buf,
                          int * used, int * max );

#endif
