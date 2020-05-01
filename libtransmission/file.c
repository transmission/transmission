/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* strlen() */

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "tr-assert.h"
#include "utils.h"

bool tr_sys_file_read_line(tr_sys_file_t handle, char* buffer, size_t buffer_size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != NULL);
    TR_ASSERT(buffer_size > 0);

    bool ret = false;
    size_t offset = 0;
    uint64_t bytes_read;

    while (buffer_size > 0)
    {
        size_t const bytes_needed = MIN(buffer_size, 1024u);

        ret = tr_sys_file_read(handle, buffer + offset, bytes_needed, &bytes_read, error);

        if (!ret || (offset == 0 && bytes_read == 0))
        {
            ret = false;
            break;
        }

        TR_ASSERT(bytes_read <= bytes_needed);
        TR_ASSERT(bytes_read <= buffer_size);

        int64_t delta = 0;

        for (size_t i = 0; i < bytes_read; ++i, ++offset, --buffer_size)
        {
            if (buffer[offset] == '\n')
            {
                delta = i - (int64_t)bytes_read + 1;
                break;
            }
        }

        if (delta != 0 || buffer_size == 0 || bytes_read == 0)
        {
            if (delta != 0)
            {
                ret = tr_sys_file_seek(handle, delta, TR_SEEK_CUR, NULL, error);

                if (!ret)
                {
                    break;
                }
            }

            if (offset > 0 && buffer[offset - 1] == '\r')
            {
                buffer[offset - 1] = '\0';
            }
            else
            {
                buffer[offset] = '\0';
            }

            break;
        }
    }

    return ret;
}

bool tr_sys_file_write_line(tr_sys_file_t handle, char const* buffer, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != NULL);

    bool ret = tr_sys_file_write(handle, buffer, strlen(buffer), NULL, error);

    if (ret)
    {
        ret = tr_sys_file_write(handle, TR_NATIVE_EOL_STR, TR_NATIVE_EOL_STR_SIZE, NULL, error);
    }

    return ret;
}

bool tr_sys_file_write_fmt(tr_sys_file_t handle, char const* format, tr_error** error, ...)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(format != NULL);

    bool ret = false;
    char* buffer;
    va_list args;

    va_start(args, error);
    buffer = tr_strdup_vprintf(format, args);
    va_end(args);

    if (buffer != NULL)
    {
        ret = tr_sys_file_write(handle, buffer, strlen(buffer), NULL, error);
        tr_free(buffer);
    }
    else
    {
        tr_error_set_literal(error, 0, "Unable to format message.");
    }

    return ret;
}
