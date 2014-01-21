/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_LIST_H
#define TR_LIST_H

/**
 * @addtogroup utils Utilities
 * @{
 */

#include "transmission.h" /* inline */

/** @brief simple list structure similar to glib's GList */
typedef struct tr_list
{
    void *  data;
    struct tr_list  * next;
    struct tr_list  * prev;
}
tr_list;

typedef int (*TrListCompareFunc)(const void * a, const void * b);
typedef void (*TrListForeachFunc)(void *);

/**
 * @brief return the number of items in the list
 * @return the number of items in the list
 */
int      tr_list_size (const tr_list * list);

/**
 * @brief free the specified list and set its pointer to NULL
 * @param list pointer to the list to be freed
 * @param func optional function to invoke on each item in the list
 */
void     tr_list_free (tr_list ** list, TrListForeachFunc data_free_func);

/**
 * @brief append an item to the specified list
 * @param list pointer to the list
 * @param item the item to append
 */
void tr_list_append (tr_list ** list, void * data);

/**
 * @brief prepend an item to the specified list
 * @param list pointer to the list
 * @param item the item to prepend
 */
void tr_list_prepend (tr_list ** list, void * data);

/**
 * @brief remove the next item in the list
 * @return the next item in the list, or NULL if the list is empty
 * @param list pointer to the list
 */
void* tr_list_pop_front (tr_list ** list);

/**
 * @brief remove the list's node that contains the specified data pointer
 * @param list pointer to the list
 * @param data data to remove
 * @return the removed data pointer, or NULL if no match was found
 */
void* tr_list_remove_data (tr_list ** list, const void * data);

/**
 * @brief remove the list's node that compares equal to "b" when compared with "compare_func"
 * @param list pointer to the list
 * @param b the comparison key
 * @param compare_func the comparison function. The arguments passed to it will be the list's pointers and the comparison key "b"
 * @return the removed data pointer, or NULL if no match was found
 */
void*    tr_list_remove (tr_list **        list,
                         const void *      b,
                         TrListCompareFunc compare_func);

/**
 * @brief find the list node whose data that compares equal to "b" when compared with "compare_func"
 * @param list pointer to the list
 * @param b the comparison key
 * @param compare_func the comparison function. The arguments passed to it will be the list's pointers and the comparison key "b"
 * @return the matching list node, or NULL if not match was found
 */
tr_list* tr_list_find (tr_list *         list,
                       const void *      b,
                       TrListCompareFunc compare_func);

/**
 * @brief Insert in an ordered list
 * @param list pointer to the list
 * @param item the item to be inserted
 * @param compare the comparison function.
 */
void tr_list_insert_sorted (tr_list          ** list,
                            void              * data,
                            TrListCompareFunc   compare);



/* @} */
#endif /* TR_LIST_H */

