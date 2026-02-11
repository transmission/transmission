// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cctype> // for isalpha()
#include <cstring>
#include <ctime>
#include <iterator> // for std::back_inserter
#include <optional>
#include <string>
#include <string_view>

#include <shlobj.h> /* SHCreateDirectoryEx() */
#include <winioctl.h> /* FSCTL_SET_SPARSE */

#include <fmt/format.h>

#include "lib/base/string-utils.h"
#include "lib/base/tr-assert.h"

#include "libtransmission/crypto-utils.h" /* tr_rand_int() */
#include "libtransmission/error.h"
#include "libtransmission/file.h"
#include "libtransmission/types.h"

using namespace std::literals;

struct tr_sys_dir_win32
{
    std::wstring pattern;
    HANDLE find_handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW find_data = {};
    std::string utf8_name;
};

namespace
{
auto constexpr NativeLocalPathPrefix = L"\\\\?\\"sv;
auto constexpr NativeUncPathPrefix = L"\\\\?\\UNC\\"sv;

void set_system_error(tr_error* error, DWORD code)
{
    if (error != nullptr)
    {
        auto const message = tr_win32_format_message(code);
        error->set(code, !std::empty(message) ? message : fmt::format("Unknown error: {:#08x}", code));
    }
}

void set_system_error_if_file_found(tr_error* error, DWORD code)
{
    if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND && code != ERROR_NO_MORE_FILES)
    {
        set_system_error(error, code);
    }
}

constexpr bool to_bool(BOOL value) noexcept
{
    return value != FALSE;
}

auto constexpr Slashes = R"(\/)"sv;

constexpr bool is_slash(char c)
{
    return tr_strv_contains(Slashes, c);
}

constexpr bool is_unc_path(std::string_view path)
{
    return std::size(path) >= 2 && is_slash(path[0]) && path[1] == path[0];
}

bool is_valid_path(std::string_view path)
{
    if (is_unc_path(path))
    {
        if (path[2] != '\0' && isalnum(path[2]) == 0)
        {
            return false;
        }
    }
    else if (auto const pos = path.find(':'); pos != std::string_view::npos)
    {
        if (pos != 1 || isalpha(path[0]) == 0)
        {
            return false;
        }

        path.remove_prefix(2);
    }

    return path.find_first_of(R"(<>:"|?*)"sv) == std::string_view::npos;
}

[[nodiscard]] auto path_to_fixed_native_path(std::string_view const path)
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

/* Extending maximum path length limit up to ~32K. See "Naming Files, Paths, and Namespaces"
   https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx for more info */
[[nodiscard]] auto path_to_native_path(std::string_view path)
{
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

std::string native_path_to_path(std::wstring_view wide_path)
{
    if (std::empty(wide_path))
    {
        return {};
    }

    if (tr_strv_starts_with(wide_path, NativeUncPathPrefix))
    {
        wide_path.remove_prefix(std::size(NativeUncPathPrefix));
        auto path = tr_win32_native_to_utf8(wide_path);
        path.insert(0, R"(\\)"sv);
        return path;
    }

    if (tr_strv_starts_with(wide_path, NativeLocalPathPrefix))
    {
        wide_path.remove_prefix(std::size(NativeLocalPathPrefix));
        return tr_win32_native_to_utf8(wide_path);
    }

    return tr_win32_native_to_utf8(wide_path);
}

[[nodiscard]] tr_sys_file_t open_file(
    std::string_view const path,
    DWORD const access,
    DWORD const disposition,
    DWORD const flags,
    tr_error* error)
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

[[nodiscard]] bool create_dir(
    std::string_view const path,
    int const flags,
    int /*permissions*/,
    bool const okay_if_exists,
    tr_error* error)
{
    bool ret = false;
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
        ret = to_bool(CreateDirectoryW(wide_path.c_str(), nullptr));

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

template<typename CallbackT>
void create_temp_path(char* path_template, CallbackT const& callback, tr_error* error)
{
    TR_ASSERT(path_template != nullptr);

    auto path = std::string{ path_template };
    auto path_size = std::size(path);

    TR_ASSERT(path_size > 0);

    auto local_error = tr_error{};

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

        local_error = {};

        callback(path.c_str(), &local_error);

        if (!local_error)
        {
            break;
        }
    }

    if (!local_error)
    {
        std::copy_n(std::begin(path), path_size, path_template);
    }
    else if (error != nullptr)
    {
        *error = std::move(local_error);
    }
}

} // namespace

