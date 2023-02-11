// This file Copyright © 2013-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cctype> // for isalpha()
#include <cstring>
#include <iterator> // for std::back_inserter
#include <optional>
#include <string>
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
    std::wstring pattern;
    HANDLE find_handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW find_data = {};
    std::string utf8_name;
};

static auto constexpr NativeLocalPathPrefix = L"\\\\?\\"sv;
static auto constexpr NativeUncPathPrefix = L"\\\\?\\UNC\\"sv;

static void set_system_error(tr_error** error, DWORD code)
{
    if (error == nullptr)
    {
        return;
    }

    if (auto const message = tr_win32_format_message(code); !std::empty(message))
    {
        tr_error_set(error, code, message);
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

static constexpr time_t filetime_to_unix_time(FILETIME const& t)
{
    uint64_t tmp = 0;
    tmp |= t.dwHighDateTime;
    tmp <<= 32;
    tmp |= t.dwLowDateTime;
    tmp /= 10; /* to microseconds */
    tmp -= DELTA_EPOCH_IN_MICROSECS;

    return tmp / 1000000UL;
}

static constexpr auto stat_to_sys_path_info(DWORD attributes, DWORD size_low, DWORD size_high, FILETIME const& mtime)
{
    auto info = tr_sys_path_info{};

    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        info.type = TR_SYS_PATH_IS_DIRECTORY;
    }
    else if ((attributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_VIRTUAL)) == 0)
    {
        info.type = TR_SYS_PATH_IS_FILE;
    }
    else
    {
        info.type = TR_SYS_PATH_IS_OTHER;
    }

    info.size = size_high;
    info.size <<= 32;
    info.size |= size_low;

    info.last_modified_at = filetime_to_unix_time(mtime);

    return info;
}

static auto constexpr Slashes = "\\/"sv;

