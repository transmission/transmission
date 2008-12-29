/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include "transmission.h"
#include "list.h"
#include "utils.h"

/***
****
***/

static tr_list * _unusedNodes = NULL;

static const tr_list TR_LIST_INIT = { NULL, NULL, NULL };

static tr_list*
node_alloc( void )
{
    tr_list * node;

    if( _unusedNodes == NULL )
        node = tr_new( tr_list, 1 );
    else {
        node = _unusedNodes;
        _unusedNodes = node->next;
    }

    *node = TR_LIST_INIT;
    return node;
}

static void
node_free( tr_list* node )
{
    if( node )
    {
        *node = TR_LIST_INIT;
        node->next = _unusedNodes;
        _unusedNodes = node;
    }
}

/***
****
***/

void
tr_list_free( tr_list**         list,
              TrListForeachFunc data_free_func )
{
    while( *list )
    {
        tr_list *node = *list;
        *list = ( *list )->next;
        if( data_free_func )
            data_free_func( node->data );
        node_free( node );
    }
}

void
tr_list_prepend( tr_list ** list,
                 void *     data )
{
    tr_list * node = node_alloc ( );

    node->data = data;
    node->next = *list;
    if( *list )
        ( *list )->prev = node;
    *list = node;
}

void
tr_list_append( tr_list ** list,
                void *     data )
{
    tr_list * node = node_alloc( );

    node->data = data;
    if( !*list )
        *list = node;
    else
    {
        tr_list * l = *list;
        while( l->next )
            l = l->next;

        l->next = node;
        node->prev = l;
    }
}

static tr_list*
tr_list_find_data( tr_list *    list,
                   const void * data )
{
    for( ; list; list = list->next )
        if( list->data == data )
            return list;

    return NULL;
}

static void*
tr_list_remove_node( tr_list ** list,
                     tr_list *  node )
{
    void *    data;
    tr_list * prev = node ? node->prev : NULL;
    tr_list * next = node ? node->next : NULL;

    if( prev ) prev->next = next;
    if( next ) next->prev = prev;
    if( *list == node ) *list = next;
    data = node ? node->data : NULL;
    node_free( node );
    return data;
}

void*
tr_list_pop_front( tr_list ** list )
{
    void * ret = NULL;

    if( *list )
    {
        ret = ( *list )->data;
        tr_list_remove_node( list, *list );
    }
    return ret;
}

void*
tr_list_remove_data( tr_list **   list,
                     const void * data )
{
    return tr_list_remove_node( list, tr_list_find_data( *list, data ) );
}

void*
tr_list_remove( tr_list **        list,
                const void *      b,
                TrListCompareFunc compare_func )
{
    return tr_list_remove_node( list, tr_list_find( *list, b, compare_func ) );
}

tr_list*
tr_list_find( tr_list *         list,
              const void *      b,
              TrListCompareFunc func )
{
    for( ; list; list = list->next )
        if( !func( list->data, b ) )
            return list;

    return NULL;
}

int
tr_list_size( const tr_list * list )
{
    int size = 0;

    while( list )
    {
        ++size;
        list = list->next;
    }

    return size;
}



/*
 * Double-linked list with easy memory management and fast
 * insert/remove operations
 */

void
__tr_list_init( struct __tr_list * head )
{
    head->next = head;
    head->prev = head;
}

void
__tr_list_insert( struct __tr_list * list,
		  struct __tr_list * prev,
		  struct __tr_list * next)
{
    next->prev = list;
    list->next = next;
    list->prev = prev;
    prev->next = list;
}

void
__tr_list_splice( struct __tr_list * prev,
		  struct __tr_list * next)
{
    next->prev = prev;
    prev->next = next;
}

 
void
__tr_list_append( struct __tr_list * head,
		  struct __tr_list * list)
{
    __tr_list_insert( list, head->prev, head );
}

void
__tr_list_remove( struct __tr_list * head )
{
    __tr_list_splice( head->prev, head->next );
    head->next = head->prev = NULL;
}

void
__tr_list_destroy( struct __tr_list * head,
                   __tr_list_free_t   func)
{
    while ( head->next != head )
    {
        struct __tr_list * list = head->next;
        __tr_list_splice( list->prev, list->next );

        func( list );
    }
}
