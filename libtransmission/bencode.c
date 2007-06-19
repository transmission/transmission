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

#include "transmission.h"

/* setting to 1 to help expose bugs with tr_bencListAdd and tr_bencDictAdd */
#define LIST_SIZE   20 /* number of items to increment list/dict buffer by */

static int makeroom( benc_val_t * val, int count )
{
    assert( TYPE_LIST == val->type || TYPE_DICT == val->type );

    if( val->val.l.count + count > val->val.l.alloc )
    {
        /* We need a bigger boat */
        const int len = val->val.l.alloc + count +
            ( count % LIST_SIZE ? LIST_SIZE - ( count % LIST_SIZE ) : 0 );
        void * new = realloc( val->val.l.vals, len * sizeof( benc_val_t ) );
        if( NULL == new )
            return 1;

        val->val.l.alloc = len;
        val->val.l.vals  = new;
    }

    return 0;
}

int _tr_bencLoad( char * buf, int len, benc_val_t * val, char ** end )
{
    char * p, * e, * foo;

    if( NULL == buf || 1 >= len )
    {
        return 1;
    }

    if( !end )
    {
        /* So we only have to check once */
        end = &foo;
    }

    if( buf[0] == 'i' )
    {
        int64_t num;

        e = memchr( &buf[1], 'e', len - 1 );
        if( NULL == e )
        {
            return 1;
        }

        /* Integer: i1242e */
        *e = '\0';
        num = strtoll( &buf[1], &p, 10 );
        *e = 'e';

        if( p != e )
        {
            return 1;
        }

        tr_bencInitInt( val, num );
        *end = p + 1;
    }
    else if( buf[0] == 'l' || buf[0] == 'd' )
    {
        /* List: l<item1><item2>e
           Dict: d<string1><item1><string2><item2>e
           A dictionary is just a special kind of list with an even
           count of items, and where even items are strings. */
        char * cur;
        char   is_dict;
        char   str_expected;

        is_dict      = ( buf[0] == 'd' );
        cur          = &buf[1];
        str_expected = 1;
        tr_bencInit( val, ( is_dict ? TYPE_DICT : TYPE_LIST ) );
        while( cur - buf < len && cur[0] != 'e' )
        {
            if( makeroom( val, 1 ) ||
                tr_bencLoad( cur, len - (cur - buf),
                             &val->val.l.vals[val->val.l.count], &p ) )
            {
                tr_bencFree( val );
                return 1;
            }
            val->val.l.count++;
            if( is_dict && str_expected &&
                val->val.l.vals[val->val.l.count - 1].type != TYPE_STR )
            {
                tr_bencFree( val );
                return 1;
            }
            str_expected = !str_expected;

            cur = p;
        }

        if( is_dict && ( val->val.l.count & 1 ) )
        {
            tr_bencFree( val );
            return 1;
        }

        *end = cur + 1;
    }
    else
    {
        int    slen;
        char * sbuf;

        e = memchr( buf, ':', len );
        if( NULL == e )
        {
            return 1;
        }

        /* String: 12:whateverword */
        e[0] = '\0';
        slen = strtol( buf, &p, 10 );
        e[0] = ':';

        if( p != e || 0 > slen || len - ( ( p + 1 ) - buf ) < slen )
        {
            return 1;
        }

        sbuf = malloc( slen + 1 );
        if( NULL == sbuf )
        {
            return 1;
        }

        memcpy( sbuf, p + 1, slen );
        sbuf[slen] = '\0';
        tr_bencInitStr( val, sbuf, slen, 0 );

        *end = p + 1 + val->val.s.i;
    }

    val->begin = buf;
    val->end   = *end;

    return 0;
}

static void __bencPrint( benc_val_t * val, int space )
{
    int ii;

    for( ii = 0; ii < space; ii++ )
    {
        putc( ' ', stderr );
    }

    switch( val->type )
    {
        case TYPE_INT:
            fprintf( stderr, "int:  %"PRId64"\n", val->val.i );
            break;

        case TYPE_STR:
            for( ii = 0; val->val.s.i > ii; ii++ )
            {
                if( '\\' == val->val.s.s[ii] )
                {
                    putc( '\\', stderr );
                    putc( '\\', stderr );
                }
                else if( isprint( val->val.s.s[ii] ) )
                {
                    putc( val->val.s.s[ii], stderr );
                }
                else
                {
                    fprintf( stderr, "\\x%02x", val->val.s.s[ii] );
                }
            }
            putc( '\n', stderr );
            break;

        case TYPE_LIST:
            fprintf( stderr, "list\n" );
            for( ii = 0; ii < val->val.l.count; ii++ )
            {
                __bencPrint( &val->val.l.vals[ii], space + 1 );
            }
            break;

        case TYPE_DICT:
            fprintf( stderr, "dict\n" );
            for( ii = 0; ii < val->val.l.count; ii++ )
            {
                __bencPrint( &val->val.l.vals[ii], space + 1 );
            }
            break;
    }
}

void tr_bencPrint( benc_val_t * val )
{
    __bencPrint( val, 0 );
}

void tr_bencFree( benc_val_t * val )
{
    int i;

    switch( val->type )
    {
        case TYPE_INT:
            break;

        case TYPE_STR:
            if( !val->val.s.nofree )
            {
                free( val->val.s.s );
            }
            break;

        case TYPE_LIST:
        case TYPE_DICT:
            for( i = 0; i < val->val.l.count; i++ )
            {
                tr_bencFree( &val->val.l.vals[i] );
            }
            free( val->val.l.vals );
            break;
    }
}

