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

#include <assert.h>
#include <ctype.h>
#include <errno.h>
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
    tr_bool        hasContent;
    tr_benc      * top;
    tr_ptrArray    stack;
    char         * key;
};

static tr_benc*
getNode( struct json_benc_data * data )
{
    tr_benc * parent;
    tr_benc * node = NULL;

    if( tr_ptrArrayEmpty( &data->stack ) )
        parent = NULL;
    else
        parent = tr_ptrArrayBack( &data->stack );

    if( !parent )
        node = data->top;
    else if( tr_bencIsList( parent ) )
        node = tr_bencListAdd( parent );
    else if( tr_bencIsDict( parent ) && data->key )
    {
        node = tr_bencDictAdd( parent, data->key );
        tr_free( data->key );
        data->key = NULL;
    }

    return node;
}

static int
callback( void *             vdata,
          int                type,
          const JSON_value * value )
{
    struct json_benc_data * data = vdata;
    tr_benc *               node;

    switch( type )
    {
        case JSON_T_ARRAY_BEGIN:
            data->hasContent = TRUE;
            node = getNode( data );
            tr_bencInitList( node, 0 );
            tr_ptrArrayAppend( &data->stack, node );
            break;

        case JSON_T_ARRAY_END:
            tr_ptrArrayPop( &data->stack );
            break;

        case JSON_T_OBJECT_BEGIN:
            data->hasContent = TRUE;
            node = getNode( data );
            tr_bencInitDict( node, 0 );
            tr_ptrArrayAppend( &data->stack, node );
            break;

        case JSON_T_OBJECT_END:
            tr_ptrArrayPop( &data->stack );
            break;

        case JSON_T_FLOAT:
            data->hasContent = TRUE;
            tr_bencInitReal( getNode( data ), (double)value->vu.float_value );
            break;

        case JSON_T_NULL:
            data->hasContent = TRUE;
            tr_bencInitStr( getNode( data ), "", 0 );
            break;

        case JSON_T_INTEGER:
            data->hasContent = TRUE;
            tr_bencInitInt( getNode( data ), value->vu.integer_value );
            break;

        case JSON_T_TRUE:
            data->hasContent = TRUE;
            tr_bencInitBool( getNode( data ), 1 );
            break;

        case JSON_T_FALSE:
            data->hasContent = TRUE;
            tr_bencInitBool( getNode( data ), 0 );
            break;

        case JSON_T_STRING:
            data->hasContent = TRUE;
            tr_bencInitStr( getNode( data ),
                            value->vu.str.value,
                            value->vu.str.length );
            break;

        case JSON_T_KEY:
            data->hasContent = TRUE;
            assert( !data->key );
            data->key = tr_strdup( value->vu.str.value );
            break;
    }

    return 1;
}

int
tr_jsonParse( const char     * source,
              const void     * vbuf,
              size_t           len,
              tr_benc        * setme_benc,
              const uint8_t ** setme_end )
{
    int                         line = 1;
    int                         column = 1;
    int                         err = 0;
    const unsigned char       * buf = vbuf;
    const void                * bufend = buf + len;
    JSON_config                 config;
    struct JSON_parser_struct * checker;
    struct json_benc_data       data;

    init_JSON_config( &config );
    config.callback = callback;
    config.callback_ctx = &data;
    config.depth = -1;

    data.hasContent = FALSE;
    data.key = NULL;
    data.top = setme_benc;
    data.stack = TR_PTR_ARRAY_INIT;

    checker = new_JSON_parser( &config );
    while( ( buf != bufend ) && JSON_parser_char( checker, *buf ) ) {
        if( *buf != '\n' )
            ++column;
        else {
            ++line;
            column = 1;
        }
        ++buf;
    }

    if( buf != bufend ) {
        if( source )
            tr_err( "JSON parser failed in %s at line %d, column %d: \"%.16s\"", source, line, column, buf );
        else
            tr_err( "JSON parser failed at line %d, column %d: \"%.16s\"", line, column, buf );
        err = EILSEQ;
    }

    if( !data.hasContent )
        err = EINVAL;

    if( setme_end )
        *setme_end = (const uint8_t*) buf;

    delete_JSON_parser( checker );
    tr_ptrArrayDestruct( &data.stack, NULL );
    return err;
}

