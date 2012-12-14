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
#include <ctype.h>
#include <math.h> /* fabs() */
#include <stdio.h>
#include <string.h>
#include <errno.h> /* EILSEQ, EINVAL */

#include <locale.h> /* setlocale() */

#include <event2/buffer.h> /* evbuffer_add() */
#include <event2/util.h> /* evutil_strtoll () */

#define JSONSL_STATE_USER_FIELDS /* no fields */
#include "jsonsl.h"
#include "jsonsl.c"

#define __LIBTRANSMISSION_VARIANT_MODULE___
#include "transmission.h"
#include "ConvertUTF.h"
#include "list.h"
#include "ptrarray.h"
#include "utils.h"
#include "variant.h"
#include "variant-common.h"

/* arbitrary value... this is much deeper than our code goes */
#define MAX_DEPTH 64

struct json_wrapper_data
{
  int error;
  bool has_content;
  tr_variant * top;
  char * key;
  const char * source;
  tr_ptrArray stack;
};

static tr_variant*
get_node (struct jsonsl_st * jsn)
{
  tr_variant * parent;
  tr_variant * node = NULL;
  struct json_wrapper_data * data = jsn->data;

  parent = tr_ptrArrayEmpty (&data->stack)
         ? NULL
         : tr_ptrArrayBack (&data->stack);

  if (!parent)
    {
      node = data->top;
    }
  else if (tr_variantIsList (parent))
    {
      node = tr_variantListAdd (parent);
    }
  else if (tr_variantIsDict (parent) && (data->key!=NULL))
    {
      node = tr_variantDictAdd (parent, data->key);
      tr_free (data->key);
      data->key = NULL;
    }

  return node;
}


static void
error_handler (jsonsl_t                  jsn,
               jsonsl_error_t            error,
               struct jsonsl_state_st  * state   UNUSED,
               const jsonsl_char_t     * buf)
{
  struct json_wrapper_data * data = jsn->data;

  if (data->source)
    {
      tr_err ("JSON parse failed in %s at pos %zu: %s -- remaining text \"%.16s\"",
              data->source,
              jsn->pos,
              jsonsl_strerror (error),
              buf);
    }
  else
    {
      tr_err ("JSON parse failed at pos %zu: %s -- remaining text \"%.16s\"",
              jsn->pos,
              jsonsl_strerror (error),
              buf);
    }

  data->error = EILSEQ;
}

static int
error_callback (jsonsl_t                  jsn,
                jsonsl_error_t            error,
                struct jsonsl_state_st  * state,
                jsonsl_char_t           * at)
{
  error_handler (jsn, error, state, at);
  return 0; /* bail */
}

static void
action_callback_PUSH (jsonsl_t                  jsn,
                      jsonsl_action_t           action  UNUSED,
                      struct jsonsl_state_st  * state,
                      const jsonsl_char_t     * buf     UNUSED)
{
  tr_variant * node;
  struct json_wrapper_data * data = jsn->data;

  switch (state->type)
    {
      case JSONSL_T_LIST:
        data->has_content = true;
        node = get_node (jsn);
        tr_variantInitList (node, 0);
        tr_ptrArrayAppend (&data->stack, node);
        break;

      case JSONSL_T_OBJECT:
        data->has_content = true;
        node = get_node (jsn);
        tr_variantInitDict (node, 0);
        tr_ptrArrayAppend (&data->stack, node);
        break;

      default:
        /* nothing else interesting on push */
        break;
    }
}

/* like sscanf(in+2, "%4x", &val) but less slow */
static bool
decode_hex_string (const char * in, unsigned int * setme)
{
  bool success;
  char buf[5];
  char * end;

  assert (in != NULL);
  assert (in[0] == '\\');
  assert (in[1] == 'u');

  memcpy (buf, in+2, 4);
  buf[4] = '\0';
  *setme = strtoul (buf, &end, 16);
  success = end == buf+4;

  return success;
}