std::string_view tr_sys_path_basename(std::string_view path, tr_error* error)
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

namespace
{
[[nodiscard]] bool isWindowsDeviceRoot(char ch) noexcept
{
    return isalpha(static_cast<int>(ch)) != 0;
}

[[nodiscard]] constexpr bool isPathSeparator(char ch) noexcept
{
    return ch == '/' || ch == '\\';
}
} // namespace

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
    auto matched_slash = true;
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

bool tr_sys_path_rename(std::string_view const src_path, std::string_view const dst_path, tr_error* error)
{
    bool ret = false;
    auto const wide_src_path = path_to_native_path(src_path);
    auto const wide_dst_path = path_to_native_path(dst_path);

    if (!std::empty(wide_src_path) && !std::empty(wide_dst_path))
    {
        DWORD flags = MOVEFILE_REPLACE_EXISTING;

        if (auto const src_attributes = GetFileAttributesW(wide_src_path.c_str());
            src_attributes != INVALID_FILE_ATTRIBUTES && (src_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            flags = 0;
        }
        else if (auto const dst_attributes = GetFileAttributesW(wide_dst_path.c_str());
                 dst_attributes != INVALID_FILE_ATTRIBUTES && (dst_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            flags = 0;
        }

        ret = to_bool(MoveFileExW(wide_src_path.c_str(), wide_dst_path.c_str(), flags));
    }

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_path_copy(std::string_view const src_path, std::string_view const dst_path, tr_error* error)
{
    auto const wide_src_path = path_to_native_path(src_path);
    auto const wide_dst_path = path_to_native_path(dst_path);
    if (std::empty(wide_src_path) || std::empty(wide_dst_path))
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    auto cancel = BOOL{ FALSE };
    DWORD const flags = COPY_FILE_ALLOW_DECRYPTED_DESTINATION | COPY_FILE_FAIL_IF_EXISTS;
    if (!to_bool(CopyFileExW(wide_src_path.c_str(), wide_dst_path.c_str(), nullptr, nullptr, &cancel, flags)))
    {
        set_system_error(error, GetLastError());
        return false;
    }

    return true;
}

bool tr_sys_path_remove(std::string_view const path, tr_error* error)
{
    bool ret = false;

    if (auto const wide_path = path_to_native_path(path); !std::empty(wide_path))
    {
        DWORD const attributes = GetFileAttributesW(wide_path.c_str());

        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                ret = to_bool(RemoveDirectoryW(wide_path.c_str()));
            }
            else
            {
                ret = to_bool(DeleteFileW(wide_path.c_str()));
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

tr_sys_file_t tr_sys_file_open(std::string_view const path, int const flags, int /*permissions*/, tr_error* error)
{
    TR_ASSERT((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) != 0);

    DWORD native_access = 0;
    DWORD native_disposition = OPEN_EXISTING;
    DWORD native_flags = FILE_ATTRIBUTE_NORMAL;

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

    auto ret = open_file(path, native_access, native_disposition, native_flags, error);
    auto success = ret != TR_BAD_SYS_FILE;

    if (!success)
    {
        set_system_error(error, GetLastError());

        CloseHandle(ret);
        ret = TR_BAD_SYS_FILE;
    }

    return ret;
}

tr_sys_file_t tr_sys_file_open_temp(char* path_template, tr_error* error)
{
    TR_ASSERT(path_template != nullptr);

    tr_sys_file_t ret = TR_BAD_SYS_FILE;

    create_temp_path(
        path_template,
        [&ret](char const* path, tr_error* error)
        { ret = open_file(path, GENERIC_READ | GENERIC_WRITE, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, error); },
        error);

    return ret;
}

bool tr_sys_file_close(tr_sys_file_t handle, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool const ret = to_bool(CloseHandle(handle));

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_read(tr_sys_file_t handle, void* buffer, uint64_t size, uint64_t* bytes_read, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_read = 0;

    if (to_bool(ReadFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_read, nullptr)))
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
    tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    auto overlapped = OVERLAPPED{};
    DWORD my_bytes_read = 0;

    overlapped.Offset = static_cast<DWORD>(offset);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    overlapped.hEvent = nullptr;

    if (to_bool(ReadFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_read, &overlapped)))
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

bool tr_sys_file_write(tr_sys_file_t handle, void const* buffer, uint64_t size, uint64_t* bytes_written, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_written = 0;

    if (to_bool(WriteFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_written, nullptr)))
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
    tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != nullptr || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    auto overlapped = OVERLAPPED{};
    DWORD my_bytes_written = 0;

    overlapped.Offset = static_cast<DWORD>(offset);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
    overlapped.hEvent = nullptr;

    if (to_bool(WriteFile(handle, buffer, static_cast<DWORD>(size), &my_bytes_written, &overlapped)))
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

bool tr_sys_file_truncate(tr_sys_file_t handle, uint64_t size, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    FILE_END_OF_FILE_INFO info;
    info.EndOfFile.QuadPart = size;

    bool const ret = to_bool(SetFileInformationByHandle(handle, FileEndOfFileInfo, &info, sizeof(info)));

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_preallocate(tr_sys_file_t handle, uint64_t size, int flags, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    if ((flags & TR_SYS_FILE_PREALLOC_SPARSE) != 0)
    {
        DWORD tmp = 0;

        if (!to_bool(DeviceIoControl(handle, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &tmp, nullptr)))
        {
            set_system_error(error, GetLastError());
            return false;
        }
    }

    return tr_sys_file_truncate(handle, size, error);
}

bool tr_sys_file_lock(tr_sys_file_t handle, int operation, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT((operation & ~(TR_SYS_FILE_LOCK_SH | TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB)) == 0);
    TR_ASSERT(!!(operation & TR_SYS_FILE_LOCK_SH) + !!(operation & TR_SYS_FILE_LOCK_EX) == 1);

    auto overlapped = OVERLAPPED{};

    DWORD native_flags = 0;

    if ((operation & TR_SYS_FILE_LOCK_EX) != 0)
    {
        native_flags |= LOCKFILE_EXCLUSIVE_LOCK;
    }

    if ((operation & TR_SYS_FILE_LOCK_NB) != 0)
    {
        native_flags |= LOCKFILE_FAIL_IMMEDIATELY;
    }

    bool const ret = LockFileEx(handle, native_flags, 0, MAXDWORD, MAXDWORD, &overlapped) != FALSE;

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

std::string tr_sys_dir_get_current(tr_error* error)
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

bool tr_sys_dir_create_temp(char* path_template, tr_error* error)
{
    TR_ASSERT(path_template != nullptr);

    bool ret = false;

    create_temp_path(
        path_template,
        [&ret](char const* path, tr_error* error) { ret = create_dir(path, 0, 0, false, error); },
        error);

    return ret;
}

tr_sys_dir_t tr_sys_dir_open(std::string_view path, tr_error* error)
{
    TR_ASSERT(!std::empty(path));

    if (auto const info = tr_sys_path_get_info(path, 0); !info || !info->isFolder())
    {
        set_system_error(error, ERROR_DIRECTORY);
        return TR_BAD_SYS_DIR;
    }

    auto const pattern = path_to_native_path(path);
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

char const* tr_sys_dir_read_name(tr_sys_dir_t handle, tr_error* error)
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
        if (!to_bool(FindNextFileW(handle->find_handle, &handle->find_data)))
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

bool tr_sys_dir_close(tr_sys_dir_t handle, tr_error* error)
{
    TR_ASSERT(handle != TR_BAD_SYS_DIR);

    bool const ret = to_bool(FindClose(handle->find_handle));

    if (!ret)
    {
        set_system_error(error, GetLastError());
    }

    delete handle;

    return ret;
}
