/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <assert.h>
//#include <string.h> /* memcpy, memcmp, strstr */
//#include <stdlib.h> /* qsort */
#include <stdio.h> /* printf */
//#include <limits.h> /* INT_MAX */
//
#include "JSON_checker.h"

#include "transmission.h"
#include "bencode.h"
#include "ptrarray.h"
#include "utils.h"

struct json_benc_data
{
    tr_benc * top;
    tr_ptrArray * stack;
    char * key;
};

static tr_benc*
getNode( struct json_benc_data * data )
{
    tr_benc * parent;
    tr_benc * node = NULL;

    if( tr_ptrArrayEmpty( data->stack ) )
        parent = NULL;
     else
        parent = tr_ptrArrayBack( data->stack );

    if( !parent )
        node = data->top;
    else if( tr_bencIsList( parent ) )
        node = tr_bencListAdd( parent );
    else if( tr_bencIsDict( parent ) && data->key ) {
        node = tr_bencDictAdd( parent, data->key );
        tr_free( data->key );
        data->key = NULL;
    }

    return node;
}

static int
callback( void * vdata, int type, const struct JSON_value_struct * value )
{
    struct json_benc_data * data = vdata;
    tr_benc * node;

    switch( type )
    {
        case JSON_T_ARRAY_BEGIN:
            node = getNode( data );
            tr_bencInitList( node, 0 );
            tr_ptrArrayAppend( data->stack, node ); 
            break;

        case JSON_T_ARRAY_END:
            tr_ptrArrayPop( data->stack );
            break;

        case JSON_T_OBJECT_BEGIN:
            node = getNode( data );
            tr_bencInitDict( node, 0 );
            tr_ptrArrayAppend( data->stack, node ); 
            break;

        case JSON_T_OBJECT_END:
            tr_ptrArrayPop( data->stack );
            break;

        case JSON_T_FLOAT: {
            char buf[128];
            snprintf( buf, sizeof( buf ), "%f", (double)value->float_value );
            tr_bencInitStrDup( getNode( data ), buf );
            break;
        }

        case JSON_T_NULL:
            break;

        case JSON_T_INTEGER:
            tr_bencInitInt( getNode( data ), value->integer_value );
            break;

        case JSON_T_TRUE:
            tr_bencInitInt( getNode( data ), 1 );
            break;

        case JSON_T_FALSE:
            tr_bencInitInt( getNode( data ), 0 );
            break;

        case JSON_T_STRING:
            tr_bencInitStrDup( getNode( data ), value->string_value );
            break;

        case JSON_T_KEY:
            assert( !data->key );
            data->key = tr_strdup( value->string_value );
            break;
    }

    return 1;
}

int
tr_jsonParse( const void      * vbuf,
              const void      * bufend,
              tr_benc         * setme_benc,
              const uint8_t  ** setme_end )
{
    int err = 0;
    const char * buf = vbuf;
    struct JSON_checker_struct * checker;
    struct json_benc_data data;

    data.key = NULL;
    data.top = setme_benc;
    data.stack = tr_ptrArrayNew( );

    checker = new_JSON_checker( -1, callback, &data, 0 );
    while( ( buf != bufend ) && JSON_checker_char( checker, *buf ) )
        ++buf;
    if( buf != bufend )
        err = TR_ERROR;

    if( setme_end )
        *setme_end = (const uint8_t*) buf;

    tr_ptrArrayFree( data.stack, NULL );
    return err;
}
