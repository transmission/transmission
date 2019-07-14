/*
 * This file Copyright (C) 2013-2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <ctype.h> /* isalpha() */

#include <shlobj.h> /* SHCreateDirectoryEx() */
#include <winioctl.h> /* FSCTL_SET_SPARSE */

#include "transmission.h"
#include "crypto-utils.h" /* tr_rand_int() */
#include "error.h"
#include "file.h"
#include "tr-assert.h"
#include "utils.h"

#ifndef MAXSIZE_T
#define MAXSIZE_T ((SIZE_T)~((SIZE_T)0))
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

    if (error == NULL)
    {
        return;
    }

    message = tr_win32_format_message(code);

    if (message != NULL)
    {
        tr_error_set_literal(error, code, message);
        tr_free(message);
    }
    else
    {
        tr_error_set(error, code, "Unknown error: 0x%08lx", code);
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
    TR_ASSERT(t != NULL);

    uint64_t tmp = 0;
    tmp |= t->dwHighDateTime;
    tmp <<= 32;
    tmp |= t->dwLowDateTime;
    tmp /= 10; /* to microseconds */
    tmp -= DELTA_EPOCH_IN_MICROSECS;

    return tmp / 1000000UL;
}

static void stat_to_sys_path_info(DWORD attributes, DWORD size_low, DWORD size_high, FILETIME const* mtime,
    tr_sys_path_info* info)
{
    TR_ASSERT(mtime != NULL);
    TR_ASSERT(info != NULL);

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

static inline bool is_slash(char c)
{
    return c == '\\' || c == '/';
}

static inline bool is_unc_path(char const* path)
{
    return is_slash(path[0]) && path[1] == path[0];
}

static bool is_valid_path(char const* path)
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
        char const* colon_pos = strchr(path, ':');

        if (colon_pos != NULL)
        {
            if (colon_pos != path + 1 || !isalpha(path[0]))
            {
                return false;
            }

            path += 2;
        }
    }

    return strpbrk(path, "<>:\"|?*") == NULL;
}

static wchar_t* path_to_native_path_ex(char const* path, int extra_chars_after, int* real_result_size)
{
    /* Extending maximum path length limit up to ~32K. See "Naming Files, Paths, and Namespaces"
       (https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx) for more info */

    bool const is_relative = tr_sys_path_is_relative(path);
    bool const is_unc = is_unc_path(path);

    /* `-2` for UNC since we overwrite existing prefix slashes */
    int const extra_chars_before = is_relative ? 0 : (is_unc ? TR_N_ELEMENTS(native_unc_path_prefix) - 2 :
        TR_N_ELEMENTS(native_local_path_prefix));

    /* TODO (?): TR_ASSERT(!is_relative); */

    wchar_t* const wide_path = tr_win32_utf8_to_native_ex(path, -1, extra_chars_before, extra_chars_after, real_result_size);

    if (wide_path == NULL)
    {
        return NULL;
    }

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

    while ((p = wcschr(p, L'/')) != NULL)
    {
        *p++ = L'\\';
    }

    if (real_result_size != NULL)
    {
        *real_result_size += extra_chars_before;
    }

    return wide_path;
}

static wchar_t* path_to_native_path(char const* path)
{
    return path_to_native_path_ex(path, 0, NULL);
}

static char* native_path_to_path(wchar_t const* wide_path)
{
    if (wide_path == NULL)
    {
        return NULL;
    }

    bool const is_unc = wcsncmp(wide_path, native_unc_path_prefix, TR_N_ELEMENTS(native_unc_path_prefix)) == 0;
    bool const is_local = !is_unc && wcsncmp(wide_path, native_local_path_prefix, TR_N_ELEMENTS(native_local_path_prefix)) == 0;

    size_t const skip_chars = is_unc ? TR_N_ELEMENTS(native_unc_path_prefix) :
        (is_local ? TR_N_ELEMENTS(native_local_path_prefix) : 0);

    char* const path = tr_win32_native_to_utf8_ex(wide_path + skip_chars, -1, is_unc ? 2 : 0, 0, NULL);

    if (is_unc && path != NULL)
    {
        path[0] = '\\';
        path[1] = '\\';
    }

    return path;
}

