/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "list.h"
#include "platform.h"
#include "utils.h"

static tr_list const TR_LIST_CLEAR =
{
    .data = NULL,
    .next = NULL,
    .prev = NULL
};

static tr_list* recycled_nodes = NULL;

static tr_lock* getRecycledNodesLock(void)
{
    static tr_lock* l = NULL;

    if (l == NULL)
    {
        l = tr_lockNew();
    }

    return l;
}

static tr_list* node_alloc(void)
{
    tr_list* ret = NULL;
    tr_lock* lock = getRecycledNodesLock();

    tr_lockLock(lock);

    if (recycled_nodes != NULL)
    {
        ret = recycled_nodes;
        recycled_nodes = recycled_nodes->next;
    }

    tr_lockUnlock(lock);

    if (ret == NULL)
    {
        ret = tr_new(tr_list, 1);
    }

    *ret = TR_LIST_CLEAR;
    return ret;
}

static void node_free(tr_list* node)
{
    tr_lock* lock = getRecycledNodesLock();

    if (node != NULL)
    {
        *node = TR_LIST_CLEAR;
        tr_lockLock(lock);
        node->next = recycled_nodes;
        recycled_nodes = node;
        tr_lockUnlock(lock);
    }
}

/***
****
***/

void tr_list_free(tr_list** list, TrListForeachFunc data_free_func)
{
    while (*list != NULL)
    {
        tr_list* node = *list;
        *list = (*list)->next;

        if (data_free_func != NULL)
        {
            data_free_func(node->data);
        }

        node_free(node);
    }
}

void tr_list_prepend(tr_list** list, void* data)
{
    tr_list* node = node_alloc();

    node->data = data;
    node->next = *list;

    if (*list != NULL)
    {
        (*list)->prev = node;
    }

    *list = node;
}

void tr_list_append(tr_list** list, void* data)
{
    tr_list* node = node_alloc();

    node->data = data;

    if (*list == NULL)
    {
        *list = node;
    }
    else
    {
        tr_list* l = *list;

        while (l->next != NULL)
        {
            l = l->next;
        }

        l->next = node;
        node->prev = l;
    }
}

static tr_list* tr_list_find_data(tr_list* list, void const* data)
{
    for (; list != NULL; list = list->next)
    {
        if (list->data == data)
        {
            return list;
        }
    }

    return NULL;
}

static void* tr_list_remove_node(tr_list** list, tr_list* node)
{
    void* data;
    tr_list* prev = node != NULL ? node->prev : NULL;
    tr_list* next = node != NULL ? node->next : NULL;

    if (prev != NULL)
    {
        prev->next = next;
    }

    if (next != NULL)
    {
        next->prev = prev;
    }

    if (*list == node)
    {
        *list = next;
    }

    data = node != NULL ? node->data : NULL;
    node_free(node);
    return data;
}

void* tr_list_pop_front(tr_list** list)
{
    void* ret = NULL;

    if (*list != NULL)
    {
        ret = (*list)->data;
        tr_list_remove_node(list, *list);
    }

    return ret;
}

void* tr_list_remove_data(tr_list** list, void const* data)
{
    return tr_list_remove_node(list, tr_list_find_data(*list, data));
}

void* tr_list_remove(tr_list** list, void const* b, TrListCompareFunc compare_func)
{
    return tr_list_remove_node(list, tr_list_find(*list, b, compare_func));
}

tr_list* tr_list_find(tr_list* list, void const* b, TrListCompareFunc func)
{
    for (; list != NULL; list = list->next)
    {
        if (func(list->data, b) == 0)
        {
            return list;
        }
    }

    return NULL;
}

void tr_list_insert_sorted(tr_list** list, void* data, TrListCompareFunc compare)
{
    /* find the node that we'll insert this data before */
    tr_list* next_node = NULL;

    for (tr_list* l = *list; l != NULL; l = l->next)
    {
        int const c = (*compare)(data, l->data);

        if (c <= 0)
        {
            next_node = l;
            break;
        }
    }

    if (next_node == NULL)
    {
        tr_list_append(list, data);
    }
    else if (next_node == *list)
    {
        tr_list_prepend(list, data);
    }
    else
    {
        tr_list* node = node_alloc();
        node->data = data;
        node->prev = next_node->prev;
        node->next = next_node;
        node->prev->next = node;
        node->next->prev = node;
    }
}

int tr_list_size(tr_list const* list)
{
    int size = 0;

    while (list != NULL)
    {
        ++size;
        list = list->next;
    }

    return size;
}
