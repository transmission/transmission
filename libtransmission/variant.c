/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h> /* rename () */
#include <stdlib.h> /* strtoul (), strtod (), realloc (), qsort (), mkstemp () */
#include <string.h>

#ifdef WIN32 /* tr_mkstemp () */
 #include <fcntl.h>
 #define _S_IREAD 256
 #define _S_IWRITE 128
#endif

#include <locale.h> /* setlocale () */
#include <unistd.h> /* write (), unlink () */

#include <event2/buffer.h>

#define __LIBTRANSMISSION_VARIANT_MODULE___
#include "transmission.h"
#include "ConvertUTF.h"  
#include "fdlimit.h" /* tr_close_file () */
#include "platform.h" /* TR_PATH_MAX */
#include "utils.h" /* tr_new (), tr_free () */
#include "variant.h"
#include "variant-common.h"

/**
***
**/

bool
tr_variantIsContainer (const tr_variant * v)
{
  return tr_variantIsList (v) || tr_variantIsDict (v);
}

bool
tr_variantIsSomething (const tr_variant * v)
{
  return tr_variantIsContainer (v)
      || tr_variantIsInt (v)
      || tr_variantIsString (v)
      || tr_variantIsReal (v)
      || tr_variantIsBool (v);
}

void
tr_variantInit (tr_variant * v, char type)
{
  memset (v, 0, sizeof (*v));
  v->type = type;
}

/***
****
***/

/* returns true if the variant's string was malloced.
 * this occurs when the string is too long for our string buffer */
static inline int
stringIsAlloced (const tr_variant * variant)
{
  return variant->val.s.len >= sizeof (variant->val.s.str.buf);
}

/* returns a const pointer to the variant's string */
static inline const char*
getStr (const tr_variant* variant)
{
  return stringIsAlloced (variant) ? variant->val.s.str.ptr
                                   : variant->val.s.str.buf;
}

static int
dictIndexOf (const tr_variant * dict, const char * key)
{
  if (tr_variantIsDict (dict))
    {
      size_t i;
      const size_t len = strlen (key);

      for (i=0; (i+1) < dict->val.l.count; i += 2)
        {
          const tr_variant * child = dict->val.l.vals + i;
          if ((child->type == TR_VARIANT_TYPE_STR) 
              && (child->val.s.len == len)
              && !memcmp (getStr (child), key, len))
            return i;
        }
    }

  return -1;
}

tr_variant *
tr_variantDictFind (tr_variant * dict,
                    const char * key)
{
  const int i = dictIndexOf (dict, key);

  return i < 0 ? NULL : &dict->val.l.vals[i + 1];
}

static bool
tr_variantDictFindType (tr_variant   * dict,
                        const char   * key,
                        int            type,
                        tr_variant  ** setme)
{
  return tr_variantIsType (*setme = tr_variantDictFind (dict, key), type);
}

size_t
tr_variantListSize (const tr_variant * list)
{
  return tr_variantIsList (list) ? list->val.l.count : 0;
}

tr_variant*
tr_variantListChild (tr_variant * val,
                  size_t    i)
{
  tr_variant * ret = NULL;

  if (tr_variantIsList (val) && (i < val->val.l.count))
    ret = val->val.l.vals + i;

  return ret;
}

int
tr_variantListRemove (tr_variant * list, size_t i)
{
  if (tr_variantIsList (list) && (i < list->val.l.count))
    {
      tr_variantFree (&list->val.l.vals[i]);
      tr_removeElementFromArray (list->val.l.vals, i,
                                 sizeof (tr_variant),
                                 list->val.l.count--);
      return 1;
    }

  return 0;
}

static void
tr_variant_warning (const char * err)
{
  fprintf (stderr, "warning: %s\n", err);
}

bool
tr_variantGetInt (const tr_variant  * val,
                  int64_t           * setme)
{
  bool success = false;

  if (!success && ((success = tr_variantIsInt (val))))
    if (setme)
      *setme = val->val.i;

  if (!success && ((success = tr_variantIsBool (val))))
    {
      tr_variant_warning ("reading bool as an int");
      if (setme)
        *setme = val->val.b ? 1 : 0;
    }

  return success;
}

