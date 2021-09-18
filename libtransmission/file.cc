/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* strlen() */

#ifdef __APPLE__
#include <Availability.h> // for deployment target to support pre-catalina targets without std::fs
#endif
#if ((defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || (defined(__cplusplus) && __cplusplus >= 201703L)) && \
    defined(__has_include)
#if __has_include(<filesystem>) && (!defined(__MAC_OS_X_VERSION_MIN_REQUIRED) || __MAC_OS_X_VERSION_MIN_REQUIRED >= 101500)
#define GHC_USE_STD_FS
#include <filesystem>
namespace fs = std::filesystem;
#endif
#endif
#ifndef GHC_USE_STD_FS
#include <ghc/filesystem.hpp>
namespace fs = ghc::filesystem;
#endif

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "tr-assert.h"
#include "utils.h"

namespace fs = ghc::filesystem;

bool tr_sys_file_read_line(tr_sys_file_t handle, char* buffer, size_t buffer_size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr);
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

bool tr_sys_file_write_line(tr_sys_file_t handle, char const* buffer, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr);

    bool ret = tr_sys_file_write(handle, buffer, strlen(buffer), nullptr, error);

    if (ret)
    {
        ret = tr_sys_file_write(handle, TR_NATIVE_EOL_STR, TR_NATIVE_EOL_STR_SIZE, nullptr, error);
    }

    return ret;
}

bool tr_sys_file_write_fmt(tr_sys_file_t handle, char const* format, tr_error** error, ...)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(format != nullptr);

    bool ret = false;
    char* buffer;
    va_list args;

    va_start(args, error);
    buffer = tr_strdup_vprintf(format, args);
    va_end(args);

    if (buffer != nullptr)
    {
        ret = tr_sys_file_write(handle, buffer, strlen(buffer), nullptr, error);
        tr_free(buffer);
    }
    else
    {
        tr_error_set_literal(error, 0, "Unable to format message.");
    }

    return ret;
}

#include <iostream>

bool tr_sys_dir_create(char const* path_in, int flags, int permissions, tr_error** error)
{
    TR_ASSERT(path_in != nullptr);
    auto const path = fs::path{ path_in };

    auto ec = std::error_code{};
    auto const status = fs::status(path, ec);
    if (fs::exists(status))
    {
        std::cerr << __FILE__ << ':' << __LINE__ << ' ' << path_in << " exists" << std::endl;
        if (fs::is_directory(status))
        {
            std::cerr << __FILE__ << ':' << __LINE__ << ' ' << path_in << " is already a directory" << std::endl;
            return true;
        }

        tr_error_set_literal(error, ENOTDIR, tr_strerror(ENOTDIR));
        return false;
    }

    if ((flags & TR_SYS_DIR_CREATE_PARENTS) != 0)
    {
        auto const ok = fs::create_directories(path, ec);
        std::cerr << __FILE__ << ':' << __LINE__ << " ok " << ok << " path " << path << " ec " << ec << " code " << ec.value()
                  << " message " << ec.message() << std::endl;
    }
    else
    {
        auto const ok = fs::create_directory(path, ec);
        std::cerr << __FILE__ << ':' << __LINE__ << " ok " << ok << " path " << path << " ec " << ec << " code " << ec.value()
                  << " message " << ec.message() << std::endl;
    }

    if (!ec)
    {
        fs::permissions(path, fs::perms(permissions), fs::perm_options::replace, ec);
        std::cerr << __FILE__ << ':' << __LINE__ << " ec " << ec << std::endl;
    }

    if (ec)
    {
        tr_error_set_literal(error, ec.value(), ec.message().c_str());
        std::cerr << __FILE__ << ':' << __LINE__ << " error is " << ec.value() << ' ' << ec.message() << std::endl;
        return false;
    }

    return true;
}
