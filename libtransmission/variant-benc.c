/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isdigit () */
#include <errno.h>
#include <stdlib.h> /* strtoul (), strtod (), realloc (), qsort (), mkstemp () */
#include <string.h> /* strlen (), memchr () */

#include <locale.h> /* setlocale () */

#include <event2/buffer.h>

#include "ConvertUTF.h"

#define __LIBTRANSMISSION_VARIANT_MODULE___
#include "transmission.h"
#include "ptrarray.h"
#include "utils.h" /* tr_strlcpy () */
#include "variant.h"
#include "variant-common.h"

/***
****  tr_variantParse ()
****  tr_variantLoad ()
***/

/**
 * The initial i and trailing e are beginning and ending delimiters.
 * You can have negative numbers such as i-3e. You cannot prefix the
 * number with a zero such as i04e. However, i0e is valid.
 * Example: i3e represents the integer "3"
 * NOTE: The maximum number of bit of this integer is unspecified,
 * but to handle it as a signed 64bit integer is mandatory to handle
 * "large files" aka .torrent for more that 4Gbyte
 */
int
tr_bencParseInt (const uint8_t  * buf,
                 const uint8_t  * bufend,
                 const uint8_t ** setme_end,
                 int64_t        * setme_val)
{
    char *       endptr;
    const void * begin;
    const void * end;
    int64_t      val;

    if (buf >= bufend)
        return EILSEQ;
    if (*buf != 'i')
        return EILSEQ;

    begin = buf + 1;
    end = memchr (begin, 'e', (bufend - buf) - 1);
    if (end == NULL)
        return EILSEQ;

    errno = 0;
    val = evutil_strtoll (begin, &endptr, 10);
    if (errno || (endptr != end)) /* incomplete parse */
        return EILSEQ;
    if (val && * (const char*)begin == '0') /* no leading zeroes! */
        return EILSEQ;

    *setme_end = (const uint8_t*)end + 1;
    *setme_val = val;
    return 0;
}

/**
 * Byte strings are encoded as follows:
 * <string length encoded in base ten ASCII>:<string data>
 * Note that there is no constant beginning delimiter, and no ending delimiter.
 * Example: 4:spam represents the string "spam"
 */
int
tr_bencParseStr (const uint8_t  * buf,
                 const uint8_t  * bufend,
                 const uint8_t ** setme_end,
                 const uint8_t ** setme_str,
                 size_t *         setme_strlen)
{
    size_t       len;
    const void * end;
    char *       endptr;

    if (buf >= bufend)
        return EILSEQ;

    if (!isdigit (*buf))
        return EILSEQ;

    end = memchr (buf, ':', bufend - buf);
    if (end == NULL)
        return EILSEQ;

    errno = 0;
    len = strtoul ((const char*)buf, &endptr, 10);
    if (errno || endptr != end)
        return EILSEQ;

    if ((const uint8_t*)end + 1 + len > bufend)
        return EILSEQ;

    *setme_end = (const uint8_t*)end + 1 + len;
    *setme_str = (const uint8_t*)end + 1;
    *setme_strlen = len;
    return 0;
}

static int
makeroom (tr_variant * container, size_t count)
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