bool
tr_variantGetStr (const tr_variant   * val,
                  const char        ** setme,
                  size_t             * len)
{
  const bool success = tr_variantIsString (val);

  if (success)
    *setme = getStr (val);

  if (len != NULL)
    *len = success ? val->val.s.len : 0;

  return success;
}

bool
tr_variantGetRaw (const tr_variant   * val,
                  const uint8_t     ** setme_raw,
                  size_t             * setme_len)
{
  const bool success = tr_variantIsString (val);

  if (success)
    {
      *setme_raw = (uint8_t*) getStr (val);
      *setme_len = val->val.s.len;
    }

  return success;
}

bool
tr_variantGetBool (const tr_variant * val, bool * setme)
{
  const char * str;
  bool success = false;

  if ((success = tr_variantIsBool (val)))
    *setme = val->val.b;

  if (!success && tr_variantIsInt (val))
    if ((success = (val->val.i==0 || val->val.i==1)))
      *setme = val->val.i!=0;

  if (!success && tr_variantGetStr (val, &str, NULL))
    if ((success = (!strcmp (str,"true") || !strcmp (str,"false"))))
      *setme = !strcmp (str,"true");

  return success;
}

bool
tr_variantGetReal (const tr_variant * val, double * setme)
{
  bool success = false;

  if (!success && ((success = tr_variantIsReal (val))))
    *setme = val->val.d;

  if (!success && ((success = tr_variantIsInt (val))))
    *setme = val->val.i;

  if (!success && tr_variantIsString (val))
    {
      char * endptr;
      char locale[128];
      double d;

      /* the json spec requires a '.' decimal point regardless of locale */
      tr_strlcpy (locale, setlocale (LC_NUMERIC, NULL), sizeof (locale));
      setlocale (LC_NUMERIC, "POSIX");
      d  = strtod (getStr (val), &endptr);
      setlocale (LC_NUMERIC, locale);

      if ((success = (getStr (val) != endptr) && !*endptr))
        *setme = d;
    }

  return success;
}

bool
tr_variantDictFindInt (tr_variant * dict,
                       const char * key,
                       int64_t    * setme)
{
  tr_variant * child = tr_variantDictFind (dict, key);
  return tr_variantGetInt (child, setme);
}

bool
tr_variantDictFindBool (tr_variant * dict,
                        const char * key,
                        bool       * setme)
{
  tr_variant * child = tr_variantDictFind (dict, key);
  return tr_variantGetBool (child, setme);
}

bool
tr_variantDictFindReal (tr_variant * dict,
                       const char  * key,
                       double      * setme)
{
  tr_variant * child = tr_variantDictFind (dict, key);
  return tr_variantGetReal (child, setme);
}

bool
tr_variantDictFindStr (tr_variant  * dict,
                       const char  * key,
                       const char ** setme,
                       size_t      * len)
{
  tr_variant * child = tr_variantDictFind (dict, key);
  return tr_variantGetStr (child, setme, len);
}

bool
tr_variantDictFindList (tr_variant   * dict,
                        const char   * key,
                        tr_variant  ** setme)
{
  return tr_variantDictFindType (dict,
                                 key,
                                 TR_VARIANT_TYPE_LIST,
                                 setme);
}

bool
tr_variantDictFindDict (tr_variant   * dict,
                        const char   * key,
                        tr_variant  ** setme)
{
  return tr_variantDictFindType (dict,
                                 key,
                                 TR_VARIANT_TYPE_DICT,
                                 setme);
}

bool
tr_variantDictFindRaw (tr_variant      * dict,
                       const char      * key,
                       const uint8_t  ** setme_raw,
                       size_t          * setme_len)
{
  tr_variant * child = tr_variantDictFind (dict, key);
  return tr_variantGetRaw (child, setme_raw, setme_len);
}

/***
****
***/

