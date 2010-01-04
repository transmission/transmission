/*
 * This file Copyright (C) 2007-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_LIST_H
#define TR_LIST_H

#include "transmission.h" /* inline */

typedef struct tr_list
{
    void *  data;
    struct tr_list  * next;
    struct tr_list  * prev;
}
tr_list;

typedef int ( *TrListCompareFunc )( const void * a, const void * b );
typedef void ( *TrListForeachFunc )( void * );

int      tr_list_size( const tr_list * list );

void     tr_list_free( tr_list **        list,
                       TrListForeachFunc data_free_func );

void     tr_list_append( tr_list ** list,
                         void *     data );

void     tr_list_prepend( tr_list ** list,
                          void *     data );

void*    tr_list_pop_front( tr_list ** list );

void*    tr_list_remove_data( tr_list **   list,
                              const void * data );

void*    tr_list_remove( tr_list **        list,
                         const void *      b,
                         TrListCompareFunc compare_func );

tr_list* tr_list_find( tr_list *         list,
                       const void *      b,
                       TrListCompareFunc compare_func );


/*
 * Double-linked list with easy memory management and fast
 * insert/remove operations
 */

struct __tr_list
{
    struct __tr_list * next, * prev;
};

/**
 * Given a __tr_list node that's embedded in a struct, returns a pointer to the struct.
 * @param ptr     pointer to the embedded __tr_list
 * @param type    struct type that has contains the __tr_list
 * @param field   the name of the struct's _tr_list field
 */
#define __tr_list_entry(ptr,type,field) ((type*) (((char*)ptr) - offsetof(type,field)))

typedef int  ( *__tr_list_cmp_t ) ( const void * a, const void * b );
typedef void ( *__tr_list_free_t )( void * );


/**
 *    __tr_list_init()
 *
 * Init @head as an empty list.
 */
static inline void
__tr_list_init( struct __tr_list * head )
{
    head->next = head->prev = head;
}


/**
 *    __tr_list_insert()
 *
 * Insert @list between @prev and @next.
 */
void
__tr_list_insert( struct __tr_list * list,
                  struct __tr_list * prev,
                  struct __tr_list * next);

/**
 *    __tr_list_splice()
 *
 * Connect @prev with @next, removing any nodes that were
 * in between.
 */
void
__tr_list_splice( struct __tr_list * prev,
                  struct __tr_list * next);

/**
 *    __tr_list_append()
 *
 * Append @list to the end of @head.
 */
static inline void
__tr_list_append( struct __tr_list * head,
                  struct __tr_list * list)
{
    __tr_list_insert( list, head->prev, head );
}


/**
 *    __tr_list_remove()
 *
 * Remove @head from the list it is in.
 */
void
__tr_list_remove( struct __tr_list * head );

/**
 *    __tr_list_destroy()
 *
 * Destroy the list and free all nodes
 */
void
__tr_list_destroy( struct __tr_list * head,
                   __tr_list_free_t   func);

#endif /* TR_LIST_H */

