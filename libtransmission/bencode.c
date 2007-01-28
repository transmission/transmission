/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#define LIST_SIZE   20

int _tr_bencLoad( char * buf, int len, benc_val_t * val, char ** end )
{
    char * p, * e, * foo;

    if( 1 >= len )
    {
        return 1;
    }

    if( !end )
    {
        /* So we only have to check once */
        end = &foo;
    }

    val->begin = buf;

    if( buf[0] == 'i' )
    {
        e = memchr( &buf[1], 'e', len - 1 );
        if( NULL == e )
        {
            return 1;
        }

        /* Integer: i1242e */
        val->type  = TYPE_INT;
        *e         = '\0';
        val->val.i = strtoll( &buf[1], &p, 10 );
        *e         = 'e';

        if( p != e )
        {
            return 1;
        }

        val->end = p + 1;
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

        is_dict          = ( buf[0] == 'd' );
        val->type        = is_dict ? TYPE_DICT : TYPE_LIST;
        val->val.l.alloc = LIST_SIZE;
        val->val.l.count = 0;
        val->val.l.vals  = malloc( LIST_SIZE * sizeof( benc_val_t ) );
        cur              = &buf[1];
        str_expected     = 1;
        while( cur - buf < len && cur[0] != 'e' )
        {
            if( val->val.l.count == val->val.l.alloc )
            {
                /* We need a bigger boat */
                val->val.l.alloc += LIST_SIZE;
                val->val.l.vals   =  realloc( val->val.l.vals,
                        val->val.l.alloc  * sizeof( benc_val_t ) );
            }
            if( tr_bencLoad( cur, len - (cur - buf),
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

        val->end = cur + 1;
    }
    else
    {
        e = memchr( buf, ':', len );
        if( NULL == e )
        {
            return 1;
        }

        /* String: 12:whateverword */
        val->type    = TYPE_STR;
        e[0]         = '\0';
        val->val.s.i = strtol( buf, &p, 10 );
        e[0]         = ':';

        if( p != e || 0 > val->val.s.i ||
            val->val.s.i > len - ((p + 1) - buf) )
        {
            return 1;
        }

        val->val.s.s               = malloc( val->val.s.i + 1 );
        val->val.s.s[val->val.s.i] = 0;
        memcpy( val->val.s.s, p + 1, val->val.s.i );

        val->end = p + 1 + val->val.s.i;
    }

    *end = val->end;

    return 0;
}

static void __bencPrint( benc_val_t * val, int space )
{
    int i;

    for( i = 0; i < space; i++ )
    {
        fprintf( stderr, " " );
    }

    switch( val->type )
    {
        case TYPE_INT:
            fprintf( stderr, "int:  %"PRIu64"\n", val->val.i );
            break;

        case TYPE_STR:
            fwrite( val->val.s.s, 1, val->val.s.i, stderr );
            putc( '\n', stderr );
            break;

        case TYPE_LIST:
            fprintf( stderr, "list\n" );
            for( i = 0; i < val->val.l.count; i++ )
                __bencPrint( &val->val.l.vals[i], space + 1 );
            break;

        case TYPE_DICT:
            fprintf( stderr, "dict\n" );
            for( i = 0; i < val->val.l.count; i++ )
                __bencPrint( &val->val.l.vals[i], space + 1 );
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
            if( val->val.s.s )
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

benc_val_t * tr_bencDictFind( benc_val_t * val, char * key )
{
    int i;
    if( val->type != TYPE_DICT )
    {
        return NULL;
    }
    
    for( i = 0; i < val->val.l.count; i += 2 )
    {
        if( !strcmp( val->val.l.vals[i].val.s.s, key ) )
        {
            return &val->val.l.vals[i+1];
        }
    }

    return NULL;
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
            if( tr_sprintf( buf, used, max, "i%"PRIu64"e", val->val.i ) )
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