void
tr_variantInitRaw (tr_variant * variant, const void * src, size_t byteCount)
{
  char * setme;
  tr_variantInit (variant, TR_VARIANT_TYPE_STR);

  /* There's no way in benc notation to distinguish between
   * zero-terminated strings and raw byte arrays.
   * Because of this, tr_variantMergeDicts () and tr_variantListCopy ()
   * don't know whether or not a TR_VARIANT_TYPE_STR node needs a '\0'.
   * Append one, een to the raw arrays, just to be safe. */

  if (byteCount < sizeof (variant->val.s.str.buf))
    setme = variant->val.s.str.buf;
  else
    setme = variant->val.s.str.ptr = tr_new (char, byteCount + 1);

  memcpy (setme, src, byteCount);
  setme[byteCount] = '\0';
  variant->val.s.len = byteCount;
}

void
tr_variantInitStr (tr_variant * variant, const void * str, int len)
{
  if (str == NULL)
    len = 0;
  else if (len < 0)
    len = strlen (str);

  tr_variantInitRaw (variant, str, len);
}

void
tr_variantInitBool (tr_variant * variant, bool value)
{
  tr_variantInit (variant, TR_VARIANT_TYPE_BOOL);
  variant->val.b = value != 0;
}

void
tr_variantInitReal (tr_variant * b, double value)
{
  tr_variantInit (b, TR_VARIANT_TYPE_REAL);
  b->val.d = value;
}

void
tr_variantInitInt (tr_variant * variant, int64_t value)
{
  tr_variantInit (variant, TR_VARIANT_TYPE_INT);
  variant->val.i = value;
}

int
tr_variantInitList (tr_variant * variant, size_t reserve_count)
{
  tr_variantInit (variant, TR_VARIANT_TYPE_LIST);
  return tr_variantListReserve (variant, reserve_count);
}

static int
containerReserve (tr_variant * container, size_t count)
{
  const size_t needed = container->val.l.count + count;

  assert (tr_variantIsContainer (container));

  if (needed > container->val.l.alloc)
    {
      size_t n;
      void * tmp;

      /* scale the alloc size in powers-of-2 */
      n = container->val.l.alloc ? container->val.l.alloc : 8;
      while (n < needed)
        n *= 2u;

      tmp = realloc (container->val.l.vals, n * sizeof (tr_variant));
      if (tmp == NULL)
        return 1;

      container->val.l.alloc = n;
      container->val.l.vals = tmp;
    }

  return 0;
}

int
tr_variantListReserve (tr_variant * list, size_t count)
{
  assert (tr_variantIsList (list));
  return containerReserve (list, count);
}

int
tr_variantInitDict (tr_variant * variant, size_t reserve_count)
{
  tr_variantInit (variant, TR_VARIANT_TYPE_DICT);
  return tr_variantDictReserve (variant, reserve_count);
}

int
tr_variantDictReserve (tr_variant  * dict,
                       size_t        reserve_count)
{
  assert (tr_variantIsDict (dict));
  return containerReserve (dict, reserve_count * 2);
}

tr_variant *
tr_variantListAdd (tr_variant * list)
{
  tr_variant * child;

  assert (tr_variantIsList (list));

  containerReserve (list, 1);
  child = &list->val.l.vals[list->val.l.count++];
  tr_variantInit (child, TR_VARIANT_TYPE_INT);

  return child;
}

tr_variant *
tr_variantListAddInt (tr_variant  * list,
                      int64_t       val)
{
  tr_variant * child = tr_variantListAdd (list);
  tr_variantInitInt (child, val);
  return child;
}

tr_variant *
tr_variantListAddReal (tr_variant  * list,
                       double        val)
{
  tr_variant * child = tr_variantListAdd (list);
  tr_variantInitReal (child, val);
  return child;
}

tr_variant *
tr_variantListAddBool (tr_variant  * list,
                       bool          val)
{
  tr_variant * child = tr_variantListAdd (list);
  tr_variantInitBool (child, val);
  return child;
}