static char*
extract_string (jsonsl_t jsn, struct jsonsl_state_st * state, size_t * len)
{
  const char * in_begin;
  const char * in_end;
  const char * in_it;
  size_t out_buflen;
  char * out_buf;
  char * out_it;

  in_begin = jsn->base + state->pos_begin;
  if (*in_begin == '"')
    in_begin++;
  in_end = jsn->base + state->pos_cur;

  out_buflen = (in_end-in_begin)*3 + 1;
  out_buf = tr_new0 (char, out_buflen);
  out_it = out_buf;

  for (in_it=in_begin; in_it!=in_end;)
    {
      bool unescaped = false;

      if (*in_it=='\\' && in_end-in_it>=2)
        {
          switch (in_it[1])
            {
              case 'b' : *out_it++ = '\b'; in_it+=2; unescaped = true; break;
              case 'f' : *out_it++ = '\f'; in_it+=2; unescaped = true; break;
              case 'n' : *out_it++ = '\n'; in_it+=2; unescaped = true; break;
              case 'r' : *out_it++ = '\r'; in_it+=2; unescaped = true; break;
              case 't' : *out_it++ = '\t'; in_it+=2; unescaped = true; break;
              case '/' : *out_it++ = '/' ; in_it+=2; unescaped = true; break;
              case '"' : *out_it++ = '"' ; in_it+=2; unescaped = true; break;
              case '\\': *out_it++ = '\\'; in_it+=2; unescaped = true; break;
              case 'u':
                {
                  if (in_end - in_it >= 6)
                    {
                      unsigned int val = 0;
                      if (decode_hex_string (in_it, &val))
                        {
                          UTF32 str32_buf[2] = { val, 0 };
                          const UTF32 * str32_walk = str32_buf;
                          const UTF32 * str32_end = str32_buf + 1;
                          UTF8 str8_buf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
                          UTF8 * str8_walk = str8_buf;
                          UTF8 * str8_end = str8_buf + 8;

                          if (ConvertUTF32toUTF8 (&str32_walk, str32_end, &str8_walk, str8_end, 0) == 0)
                            {
                              const size_t len = str8_walk - str8_buf;
                              memcpy (out_it, str8_buf, len);
                              out_it += len;
                              unescaped = true;
                            }

                          in_it += 6;
                          break;
                        }
                    }
                }
            }
        }

      if (!unescaped)
        *out_it++ = *in_it++;
    }

  if (len != NULL)
    *len = out_it - out_buf;

  return out_buf;
}

static void
action_callback_POP (jsonsl_t                  jsn,
                     jsonsl_action_t           action  UNUSED,
                     struct jsonsl_state_st  * state,
                     const jsonsl_char_t     * buf     UNUSED)
{
  struct json_wrapper_data * data = jsn->data;

  if (state->type == JSONSL_T_STRING)
    {
      size_t len = 0;
      char * str = extract_string (jsn, state, &len);
      tr_variantInitStr (get_node (jsn), str, len);
      data->has_content = true;
      tr_free (str);
    }
  else if (state->type == JSONSL_T_HKEY)
    {
      char * str = extract_string (jsn, state, NULL);
      data->has_content = true;
      data->key = str;
    }
  else if ((state->type == JSONSL_T_LIST) || (state->type == JSONSL_T_OBJECT))
    {
      tr_ptrArrayPop (&data->stack);
    }
  else if (state->type == JSONSL_T_SPECIAL)
    {
      if (state->special_flags & JSONSL_SPECIALf_NUMNOINT)
        {
          const char * begin = jsn->base + state->pos_begin;
          data->has_content = true;
          tr_variantInitReal (get_node (jsn), strtod (begin, NULL));
        }
      else if (state->special_flags & JSONSL_SPECIALf_NUMERIC)
        {
          const char * begin = jsn->base + state->pos_begin;
          data->has_content = true;
          tr_variantInitInt (get_node (jsn), evutil_strtoll (begin, NULL, 10));
        }
      else if (state->special_flags & JSONSL_SPECIALf_BOOLEAN)
        {
          const bool b = (state->special_flags & JSONSL_SPECIALf_TRUE) != 0;
          data->has_content = true;
          tr_variantInitBool (get_node (jsn), b);
        }
      else if (state->special_flags & JSONSL_SPECIALf_NULL)
        {
          data->has_content = true;
          tr_variantInitStr (get_node (jsn), "", 0);
        }
    }
}