static tr_variant*
getNode (tr_variant  * top,
         tr_ptrArray * parentStack,
         int           type)
{
    tr_variant * parent;

    assert (top);
    assert (parentStack);

    if (tr_ptrArrayEmpty (parentStack))
        return top;

    parent = tr_ptrArrayBack (parentStack);
    assert (parent);

    /* dictionary keys must be strings */
    if ((parent->type == TR_VARIANT_TYPE_DICT)
      && (type != TR_VARIANT_TYPE_STR)
      && (! (parent->val.l.count % 2)))
        return NULL;

    makeroom (parent, 1);
    return parent->val.l.vals + parent->val.l.count++;
}

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
static int
tr_variantParseImpl (const void    * buf_in,
                     const void    * bufend_in,
                     tr_variant    * top,
                     tr_ptrArray   * parentStack,
                     const char   ** setme_end)
{
    int             err;
    const uint8_t * buf = buf_in;
    const uint8_t * bufend = bufend_in;

    tr_variantInit (top, 0);

    while (buf != bufend)
    {
        if (buf > bufend) /* no more text to parse... */
            return 1;

        if (*buf == 'i') /* int */
        {
            int64_t         val;
            const uint8_t * end;
            tr_variant *       node;

            if ((err = tr_bencParseInt (buf, bufend, &end, &val)))
                return err;

            node = getNode (top, parentStack, TR_VARIANT_TYPE_INT);
            if (!node)
                return EILSEQ;

            tr_variantInitInt (node, val);
            buf = end;

            if (tr_ptrArrayEmpty (parentStack))
                break;
        }
        else if (*buf == 'l') /* list */
        {
            tr_variant * node = getNode (top, parentStack, TR_VARIANT_TYPE_LIST);
            if (!node)
                return EILSEQ;
            tr_variantInit (node, TR_VARIANT_TYPE_LIST);
            tr_ptrArrayAppend (parentStack, node);
            ++buf;
        }
        else if (*buf == 'd') /* dict */
        {
            tr_variant * node = getNode (top, parentStack, TR_VARIANT_TYPE_DICT);
            if (!node)
                return EILSEQ;
            tr_variantInit (node, TR_VARIANT_TYPE_DICT);
            tr_ptrArrayAppend (parentStack, node);
            ++buf;
        }
        else if (*buf == 'e') /* end of list or dict */
        {
            tr_variant * node;
            ++buf;
            if (tr_ptrArrayEmpty (parentStack))
                return EILSEQ;

            node = tr_ptrArrayBack (parentStack);
            if (tr_variantIsDict (node) && (node->val.l.count % 2))
            {
                /* odd # of children in dict */
                tr_variantFree (&node->val.l.vals[--node->val.l.count]);
                return EILSEQ;
            }

            tr_ptrArrayPop (parentStack);
            if (tr_ptrArrayEmpty (parentStack))
                break;
        }
        else if (isdigit (*buf)) /* string? */
        {
            const uint8_t * end;
            const uint8_t * str;
            size_t          str_len;
            tr_variant *       node;

            if ((err = tr_bencParseStr (buf, bufend, &end, &str, &str_len)))
                return err;

            node = getNode (top, parentStack, TR_VARIANT_TYPE_STR);
            if (!node)
                return EILSEQ;

            tr_variantInitStr (node, str, str_len);
            buf = end;

            if (tr_ptrArrayEmpty (parentStack))
                break;
        }
        else /* invalid bencoded text... march past it */
        {
            ++buf;
        }
    }

    err = !tr_variantIsSomething (top) || !tr_ptrArrayEmpty (parentStack);

    if (!err && setme_end)
        *setme_end = (const char*) buf;

    return err;
}


int
tr_variantParseBenc (const void     * buf,
                     const void     * end,
                     tr_variant     * top,
                     const char    ** setme_end)
{
  int err;
  tr_ptrArray parentStack = TR_PTR_ARRAY_INIT;

  top->type = 0; /* set to `uninitialized' */
  err = tr_variantParseImpl (buf, end, top, &parentStack, setme_end);
  if (err)
    tr_variantFree (top);

  tr_ptrArrayDestruct (&parentStack, NULL);
  return err;
}

/****
*****
****/

static void
saveIntFunc (const tr_variant * val, void * evbuf)
{
  evbuffer_add_printf (evbuf, "i%" PRId64 "e", val->val.i);
}

static void
saveBoolFunc (const tr_variant * val, void * evbuf)
{
  if (val->val.b)
    evbuffer_add (evbuf, "i1e", 3);
  else
    evbuffer_add (evbuf, "i0e", 3);
}

static void
saveRealFunc (const tr_variant * val, void * evbuf)
{
  char buf[128];
  char locale[128];
  size_t len;

  /* always use a '.' decimal point s.t. locale-hopping doesn't bite us */
  tr_strlcpy (locale, setlocale (LC_NUMERIC, NULL), sizeof (locale));
  setlocale (LC_NUMERIC, "POSIX");
  tr_snprintf (buf, sizeof (buf), "%f", val->val.d);
  setlocale (LC_NUMERIC, locale);

  len = strlen (buf);
  evbuffer_add_printf (evbuf, "%lu:", (unsigned long)len);
  evbuffer_add (evbuf, buf, len);
}

static void
saveStringFunc (const tr_variant * v, void * evbuf)
{
  size_t len;
  const char * str;
  tr_variantGetStr (v, &str, &len);
  evbuffer_add_printf (evbuf, "%zu:", len);
  evbuffer_add (evbuf, str, len);
}

static void
saveDictBeginFunc (const tr_variant * val UNUSED, void * evbuf)
{
  evbuffer_add (evbuf, "d", 1);
}

static void
saveListBeginFunc (const tr_variant * val UNUSED, void * evbuf)
{
  evbuffer_add (evbuf, "l", 1);
}

static void
saveContainerEndFunc (const tr_variant * val UNUSED, void * evbuf)
{
  evbuffer_add (evbuf, "e", 1);
}

static const struct VariantWalkFuncs walk_funcs = { saveIntFunc,
                                                    saveBoolFunc,
                                                    saveRealFunc,
                                                    saveStringFunc,
                                                    saveDictBeginFunc,
                                                    saveListBeginFunc,
                                                    saveContainerEndFunc };

void
tr_variantToBufBenc (const tr_variant * top, struct evbuffer * buf)
{
  tr_variantWalk (top, &walk_funcs, buf, true);
}

