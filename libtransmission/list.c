/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include "transmission.h"
#include "list.h"
#include "platform.h"
#include "utils.h"

static const tr_list TR_LIST_CLEAR = { NULL, NULL, NULL };

static tr_list * recycled_nodes = NULL;

static tr_lock*
getRecycledNodesLock (void)
{
  static tr_lock * l = NULL;

  if (!l)
    l = tr_lockNew ();

  return l;
}

static tr_list*
node_alloc (void)
{
  tr_list * ret = NULL;
  tr_lock * lock = getRecycledNodesLock ();

  tr_lockLock (lock);

  if (recycled_nodes != NULL)
    {
      ret = recycled_nodes;
      recycled_nodes = recycled_nodes->next;
    }

  tr_lockUnlock (lock);

  if (ret == NULL)
    {
      ret = tr_new (tr_list, 1);
    }

  *ret = TR_LIST_CLEAR;
  return ret;
}

static void
node_free (tr_list* node)
{
  tr_lock * lock = getRecycledNodesLock ();

  if (node != NULL)
    {
      *node = TR_LIST_CLEAR;
      tr_lockLock (lock);
      node->next = recycled_nodes;
      recycled_nodes = node;
      tr_lockUnlock (lock);
    }
}

/***
****
***/

void
tr_list_free (tr_list**         list,
              TrListForeachFunc data_free_func)
{
  while (*list)
    {
      tr_list *node = *list;
      *list = (*list)->next;
      if (data_free_func)
        data_free_func (node->data);
      node_free (node);
    }
}

void
tr_list_prepend (tr_list ** list,
                 void *     data)
{
  tr_list * node = node_alloc ();

  node->data = data;
  node->next = *list;
  if (*list)
    (*list)->prev = node;
  *list = node;
}

void
tr_list_append (tr_list ** list,
                void *     data)
{
  tr_list * node = node_alloc ();

  node->data = data;

  if (!*list)
    {
      *list = node;
    }
  else
    {
      tr_list * l = *list;

      while (l->next)
        l = l->next;

      l->next = node;
      node->prev = l;
    }
}

static tr_list*
tr_list_find_data (tr_list *    list,
                   const void * data)
{
  for (; list; list = list->next)
    if (list->data == data)
      return list;

  return NULL;
}

static void*
tr_list_remove_node (tr_list ** list,
                     tr_list *  node)
{
  void *    data;
  tr_list * prev = node ? node->prev : NULL;
  tr_list * next = node ? node->next : NULL;

  if (prev) prev->next = next;
  if (next) next->prev = prev;
  if (*list == node) *list = next;
  data = node ? node->data : NULL;
  node_free (node);
  return data;
}

void*
tr_list_pop_front (tr_list ** list)
{
  void * ret = NULL;

  if (*list)
    {
      ret = (*list)->data;
      tr_list_remove_node (list, *list);
    }

  return ret;
}

void*
tr_list_remove_data (tr_list **   list,
                     const void * data)
{
  return tr_list_remove_node (list, tr_list_find_data (*list, data));
}

void*
tr_list_remove (tr_list **        list,
                const void *      b,
                TrListCompareFunc compare_func)
{
  return tr_list_remove_node (list, tr_list_find (*list, b, compare_func));
}

tr_list*
tr_list_find (tr_list *         list,
              const void *      b,
              TrListCompareFunc func)
{
  for (; list; list = list->next)
    if (!func (list->data, b))
      return list;

  return NULL;
}

void
tr_list_insert_sorted (tr_list            ** list,
                       void                * data,
                       TrListCompareFunc     compare)
{
  /* find l, the node that we'll insert this data before */
  tr_list * l;

  for (l = *list; l != NULL; l = l->next)
    {
      const int c = (compare)(data, l->data);
      if (c <= 0)
        break;
    }

  if (l == NULL)
    {
      tr_list_append (list, data);
    }
  else if (l == *list)
    {
      tr_list_prepend (list, data);
    }
  else
    {
      tr_list * node = node_alloc ();
      node->data = data;
      node->prev = l->prev;
      node->next = l;
      node->prev->next = node;
      node->next->prev = node;
    }
}

int
tr_list_size (const tr_list * list)
{
  int size = 0;

  while (list)
    {
      ++size;
      list = list->next;
    }

  return size;
}