tr_variant *
tr_variantListAddStr (tr_variant  * list,
                      const char  * val)
{
  tr_variant * child = tr_variantListAdd (list);
  tr_variantInitStr (child, val, -1);
  return child;
}

tr_variant *
tr_variantListAddRaw (tr_variant  * list,
                      const void  * val,
                      size_t        len)
{
  tr_variant * child = tr_variantListAdd (list);
  tr_variantInitRaw (child, val, len);
  return child;
}

tr_variant*
tr_variantListAddList (tr_variant  * list,
                       size_t        reserve_count)
{
  tr_variant * child = tr_variantListAdd (list);
  tr_variantInitList (child, reserve_count);
  return child;
}

tr_variant*
tr_variantListAddDict (tr_variant  * list,
                       size_t        reserve_count)
{
  tr_variant * child = tr_variantListAdd (list);
  tr_variantInitDict (child, reserve_count);
  return child;
}

tr_variant *
tr_variantDictAdd (tr_variant  * dict,
                   const char  * key)
{
  tr_variant * child_key;
  tr_variant * child_val;

  assert (tr_variantIsDict (dict));

  containerReserve (dict, 2);

  child_key = dict->val.l.vals + dict->val.l.count++;
  tr_variantInitStr (child_key, key, -1);

  child_val = dict->val.l.vals + dict->val.l.count++;
  tr_variantInit (child_val, TR_VARIANT_TYPE_INT);
  return child_val;
}

static tr_variant*
dictFindOrAdd (tr_variant * dict, const char * key, int type)
{
  tr_variant * child;

  /* see if it already exists, and if so, try to reuse it */
  if ((child = tr_variantDictFind (dict, key)))
    {
      if (!tr_variantIsType (child, type))
        {
          tr_variantDictRemove (dict, key);
          child = NULL;
        }
    }

  /* if it doesn't exist, create it */
  if (child == NULL)
    child = tr_variantDictAdd (dict, key);

  return child;
}

tr_variant*
tr_variantDictAddInt (tr_variant * dict,
                      const char * key,
                      int64_t      val)
{
  tr_variant * child = dictFindOrAdd (dict, key, TR_VARIANT_TYPE_INT);
  tr_variantInitInt (child, val);
  return child;
}

tr_variant*
tr_variantDictAddBool (tr_variant * dict, const char * key, bool val)
{
  tr_variant * child = dictFindOrAdd (dict, key, TR_VARIANT_TYPE_BOOL);
  tr_variantInitBool (child, val);
  return child;
}

tr_variant*
tr_variantDictAddReal (tr_variant * dict, const char * key, double val)
{
  tr_variant * child = dictFindOrAdd (dict, key, TR_VARIANT_TYPE_REAL);
  tr_variantInitReal (child, val);
  return child;
}

static tr_variant *
dictRecycleOrAdd (tr_variant * dict, const char * key)
{
  tr_variant * child;

  /* see if it already exists, and if so, try to reuse it */
  if ((child = tr_variantDictFind (dict, key)))
    {
      if (tr_variantIsString (child))
        {
          if (stringIsAlloced (child))
            tr_free (child->val.s.str.ptr);
        }
      else
        {
          tr_variantDictRemove (dict, key);
          child = NULL;
        }
    }

  /* if it doesn't exist, create it */
  if (child == NULL)
    child = tr_variantDictAdd (dict, key);

  return child;
}


tr_variant*
tr_variantDictAddStr (tr_variant * dict, const char * key, const char * val)
{
  tr_variant * child = dictRecycleOrAdd (dict, key);
  tr_variantInitStr (child, val, -1);
  return child;
}

tr_variant*
tr_variantDictAddRaw (tr_variant * dict,
                      const char * key,
                      const void * src,
                      size_t       len)
{
  tr_variant * child = dictRecycleOrAdd (dict, key);
  tr_variantInitRaw (child, src, len);
  return child;
}

tr_variant*
tr_variantDictAddList (tr_variant * dict,
                       const char * key,
                       size_t       reserve_count)
{
  tr_variant * child = tr_variantDictAdd (dict, key);
  tr_variantInitList (child, reserve_count);
  return child;
}

