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
#include <ctype.h>
#include <string.h>
#include <stdio.h> /* printf */

#include <event.h> /* evbuffer */

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

    delete_JSON_checker( checker );
    tr_ptrArrayFree( data.stack, NULL );
    return err;
}

/***
**** RISON-to-JSON converter
***/

enum { ESCAPE,
       STRING_BEGIN,
       STRING, ESCAPE_STRING,
       UNQUOTED_STRING, ESCAPE_UNQUOTED_STRING,
       VAL_BEGIN,
       OTHER };

char*
tr_rison2json( const char * str, int rison_len )
{
    struct evbuffer * out = evbuffer_new( );
    int stack[1000], *parents=stack;
    int mode = OTHER;
    char * ret;
    const char * end;

    if( rison_len < 0 )
        end = str + strlen( str );
    else
        end = str + rison_len;

#define IN_OBJECT ((parents!=stack) && (parents[-1]=='}'))

    for( ; str!=end; ++str )
    {
        if( mode == ESCAPE )
        {
            switch( *str )
            {
                case '(': evbuffer_add_printf( out, "[ " ); *parents++ = ']'; break;
                case 't': evbuffer_add_printf( out, " true" ); break;
                case 'f': evbuffer_add_printf( out, " false" ); break;
                case 'n': evbuffer_add_printf( out, " null" ); break;
                default: fprintf( stderr, "invalid escape sequence!\n" ); break;
            }
            mode = OTHER;
        }
        else if( mode == STRING_BEGIN )
        {
            switch( *str )
            {
                case '\'': evbuffer_add_printf( out, "\"" ); mode = STRING; break;
                case ')': evbuffer_add_printf( out, " %c", *--parents ); mode = OTHER; break;
                default: evbuffer_add_printf( out, "\"%c", *str ); mode = UNQUOTED_STRING; break;
            }
        }
        else if( mode == UNQUOTED_STRING )
        {
            switch( *str )
            {
                case '\'': evbuffer_add_printf( out, "\"" ); mode = OTHER; break;
                case ':': evbuffer_add_printf( out, "\": "); mode = VAL_BEGIN; break;
                case '!': mode = ESCAPE_UNQUOTED_STRING; break;
                case ')': if( IN_OBJECT ) { evbuffer_add_printf( out, "\" }"); mode = OTHER; break; }
                case ',': if( IN_OBJECT ) { evbuffer_add_printf( out, "\", "); mode = STRING_BEGIN; break; }
                          /* fallthrough */
                default: evbuffer_add_printf( out, "%c", *str ); break;
            }
        }
        else if( mode == VAL_BEGIN )
        {
            if( *str == '\'' ) { evbuffer_add_printf( out, "\"" ); mode = STRING; }
            else if( isdigit( *str ) ) { evbuffer_add_printf( out, "%c", *str ); mode = OTHER; }
            else { evbuffer_add_printf( out, "\"%c", *str ); mode = UNQUOTED_STRING; }
        }
        else if( mode == STRING )
        {
            switch( *str )
            {
                case '\'': evbuffer_add_printf( out, "\"" ); mode = OTHER; break;
                case '!': mode = ESCAPE_STRING; break;
                default: evbuffer_add_printf( out, "%c", *str ); break;
            }
        }
        else if( mode == ESCAPE_STRING || mode == ESCAPE_UNQUOTED_STRING )
        {
            switch( *str )
            {
                case '!': evbuffer_add_printf( out, "!" ); break;
                case '\'': evbuffer_add_printf( out, "'" ); break;
                default: fprintf( stderr, "invalid string escape sequence\n" ); break;
            }
            if( mode == ESCAPE_UNQUOTED_STRING ) mode = UNQUOTED_STRING;
            if( mode == ESCAPE_STRING ) mode = STRING;
        }
        else
        {
            switch( *str )
            {
                case '(': evbuffer_add_printf( out, "{ " ); mode=STRING_BEGIN; *parents++ = '}'; break;
                case '!': mode = ESCAPE; break;
                case ')': evbuffer_add_printf( out, " %c", *--parents ); break;
                case '\'': evbuffer_add_printf( out, "\"" ); mode = STRING; break;
                case ':': if( IN_OBJECT ) {
                              evbuffer_add_printf( out, ": " ); mode = VAL_BEGIN;
                          } else {
                              evbuffer_add_printf( out, "%c", *str );
                          }
                          break;
                case ',': if( IN_OBJECT ) {
                              evbuffer_add_printf( out, ", " ); mode = STRING_BEGIN;
                          } else {
                              evbuffer_add_printf( out, "%c", *str );
                          }
                          break;
                default: evbuffer_add_printf( out, "%c", *str ); break;
            }
        }
    }

    ret = tr_strdup( (char*) EVBUFFER_DATA( out ) );
    evbuffer_free( out );
    return ret;
}
