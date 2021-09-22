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

static tr_list* recycled_nodes = nullptr;

static tr_lock* getRecycledNodesLock(void)
{
    static tr_lock* l = nullptr;

    if (l == nullptr)
    {
        l = tr_lockNew();
    }

    return l;
}

static tr_list* node_alloc(void)
{
    tr_list* ret = nullptr;
    tr_lock* lock = getRecycledNodesLock();

    tr_lockLock(lock);

    if (recycled_nodes != nullptr)
    {
        ret = recycled_nodes;
        recycled_nodes = recycled_nodes->next;
    }

    tr_lockUnlock(lock);

    if (ret == nullptr)
    {
        ret = tr_new(tr_list, 1);
    }

    *ret = {};
    return ret;
}

static void node_free(tr_list* node)
{
    tr_lock* lock = getRecycledNodesLock();

    if (node != nullptr)
    {
        *node = {};
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
    while (*list != nullptr)
    {
        tr_list* node = *list;
        *list = (*list)->next;

        if (data_free_func != nullptr)
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

    if (*list != nullptr)
    {
        (*list)->prev = node;
    }

    *list = node;
}

void tr_list_append(tr_list** list, void* data)
{
    tr_list* node = node_alloc();

    node->data = data;

    if (*list == nullptr)
    {
        *list = node;
    }
    else
    {
        tr_list* l = *list;

        while (l->next != nullptr)
        {
            l = l->next;
        }

        l->next = node;
        node->prev = l;
    }
}

static tr_list* tr_list_find_data(tr_list* list, void const* data)
{
    for (; list != nullptr; list = list->next)
    {
        if (list->data == data)
        {
            return list;
        }
    }

    return nullptr;
}

static void* tr_list_remove_node(tr_list** list, tr_list* node)
{
    void* data;
    tr_list* prev = node != nullptr ? node->prev : nullptr;
    tr_list* next = node != nullptr ? node->next : nullptr;

    if (prev != nullptr)
    {
        prev->next = next;
    }

    if (next != nullptr)
    {
        next->prev = prev;
    }

    if (*list == node)
    {
        *list = next;
    }

    data = node != nullptr ? node->data : nullptr;
    node_free(node);
    return data;
}

void* tr_list_pop_front(tr_list** list)
{
    void* ret = nullptr;

    if (*list != nullptr)
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
    for (; list != nullptr; list = list->next)
    {
        if (func(list->data, b) == 0)
        {
            return list;
        }
    }

    return nullptr;
}

void tr_list_insert_sorted(tr_list** list, void* data, TrListCompareFunc compare)
{
    /* find the node that we'll insert this data before */
    tr_list* next_node = nullptr;

    for (tr_list* l = *list; l != nullptr; l = l->next)
    {
        int const c = (*compare)(data, l->data);

        if (c <= 0)
        {
            next_node = l;
            break;
        }
    }

    if (next_node == nullptr)
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

    while (list != nullptr)
    {
        ++size;
        list = list->next;
    }

    return size;
}
