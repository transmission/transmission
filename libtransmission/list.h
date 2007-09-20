/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_LIST_H
#define TR_LIST_H

typedef struct tr_list
{
    void            * data;
    struct tr_list  * next;
    struct tr_list  * prev;
}
tr_list;

typedef int (*TrListCompareFunc)(const void * a, const void * b);
typedef void (*TrListForeachFunc)(void *);

int         tr_list_size           ( const tr_list    * list );

void        tr_list_free           ( tr_list         ** list );

void        tr_list_append         ( tr_list         ** list,
                                     void             * data );

void        tr_list_prepend        ( tr_list         ** list,
                                     void             * data );

void*       tr_list_pop_front      ( tr_list         ** list );

void*       tr_list_remove_data    ( tr_list         ** list,
                                     const void       * data );

void*       tr_list_remove         ( tr_list         ** list,
                                     const void       * b,
                                     TrListCompareFunc  compare_func );

void        tr_list_insert_sorted  ( tr_list         ** list,
                                     void             * data,
                                     TrListCompareFunc  compare_func );

tr_list*  tr_list_find           ( tr_list          * list,
                                   const void       * b,
                                   TrListCompareFunc  compare_func );

tr_list*  tr_list_find_data      ( tr_list          * list,
                                   const void       * data );

void        tr_list_foreach      ( tr_list          * list,
                                   TrListForeachFunc  foreach_func );

#endif /* TR_LIST_H */

