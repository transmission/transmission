// This file Copyright © 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstring>
#include <ctype.h> /* isalpha() */
#include <string_view>

#include <shlobj.h> /* SHCreateDirectoryEx() */
#include <winioctl.h> /* FSCTL_SET_SPARSE */

#include <fmt/format.h>

#include "transmission.h"
#include "crypto-utils.h" /* tr_rand_int() */
#include "error.h"
#include "file.h"
#include "tr-assert.h"
#include "utils.h"

using namespace std::literals;

#ifndef MAXSIZE_T
#define MAXSIZE_T ((SIZE_T) ~((SIZE_T)0))
#endif

/* MSDN (http://msdn.microsoft.com/en-us/library/2k2xf226.aspx) only mentions
   "i64" suffix for C code, but no warning is issued */
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL

struct tr_sys_dir_win32
{
    wchar_t* pattern;
    HANDLE find_handle;
    WIN32_FIND_DATAW find_data;
    char* utf8_name;
};

static wchar_t const native_local_path_prefix[] = { '\\', '\\', '?', '\\' };
static wchar_t const native_unc_path_prefix[] = { '\\', '\\', '?', '\\', 'U', 'N', 'C', '\\' };

static void set_system_error(tr_error** error, DWORD code)
{
    char* message;

    if (error == nullptr)
    {
        return;
    }

    message = tr_win32_format_message(code);

    if (message != nullptr)
    {
        tr_error_set(error, code, message);
        tr_free(message);
    }
    else
    {
        tr_error_set(error, code, fmt::format(FMT_STRING("Unknown error: {:#08x}"), code));
    }
}

static void set_system_error_if_file_found(tr_error** error, DWORD code)
{
    if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND && code != ERROR_NO_MORE_FILES)
    {
        set_system_error(error, code);
    }
}

static time_t filetime_to_unix_time(FILETIME const* t)
{
    TR_ASSERT(t != nullptr);

    uint64_t tmp = 0;
    tmp |= t->dwHighDateTime;
    tmp <<= 32;
    tmp |= t->dwLowDateTime;
    tmp /= 10; /* to microseconds */
    tmp -= DELTA_EPOCH_IN_MICROSECS;

    return tmp / 1000000UL;
}

static void stat_to_sys_path_info(
    DWORD attributes,
    DWORD size_low,
    DWORD size_high,
    FILETIME const* mtime,
    tr_sys_path_info* info)
{
    TR_ASSERT(mtime != nullptr);
    TR_ASSERT(info != nullptr);

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        info->type = TR_SYS_PATH_IS_DIRECTORY;
    }
    else if ((attributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_VIRTUAL)) == 0)
    {
        info->type = TR_SYS_PATH_IS_FILE;
    }
    else
    {
        info->type = TR_SYS_PATH_IS_OTHER;
    }

    info->size = size_high;
    info->size <<= 32;
    info->size |= size_low;

    info->last_modified_at = filetime_to_unix_time(mtime);
}

static constexpr bool is_slash(char c)
{
    return c == '\\' || c == '/';
}

static constexpr bool is_unc_path(std::string_view path)
{
    return std::size(path) >= 2 && is_slash(path[0]) && path[1] == path[0];
}

static bool is_valid_path(std::string_view path)
{
    if (is_unc_path(path))
    {
        if (path[2] != '\0' && !isalnum(path[2]))
        {
            return false;
        }
    }
    else
    {
        auto pos = path.find(':');

        if (pos != path.npos)
        {
            if (pos != 1 || !isalpha(path[0]))
            {
                return false;
            }

            path.remove_prefix(2);
        }
    }

    return path.find_first_of("<>:\"|?*"sv) == path.npos;
}