benc_val_t * tr_bencDictFind( benc_val_t * val, const char * key )
{
    int len, ii;

    if( val->type != TYPE_DICT )
    {
        return NULL;
    }

    len = strlen( key );
    
    for( ii = 0; ii + 1 < val->val.l.count; ii += 2 )
    {
        if( TYPE_STR  != val->val.l.vals[ii].type ||
            len       != val->val.l.vals[ii].val.s.i ||
            0 != memcmp( val->val.l.vals[ii].val.s.s, key, len ) )
        {
            continue;
        }
        return &val->val.l.vals[ii+1];
    }

    return NULL;
}

benc_val_t * tr_bencDictFindFirst( benc_val_t * val, ... )
{
    const char * key;
    benc_val_t * ret;
    va_list      ap;

    ret = NULL;
    va_start( ap, val );
    while( ( key = va_arg( ap, const char * ) ) )
    {
        ret = tr_bencDictFind( val, key );
        if( NULL != ret )
        {
            break;
        }
    }
    va_end( ap );

    return ret;
}

char * tr_bencStealStr( benc_val_t * val )
{
    assert( TYPE_STR == val->type );
    val->val.s.nofree = 1;
    return val->val.s.s;
}

void _tr_bencInitStr( benc_val_t * val, char * str, int len, int nofree )
{
    tr_bencInit( val, TYPE_STR );
    val->val.s.s      = str;
    val->val.s.nofree = nofree;
    if( 0 >= len )
    {
        len = ( NULL == str ? 0 : strlen( str ) );
    }
    val->val.s.i = len;
}

int tr_bencInitStrDup( benc_val_t * val, const char * str )
{
    char * newStr = tr_strdup( str );
    if( newStr == NULL )
        return 1;

    _tr_bencInitStr( val, newStr, 0, 0 );
    return 0;
}

void tr_bencInitInt( benc_val_t * val, int64_t num )
{
    tr_bencInit( val, TYPE_INT );
    val->val.i = num;
}

int tr_bencListReserve( benc_val_t * val, int count )
{
    assert( TYPE_LIST == val->type );

    return makeroom( val, count );
}

int tr_bencDictReserve( benc_val_t * val, int count )
{
    assert( TYPE_DICT == val->type );

    return makeroom( val, count * 2 );
}

benc_val_t * tr_bencListAdd( benc_val_t * list )
{
    benc_val_t * item;

    assert( TYPE_LIST == list->type );
    assert( list->val.l.count < list->val.l.alloc );

    item = &list->val.l.vals[list->val.l.count];
    list->val.l.count++;
    tr_bencInit( item, TYPE_INT );

    return item;
}

benc_val_t * tr_bencDictAdd( benc_val_t * dict, const char * key )
{
    benc_val_t * keyval, * itemval;
    int i;

    assert( TYPE_DICT == dict->type );
    assert( dict->val.l.count + 2 <= dict->val.l.alloc );

    /* Keep dictionaries sorted by keys alphabetically.
       BitTornado-based clients (and maybe others) need this. */
    for( i = 0; i < dict->val.l.count; i += 2 )
    {
        assert( TYPE_STR == dict->val.l.vals[i].type );
        if( strcmp( key, dict->val.l.vals[i].val.s.s ) < 0 )
            break;
    }
    memmove( &dict->val.l.vals[i+2], &dict->val.l.vals[i],
             ( dict->val.l.count - i ) * sizeof(benc_val_t) );
    keyval  = &dict->val.l.vals[i];
    itemval = &dict->val.l.vals[i+1];
    dict->val.l.count += 2;

    tr_bencInitStr( keyval, key, -1, 1 );
    tr_bencInit( itemval, TYPE_INT );

    return itemval;
}

char * tr_bencSaveMalloc( benc_val_t * val, int * len )
{
    char * buf   = NULL;
    int alloc = 0;

    *len = 0;
    if( tr_bencSave( val, &buf, len, &alloc ) )
    {
        if( NULL != buf )
        {
            free(buf);
        }
        *len = 0;
        return NULL;
    }

    return buf;
}

int tr_bencSave( benc_val_t * val, char ** buf, int * used, int * max )
{
    int ii;    

    switch( val->type )
    {
        case TYPE_INT:
            if( tr_sprintf( buf, used, max, "i%"PRId64"e", val->val.i ) )
            {
                return 1;
            }
            break;

        case TYPE_STR:
            if( tr_sprintf( buf, used, max, "%i:", val->val.s.i ) ||
                tr_concat( buf, used,  max, val->val.s.s, val->val.s.i ) )
            {
                return 1;
            }
            break;

        case TYPE_LIST:
        case TYPE_DICT:
            if( tr_sprintf( buf, used, max,
                            (TYPE_LIST == val->type ? "l" : "d") ) )
            {
                return 1;
            }
            for( ii = 0; val->val.l.count > ii; ii++ )
            {
                if( tr_bencSave( val->val.l.vals + ii, buf, used, max ) )
                {
                    return 1;
                }
            }
            if( tr_sprintf( buf, used, max, "e" ) )
            {
                return 1;
            }
            break;
    }

    return 0;
}