static tr_sys_file_t open_file(char const* path, DWORD access, DWORD disposition, DWORD flags, tr_error** error)
{
    TR_ASSERT(path != NULL);

    tr_sys_file_t ret = TR_BAD_SYS_FILE;
    wchar_t* wide_path = path_to_native_path(path);

    if (wide_path != NULL)
    {
        ret = CreateFileW(wide_path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disposition, flags,
            NULL);
    }

    if (ret == TR_BAD_SYS_FILE)
    {
        set_system_error(error, GetLastError());
    }

    tr_free(wide_path);

    return ret;
}

static bool create_dir(char const* path, int flags, int permissions, bool okay_if_exists, tr_error** error)
{
    TR_ASSERT(path != NULL);

    (void)permissions;

    bool ret;
    DWORD error_code = ERROR_SUCCESS;
    wchar_t* wide_path = path_to_native_path(path);

    if ((flags & TR_SYS_DIR_CREATE_PARENTS) != 0)
    {
        error_code = SHCreateDirectoryExW(NULL, wide_path, NULL);
        ret = error_code == ERROR_SUCCESS;
    }
    else
    {
        ret = CreateDirectoryW(wide_path, NULL);

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

static void create_temp_path(char* path_template, void (* callback)(char const* path, void* param,
    tr_error** error), void* callback_param, tr_error** error)
{
    TR_ASSERT(path_template != NULL);
    TR_ASSERT(callback != NULL);

    char* path = tr_strdup(path_template);
    size_t path_size = strlen(path);

    TR_ASSERT(path_size > 0);

    tr_error* my_error = NULL;

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

        if (my_error == NULL)
        {
            break;
        }
    }

    if (my_error != NULL)
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
    TR_ASSERT(path != NULL);

    bool ret = false;
    HANDLE handle = INVALID_HANDLE_VALUE;
    wchar_t* wide_path = path_to_native_path(path);

    if (wide_path != NULL)
    {
        DWORD attributes = GetFileAttributesW(wide_path);

        if (attributes != INVALID_FILE_ATTRIBUTES)
        {
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                handle = CreateFileW(wide_path, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
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
    TR_ASSERT(path != NULL);
    TR_ASSERT(info != NULL);

    bool ret = false;
    wchar_t* wide_path = path_to_native_path(path);

    if ((flags & TR_SYS_PATH_NO_FOLLOW) == 0)
    {
        HANDLE handle = INVALID_HANDLE_VALUE;

        if (wide_path != NULL)
        {
            handle = CreateFileW(wide_path, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        }

        if (handle != INVALID_HANDLE_VALUE)
        {
            tr_error* my_error = NULL;
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

        if (wide_path != NULL)
        {
            ret = GetFileAttributesExW(wide_path, GetFileExInfoStandard, &attributes);
        }

        if (ret)
        {
            stat_to_sys_path_info(attributes.dwFileAttributes, attributes.nFileSizeLow, attributes.nFileSizeHigh,
                &attributes.ftLastWriteTime, info);
        }
        else
        {
            set_system_error(error, GetLastError());
        }
    }

    tr_free(wide_path);

    return ret;
}

bool tr_sys_path_is_relative(char const* path)
{
    TR_ASSERT(path != NULL);

    /* UNC path: `\\...`. */
    if (is_unc_path(path))
    {
        return false;
    }

    /* Local path: `X:` or `X:\...`. */
    if (isalpha(path[0]) && path[1] == ':' && (path[2] == '\0' || is_slash(path[2])))
    {
        return false;
    }

    return true;
}

bool tr_sys_path_is_same(char const* path1, char const* path2, tr_error** error)
{
    TR_ASSERT(path1 != NULL);
    TR_ASSERT(path2 != NULL);

    bool ret = false;
    wchar_t* wide_path1 = NULL;
    wchar_t* wide_path2 = NULL;
    HANDLE handle1 = INVALID_HANDLE_VALUE;
    HANDLE handle2 = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION fi1, fi2;

    wide_path1 = path_to_native_path(path1);

    if (wide_path1 == NULL)
    {
        goto fail;
    }

    wide_path2 = path_to_native_path(path2);

    if (wide_path2 == NULL)
    {
        goto fail;
    }

    handle1 = CreateFileW(wide_path1, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (handle1 == INVALID_HANDLE_VALUE)
    {
        goto fail;
    }

    handle2 = CreateFileW(wide_path2, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

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
    TR_ASSERT(path != NULL);

    char* ret = NULL;
    wchar_t* wide_path;
    wchar_t* wide_ret = NULL;
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD wide_ret_size;

    wide_path = path_to_native_path(path);

    if (wide_path == NULL)
    {
        goto fail;
    }

    handle = CreateFileW(wide_path, FILE_READ_EA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, NULL);

    if (handle == INVALID_HANDLE_VALUE)
    {
        goto fail;
    }

    wide_ret_size = GetFinalPathNameByHandleW(handle, NULL, 0, 0);

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

    if (ret != NULL)
    {
        goto cleanup;
    }

fail:
    set_system_error(error, GetLastError());

    tr_free(ret);
    ret = NULL;

cleanup:
    tr_free(wide_ret);
    tr_free(wide_path);

    if (handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
    }

    return ret;
}

char* tr_sys_path_basename(char const* path, tr_error** error)
{
    if (tr_str_is_empty(path))
    {
        return tr_strdup(".");
    }

    if (!is_valid_path(path))
    {
        set_system_error(error, ERROR_PATH_NOT_FOUND);
        return NULL;
    }

    char const* end = path + strlen(path);

    while (end > path && is_slash(*(end - 1)))
    {
        --end;
    }

    if (end == path)
    {
        return tr_strdup("/");
    }

    char const* name = end;

    while (name > path && *(name - 1) != ':' && !is_slash(*(name - 1)))
    {
        --name;
    }

    if (name == end)
    {
        return tr_strdup("/");
    }

    return tr_strndup(name, end - name);
}

char* tr_sys_path_dirname(char const* path, tr_error** error)
{
    if (tr_str_is_empty(path))
    {
        return tr_strdup(".");
    }

    if (!is_valid_path(path))
    {
        set_system_error(error, ERROR_PATH_NOT_FOUND);
        return NULL;
    }

    bool const is_unc = is_unc_path(path);

    if (is_unc && path[2] == '\0')
    {
        return tr_strdup(path);
    }

    char const* end = path + strlen(path);

    while (end > path && is_slash(*(end - 1)))
    {
        --end;
    }

    if (end == path)
    {
        return tr_strdup("/");
    }

    char const* name = end;

    while (name > path && *(name - 1) != ':' && !is_slash(*(name - 1)))
    {
        --name;
    }

    while (name > path && is_slash(*(name - 1)))
    {
        --name;
    }

    if (name == path)
    {
        return tr_strdup(is_unc ? "\\\\" : ".");
    }

    if (name > path && *(name - 1) == ':' && *name != '\0' && !is_slash(*name))
    {
        return tr_strdup_printf("%c:.", path[0]);
    }

    return tr_strndup(path, name - path);
}

bool tr_sys_path_rename(char const* src_path, char const* dst_path, tr_error** error)
{
    TR_ASSERT(src_path != NULL);
    TR_ASSERT(dst_path != NULL);

    bool ret = false;
    wchar_t* wide_src_path = path_to_native_path(src_path);
    wchar_t* wide_dst_path = path_to_native_path(dst_path);

    if (wide_src_path != NULL && wide_dst_path != NULL)
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

bool tr_sys_path_remove(char const* path, tr_error** error)
{
    TR_ASSERT(path != NULL);

    bool ret = false;
    wchar_t* wide_path = path_to_native_path(path);

    if (wide_path != NULL)
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
    if (path == NULL)
    {
        return NULL;
    }

    for (char* slash = strchr(path, '/'); slash != NULL; slash = strchr(slash, '/'))
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
        TR_ASSERT_MSG(false, "unknown standard file %d", (int)std_file);
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return TR_BAD_SYS_FILE;
    }

    if (ret == TR_BAD_SYS_FILE)
    {
        set_system_error(error, GetLastError());
    }
    else if (ret == NULL)
    {
        ret = TR_BAD_SYS_FILE;
    }

    return ret;
}

tr_sys_file_t tr_sys_file_open(char const* path, int flags, int permissions, tr_error** error)
{
    TR_ASSERT(path != NULL);
    TR_ASSERT((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) != 0);

    (void)permissions;

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
        success = SetFilePointer(ret, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER;
    }

    if (!success)
    {
        if (error == NULL)
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

    TR_ASSERT(result != NULL);

    *result = open_file(path, GENERIC_READ | GENERIC_WRITE, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, error);
}

tr_sys_file_t tr_sys_file_open_temp(char* path_template, tr_error** error)
{
    TR_ASSERT(path_template != NULL);

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
    TR_ASSERT(info != NULL);

    BY_HANDLE_FILE_INFORMATION attributes;
    bool ret = GetFileInformationByHandle(handle, &attributes);

    if (ret)
    {
        stat_to_sys_path_info(attributes.dwFileAttributes, attributes.nFileSizeLow, attributes.nFileSizeHigh,
            &attributes.ftLastWriteTime, info);
    }
    else
    {
        set_system_error(error, GetLastError());
    }

    return ret;
}

bool tr_sys_file_seek(tr_sys_file_t handle, int64_t offset, tr_seek_origin_t origin, uint64_t* new_offset, tr_error** error)
{
    TR_STATIC_ASSERT(TR_SEEK_SET == FILE_BEGIN, "values should match");
    TR_STATIC_ASSERT(TR_SEEK_CUR == FILE_CURRENT, "values should match");
    TR_STATIC_ASSERT(TR_SEEK_END == FILE_END, "values should match");

    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(origin == TR_SEEK_SET || origin == TR_SEEK_CUR || origin == TR_SEEK_END);

    bool ret = false;
    LARGE_INTEGER native_offset;
    LARGE_INTEGER new_native_pointer;

    native_offset.QuadPart = offset;

    if (SetFilePointerEx(handle, native_offset, &new_native_pointer, origin))
    {
        if (new_offset != NULL)
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
    TR_ASSERT(buffer != NULL || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_read;

    if (ReadFile(handle, buffer, (DWORD)size, &my_bytes_read, NULL))
    {
        if (bytes_read != NULL)
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

bool tr_sys_file_read_at(tr_sys_file_t handle, void* buffer, uint64_t size, uint64_t offset, uint64_t* bytes_read,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != NULL || size == 0);

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
    overlapped.hEvent = NULL;

    if (ReadFile(handle, buffer, (DWORD)size, &my_bytes_read, &overlapped))
    {
        if (bytes_read != NULL)
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
    TR_ASSERT(buffer != NULL || size == 0);

    if (size > MAXDWORD)
    {
        set_system_error(error, ERROR_INVALID_PARAMETER);
        return false;
    }

    bool ret = false;
    DWORD my_bytes_written;

    if (WriteFile(handle, buffer, (DWORD)size, &my_bytes_written, NULL))
    {
        if (bytes_written != NULL)
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

bool tr_sys_file_write_at(tr_sys_file_t handle, void const* buffer, uint64_t size, uint64_t offset, uint64_t* bytes_written,
    tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(buffer != NULL || size == 0);

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
    overlapped.hEvent = NULL;

    if (WriteFile(handle, buffer, (DWORD)size, &my_bytes_written, &overlapped))
    {
        if (bytes_written != NULL)
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

bool tr_sys_file_advise(tr_sys_file_t handle, uint64_t offset, uint64_t size, tr_sys_file_advice_t advice, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);
    TR_ASSERT(size > 0);
    TR_ASSERT(advice == TR_SYS_FILE_ADVICE_WILL_NEED || advice == TR_SYS_FILE_ADVICE_DONT_NEED);

    (void)handle;
    (void)offset;
    (void)size;
    (void)advice;
    (void)error;

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

        if (!DeviceIoControl(handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &tmp, NULL))
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
        return false;
    }

    void* ret = NULL;
    HANDLE mappingHandle = CreateFileMappingW(handle, NULL, PAGE_READONLY, 0, 0, NULL);

    if (mappingHandle != NULL)
    {
        ULARGE_INTEGER native_offset;

        native_offset.QuadPart = offset;

        ret = MapViewOfFile(mappingHandle, FILE_MAP_READ, native_offset.u.HighPart, native_offset.u.LowPart, (SIZE_T)size);
    }

    if (ret == NULL)
    {
        set_system_error(error, GetLastError());
    }

    CloseHandle(mappingHandle);

    return ret;
}

bool tr_sys_file_unmap(void const* address, uint64_t size, tr_error** error)
{
    TR_ASSERT(address != NULL);
    TR_ASSERT(size > 0);

    (void)size;

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
    TR_ASSERT(!!(operation & TR_SYS_FILE_LOCK_SH) + !!(operation & TR_SYS_FILE_LOCK_EX) +
        !!(operation & TR_SYS_FILE_LOCK_UN) == 1);

    bool ret;
    OVERLAPPED overlapped = { .Pointer = 0, .hEvent = NULL };

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
    char* ret = NULL;
    wchar_t* wide_ret = NULL;
    DWORD size;

    size = GetCurrentDirectoryW(0, NULL);

    if (size != 0)
    {
        wide_ret = tr_new(wchar_t, size);

        if (GetCurrentDirectoryW(size, wide_ret) != 0)
        {
            ret = tr_win32_native_to_utf8(wide_ret, size);
        }
    }

    if (ret == NULL)
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

    TR_ASSERT(result != NULL);

    *result = create_dir(path, 0, 0, false, error);
}

bool tr_sys_dir_create_temp(char* path_template, tr_error** error)
{
    TR_ASSERT(path_template != NULL);

    bool ret = false;

    create_temp_path(path_template, dir_create_temp_callback, &ret, error);

    return ret;
}

tr_sys_dir_t tr_sys_dir_open(char const* path, tr_error** error)
{
#ifndef __clang__
    /* Clang gives "static_assert expression is not an integral constant expression" error */
    TR_STATIC_ASSERT(TR_BAD_SYS_DIR == NULL, "values should match");
#endif

    TR_ASSERT(path != NULL);

    tr_sys_dir_t ret = tr_new(struct tr_sys_dir_win32, 1);

    int pattern_size;
    ret->pattern = path_to_native_path_ex(path, 2, &pattern_size);

    if (ret->pattern != NULL)
    {
        ret->pattern[pattern_size + 0] = L'\\';
        ret->pattern[pattern_size + 1] = L'*';

        ret->find_handle = INVALID_HANDLE_VALUE;
        ret->utf8_name = NULL;
    }
    else
    {
        set_system_error(error, GetLastError());

        tr_free(ret->pattern);
        tr_free(ret);

        ret = NULL;
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
        return NULL;
    }

    char* ret = tr_win32_native_to_utf8(handle->find_data.cFileName, -1);

    if (ret != NULL)
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