static wchar_t* path_to_native_path_ex(char const* path, int extra_chars_after, int* setme_real_result_size)
{
    if (path == nullptr)
    {
        return nullptr;
    }

    /* Extending maximum path length limit up to ~32K. See "Naming Files, Paths, and Namespaces"
       (https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx) for more info */

    bool const is_relative = tr_sys_path_is_relative(path);
    bool const is_unc = is_unc_path(path);

    /* `-2` for UNC since we overwrite existing prefix slashes */
    int const extra_chars_before = is_relative ?
        0 :
        (is_unc ? TR_N_ELEMENTS(native_unc_path_prefix) - 2 : TR_N_ELEMENTS(native_local_path_prefix));

    /* TODO (?): TR_ASSERT(!is_relative); */

    int real_result_size = 0;
    wchar_t* const wide_path = tr_win32_utf8_to_native_ex(path, -1, extra_chars_before, extra_chars_after, &real_result_size);

    if (wide_path == nullptr)
    {
        return nullptr;
    }

    real_result_size += extra_chars_before;

    /* Relative paths cannot be used with "\\?\" prefixes. This also means that relative paths are
       limited to ~260 chars... but we should rarely work with relative paths in the first place */
    if (!is_relative)
    {
        if (is_unc)
        {
            /* UNC path: "\\server\share" -> "\\?\UNC\server\share" */
            memcpy(wide_path, native_unc_path_prefix, sizeof(native_unc_path_prefix));
        }
        else
        {
            /* Local path: "C:" -> "\\?\C:" */
            memcpy(wide_path, native_local_path_prefix, sizeof(native_local_path_prefix));
        }
    }

    /* Automatic '/' to '\' conversion is disabled for "\\?\"-prefixed paths */
    wchar_t* p = wide_path + extra_chars_before;

    while ((p = wcschr(p, L'/')) != nullptr)
    {
        *p++ = L'\\';
    }

    /* Squash multiple consecutive path separators into one to avoid ERROR_INVALID_NAME */
    wchar_t* first_conseq_sep = wide_path + extra_chars_before;

    while ((first_conseq_sep = std::wcsstr(first_conseq_sep, L"\\\\")) != nullptr)
    {
        wchar_t const* last_conseq_sep = first_conseq_sep + 1;
        while (*(last_conseq_sep + 1) == L'\\')
        {
            ++last_conseq_sep;
        }

        std::copy_n(last_conseq_sep, real_result_size - (last_conseq_sep - wide_path) + 1, first_conseq_sep);
        real_result_size -= last_conseq_sep - first_conseq_sep;
    }

    if (setme_real_result_size != nullptr)
    {
        *setme_real_result_size = real_result_size;
    }

    return wide_path;
}

static wchar_t* path_to_native_path(char const* path)
{
    return path_to_native_path_ex(path, 0, nullptr);
}

static char* native_path_to_path(wchar_t const* wide_path)
{
    if (wide_path == nullptr)
    {
        return nullptr;
    }

    bool const is_unc = wcsncmp(wide_path, native_unc_path_prefix, TR_N_ELEMENTS(native_unc_path_prefix)) == 0;
    bool const is_local = !is_unc && wcsncmp(wide_path, native_local_path_prefix, TR_N_ELEMENTS(native_local_path_prefix)) == 0;

    size_t const skip_chars = is_unc ? TR_N_ELEMENTS(native_unc_path_prefix) :
                                       (is_local ? TR_N_ELEMENTS(native_local_path_prefix) : 0);

    char* const path = tr_win32_native_to_utf8_ex(wide_path + skip_chars, -1, is_unc ? 2 : 0, 0, nullptr);

    if (is_unc && path != nullptr)
    {
        path[0] = '\\';
        path[1] = '\\';
    }

    return path;
}

static tr_sys_file_t open_file(char const* path, DWORD access, DWORD disposition, DWORD flags, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    tr_sys_file_t ret = TR_BAD_SYS_FILE;
    wchar_t* wide_path = path_to_native_path(path);

    if (wide_path != nullptr)
    {
        ret = CreateFileW(
            wide_path,
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            disposition,
            flags,
            nullptr);
    }

    if (ret == TR_BAD_SYS_FILE)
    {
        set_system_error(error, GetLastError());
    }

    tr_free(wide_path);

    return ret;
}

static bool create_dir(char const* path, int flags, int /*permissions*/, bool okay_if_exists, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool ret;
    DWORD error_code = ERROR_SUCCESS;
    wchar_t* wide_path = path_to_native_path(path);

    if ((flags & TR_SYS_DIR_CREATE_PARENTS) != 0)
    {
        error_code = SHCreateDirectoryExW(nullptr, wide_path, nullptr);
        ret = error_code == ERROR_SUCCESS;
    }
    else
    {
        ret = CreateDirectoryW(wide_path, nullptr);

        if (!ret)
        {
            error_code = GetLastError();
        }
    }

    if (!ret && error_code == ERROR_ALREADY_EXISTS && okay_if_exists)
    {
        DWORD const attributes = GetFileAttributesW(wide_path);

        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            ret = true;
        }
    }

    if (!ret)
    {
        set_system_error(error, error_code);
    }

    tr_free(wide_path);

    return ret;
}

