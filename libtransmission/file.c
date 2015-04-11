/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* strlen () */

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "utils.h"

bool
tr_sys_file_read_line (tr_sys_file_t    handle,
                       char           * buffer,
                       size_t           buffer_size,
                       tr_error      ** error)
{
  bool ret = false;
  size_t offset = 0;
  uint64_t bytes_read;

  assert (handle != TR_BAD_SYS_FILE);
  assert (buffer != NULL);
  assert (buffer_size > 0);

  while (buffer_size > 0)
    {
      size_t i;
      bool found_eol = false;

      ret = tr_sys_file_read (handle, buffer + offset, MIN(buffer_size, 1024u),
                              &bytes_read, error);
      if (!ret || (offset == 0 && bytes_read == 0))
        {
          ret = false;
          break;
        }

      for (i = 0; i < bytes_read; ++i, ++offset, --buffer_size)
        {
          if (buffer[offset] == '\n')
            {
              found_eol = true;
              break;
            }
        }

      if (found_eol || buffer_size == 0 || bytes_read == 0)
        {
          const int64_t delta = -(int64_t) bytes_read + i + (found_eol ? 1 : 0);

          if (delta != 0)
            {
              ret = tr_sys_file_seek (handle, delta, TR_SEEK_CUR, NULL, error);
              if (!ret)
                break;
            }

          if (offset > 0 && buffer[offset - 1] == '\r')
            buffer[offset - 1] = '\0';
          else
            buffer[offset] = '\0';

          break;
        }
    }

  return ret;
}

bool
tr_sys_file_write_line (tr_sys_file_t    handle,
                        const char     * buffer,
                        tr_error      ** error)
{
  bool ret;

  assert (handle != TR_BAD_SYS_FILE);
  assert (buffer != NULL);

  ret = tr_sys_file_write (handle, buffer, strlen (buffer), NULL, error);

  if (ret)
    ret = tr_sys_file_write (handle, TR_NATIVE_EOL_STR, TR_NATIVE_EOL_STR_SIZE,
                             NULL, error);

  return ret;
}

bool
tr_sys_file_write_fmt (tr_sys_file_t    handle,
                       const char     * format,
                       tr_error      ** error,
                                        ...)
{
  bool ret = false;
  char * buffer;
  va_list args;

  assert (handle != TR_BAD_SYS_FILE);
  assert (format != NULL);

  va_start (args, error);
  buffer = tr_strdup_vprintf (format, args);
  va_end (args);

  if (buffer != NULL)
    {
      ret = tr_sys_file_write (handle, buffer, strlen (buffer), NULL, error);
      tr_free (buffer);
    }
  else
    {
      tr_error_set_literal (error, 0, "Unable to format message.");
    }

  return ret;
}
