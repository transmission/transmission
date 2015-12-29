/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isdigit() */
#include <errno.h>
#include <stdlib.h> /* strtoul() */
#include <string.h> /* strlen(), memchr() */

#include <event2/buffer.h>

#include "ConvertUTF.h"

#define __LIBTRANSMISSION_VARIANT_MODULE__
#include "transmission.h"
#include "ptrarray.h"
#include "utils.h" /* tr_snprintf() */
#include "variant.h"
#include "variant-common.h"

/***
****  tr_variantParse()
****  tr_variantLoad()
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
  char * endptr;
  const void * begin;
  const void * end;
  int64_t val;

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
  const void * end;
  size_t len;
  char * ulend;
  const uint8_t * strbegin;
  const uint8_t * strend;

  if (buf >= bufend)
    goto err;

  if (!isdigit (*buf))
    goto err;

  end = memchr (buf, ':', bufend - buf);
  if (end == NULL)
    goto err;

  errno = 0;
  len = strtoul ((const char*)buf, &ulend, 10);
  if (errno || ulend != end)
    goto err;

  strbegin = (const uint8_t*)end + 1;
  strend = strbegin + len;
  if ((strend<strbegin) || (strend>bufend))
    goto err;

  *setme_end = (const uint8_t*)end + 1 + len;
  *setme_str = (const uint8_t*)end + 1;
  *setme_strlen = len;
  return 0;

err:
  *setme_end = NULL;
  *setme_str = NULL;
  *setme_strlen= 0;
  return EILSEQ;
}

static tr_variant*
get_node (tr_ptrArray * stack, tr_quark * key, tr_variant * top, int * err)
{
  tr_variant * node = NULL;

  if (tr_ptrArrayEmpty (stack))
    {
      node = top;
    }
  else
    {
      tr_variant * parent = tr_ptrArrayBack (stack);

      if (tr_variantIsList (parent))
        {
          node = tr_variantListAdd (parent);
        }
      else if (*key && tr_variantIsDict (parent))
        {
          node = tr_variantDictAdd (parent, *key);
          *key = 0;
        }
      else
        {
          *err = EILSEQ;
        }
    }

  return node;
}

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted bencoded data. (#667)
 */
int
tr_variantParseBenc (const void    * buf_in,
                     const void    * bufend_in,
                     tr_variant    * top,
                     const char   ** setme_end)
{
  int err = 0;
  const uint8_t * buf = buf_in;
  const uint8_t * bufend = bufend_in;
  tr_ptrArray stack = TR_PTR_ARRAY_INIT;
  tr_quark key = 0;

  tr_variantInit (top, 0);

  while (buf != bufend)
    {
      if (buf > bufend) /* no more text to parse... */
        err = EILSEQ;

      if (err)
        break;

      if (*buf == 'i') /* int */
        {
          int64_t val;
          const uint8_t * end;
          tr_variant * v;

          if ((err = tr_bencParseInt (buf, bufend, &end, &val)))
            break;
          buf = end;

          if ((v = get_node (&stack, &key, top, &err)))
            tr_variantInitInt (v, val);
        }
      else if (*buf == 'l') /* list */
        {
          tr_variant * v;

          ++buf;

          if ((v = get_node (&stack, &key, top, &err)))
            {
              tr_variantInitList (v, 0);
              tr_ptrArrayAppend (&stack, v);
            }
        }
      else if (*buf == 'd') /* dict */
        {
          tr_variant * v;

          ++buf;

          if ((v = get_node (&stack, &key, top, &err)))
            {
              tr_variantInitDict (v, 0);
              tr_ptrArrayAppend (&stack, v);
            }
        }
      else if (*buf == 'e') /* end of list or dict */
        {
          ++buf;

          if (tr_ptrArrayEmpty (&stack) || (key != 0))
            {
              err = EILSEQ;
              break;
            }
          else
            {
              tr_ptrArrayPop (&stack);
              if (tr_ptrArrayEmpty (&stack))
                break;
            }
        }
      else if (isdigit (*buf)) /* string? */
        {
          tr_variant * v;
          const uint8_t * end;
          const uint8_t * str;
          size_t str_len;

          if ((err = tr_bencParseStr (buf, bufend, &end, &str, &str_len)))
            break;
          buf = end;

          if (!key && !tr_ptrArrayEmpty(&stack) && tr_variantIsDict(tr_ptrArrayBack(&stack)))
            key = tr_quark_new (str, str_len);
          else if ((v = get_node (&stack, &key, top, &err)))
            tr_variantInitStr (v, str, str_len);
        }
      else /* invalid bencoded text... march past it */
        {
          ++buf;
        }

      if (tr_ptrArrayEmpty (&stack))
        break;
    }

  if (!err && (!top->type || !tr_ptrArrayEmpty(&stack)))
    err = EILSEQ;

  if (!err && setme_end)
    *setme_end = (const char*) buf;

  tr_ptrArrayDestruct (&stack, NULL);
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
  int len;
  char buf[128];

  len = tr_snprintf (buf, sizeof (buf), "%f", val->val.d);
  evbuffer_add_printf (evbuf, "%d:", len);
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