static void create_temp_path(
    char* path_template,
    void (*callback)(char const* path, void* param, tr_error** error),
    void* callback_param,
    tr_error** error)
{
    TR_ASSERT(path_template != nullptr);
    TR_ASSERT(callback != nullptr);

    char* path = tr_strdup(path_template);
    size_t path_size = strlen(path);

    TR_ASSERT(path_size > 0);

    tr_error* my_error = nullptr;

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        size_t i = path_size;

        while (i > 0 && path_template[i - 1] == 'X')
        {
            int const c = tr_rand_int(26 + 26 + 10);
            path[i - 1] = c < 26 ? c + 'A' : (c < 26 + 26 ? (c - 26) + 'a' : (c - 26 - 26) + '0');
            --i;
        }

        TR_ASSERT(path_size >= i + 6);

        tr_error_clear(&my_error);

        (*callback)(path, callback_param, &my_error);

        if (my_error == nullptr)
        {
            break;
        }
    }

    if (my_error != nullptr)
    {
        tr_error_propagate(error, &my_error);
    }
    else
    {
        memcpy(path_template, path, path_size);
    }

    tr_free(path);
}

bool tr_sys_path_exists(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool ret = false;
    HANDLE handle = INVALID_HANDLE_VALUE;
    wchar_t* wide_path = path_to_native_path(path);

    if (wide_path != nullptr)
    {
        DWORD attributes = GetFileAttributesW(wide_path);

        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                handle = CreateFileW(wide_path, 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                ret = handle != INVALID_HANDLE_VALUE;
            }
            else
            {
                ret = true;
            }
        }
    }

    if (!ret)
    {
        set_system_error_if_file_found(error, GetLastError());
    }

    if (handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
    }

    tr_free(wide_path);

    return ret;
}

