/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h> /* printf */

#include <event.h> /* evbuffer */

#include "JSON_parser.h"

#include "transmission.h"
#include "bencode.h"
#include "json.h"
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
callback( void * vdata, int type, const JSON_value * value )
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
            tr_snprintf( buf, sizeof( buf ), "%f", (double)value->vu.float_value );
            tr_bencInitStr( getNode( data ), buf, -1 );
            break;
        }

        case JSON_T_NULL:
            break;

        case JSON_T_INTEGER:
            tr_bencInitInt( getNode( data ), value->vu.integer_value );
            break;

        case JSON_T_TRUE:
            tr_bencInitInt( getNode( data ), 1 );
            break;

        case JSON_T_FALSE:
            tr_bencInitInt( getNode( data ), 0 );
            break;

        case JSON_T_STRING:
            tr_bencInitStr( getNode( data ), value->vu.str.value, value->vu.str.length );
            break;

        case JSON_T_KEY:
            assert( !data->key );
            data->key = tr_strdup( value->vu.str.value );
            break;
    }

    return 1;
}

int
tr_jsonParse( const void      * vbuf,
              size_t            len,
              tr_benc         * setme_benc,
              const uint8_t  ** setme_end )
{
    int err = 0;
    const unsigned char * buf = vbuf;
    const void * bufend = buf + len;
    struct JSON_config_struct config;
    struct JSON_parser_struct * checker;
    struct json_benc_data data;

    init_JSON_config( &config );
    config.callback = callback;
    config.callback_ctx = &data;
    config.depth = -1;

    data.key = NULL;
    data.top = setme_benc;
    data.stack = tr_ptrArrayNew( );

    checker = new_JSON_parser( &config );
    while( ( buf != bufend ) && JSON_parser_char( checker, *buf ) )
        ++buf;
    if( buf != bufend )
        err = TR_ERROR;

    if( setme_end )
        *setme_end = (const uint8_t*) buf;

    delete_JSON_parser( checker );
    tr_ptrArrayFree( data.stack, NULL );
    return err;
}