tr_variant*
tr_variantDictAddDict (tr_variant * dict,
                       const char * key,
                       size_t       reserve_count)
{
  tr_variant * child = tr_variantDictAdd (dict, key);
  tr_variantInitDict (child, reserve_count);
  return child;
}

int
tr_variantDictRemove (tr_variant * dict,
                      const char * key)
{
  const int i = dictIndexOf (dict, key);

  if (i >= 0)
    {
      const int n = dict->val.l.count;
      tr_variantFree (&dict->val.l.vals[i]);
      tr_variantFree (&dict->val.l.vals[i + 1]);
      if (i + 2 < n)
        {
          dict->val.l.vals[i]   = dict->val.l.vals[n - 2];
          dict->val.l.vals[i + 1] = dict->val.l.vals[n - 1];
        }
      dict->val.l.count -= 2;
    }

  return i >= 0; /* return true if found */
}

/***
****  BENC WALKING
***/

struct KeyIndex
{
  const char *  key;
  int           index;
};

static int
compareKeyIndex (const void * va, const void * vb)
{
  const struct KeyIndex * a = va;
  const struct KeyIndex * b = vb;

  return strcmp (a->key, b->key);
}

struct SaveNode
{
  const tr_variant * val;
  int valIsVisited;
  int childCount;
  int childIndex;
  int * children;
};

static void
nodeInitDict (struct SaveNode   * node,
              const tr_variant  * val,
              bool                sort_dicts)
{
  const int n = val->val.l.count;
  const int nKeys = n / 2;

  assert (tr_variantIsDict (val));

  node->val = val;
  node->children = tr_new0 (int, n);

  if (sort_dicts)
    {
      int i, j;
      struct KeyIndex * indices = tr_new (struct KeyIndex, nKeys);

      for (i=j=0; i<n; i+=2, ++j)
        {
          indices[j].key = getStr (&val->val.l.vals[i]);
          indices[j].index = i;
        }

      qsort (indices, j, sizeof (struct KeyIndex), compareKeyIndex);

      for (i=0; i<j; ++i)
        {
          const int index = indices[i].index;
          node->children[node->childCount++] = index;
          node->children[node->childCount++] = index + 1;
        }

      tr_free (indices);
    }
  else
    {
      int i;

      for (i=0; i<n; ++i)
        node->children[node->childCount++] = i;
    }

  assert (node->childCount == n);
}

static void
nodeInitList (struct SaveNode   * node,
              const tr_variant  * val)
{
  int i;
  int n;

  assert (tr_variantIsList (val));

  n = val->val.l.count;
  node->val = val;
  node->childCount = n;
  node->children = tr_new0 (int, n);
  for (i=0; i<n; ++i) /* a list's children don't need to be reordered */
    node->children[i] = i;
}

static void
nodeInitLeaf (struct SaveNode   * node,
              const tr_variant  * variant)
{
  assert (!tr_variantIsContainer (variant));

  node->val = variant;
}