bool tr_sys_path_get_info(char const* path, int flags, tr_sys_path_info* info, tr_error** error)
{
    TR_ASSERT(path != nullptr);
    TR_ASSERT(info != nullptr);

    bool ret = false;
    wchar_t* wide_path = path_to_native_path(path);

    if ((flags & TR_SYS_PATH_NO_FOLLOW) == 0)
    {
        HANDLE handle = INVALID_HANDLE_VALUE;

        if (wide_path != nullptr)
        {
            handle = CreateFileW(wide_path, 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        }

        if (handle != INVALID_HANDLE_VALUE)
        {
            tr_error* my_error = nullptr;
            ret = tr_sys_file_get_info(handle, info, &my_error);

            if (!ret)
            {
                tr_error_propagate(error, &my_error);
            }

            CloseHandle(handle);
        }
        else
        {
            set_system_error(error, GetLastError());
        }
    }
    else
    {
        WIN32_FILE_ATTRIBUTE_DATA attributes;

        if (wide_path != nullptr)
        {
            ret = GetFileAttributesExW(wide_path, GetFileExInfoStandard, &attributes);
        }

        if (ret)
        {
            stat_to_sys_path_info(
                attributes.dwFileAttributes,
                attributes.nFileSizeLow,
                attributes.nFileSizeHigh,
                &attributes.ftLastWriteTime,
                info);
        }
        else
        {
            set_system_error(error, GetLastError());
        }
    }

    tr_free(wide_path);

    return ret;
}

bool tr_sys_path_is_relative(std::string_view path)
{
    /* UNC path: `\\...`. */
    if (is_unc_path(path))
    {
        return false;
    }

    /* Local path: `X:` */
    if (std::size(path) == 2 && isalpha(path[0]) && path[1] == ':')
    {
        return false;
    }

    /* Local path: `X:\...`. */
    if (std::size(path) > 2 && isalpha(path[0]) && path[1] == ':' && is_slash(path[2]))
    {
        return false;
    }

    return true;
}

bool tr_sys_path_is_same(char const* path1, char const* path2, tr_error** error)
{
    TR_ASSERT(path1 != nullptr);
    TR_ASSERT(path2 != nullptr);

    bool ret = false;
    wchar_t* wide_path1 = nullptr;
    wchar_t* wide_path2 = nullptr;
    HANDLE handle1 = INVALID_HANDLE_VALUE;
    HANDLE handle2 = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION fi1, fi2;

    wide_path1 = path_to_native_path(path1);

    if (wide_path1 == nullptr)
    {
        goto fail;
    }

    wide_path2 = path_to_native_path(path2);

    if (wide_path2 == nullptr)
    {
        goto fail;
    }

    handle1 = CreateFileW(wide_path1, 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    if (handle1 == INVALID_HANDLE_VALUE)
    {
        goto fail;
    }

    handle2 = CreateFileW(wide_path2, 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

    if (handle2 == INVALID_HANDLE_VALUE)
    {
        goto fail;
    }

    /* TODO: Use GetFileInformationByHandleEx on >= Server 2012 */

    if (!GetFileInformationByHandle(handle1, &fi1) || !GetFileInformationByHandle(handle2, &fi2))
    {
        goto fail;
    }

    ret = fi1.dwVolumeSerialNumber == fi2.dwVolumeSerialNumber && fi1.nFileIndexHigh == fi2.nFileIndexHigh &&
        fi1.nFileIndexLow == fi2.nFileIndexLow;

    goto cleanup;

fail:
    set_system_error_if_file_found(error, GetLastError());

cleanup:
    CloseHandle(handle2);
    CloseHandle(handle1);

    tr_free(wide_path2);
    tr_free(wide_path1);

    return ret;
}

char* tr_sys_path_resolve(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    char* ret = nullptr;
    wchar_t* wide_path;
    wchar_t* wide_ret = nullptr;
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD wide_ret_size;

    wide_path = path_to_native_path(path);

    if (wide_path == nullptr)
    {
        goto fail;
    }

    handle = CreateFileW(
        wide_path,
        FILE_READ_EA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (handle == INVALID_HANDLE_VALUE)
    {
        goto fail;
    }

    wide_ret_size = GetFinalPathNameByHandleW(handle, nullptr, 0, 0);

    if (wide_ret_size == 0)
    {
        goto fail;
    }

    wide_ret = tr_new(wchar_t, wide_ret_size);

    if (GetFinalPathNameByHandleW(handle, wide_ret, wide_ret_size, 0) != wide_ret_size - 1)
    {
        goto fail;
    }

    TR_ASSERT(wcsncmp(wide_ret, L"\\\\?\\", 4) == 0);

    ret = native_path_to_path(wide_ret);

    if (ret != nullptr)
    {
        goto cleanup;
    }

fail:
    set_system_error(error, GetLastError());

    tr_free(ret);
    ret = nullptr;

cleanup:
    tr_free(wide_ret);
    tr_free(wide_path);

    if (handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
    }

    return ret;
}

std::string tr_sys_path_basename(std::string_view path, tr_error** error)
{
    if (std::empty(path))
    {
        return ".";
    }

    if (!is_valid_path(path))
    {
        set_system_error(error, ERROR_PATH_NOT_FOUND);
        return {};
    }

    char const* const begin = std::data(path);
    char const* end = begin + std::size(path);

    while (end > begin && is_slash(*(end - 1)))
    {
        --end;
    }

    if (end == begin)
    {
        return "/";
    }

    char const* name = end;

    while (name > begin && *(name - 1) != ':' && !is_slash(*(name - 1)))
    {
        --name;
    }

    if (name == end)
    {
        return "/";
    }

    return { name, size_t(end - name) };
}

[[nodiscard]] static bool isWindowsDeviceRoot(char ch) noexcept
{
    return isalpha(static_cast<int>(ch)) != 0;
}

[[nodiscard]] static constexpr bool isPathSeparator(char ch) noexcept
{
    return ch == '/' || ch == '\\';
}

// This function is adapted from Node.js's path.win32.dirname() function,
// which is copyrighted by Joyent, Inc. and other Node contributors
// and is distributed under MIT (SPDX:MIT) license.
std::string_view tr_sys_path_dirname(std::string_view path)
{
    auto const len = std::size(path);

    if (len == 0)
    {
        return "."sv;
    }

    if (len == 1)
    {
        return isPathSeparator(path[0]) ? path : "."sv;
    }

    auto root_end = std::string_view::npos;
    auto offset = std::string_view::size_type{ 0 };

    // Try to match a root
    if (isPathSeparator(path[0]))
    {
        // Possible UNC root

        root_end = offset = 1;

        if (isPathSeparator(path[1]))
        {
            // Matched double path separator at beginning
            std::string_view::size_type j = 2;
            std::string_view::size_type last = j;
            // Match 1 or more non-path separators
            while (j < len && !isPathSeparator(path[j]))
            {
                j++;
            }
            if (j < len && j != last)
            {
                // Matched!
                last = j;
                // Match 1 or more path separators
                while (j < len && isPathSeparator(path[j]))
                {
                    j++;
                }
                if (j < len && j != last)
                {
                    // Matched!
                    last = j;
                    // Match 1 or more non-path separators
                    while (j < len && !isPathSeparator(path[j]))
                    {
                        j++;
                    }
                    if (j == len)
                    {
                        // We matched a UNC root only
                        return path;
                    }
                    if (j != last)
                    {
                        // We matched a UNC root with leftovers

                        // Offset by 1 to include the separator after the UNC root to
                        // treat it as a "normal root" on top of a (UNC) root
                        root_end = offset = j + 1;
                    }
                }
            }
        }
        // Possible device root
    }
    else if (isWindowsDeviceRoot(path[0]) && path[1] == ':')
    {
        root_end = len > 2 && isPathSeparator(path[2]) ? 3 : 2;
        offset = root_end;
    }

    auto end = std::string_view::npos;
    auto matched_slash = bool{ true };
    for (std::string_view::size_type i = len - 1; i >= offset; --i)
    {
        if (isPathSeparator(path[i]))
        {
            if (!matched_slash)
            {
                end = i;
                break;
            }
        }
        else
        {
            // We saw the first non-path separator
            matched_slash = false;
        }
        if (i <= offset)
        {
            break;
        }
    }

    if (end == std::string_view::npos)
    {
        if (root_end == std::string_view::npos)
        {
            return "."sv;
        }

        end = root_end;
    }
    return path.substr(0, end);
}

bool tr_sys_path_rename(char const* src_path, char const* dst_path, tr_error** error)
{
    TR_ASSERT(src_path != nullptr);
    TR_ASSERT(dst_path != nullptr);

    bool ret = false;
    wchar_t* wide_src_path = path_to_native_path(src_path);
    wchar_t* wide_dst_path = path_to_native_path(dst_path);

    if (wide_src_path != nullptr && wide_dst_path != nullptr)
    {
        DWORD flags = MOVEFILE_REPLACE_EXISTING;
        DWORD attributes;

        attributes = GetFileAttributesW(wide_src_path);

        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            flags = 0;
        }
        else
        {
            attributes = GetFileAttributesW(wide_dst_path);

            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                flags = 0;
            }
        }

        ret = MoveFileExW(wide_src_path, wide_dst_path, flags);
    }

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    tr_free(wide_dst_path);
    tr_free(wide_src_path);

    return ret;
}

bool tr_sys_path_copy(char const* src_path, char const* dst_path, tr_error** error)
{
    TR_ASSERT(src_path != nullptr);
    TR_ASSERT(dst_path != nullptr);

    bool ret = false;

    wchar_t* wide_src_path = path_to_native_path(src_path);
    wchar_t* wide_dst_path = path_to_native_path(dst_path);

    if (wide_src_path == nullptr || wide_dst_path == nullptr)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        goto out;
    }

    auto cancel = BOOL{ FALSE };
    DWORD const flags = COPY_FILE_ALLOW_DECRYPTED_DESTINATION | COPY_FILE_FAIL_IF_EXISTS;
    if (CopyFileExW(wide_src_path, wide_dst_path, nullptr, nullptr, &cancel, flags) == 0)
    {
        set_system_error(error, GetLastError());
        goto out;
    }
    else
    {
        ret = true;
    }

out:
    tr_free(wide_src_path);
    tr_free(wide_dst_path);

    return ret;
}

bool tr_sys_path_remove(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool ret = false;
    wchar_t* wide_path = path_to_native_path(path);

    if (wide_path != nullptr)
    {
        DWORD const attributes = GetFileAttributesW(wide_path);

        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                ret = RemoveDirectoryW(wide_path);
            }
            else
            {
                ret = DeleteFileW(wide_path);
            }
        }
    }

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    tr_free(wide_path);

    return ret;
}

