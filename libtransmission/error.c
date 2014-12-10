/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>

#include "transmission.h"
#include "error.h"
#include "utils.h"

tr_error *
tr_error_new (int          code,
              const char * message_format,
                           ...)
{
  tr_error * error;
  va_list args;

  assert (message_format != NULL);

  va_start (args, message_format);
  error = tr_error_new_valist (code, message_format, args);
  va_end (args);

  return error;
}

tr_error *
tr_error_new_literal (int          code,
                      const char * message)
{
  tr_error * error;

  assert (message != NULL);

  error = tr_new (tr_error, 1);
  error->code = code;
  error->message = tr_strdup (message);

  return error;
}

tr_error *
tr_error_new_valist (int          code,
                     const char * message_format,
                     va_list      args)
{
  tr_error * error;

  assert (message_format != NULL);

  error = tr_new (tr_error, 1);
  error->code = code;
  error->message = tr_strdup_vprintf (message_format, args);

  return error;
}

void
tr_error_free (tr_error * error)
{
  if (error == NULL)
    return;

  tr_free (error->message);
  tr_free (error);
}

void
tr_error_set (tr_error   ** error,
              int           code,
              const char  * message_format,
                            ...)
{
  va_list args;

  if (error == NULL)
    return;

  assert (*error == NULL);
  assert (message_format != NULL);

  va_start (args, message_format);
  *error = tr_error_new_valist (code, message_format, args);
  va_end (args);
}

void
tr_error_set_literal (tr_error   ** error,
                      int           code,
                      const char  * message)
{
  if (error == NULL)
    return;

  assert (*error == NULL);
  assert (message != NULL);

  *error = tr_error_new_literal (code, message);
}

void
tr_error_propagate (tr_error ** new_error,
                    tr_error ** old_error)
{
  assert (old_error != NULL);
  assert (*old_error != NULL);

  if (new_error != NULL)
    {
      assert (*new_error == NULL);

      *new_error = *old_error;
      *old_error = NULL;
    }
  else
    {
      tr_error_clear (old_error);
    }
}

void
tr_error_clear (tr_error ** error)
{
  if (error == NULL)
    return;

  tr_error_free (*error);

  *error = NULL;
}

static void
error_prefix_valist (tr_error   ** error,
                     const char  * prefix_format,
                     va_list       args)
{
  char * prefix;
  char * new_message;

  assert (error != NULL);
  assert (*error != NULL);
  assert (prefix_format != NULL);

  prefix = tr_strdup_vprintf (prefix_format, args);

  new_message = tr_strdup_printf ("%s%s", prefix, (*error)->message);
  tr_free ((*error)->message);
  (*error)->message = new_message;

  tr_free (prefix);
}

void
tr_error_prefix (tr_error   ** error,
                 const char  * prefix_format,
                               ...)
{
  va_list args;

  assert (prefix_format != NULL);

  if (error == NULL || *error == NULL)
    return;

  va_start (args, prefix_format);
  error_prefix_valist (error, prefix_format, args);
  va_end (args);
}

void
tr_error_propagate_prefixed (tr_error   ** new_error,
                             tr_error   ** old_error,
                             const char  * prefix_format,
                                           ...)
{
  va_list args;

  assert (prefix_format != NULL);

  tr_error_propagate (new_error, old_error);

  if (new_error == NULL)
    return;

  va_start (args, prefix_format);
  error_prefix_valist (new_error, prefix_format, args);
  va_end (args);
}