static void
nodeInit (struct SaveNode   * node,
          const tr_variant  * variant,
          bool                sort_dicts)
{
  static const struct SaveNode INIT_NODE = { NULL, 0, 0, 0, NULL };

  *node = INIT_NODE;

  if (tr_variantIsList (variant))
    nodeInitList (node, variant);
  else if (tr_variantIsDict (variant))
    nodeInitDict (node, variant, sort_dicts);
  else
    nodeInitLeaf (node, variant);
}

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted data. (#667)
 */
void
tr_variantWalk (const tr_variant               * top,
                const struct VariantWalkFuncs  * walkFuncs,
                void                           * user_data,
                bool                             sort_dicts)
{
  int stackSize = 0;
  int stackAlloc = 64;
  struct SaveNode * stack = tr_new (struct SaveNode, stackAlloc);

  nodeInit (&stack[stackSize++], top, sort_dicts);

  while (stackSize > 0)
    {
      struct SaveNode * node = &stack[stackSize-1];
      const tr_variant * val;

      if (!node->valIsVisited)
        {
            val = node->val;
            node->valIsVisited = true;
        }
      else if (node->childIndex < node->childCount)
        {
            const int index = node->children[node->childIndex++];
            val = node->val->val.l.vals +  index;
        }
      else /* done with this node */
        {
            if (tr_variantIsContainer (node->val))
                walkFuncs->containerEndFunc (node->val, user_data);
            --stackSize;
            tr_free (node->children);
            continue;
        }

      if (val) switch (val->type)
        {
          case TR_VARIANT_TYPE_INT:
            walkFuncs->intFunc (val, user_data);
            break;

          case TR_VARIANT_TYPE_BOOL:
            walkFuncs->boolFunc (val, user_data);
            break;

          case TR_VARIANT_TYPE_REAL:
            walkFuncs->realFunc (val, user_data);
            break;

          case TR_VARIANT_TYPE_STR:
            walkFuncs->stringFunc (val, user_data);
            break;

          case TR_VARIANT_TYPE_LIST:
            if (val == node->val)
              {
                walkFuncs->listBeginFunc (val, user_data);
              }
            else
              {
                if (stackAlloc == stackSize)
                  {
                    stackAlloc *= 2;
                    stack = tr_renew (struct SaveNode, stack, stackAlloc);
                  }
                nodeInit (&stack[stackSize++], val, sort_dicts);
              }
            break;

          case TR_VARIANT_TYPE_DICT:
            if (val == node->val)
              {
                walkFuncs->dictBeginFunc (val, user_data);
              }
            else
              {
                if (stackAlloc == stackSize)
                  {
                    stackAlloc *= 2;
                    stack = tr_renew (struct SaveNode, stack, stackAlloc);
                  }
                nodeInit (&stack[stackSize++], val, sort_dicts);
              }
            break;

          default:
            /* did caller give us an uninitialized val? */
            tr_err ("%s", _("Invalid metadata"));
            break;
        }
    }

  tr_free (stack);
}

/****
*****
****/

static void
freeDummyFunc (const tr_variant * val UNUSED, void * buf UNUSED)
{}

static void
freeStringFunc (const tr_variant * val, void * unused UNUSED)
{
  if (stringIsAlloced (val))
    tr_free (val->val.s.str.ptr);
}

static void
freeContainerEndFunc (const tr_variant * val, void * unused UNUSED)
{
  tr_free (val->val.l.vals);
}

static const struct VariantWalkFuncs freeWalkFuncs = { freeDummyFunc,
                                                       freeDummyFunc,
                                                       freeDummyFunc,
                                                       freeStringFunc,
                                                       freeDummyFunc,
                                                       freeDummyFunc,
                                                       freeContainerEndFunc };

void
tr_variantFree (tr_variant * v)
{
  if (tr_variantIsSomething (v))
    tr_variantWalk (v, &freeWalkFuncs, NULL, false);
}

/***
****
***/

static void
tr_variantListCopy (tr_variant * target, const tr_variant * src)
{
  int i = 0;
  const tr_variant * val;

  while ((val = tr_variantListChild ((tr_variant*)src, i++)))
    {
      if (tr_variantIsBool (val))
       {
          bool boolVal = 0;
          tr_variantGetBool (val, &boolVal);
          tr_variantListAddBool (target, boolVal);
       }
     else if (tr_variantIsReal (val))
       {
         double realVal = 0;
         tr_variantGetReal (val, &realVal);
         tr_variantListAddReal (target, realVal);
       }
     else if (tr_variantIsInt (val))
       {
         int64_t intVal = 0;
         tr_variantGetInt (val, &intVal);
         tr_variantListAddInt (target, intVal);
       }
     else if (tr_variantIsString (val))
       {
         size_t len;
         const char * str;
         tr_variantGetStr (val, &str, &len);
         tr_variantListAddRaw (target, str, len);
       }
     else if (tr_variantIsDict (val))
       {
         tr_variantMergeDicts (tr_variantListAddDict (target, 0), val);
       }
     else if (tr_variantIsList (val))
       {
         tr_variantListCopy (tr_variantListAddList (target, 0), val);
       }
     else
       {
         tr_err ("tr_variantListCopy skipping item");
       }
   }
}

static size_t
tr_variantDictSize (const tr_variant * dict)
{
  size_t count = 0;

  if (tr_variantIsDict (dict))
    count = dict->val.l.count / 2;

  return count;
}

bool
tr_variantDictChild (tr_variant   * dict,
                     size_t         n,
                     const char  ** key,
                     tr_variant  ** val)
{
  bool success = 0;

  assert (tr_variantIsDict (dict));

  if (tr_variantIsDict (dict) && (n*2)+1 <= dict->val.l.count)
    {
      tr_variant * k = dict->val.l.vals + (n*2);
      tr_variant * v = dict->val.l.vals + (n*2) + 1;
      if ((success = tr_variantGetStr (k, key, NULL) && tr_variantIsSomething (v)))
        *val = v;
    }

  return success;
}

void
tr_variantMergeDicts (tr_variant * target, const tr_variant * source)
{
  size_t i;
  const size_t sourceCount = tr_variantDictSize (source);

  assert (tr_variantIsDict (target));
  assert (tr_variantIsDict (source));

  tr_variantDictReserve (target, sourceCount + tr_variantDictSize(target));

  for (i=0; i<sourceCount; ++i)
    {
      const char * key;
      tr_variant * val;
      tr_variant * t;

      if (tr_variantDictChild ((tr_variant*)source, i, &key, &val))
        {
          if (tr_variantIsBool (val))
            {
              bool boolVal;
              tr_variantGetBool (val, &boolVal);
              tr_variantDictAddBool (target, key, boolVal);
            }
          else if (tr_variantIsReal (val))
            {
              double realVal = 0;
              tr_variantGetReal (val, &realVal);
              tr_variantDictAddReal (target, key, realVal);
            }
          else if (tr_variantIsInt (val))
            {
              int64_t intVal = 0;
              tr_variantGetInt (val, &intVal);
              tr_variantDictAddInt (target, key, intVal);
            }
          else if (tr_variantIsString (val))
            {
              size_t len;
              const char * str;
              tr_variantGetStr (val, &str, &len);
              tr_variantDictAddRaw (target, key, str, len);
            }
          else if (tr_variantIsDict (val) && tr_variantDictFindDict (target, key, &t))
            {
              tr_variantMergeDicts (t, val);
            }
          else if (tr_variantIsList (val))
            {
              if (tr_variantDictFind (target, key) == NULL)
                tr_variantListCopy (tr_variantDictAddList (target, key, tr_variantListSize (val)), val);
            }
          else if (tr_variantIsDict (val))
            {
              tr_variant * target_dict = tr_variantDictFind (target, key);

              if (target_dict == NULL)
                target_dict = tr_variantDictAddDict (target, key, tr_variantDictSize (val));

              if (tr_variantIsDict (target_dict))
                tr_variantMergeDicts (target_dict, val);
            }
          else
            {
              tr_dbg ("tr_variantMergeDicts skipping \"%s\"", key);
            }
        }
    }
}

/***
****
***/

struct evbuffer *
tr_variantToBuf (const tr_variant * top, tr_variant_fmt fmt)
{
  struct evbuffer * buf = evbuffer_new ();

  evbuffer_expand (buf, 4096); /* alloc a little memory to start off with */

  switch (fmt)
    {
      case TR_VARIANT_FMT_BENC:
        tr_variantToBufBenc (top, buf);
        break;

      case TR_VARIANT_FMT_JSON:
        tr_variantToBufJson (top, buf, false);
        break;

      case TR_VARIANT_FMT_JSON_LEAN:
        tr_variantToBufJson (top, buf, true);
        break;
    }

  return buf;
}

char*
tr_variantToStr (const tr_variant * top, tr_variant_fmt fmt, int * len)
{
  struct evbuffer * buf = tr_variantToBuf (top, fmt);
  const size_t n = evbuffer_get_length (buf);
  char * ret = evbuffer_free_to_str (buf);
  if (len != NULL)
    *len = (int) n;
  return ret;
}

/* portability wrapper for mkstemp (). */
static int
tr_mkstemp (char * template)
{
#ifdef WIN32
  const int flags = O_RDWR | O_BINARY | O_CREAT | O_EXCL | _O_SHORT_LIVED;
  const mode_t mode = _S_IREAD | _S_IWRITE;
  mktemp (template);
  return open (template, flags, mode);
#else
  return mkstemp (template);
#endif
}

int
tr_variantToFile (const tr_variant  * top,
                  tr_variant_fmt      fmt,
                  const char        * filename)
{
  char * tmp;
  int fd;
  int err = 0;
  char buf[TR_PATH_MAX];

  /* follow symlinks to find the "real" file, to make sure the temporary
   * we build with tr_mkstemp () is created on the right partition */
  if (tr_realpath (filename, buf) != NULL)
    filename = buf;

  /* if the file already exists, try to move it out of the way & keep it as a backup */
  tmp = tr_strdup_printf ("%s.tmp.XXXXXX", filename);
  fd = tr_mkstemp (tmp);
  tr_set_file_for_single_pass (fd);
  if (fd >= 0)
    {
      int nleft;

      /* save the variant to a temporary file */
      {
        struct evbuffer * buf = tr_variantToBuf (top, fmt);
        const char * walk = (const char *) evbuffer_pullup (buf, -1);
        nleft = evbuffer_get_length (buf);

        while (nleft > 0)
          {
            const int n = write (fd, walk, nleft);
            if (n >= 0)
              {
                nleft -= n;
                walk += n;
              }
            else if (errno != EAGAIN)
              {
                err = errno;
                break;
              }
          }

        evbuffer_free (buf);
      }

      if (nleft > 0)
        {
          tr_err (_("Couldn't save temporary file \"%1$s\": %2$s"), tmp, tr_strerror (err));
          tr_close_file (fd);
          unlink (tmp);
        }
      else
        {
          tr_close_file (fd);

#ifdef WIN32
          if (MoveFileEx (tmp, filename, MOVEFILE_REPLACE_EXISTING))
#else
          if (!rename (tmp, filename))
#endif
            {
              tr_inf (_("Saved \"%s\""), filename);
            }
          else
            {
              err = errno;
              tr_err (_("Couldn't save file \"%1$s\": %2$s"), filename, tr_strerror (err));
              unlink (tmp);
            }
        }
    }
  else
    {
      err = errno;
      tr_err (_("Couldn't save temporary file \"%1$s\": %2$s"), tmp, tr_strerror (err));
    }

  tr_free (tmp);
  return err;
}

/***
****
***/

int
tr_variantFromFile (tr_variant      * setme,
                    tr_variant_fmt    fmt,
                    const char      * filename)
{
  int err;
  size_t buflen;
  uint8_t * buf;
  const int old_errno = errno;

  errno = 0;
  buf = tr_loadFile (filename, &buflen);

  if (errno)
    err = errno;
  else
    err = tr_variantFromBuf (setme, fmt, buf, buflen, filename, NULL);

  tr_free (buf);
  errno = old_errno;
  return err;
}

int
tr_variantFromBuf (tr_variant      * setme,
                   tr_variant_fmt    fmt,
                   const void      * buf,
                   size_t            buflen,
                   const char      * optional_source,
                   const char     ** setme_end)
{
  int err;

  switch (fmt)
    {
      case TR_VARIANT_FMT_JSON:
      case TR_VARIANT_FMT_JSON_LEAN:
        err = tr_jsonParse (optional_source, buf, buflen, setme, setme_end);
        break;

      case TR_VARIANT_FMT_BENC:
        err = tr_variantParseBenc (buf, ((const char*)buf)+buflen, setme, setme_end);
        break;
    }

  return err;
}
