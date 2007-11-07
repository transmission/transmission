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

#include <assert.h>
#include <ctype.h> /* isdigit, isprint */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/queue.h> /* libevent needs this */
#include <sys/types.h> /* libevent needs this */
#include <event.h>

#include "transmission.h"
#include "bencode.h"
#include "utils.h"

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

    if( !end )
    {
        /* So we only have to check once */
        end = &foo;
    }

    for( ;; )
    {
        if( !buf || len<1 ) /* no more text to parse... */
            return 1;

        if( *buf=='i' ) /* Integer: i1234e */
        {
            int64_t num;

            e = memchr( &buf[1], 'e', len - 1 );
            if( !e )
                return 1;

            *e = '\0';
            num = strtoll( &buf[1], &p, 10 );
            *e = 'e';

            if( p != e )
                return 1;

            tr_bencInitInt( val, num );
            *end = p + 1;
            break;
        }
        else if( *buf=='l' || *buf=='d' )
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
            break;
        }
        else if( isdigit(*buf) )
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
            break;
        }
        else /* invalid bencoded text... march past it */
        {
            ++buf;
            --len;
        }
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

    assert( tr_bencIsList( list ) );
    assert( list->val.l.count < list->val.l.alloc );

    item = &list->val.l.vals[list->val.l.count];
    list->val.l.count++;
    tr_bencInit( item, TYPE_INT );

    return item;
}

benc_val_t * tr_bencDictAdd( benc_val_t * dict, const char * key )
{
    benc_val_t * keyval, * itemval;

    assert( tr_bencIsDict( dict ) );
    assert( dict->val.l.count + 2 <= dict->val.l.alloc );

    keyval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInitStr( keyval, (char*)key, -1, 1 );

    itemval = dict->val.l.vals + dict->val.l.count++;
    tr_bencInit( itemval, TYPE_INT );

    return itemval;
}

struct KeyIndex
{
    const char * key;
    int index;
};

static int compareKeyIndex( const void * va, const void * vb )
{
    const struct KeyIndex * a = va;
    const struct KeyIndex * b = vb;
    return strcmp( a->key, b->key );
}

static void
saveImpl( struct evbuffer * out, const benc_val_t * val )
{
    int ii;

    switch( val->type )
    {
        case TYPE_INT:
            evbuffer_add_printf( out, "i%"PRId64"e", val->val.i );
            break;

        case TYPE_STR:
            evbuffer_add_printf( out, "%i:%s", (int)val->val.i, val->val.s.s );
            break;

        case TYPE_LIST:
            evbuffer_add_printf( out, "l" );
            for( ii = 0; val->val.l.count > ii; ii++ )
                saveImpl( out, val->val.l.vals + ii );
            evbuffer_add_printf( out, "e" );
            break;

        case TYPE_DICT:
            /* Keys must be strings and appear in sorted order
               (sorted as raw strings, not alphanumerics). */
            evbuffer_add_printf( out, "d" );
            if( 1 ) {
                int i;
                struct KeyIndex * indices = tr_new( struct KeyIndex, val->val.l.count );
                for( ii=i=0; i<val->val.l.count; i+=2 ) {
                    indices[ii].key = val->val.l.vals[i].val.s.s;
                    indices[ii].index = i;
                    ii++;
                }
                qsort( indices, ii, sizeof(struct KeyIndex), compareKeyIndex );
                for( i=0; i<ii; ++i ) {
                    const int index = indices[i].index;
                    saveImpl( out, val->val.l.vals + index );
                    saveImpl( out, val->val.l.vals + index + 1 );
                }
                tr_free( indices );
            }
            evbuffer_add_printf( out, "e" );
            break;
    }
}

char*
tr_bencSave( const benc_val_t * val, int * len )
{
    struct evbuffer * buf = evbuffer_new( );
    char * ret;
    saveImpl( buf, val );
    if( len != NULL )
        *len = EVBUFFER_LENGTH( buf );
    ret = tr_strndup( (char*) EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
    evbuffer_free( buf );
    return ret;
}

/**
***
**/

int
tr_bencIsStr ( const benc_val_t * val )
{
    return val!=NULL && val->type==TYPE_STR;
}

int
tr_bencIsInt ( const benc_val_t * val )
{
    return val!=NULL && val->type==TYPE_INT;
}

int
tr_bencIsList( const benc_val_t * val )
{
    return val!=NULL && val->type==TYPE_LIST;
}

int
tr_bencIsDict( const benc_val_t * val )
{
    return val!=NULL && val->type==TYPE_DICT;
}