int
tr_jsonParse (const char     * source,
              const void     * vbuf,
              size_t           len,
              tr_variant        * setme_benc,
              const char ** setme_end)
{
  int error;
  jsonsl_t jsn;
  struct json_wrapper_data data;

  jsn = jsonsl_new (MAX_DEPTH);
  jsn->action_callback_PUSH = action_callback_PUSH;
  jsn->action_callback_POP = action_callback_POP;
  jsn->error_callback = error_callback;
  jsn->data = &data;
  jsonsl_enable_all_callbacks (jsn);

  data.error = 0;
  data.has_content = false;
  data.key = NULL;
  data.top = setme_benc;
  data.stack = TR_PTR_ARRAY_INIT;
  data.source = source;

  /* parse it */
  jsonsl_feed (jsn, vbuf, len);

  /* EINVAL if there was no content */
  if (!data.error && !data.has_content)
    data.error = EINVAL;

  /* maybe set the end ptr */
  if (setme_end)
    *setme_end = ((const char*)vbuf) + jsn->pos;

  /* cleanup */
  error = data.error;
  tr_ptrArrayDestruct (&data.stack, NULL);
  jsonsl_destroy (jsn);
  return error;
}

/****
*****
****/

struct ParentState
{
  int bencType;
  int childIndex;
  int childCount;
};

struct jsonWalk
{
  bool doIndent;
  tr_list * parents;
  struct evbuffer *  out;
};

static void
jsonIndent (struct jsonWalk * data)
{
  if (data->doIndent)
    {
      char buf[1024];
      const int width = tr_list_size (data->parents) * 4;

      buf[0] = '\n';
      memset (buf+1, ' ', width);
      evbuffer_add (data->out, buf, 1+width);
    }
}

static void
jsonChildFunc (struct jsonWalk * data)
{
  if (data->parents && data->parents->data)
    {
      struct ParentState * pstate = data->parents->data;

      switch (pstate->bencType)
        {
          case TR_VARIANT_TYPE_DICT:
            {
              const int i = pstate->childIndex++;
              if (! (i % 2))
                {
                  evbuffer_add (data->out, ": ", data->doIndent ? 2 : 1);
                }
              else
                {
                  const bool isLast = pstate->childIndex == pstate->childCount;

                  if (!isLast)
                    {
                      evbuffer_add (data->out, ", ", data->doIndent ? 2 : 1);
                      jsonIndent (data);
                    }
                }
              break;
            }

          case TR_VARIANT_TYPE_LIST:
            {
              const bool isLast = ++pstate->childIndex == pstate->childCount;
              if (!isLast)
                {
                  evbuffer_add (data->out, ", ", data->doIndent ? 2 : 1);
                  jsonIndent (data);
                }
              break;
            }

          default:
            break;
        }
    }
}

static void
jsonPushParent (struct jsonWalk  * data,
                const tr_variant * benc)
{
  struct ParentState * pstate = tr_new (struct ParentState, 1);

  pstate->bencType = benc->type;
  pstate->childIndex = 0;
  pstate->childCount = benc->val.l.count;
  tr_list_prepend (&data->parents, pstate);
}

static void
jsonPopParent (struct jsonWalk * data)
{
  tr_free (tr_list_pop_front (&data->parents));
}

static void
jsonIntFunc (const tr_variant * val, void * vdata)
{
  struct jsonWalk * data = vdata;
  evbuffer_add_printf (data->out, "%" PRId64, val->val.i);
  jsonChildFunc (data);
}

static void
jsonBoolFunc (const tr_variant * val, void * vdata)
{
  struct jsonWalk * data = vdata;

  if (val->val.b)
    evbuffer_add (data->out, "true", 4);
  else
    evbuffer_add (data->out, "false", 5);

  jsonChildFunc (data);
}

static void
jsonRealFunc (const tr_variant * val, void * vdata)
{
  struct jsonWalk * data = vdata;
  char locale[128];

  if (fabs (val->val.d - (int)val->val.d) < 0.00001)
    {
      evbuffer_add_printf (data->out, "%d", (int)val->val.d);
    }
  else
    {
      /* json requires a '.' decimal point regardless of locale */
      tr_strlcpy (locale, setlocale (LC_NUMERIC, NULL), sizeof (locale));
      setlocale (LC_NUMERIC, "POSIX");
      evbuffer_add_printf (data->out, "%.4f", tr_truncd (val->val.d, 4));
      setlocale (LC_NUMERIC, locale);
    }

  jsonChildFunc (data);
}

