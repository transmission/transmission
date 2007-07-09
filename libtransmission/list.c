/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include "transmission.h"
#include "list.h"
#include "utils.h"

int
tr_list_length( const tr_list_t * list )
{
    int i = 0;
    while( list ) {
        ++i;
        list = list->next;
    }
    return i;
}

tr_list_t*
tr_list_alloc( void )
{
    return tr_new0( tr_list_t, 1 );
}

void
tr_list_free1( tr_list_t* node )
{
    tr_free( node );
}

void
tr_list_free( tr_list_t* list )
{
    while( list )
    {
        tr_list_t * node = list;
        list = list->next;
        tr_list_free1( node );
    }
}

tr_list_t*
tr_list_prepend( tr_list_t * list, void * data )
{
    tr_list_t * node = tr_list_alloc ();
    node->data = data;
    node->next = list;
    if( list )
        list->prev = node;
    return node;
}

tr_list_t*
tr_list_append( tr_list_t * list, void * data )
{
    tr_list_t * node = list;
    tr_list_t * l = tr_list_alloc( );
    l->data = data;
    if( !list )
        return l;
    while( node->next )
        node = node->next;
    node->next = l;
    l->prev = node;
    return list;
}

tr_list_t*
tr_list_find_data ( tr_list_t * list, const void * data )
{
    for(; list; list=list->next )
        if( list->data == data )
            return list;

    return NULL;
}

tr_list_t*
tr_list_remove( tr_list_t * list, const void * data )
{
    tr_list_t * node = tr_list_find_data( list, data );
    tr_list_t * prev = node ? node->prev : NULL;
    tr_list_t * next = node ? node->next : NULL;
    if( prev ) prev->next = next;
    if( next ) next->prev = prev;
    if( list == node ) list = next;
    tr_list_free1( node );
    return list;
}

tr_list_t*
tr_list_find ( tr_list_t * list , TrListCompareFunc func, const void * b )
{
    for( ; list; list=list->next )
        if( !func( list->data, b ) )
            return list;

    return NULL;
}

void
tr_list_foreach( tr_list_t * list, TrListForeachFunc func )
{
    while( list )
    {
        func( list->data );
        list = list->next;
    }
}

