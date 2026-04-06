// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifdef _WIN32
#include <cstdint>

#include <windows.h>

/* MinGW :( */
#ifndef ERROR_DIRECTORY_NOT_SUPPORTED
#define ERROR_DIRECTORY_NOT_SUPPORTED 336
#endif

#define TR_ERROR_EINVAL ERROR_INVALID_PARAMETER
#define TR_ERROR_EISDIR ERROR_DIRECTORY_NOT_SUPPORTED

// Smallest integer type that can represent all values returned
// by `DWORD GetLastError()` and `int WSAGetLastError()`.
//
// N.B. DWORD is an unsigned 32-bit integer, so int64_t is the next
// smallest signed integer type that can represent all its values.
using tr_error_code_t = int64_t;
static_assert(sizeof(int) <= sizeof(int64_t));

#else /* _WIN32 */

#include <cerrno>

#define TR_ERROR_EINVAL EINVAL
#define TR_ERROR_EISDIR EISDIR

using tr_error_code_t = int;

#endif /* _WIN32 */

constexpr bool tr_error_is_enoent(tr_error_code_t code) noexcept
{
#ifdef _WIN32
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
#else
    return code == ENOENT;
#endif
}

constexpr bool tr_error_is_enospc(tr_error_code_t code) noexcept
{
#ifdef _WIN32
    return code == ERROR_DISK_FULL;
#else
    return code == ENOSPC;
#endif
}