char* tr_sys_path_native_separators(char* path)
{
    if (path == nullptr)
    {
        return nullptr;
    }

    for (char* slash = strchr(path, '/'); slash != nullptr; slash = strchr(slash, '/'))
    {
        *slash = '\\';
    }

    return path;
}

tr_sys_file_t tr_sys_file_get_std(tr_std_sys_file_t std_file, tr_error** error)
{
    tr_sys_file_t ret = TR_BAD_SYS_FILE;

    switch (std_file)
    {
    case TR_STD_SYS_FILE_IN:
        ret = GetStdHandle(STD_INPUT_HANDLE);
        break;

    case TR_STD_SYS_FILE_OUT:
        ret = GetStdHandle(STD_OUTPUT_HANDLE);
        break;

    case TR_STD_SYS_FILE_ERR:
        ret = GetStdHandle(STD_ERROR_HANDLE);
        break;

    default:
        TR_ASSERT_MSG(false, fmt::format(FMT_STRING("unknown standard file {:d}"), std_file));
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return TR_BAD_SYS_FILE;
    }

    if (ret == TR_BAD_SYS_FILE)
    {
        set_system_error(error, GetLastError());
    }
    else if (ret == nullptr)
    {
        ret = TR_BAD_SYS_FILE;
    }

    return ret;
}