static constexpr bool is_slash(char c)
{
    return tr_strvContains(Slashes, c);
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

namespace
{
namespace path_to_native_path_helpers
{

auto path_to_fixed_native_path(std::string_view path)
{
    auto wide_path = tr_win32_utf8_to_native(path);

    // convert '/' to '\'
    static auto constexpr Convert = [](wchar_t wch)
    {
        return wch == L'/' ? L'\\' : wch;
    };
    std::transform(std::begin(wide_path), std::end(wide_path), std::begin(wide_path), Convert);

    // squash multiple consecutive separators into one to avoid ERROR_INVALID_NAME
    static auto constexpr Equal = [](wchar_t a, wchar_t b)
    {
        return a == b && a == L'\\';
    };
    auto const tmp = wide_path;
    wide_path.clear();
    std::unique_copy(std::begin(tmp), std::end(tmp), std::back_inserter(wide_path), Equal);

    return wide_path;
}

} // namespace path_to_native_path_helpers
} // namespace

/* Extending maximum path length limit up to ~32K. See "Naming Files, Paths, and Namespaces"
   https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx for more info */
static auto path_to_native_path(std::string_view path)
{
    using namespace path_to_native_path_helpers;

    if (is_unc_path(path))
    {
        // UNC path: "\\server\share" -> "\\?\UNC\server\share"
        path.remove_prefix(2); // remove path's UNC prefix slashes
        auto wide_path = path_to_fixed_native_path(path);
        wide_path.insert(0, NativeUncPathPrefix);
        return wide_path;
    }

    if (!tr_sys_path_is_relative(path))
    {
        // local path: "C:" -> "\\?\C:"
        auto wide_path = path_to_fixed_native_path(path);
        wide_path.insert(0, NativeLocalPathPrefix);
        return wide_path;
    }

    return path_to_fixed_native_path(path);
}

static std::string native_path_to_path(std::wstring_view wide_path)
{
    if (std::empty(wide_path))
    {
        return {};
    }

    if (tr_strvStartsWith(wide_path, NativeUncPathPrefix))
    {
        wide_path.remove_prefix(std::size(NativeUncPathPrefix));
        auto path = tr_win32_native_to_utf8(wide_path);
        path.insert(0, "\\\\"sv);
        return path;
    }

    if (tr_strvStartsWith(wide_path, NativeLocalPathPrefix))
    {
        wide_path.remove_prefix(std::size(NativeLocalPathPrefix));
        return tr_win32_native_to_utf8(wide_path);
    }

    return tr_win32_native_to_utf8(wide_path);
}

static tr_sys_file_t open_file(std::string_view path, DWORD access, DWORD disposition, DWORD flags, tr_error** error)
{
    tr_sys_file_t ret = TR_BAD_SYS_FILE;

    if (auto const wide_path = path_to_native_path(path); !std::empty(wide_path))
    {
        ret = CreateFileW(
            wide_path.c_str(),
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

    return ret;
}

static bool create_dir(std::string_view path, int flags, int /*permissions*/, bool okay_if_exists, tr_error** error)
{
    bool ret;
    DWORD error_code = ERROR_SUCCESS;
    auto const wide_path = path_to_native_path(path);

    // already exists (no-op)
    if (auto const info = tr_sys_path_get_info(path); info && info->isFolder())
    {
        return true;
    }

    if ((flags & TR_SYS_DIR_CREATE_PARENTS) != 0)
    {
        error_code = SHCreateDirectoryExW(nullptr, wide_path.c_str(), nullptr);
        ret = error_code == ERROR_SUCCESS;
    }
    else
    {
        ret = CreateDirectoryW(wide_path.c_str(), nullptr);

        if (!ret)
        {
            error_code = GetLastError();
        }
    }

    if (!ret && error_code == ERROR_ALREADY_EXISTS && okay_if_exists)
    {
        DWORD const attributes = GetFileAttributesW(wide_path.c_str());

        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            ret = true;
        }
    }

    if (!ret)
    {
        set_system_error(error, error_code);
    }

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

    auto path = std::string{ path_template };
    auto path_size = std::size(path);

    TR_ASSERT(path_size > 0);

    tr_error* my_error = nullptr;

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        size_t i = path_size;

        while (i > 0 && path_template[i - 1] == 'X')
        {
            static auto constexpr Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"sv;
            path[i - 1] = Chars[tr_rand_int(std::size(Chars))];
            --i;
        }

        TR_ASSERT(path_size >= i + 6);

        tr_error_clear(&my_error);

        (*callback)(path.c_str(), callback_param, &my_error);

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
        std::copy_n(std::begin(path), path_size, path_template);
    }
}

bool tr_sys_path_exists(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool ret = false;
    HANDLE handle = INVALID_HANDLE_VALUE;

    if (auto const wide_path = path_to_native_path(path); !std::empty(wide_path))
    {
        DWORD attributes = GetFileAttributesW(wide_path.c_str());

        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                handle = CreateFileW(wide_path.c_str(), 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
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

    return ret;
}

static std::optional<tr_sys_path_info> tr_sys_file_get_info_(tr_sys_file_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    auto attributes = BY_HANDLE_FILE_INFORMATION{};
    if (GetFileInformationByHandle(handle, &attributes))
    {
        return stat_to_sys_path_info(
            attributes.dwFileAttributes,
            attributes.nFileSizeLow,
            attributes.nFileSizeHigh,
            attributes.ftLastWriteTime);
    }

    set_system_error(error, GetLastError());
    return {};
}

std::optional<tr_sys_path_info> tr_sys_path_get_info(std::string_view path, int flags, tr_error** error)
{
    if (auto const wide_path = path_to_native_path(path); std::empty(wide_path))
    {
        // do nothing
    }
    else if ((flags & TR_SYS_PATH_NO_FOLLOW) != 0)
    {
        auto attributes = WIN32_FILE_ATTRIBUTE_DATA{};
        if (GetFileAttributesExW(wide_path.c_str(), GetFileExInfoStandard, &attributes))
        {
            return stat_to_sys_path_info(
                attributes.dwFileAttributes,
                attributes.nFileSizeLow,
                attributes.nFileSizeHigh,
                attributes.ftLastWriteTime);
        }
    }
    else if (auto const
                 handle = CreateFileW(wide_path.c_str(), 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
             handle != INVALID_HANDLE_VALUE)
    {
        auto ret = tr_sys_file_get_info_(handle, error);
        CloseHandle(handle);
        return ret;
    }

    set_system_error(error, GetLastError());
    return {};
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

static std::optional<BY_HANDLE_FILE_INFORMATION> get_file_info(char const* path, tr_error** error)
{
    auto const wpath = path_to_native_path(path);
    if (std::empty(wpath))
    {
        set_system_error_if_file_found(error, GetLastError());
        return {};
    }

    auto const handle = CreateFileW(wpath.c_str(), 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        set_system_error_if_file_found(error, GetLastError());
        return {};
    }

    // TODO: Use GetFileInformationByHandleEx on >= Server 2012
    auto info = BY_HANDLE_FILE_INFORMATION{};
    if (!GetFileInformationByHandle(handle, &info))
    {
        set_system_error_if_file_found(error, GetLastError());
        CloseHandle(handle);
        return {};
    }

    CloseHandle(handle);
    return info;
}

bool tr_sys_path_is_same(char const* path1, char const* path2, tr_error** error)
{
    TR_ASSERT(path1 != nullptr);
    TR_ASSERT(path2 != nullptr);

    auto const fi1 = get_file_info(path1, error);
    if (!fi1)
    {
        return false;
    }

    auto const fi2 = get_file_info(path2, error);
    if (!fi2)
    {
        return false;
    }

    return fi1->dwVolumeSerialNumber == fi2->dwVolumeSerialNumber && fi1->nFileIndexHigh == fi2->nFileIndexHigh &&
        fi1->nFileIndexLow == fi2->nFileIndexLow;
}

std::string tr_sys_path_resolve(std::string_view path, tr_error** error)
{
    auto ret = std::string{};

    if (auto const wide_path = path_to_native_path(path); !std::empty(wide_path))
    {
        if (auto const handle = CreateFileW(
                wide_path.c_str(),
                FILE_READ_EA,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                nullptr);
            handle != INVALID_HANDLE_VALUE)
        {
            if (auto const wide_ret_size = GetFinalPathNameByHandleW(handle, nullptr, 0, 0); wide_ret_size != 0)
            {
                auto wide_ret = std::wstring{};
                wide_ret.resize(wide_ret_size);
                if (GetFinalPathNameByHandleW(handle, std::data(wide_ret), wide_ret_size, 0) == wide_ret_size - 1)
                {
                    // `wide_ret_size` includes the terminating '\0'; remove it from `wide_ret`
                    wide_ret.resize(std::size(wide_ret) - 1);
                    TR_ASSERT(tr_strvStartsWith(wide_ret, NativeLocalPathPrefix));
                    ret = native_path_to_path(wide_ret);
                }
            }

            CloseHandle(handle);
        }
    }

    if (!std::empty(ret))
    {
        return ret;
    }

    set_system_error(error, GetLastError());
    return {};
}

std::string_view tr_sys_path_basename(std::string_view path, tr_error** error)
{
    if (std::empty(path))
    {
        return "."sv;
    }

    if (!is_valid_path(path))
    {
        set_system_error(error, ERROR_PATH_NOT_FOUND);
        return {};
    }

    // Remove all trailing slashes.
    // If nothing is left, return "/"
    if (auto const pos = path.find_last_not_of(Slashes); pos != std::string_view::npos)
    {
        path = path.substr(0, pos + 1);
    }
    else // all slashes
    {
        return "/"sv;
    }

    if (auto pos = path.find_last_of("\\/:"); pos != std::string_view::npos)
    {
        path.remove_prefix(pos + 1);
    }

    return !std::empty(path) ? path : "/"sv;
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
    auto const wide_src_path = path_to_native_path(src_path);
    auto const wide_dst_path = path_to_native_path(dst_path);

    if (!std::empty(wide_src_path) && !std::empty(wide_dst_path))
    {
        DWORD flags = MOVEFILE_REPLACE_EXISTING;
        DWORD attributes;

        attributes = GetFileAttributesW(wide_src_path.c_str());

        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            flags = 0;
        }
        else
        {
            attributes = GetFileAttributesW(wide_dst_path.c_str());

            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                flags = 0;
            }
        }

        ret = MoveFileExW(wide_src_path.c_str(), wide_dst_path.c_str(), flags);
    }

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_path_copy(char const* src_path, char const* dst_path, tr_error** error)
{
    TR_ASSERT(src_path != nullptr);
    TR_ASSERT(dst_path != nullptr);

    auto const wide_src_path = path_to_native_path(src_path);
    auto const wide_dst_path = path_to_native_path(dst_path);
    if (std::empty(wide_src_path) || std::empty(wide_dst_path))
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    auto cancel = BOOL{ FALSE };
    DWORD const flags = COPY_FILE_ALLOW_DECRYPTED_DESTINATION | COPY_FILE_FAIL_IF_EXISTS;
    if (CopyFileExW(wide_src_path.c_str(), wide_dst_path.c_str(), nullptr, nullptr, &cancel, flags) == 0)
    {
        set_system_error(error, GetLastError());
        return false;
    }

    return true;
}

bool tr_sys_path_remove(char const* path, tr_error** error)
{
    TR_ASSERT(path != nullptr);

    bool ret = false;

    if (auto const wide_path = path_to_native_path(path); !std::empty(wide_path))
    {
        DWORD const attributes = GetFileAttributesW(wide_path.c_str());

        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                ret = RemoveDirectoryW(wide_path.c_str());
            }
            else
            {
                ret = DeleteFileW(wide_path.c_str());
            }
        }
    }

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

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

    if ((flags & TR_SYS_FILE_CREATE) != 0)
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

bool tr_sys_file_flush_possible(tr_sys_file_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    DWORD type = GetFileType(handle);

    if (type == FILE_TYPE_UNKNOWN)
    {
        set_system_error(error, GetLastError());
        return false;
    }

    return type == FILE_TYPE_DISK;
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

std::string tr_sys_dir_get_current(tr_error** error)
{
    if (auto const size = GetCurrentDirectoryW(0, nullptr); size != 0)
    {
        auto wide_ret = std::wstring{};
        wide_ret.resize(size);
        if (GetCurrentDirectoryW(std::size(wide_ret), std::data(wide_ret)) != 0)
        {
            // `size` includes the terminating '\0'; remove it from `wide_ret`
            wide_ret.resize(std::size(wide_ret) - 1);
            return tr_win32_native_to_utf8(wide_ret);
        }
    }

    set_system_error(error, GetLastError());
    return {};
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
    TR_ASSERT(path != nullptr);

    if (auto const info = tr_sys_path_get_info(path, 0); !info || !info->isFolder())
    {
        set_system_error(error, ERROR_DIRECTORY);
        return TR_BAD_SYS_DIR;
    }

    auto pattern = path_to_native_path(path);
    if (std::empty(pattern))
    {
        set_system_error(error, GetLastError());
        return TR_BAD_SYS_DIR;
    }

    auto* const ret = new tr_sys_dir_win32{};
    ret->pattern = pattern;
    ret->pattern.append(L"\\*");
    return ret;
}

char const* tr_sys_dir_read_name(tr_sys_dir_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    DWORD error_code = ERROR_SUCCESS;

    if (handle->find_handle == INVALID_HANDLE_VALUE)
    {
        handle->find_handle = FindFirstFileW(handle->pattern.c_str(), &handle->find_data);

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

    if (auto const utf8 = tr_win32_native_to_utf8(handle->find_data.cFileName); !std::empty(utf8))
    {
        handle->utf8_name = utf8;
        return handle->utf8_name.c_str();
    }

    set_system_error(error, GetLastError());
    return nullptr;
}

bool tr_sys_dir_close(tr_sys_dir_t handle, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    bool ret = FindClose(handle->find_handle);

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    delete handle;

    return ret;
}
