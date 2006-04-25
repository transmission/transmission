/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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
int          _tr_bencLoad( char * buf, size_t len, benc_val_t * val,
                           char ** end );
void         tr_bencPrint( benc_val_t * val );
void         tr_bencFree( benc_val_t * val );
benc_val_t * tr_bencDictFind( benc_val_t * val, char * key );
char *       tr_bencSaveMalloc( benc_val_t * val, size_t * len );
int          tr_bencSave( benc_val_t * val, char ** buf,
                          size_t * used, size_t * max );

#endif