tr_sys_file_t tr_sys_file_open(char const* path, int flags, int /*permissions*/, tr_error** error)
{
    TR_ASSERT(path != nullptr);
    TR_ASSERT((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) != 0);

    tr_sys_file_t ret;
    DWORD native_access = 0;
    DWORD native_disposition = OPEN_EXISTING;
    DWORD native_flags = FILE_ATTRIBUTE_NORMAL;
    bool success;

    if ((flags & TR_SYS_FILE_READ) != 0)
    {
        native_access |= GENERIC_READ;
    }

    if ((flags & TR_SYS_FILE_WRITE) != 0)
    {
        native_access |= GENERIC_WRITE;
    }

    if ((flags & TR_SYS_FILE_CREATE_NEW) != 0)
    {
        native_disposition = CREATE_NEW;
    }
    else if ((flags & TR_SYS_FILE_CREATE) != 0)
    {
        native_disposition = (flags & TR_SYS_FILE_TRUNCATE) != 0 ? CREATE_ALWAYS : OPEN_ALWAYS;
    }
    else if ((flags & TR_SYS_FILE_TRUNCATE) != 0)
    {
        native_disposition = TRUNCATE_EXISTING;
    }

    if ((flags & TR_SYS_FILE_SEQUENTIAL) != 0)
    {
        native_flags |= FILE_FLAG_SEQUENTIAL_SCAN;
    }

    ret = open_file(path, native_access, native_disposition, native_flags, error);

    success = ret != TR_BAD_SYS_FILE;

    if (success && (flags & TR_SYS_FILE_APPEND) != 0)
    {
        success = SetFilePointer(ret, 0, nullptr, FILE_END) != INVALID_SET_FILE_POINTER;
    }

    if (!success)
    {
        if (error == nullptr)
        {
            set_system_error(error, GetLastError());
        }

        CloseHandle(ret);
        ret = TR_BAD_SYS_FILE;
    }

    return ret;
}

static void file_open_temp_callback(char const* path, void* param, tr_error** error)
{
    tr_sys_file_t* result = (tr_sys_file_t*)param;

    TR_ASSERT(result != nullptr);

    *result = open_file(path, GENERIC_READ | GENERIC_WRITE, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, error);
}

tr_sys_file_t tr_sys_file_open_temp(char* path_template, tr_error** error)
{
    TR_ASSERT(path_template != nullptr);

    tr_sys_file_t ret = TR_BAD_SYS_FILE;

    create_temp_path(path_template, file_open_temp_callback, &ret, error);

    return ret;
}

