// This file Copyright © 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <string_view>

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "tr-assert.h"

using namespace std::literals;

bool tr_sys_file_read_line(tr_sys_file_t handle, char* buffer, size_t buffer_size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr);
    TR_ASSERT(buffer_size > 0);

    auto ret = bool{};
    auto offset = size_t{};

    while (buffer_size > 0)
    {
        size_t const bytes_needed = std::min(buffer_size, size_t{ 1024 });
        auto bytes_read = uint64_t{};
        ret = tr_sys_file_read(handle, buffer + offset, bytes_needed, &bytes_read, error);

        if (!ret || (offset == 0 && bytes_read == 0))
        {
            ret = false;
            break;
        }

        TR_ASSERT(bytes_read <= bytes_needed);
        TR_ASSERT(bytes_read <= buffer_size);

        int64_t delta = 0;

        for (size_t i = 0; i < bytes_read; ++i)
        {
            if (buffer[offset] == '\n')
            {
                delta = i - (int64_t)bytes_read + 1;
                break;
            }

            ++offset;
            --buffer_size;
        }

        if (delta != 0 || buffer_size == 0 || bytes_read == 0)
        {
            if (delta != 0)
            {
                ret = tr_sys_file_seek(handle, delta, TR_SEEK_CUR, nullptr, error);

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

bool tr_sys_file_write_line(tr_sys_file_t handle, std::string_view buffer, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool ret = tr_sys_file_write(handle, std::data(buffer), std::size(buffer), nullptr, error);

    if (ret)
    {
        ret = tr_sys_file_write(handle, TR_NATIVE_EOL_STR, TR_NATIVE_EOL_STR_SIZE, nullptr, error);
    }

    return ret;
}
