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
#include <stdio.h>
#include <string.h>
#include <errno.h> /* EILSEQ, EINVAL */

#include <locale.h> /* setlocale () */

#include <event2/util.h> /* evutil_strtoll () */

#define JSONSL_STATE_USER_FIELDS /* no fields */
#include "jsonsl.h"
#include "jsonsl.c"

#include "transmission.h"
#include "ConvertUTF.h"
#include "bencode.h"
#include "json.h"
#include "ptrarray.h"
#include "utils.h"

/* arbitrary value... this is much deeper than our code goes */
#define MAX_DEPTH 64

struct json_wrapper_data
{
  int error;
  bool has_content;
  tr_benc * top;
  char * key;
  const char * source;
  tr_ptrArray stack;
};

static tr_benc*
get_node (struct jsonsl_st * jsn)
{
  tr_benc * parent;
  tr_benc * node = NULL;
  struct json_wrapper_data * data = jsn->data;

  parent = tr_ptrArrayEmpty (&data->stack)
         ? NULL
         : tr_ptrArrayBack (&data->stack);

  if (!parent)
    {
      node = data->top;
    }
  else if (tr_bencIsList (parent))
    {
      node = tr_bencListAdd (parent);
    }
  else if (tr_bencIsDict (parent) && (data->key!=NULL))
    {
      node = tr_bencDictAdd (parent, data->key);
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
  tr_benc * node;
  struct json_wrapper_data * data = jsn->data;

  switch (state->type)
    {
      case JSONSL_T_LIST:
        data->has_content = true;
        node = get_node (jsn);
        tr_bencInitList (node, 0);
        tr_ptrArrayAppend (&data->stack, node);
        break;

      case JSONSL_T_OBJECT:
        data->has_content = true;
        node = get_node (jsn);
        tr_bencInitDict (node, 0);
        tr_ptrArrayAppend (&data->stack, node);
        break;

      default:
        /* nothing else interesting on push */
        break;
    }
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
                      if (sscanf (in_it+2, "%4x", &val) == 1)
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
      tr_bencInitStr (get_node (jsn), str, len);
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
          tr_bencInitReal (get_node (jsn), strtod (begin, NULL));
        }
      else if (state->special_flags & JSONSL_SPECIALf_NUMERIC)
        {
          const char * begin = jsn->base + state->pos_begin;
          data->has_content = true;
          tr_bencInitInt (get_node (jsn), evutil_strtoll (begin, NULL, 10));
        }
      else if (state->special_flags & JSONSL_SPECIALf_BOOLEAN)
        {
          const bool b = (state->special_flags & JSONSL_SPECIALf_TRUE) != 0;
          data->has_content = true;
          tr_bencInitBool (get_node (jsn), b);
        }
      else if (state->special_flags & JSONSL_SPECIALf_NULL)
        {
          data->has_content = true;
          tr_bencInitStr (get_node (jsn), "", 0);
        }
    }
}

int
tr_jsonParse (const char     * source,
              const void     * vbuf,
              size_t           len,
              tr_benc        * setme_benc,
              const uint8_t ** setme_end)
{
  int error;
  jsonsl_t jsn;
  struct json_wrapper_data data;
  char lc_numeric[128];

  tr_strlcpy (lc_numeric, setlocale (LC_NUMERIC, NULL), sizeof (lc_numeric));
  setlocale (LC_NUMERIC, "C");

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
    *setme_end = ((const uint8_t*)vbuf) + jsn->pos;

  /* cleanup */
  error = data.error;
  tr_ptrArrayDestruct (&data.stack, NULL);
  jsonsl_destroy (jsn);
  setlocale (LC_NUMERIC, lc_numeric);
  return error;
}