static void
jsonStringFunc (const tr_variant * val, void * vdata)
{
  char * out;
  char * outwalk;
  char * outend;
  struct evbuffer_iovec vec[1];
  struct jsonWalk * data = vdata;
  const char * str;
  size_t len;
  const unsigned char * it;
  const unsigned char * end;

  tr_variantGetStr (val, &str, &len);
  it = (const unsigned char *) str;
  end = it + len;

  evbuffer_reserve_space (data->out, len * 4, vec, 1);
  out = vec[0].iov_base;
  outend = out + vec[0].iov_len;

  outwalk = out;
  *outwalk++ = '"';

  for (; it!=end; ++it)
    {
      switch (*it)
        {
          case '\b': *outwalk++ = '\\'; *outwalk++ = 'b'; break;
          case '\f': *outwalk++ = '\\'; *outwalk++ = 'f'; break;
          case '\n': *outwalk++ = '\\'; *outwalk++ = 'n'; break;
          case '\r': *outwalk++ = '\\'; *outwalk++ = 'r'; break;
          case '\t': *outwalk++ = '\\'; *outwalk++ = 't'; break;
          case '"' : *outwalk++ = '\\'; *outwalk++ = '"'; break;
          case '\\': *outwalk++ = '\\'; *outwalk++ = '\\'; break;

          default:
            if (isascii (*it))
              {
                *outwalk++ = *it;
              }
            else
              {
                const UTF8 * tmp = it;
                UTF32 buf[1] = { 0 };
                UTF32 * u32 = buf;
                ConversionResult result = ConvertUTF8toUTF32 (&tmp, end, &u32, buf + 1, 0);
                if (((result==conversionOK) || (result==targetExhausted)) && (tmp!=it))
                  {
                    outwalk += tr_snprintf (outwalk, outend-outwalk, "\\u%04x", (unsigned int)buf[0]);
                    it = tmp - 1;
                  }
              }
            break;
        }
    }

  *outwalk++ = '"';
  vec[0].iov_len = outwalk - out;
  evbuffer_commit_space (data->out, vec, 1);

  jsonChildFunc (data);
}

static void
jsonDictBeginFunc (const tr_variant * val,
                   void *          vdata)
{
  struct jsonWalk * data = vdata;

  jsonPushParent (data, val);
  evbuffer_add (data->out, "{", 1);
  if (val->val.l.count)
    jsonIndent (data);
}

static void
jsonListBeginFunc (const tr_variant * val,
                   void *          vdata)
{
  const size_t nChildren = tr_variantListSize (val);
  struct jsonWalk * data = vdata;

  jsonPushParent (data, val);
  evbuffer_add (data->out, "[", 1);
  if (nChildren)
    jsonIndent (data);
}

static void
jsonContainerEndFunc (const tr_variant * val,
                      void *          vdata)
{
  struct jsonWalk * data = vdata;
  int emptyContainer = false;

  jsonPopParent (data);

  if (!emptyContainer)
    jsonIndent (data);
  if (tr_variantIsDict (val))
    evbuffer_add (data->out, "}", 1);
  else /* list */
    evbuffer_add (data->out, "]", 1);

  jsonChildFunc (data);
}

static const struct VariantWalkFuncs walk_funcs = { jsonIntFunc,
                                                    jsonBoolFunc,
                                                    jsonRealFunc,
                                                    jsonStringFunc,
                                                    jsonDictBeginFunc,
                                                    jsonListBeginFunc,
                                                    jsonContainerEndFunc };

void
tr_variantToBufJson (const tr_variant * top, struct evbuffer * buf, bool lean)
{
  struct jsonWalk data;
  data.doIndent = !lean;
  data.out = buf;
  data.parents = NULL;
  tr_variantWalk (top, &walk_funcs, &data, true);
  if (evbuffer_get_length (buf))
    evbuffer_add_printf (buf, "\n");
}