bool tr_sys_file_close(tr_sys_file_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool ret = CloseHandle(handle);

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_get_info(tr_sys_file_t handle, tr_sys_path_info* info, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(info != nullptr);

    BY_HANDLE_FILE_INFORMATION attributes;
    bool ret = GetFileInformationByHandle(handle, &attributes);

    if (ret)
    {
        stat_to_sys_path_info(
            attributes.dwFileAttributes,
            attributes.nFileSizeLow,
            attributes.nFileSizeHigh,
            &attributes.ftLastWriteTime,
            info);
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_seek(tr_sys_file_t handle, int64_t offset, tr_seek_origin_t origin, uint64_t* new_offset, tr_error** error)
{
    static_assert(TR_SEEK_SET == FILE_BEGIN, "values should match");
    static_assert(TR_SEEK_CUR == FILE_CURRENT, "values should match");
    static_assert(TR_SEEK_END == FILE_END, "values should match");

    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(origin == TR_SEEK_SET || origin == TR_SEEK_CUR || origin == TR_SEEK_END);

    bool ret = false;
    LARGE_INTEGER native_offset;
    LARGE_INTEGER new_native_pointer;

    native_offset.QuadPart = offset;

    if (SetFilePointerEx(handle, native_offset, &new_native_pointer, origin))
    {
        if (new_offset != nullptr)
        {
            *new_offset = new_native_pointer.QuadPart;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_read(tr_sys_file_t handle, void* buffer, uint64_t size, uint64_t* bytes_read, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_read;

    if (ReadFile(handle, buffer, (DWORD)size, &my_bytes_read, nullptr))
    {
        if (bytes_read != nullptr)
        {
            *bytes_read = my_bytes_read;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_read_at(
    tr_sys_file_t handle,
    void* buffer,
    uint64_t size,
    uint64_t offset,
    uint64_t* bytes_read,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    OVERLAPPED overlapped;
    DWORD my_bytes_read;

    overlapped.Offset = (DWORD)offset;
    offset >>= 32;
    overlapped.OffsetHigh = (DWORD)offset;
    overlapped.hEvent = nullptr;

    if (ReadFile(handle, buffer, (DWORD)size, &my_bytes_read, &overlapped))
    {
        if (bytes_read != nullptr)
        {
            *bytes_read = my_bytes_read;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_write(tr_sys_file_t handle, void const* buffer, uint64_t size, uint64_t* bytes_written, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_written;

    if (WriteFile(handle, buffer, (DWORD)size, &my_bytes_written, nullptr))
    {
        if (bytes_written != nullptr)
        {
            *bytes_written = my_bytes_written;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_write_at(
    tr_sys_file_t handle,
    void const* buffer,
    uint64_t size,
    uint64_t offset,
    uint64_t* bytes_written,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    OVERLAPPED overlapped;
    DWORD my_bytes_written;

    overlapped.Offset = (DWORD)offset;
    offset >>= 32;
    overlapped.OffsetHigh = (DWORD)offset;
    overlapped.hEvent = nullptr;

    if (WriteFile(handle, buffer, (DWORD)size, &my_bytes_written, &overlapped))
    {
        if (bytes_written != nullptr)
        {
            *bytes_written = my_bytes_written;
        }

        ret = true;
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_flush(tr_sys_file_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool ret = FlushFileBuffers(handle);

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_truncate(tr_sys_file_t handle, uint64_t size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    FILE_END_OF_FILE_INFO info;
    info.EndOfFile.QuadPart = size;

    bool ret = SetFileInformationByHandle(handle, FileEndOfFileInfo, &info, sizeof(info));

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_advise(
    [[maybe_unused]] tr_sys_file_t handle,
    uint64_t /*offset*/,
    [[maybe_unused]] uint64_t size,
    [[maybe_unused]] tr_sys_file_advice_t advice,
    tr_error** /*error*/)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(size > 0);
    TR_ASSERT(advice == TR_SYS_FILE_ADVICE_WILL_NEED || advice == TR_SYS_FILE_ADVICE_DONT_NEED);

    bool ret = true;

    /* ??? */

    return ret;
}

bool tr_sys_file_preallocate(tr_sys_file_t handle, uint64_t size, int flags, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    if ((flags & TR_SYS_FILE_PREALLOC_SPARSE) != 0)
    {
        DWORD tmp;

        if (!DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmp, nullptr))
        {
            set_system_error(error, GetLastError());
            return false;
        }
    }

    return tr_sys_file_truncate(handle, size, error);
}

void* tr_sys_file_map_for_reading(tr_sys_file_t handle, uint64_t offset, uint64_t size, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(size > 0);

    if (size > MAXSIZE_T)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return nullptr;
    }

    void* ret = nullptr;
    HANDLE mappingHandle = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);

    if (mappingHandle != nullptr)
    {
        ULARGE_INTEGER native_offset;

        native_offset.QuadPart = offset;

        ret = MapViewOfFile(mappingHandle, FILE_MAP_READ, native_offset.u.HighPart, native_offset.u.LowPart, (SIZE_T)size);
    }

    if (ret == nullptr)
    {
        set_system_error(error, GetLastError());
    }

    CloseHandle(mappingHandle);

    return ret;
}

bool tr_sys_file_unmap(void const* address, [[maybe_unused]] uint64_t size, tr_error** error)
{
    TR_ASSERT(address != nullptr);
    TR_ASSERT(size > 0);

    bool ret = UnmapViewOfFile(address);

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_lock(tr_sys_file_t handle, int operation, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT((operation & ~(TR_SYS_FILE_LOCK_SH | TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB | TR_SYS_FILE_LOCK_UN)) == 0);
    TR_ASSERT(
        !!(operation & TR_SYS_FILE_LOCK_SH) + !!(operation & TR_SYS_FILE_LOCK_EX) + !!(operation & TR_SYS_FILE_LOCK_UN) == 1);

    bool ret;
    auto overlapped = OVERLAPPED{};

    if ((operation & TR_SYS_FILE_LOCK_UN) == 0)
    {
        DWORD native_flags = 0;

        if ((operation & TR_SYS_FILE_LOCK_EX) != 0)
        {
            native_flags |= LOCKFILE_EXCLUSIVE_LOCK;
        }

        if ((operation & TR_SYS_FILE_LOCK_NB) != 0)
        {
            native_flags |= LOCKFILE_FAIL_IMMEDIATELY;
        }

        ret = LockFileEx(handle, native_flags, 0, MAXDWORD, MAXDWORD, &overlapped) != FALSE;
    }
    else
    {
        ret = UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &overlapped) != FALSE;
    }

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

char* tr_sys_dir_get_current(tr_error** error)
{
    char* ret = nullptr;
    wchar_t* wide_ret = nullptr;
    DWORD size;

    size = GetCurrentDirectoryW(0, nullptr);

    if (size != 0)
    {
        wide_ret = tr_new(wchar_t, size);

        if (GetCurrentDirectoryW(size, wide_ret) != 0)
        {
            ret = tr_win32_native_to_utf8(wide_ret, size);
        }
    }

    if (ret == nullptr)
    {
        set_system_error(error, GetLastError());
    }

    tr_free(wide_ret);

    return ret;
}

bool tr_sys_dir_create(char const* path, int flags, int permissions, tr_error** error)
{
    return create_dir(path, flags, permissions, true, error);
}

static void dir_create_temp_callback(char const* path, void* param, tr_error** error)
{
    bool* result = (bool*)param;

    TR_ASSERT(result != nullptr);

    *result = create_dir(path, 0, 0, false, error);
}

bool tr_sys_dir_create_temp(char* path_template, tr_error** error)
{
    TR_ASSERT(path_template != nullptr);

    bool ret = false;

    create_temp_path(path_template, dir_create_temp_callback, &ret, error);

    return ret;
}

tr_sys_dir_t tr_sys_dir_open(char const* path, tr_error** error)
{
#ifndef __clang__
    /* Clang gives "static_assert expression is not an integral constant expression" error */
    static_assert(TR_BAD_SYS_DIR == nullptr, "values should match");
#endif

    TR_ASSERT(path != nullptr);

    tr_sys_dir_t ret = tr_new(struct tr_sys_dir_win32, 1);

    int pattern_size;
    ret->pattern = path_to_native_path_ex(path, 2, &pattern_size);

    if (ret->pattern != nullptr)
    {
        ret->pattern[pattern_size + 0] = L'\\';
        ret->pattern[pattern_size + 1] = L'*';

        ret->find_handle = INVALID_HANDLE_VALUE;
        ret->utf8_name = nullptr;
    }
    else
    {
        set_system_error(error, GetLastError());

        tr_free(ret->pattern);
        tr_free(ret);

        ret = nullptr;
    }

    return ret;
}

char const* tr_sys_dir_read_name(tr_sys_dir_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    DWORD error_code = ERROR_SUCCESS;

    if (handle->find_handle == INVALID_HANDLE_VALUE)
    {
        handle->find_handle = FindFirstFileW(handle->pattern, &handle->find_data);

        if (handle->find_handle == INVALID_HANDLE_VALUE)
        {
            error_code = GetLastError();
        }
    }
    else
    {
        if (!FindNextFileW(handle->find_handle, &handle->find_data))
        {
            error_code = GetLastError();
        }
    }

    if (error_code != ERROR_SUCCESS)
    {
        set_system_error_if_file_found(error, error_code);
        return nullptr;
    }

    char* ret = tr_win32_native_to_utf8(handle->find_data.cFileName, -1);

    if (ret != nullptr)
    {
        tr_free(handle->utf8_name);
        handle->utf8_name = ret;
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_dir_close(tr_sys_dir_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    bool ret = FindClose(handle->find_handle);

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    tr_free(handle->utf8_name);
    tr_free(handle->pattern);
    tr_free(handle);

    return ret;
}
