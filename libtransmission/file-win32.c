/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isalpha () */

#include <shlobj.h> /* SHCreateDirectoryEx () */
#include <winioctl.h> /* FSCTL_SET_SPARSE */

#include "transmission.h"
#include "crypto-utils.h" /* tr_rand_int () */
#include "error.h"
#include "file.h"
#include "utils.h"

#ifndef MAXSIZE_T
 #define MAXSIZE_T ((SIZE_T)~((SIZE_T)0))
#endif

/* MSDN (http://msdn.microsoft.com/en-us/library/2k2xf226.aspx) only mentions
   "i64" suffix for C code, but no warning is issued */
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL

struct tr_sys_dir_win32
{
  wchar_t * pattern;
  HANDLE find_handle;
  WIN32_FIND_DATAW find_data;
  char * utf8_name;
};

static void
set_system_error (tr_error ** error,
                  DWORD       code)
{
  char * message;

  if (error == NULL)
    return;

  message = tr_win32_format_message (code);

  if (message != NULL)
    {
      tr_error_set_literal (error, code, message);
      tr_free (message);
    }
  else
    {
      tr_error_set (error, code, "Unknown error: 0x%08lx", code);
    }
}

static void
set_system_error_if_file_found (tr_error ** error,
                                DWORD       code)
{
  if (code != ERROR_FILE_NOT_FOUND &&
      code != ERROR_PATH_NOT_FOUND &&
      code != ERROR_NO_MORE_FILES)
    set_system_error (error, code);
}

static time_t
filetime_to_unix_time (const FILETIME * t)
{
  uint64_t tmp = 0;

  assert (t != NULL);

  tmp |= t->dwHighDateTime;
  tmp <<= 32;
  tmp |= t->dwLowDateTime;
  tmp /= 10; /* to microseconds */
  tmp -= DELTA_EPOCH_IN_MICROSECS;

  return tmp / 1000000UL;
}

static void
stat_to_sys_path_info (DWORD              attributes,
                       DWORD              size_low,
                       DWORD              size_high,
                       const FILETIME   * mtime,
                       tr_sys_path_info * info)
{
  assert (mtime != NULL);
  assert (info != NULL);

  if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    info->type = TR_SYS_PATH_IS_DIRECTORY;
  else if (!(attributes & (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_VIRTUAL)))
    info->type = TR_SYS_PATH_IS_FILE;
  else
    info->type = TR_SYS_PATH_IS_OTHER;

  info->size = size_high;
  info->size <<= 32;
  info->size |= size_low;

  info->last_modified_at = filetime_to_unix_time (mtime);
}

static inline bool
is_slash (char c)
{
  return c == '\\' || c == '/';
}

static inline bool
is_unc_path (const char * path)
{
  return is_slash (path[0]) && path[1] == path[0];
}

static bool
is_valid_path (const char * path)
{
  if (is_unc_path (path))
    {
      if (path[2] != '\0' && !isalnum (path[2]))
        return false;
    }
  else
    {
      const char * colon_pos = strchr (path, ':');
      if (colon_pos != NULL)
        {
          if (colon_pos != path + 1 || !isalpha (path[0]))
            return false;
          path += 2;
        }
    }

  return strpbrk (path, "<>:\"|?*") == NULL;
}

static wchar_t *
path_to_native_path_ex (const char * path,
                        int          extra_chars_after,
                        int        * real_result_size)
{
  /* Extending maximum path length limit up to ~32K. See "Naming Files, Paths, and Namespaces"
     (https://msdn.microsoft.com/en-us/library/windows/desktop/aa365247.aspx) for more info */

  const wchar_t local_prefix[] = { '\\', '\\', '?', '\\' };
  const wchar_t unc_prefix[] = { '\\', '\\', '?', '\\', 'U', 'N', 'C', '\\' };

  const bool is_relative = tr_sys_path_is_relative (path);
  const bool is_unc = is_unc_path (path);

  /* `-2` for UNC since we overwrite existing prefix slashes */
  const int extra_chars_before = is_relative ? 0 : (is_unc ? ARRAYSIZE (unc_prefix) - 2
                                                           : ARRAYSIZE (local_prefix));

  /* TODO (?): assert (!is_relative); */

  wchar_t * const wide_path = tr_win32_utf8_to_native_ex (path, -1, extra_chars_before,
                                                          extra_chars_after, real_result_size);
  if (wide_path == NULL)
    return NULL;

  /* Relative paths cannot be used with "\\?\" prefixes. This also means that relative paths are
     limited to ~260 chars... but we should rarely work with relative paths in the first place */
  if (!is_relative)
    {
      if (is_unc)
        /* UNC path: "\\server\share" -> "\\?\UNC\server\share" */
        memcpy (wide_path, unc_prefix, sizeof (unc_prefix));
      else
        /* Local path: "C:" -> "\\?\C:" */
        memcpy (wide_path, local_prefix, sizeof (local_prefix));
    }

  /* Automatic '/' to '\' conversion is disabled for "\\?\"-prefixed paths */
  wchar_t * p = wide_path + extra_chars_before;
  while ((p = wcschr (p, L'/')) != NULL)
    *p++ = L'\\';

  if (real_result_size != NULL)
    *real_result_size += extra_chars_before;

  return wide_path;
}

static wchar_t *
path_to_native_path (const char * path)
{
  return path_to_native_path_ex (path, 0, NULL);
}

static tr_sys_file_t
open_file (const char  * path,
           DWORD         access,
           DWORD         disposition,
           DWORD         flags,
           tr_error   ** error)
{
  tr_sys_file_t ret = TR_BAD_SYS_FILE;
  wchar_t * wide_path;

  assert (path != NULL);

  wide_path = path_to_native_path (path);

  if (wide_path != NULL)
    ret = CreateFileW (wide_path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, disposition, flags, NULL);

  if (ret == TR_BAD_SYS_FILE)
    set_system_error (error, GetLastError ());

  tr_free (wide_path);

  return ret;
}

static bool
create_dir (const char  * path,
            int           flags,
            int           permissions,
            bool          okay_if_exists,
            tr_error   ** error)
{
  bool ret;
  wchar_t * wide_path;
  DWORD error_code = ERROR_SUCCESS;

  assert (path != NULL);

  (void) permissions;

  wide_path = path_to_native_path (path);

  if ((flags & TR_SYS_DIR_CREATE_PARENTS) != 0)
    {
      error_code = SHCreateDirectoryExW (NULL, wide_path, NULL);
      ret = error_code == ERROR_SUCCESS;
    }
  else
    {
      ret = CreateDirectoryW (wide_path, NULL);
      if (!ret)
        error_code = GetLastError ();
    }

  if (!ret && error_code == ERROR_ALREADY_EXISTS && okay_if_exists)
    {
      const DWORD attributes = GetFileAttributesW (wide_path);
      if (attributes != INVALID_FILE_ATTRIBUTES &&
          (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        ret = true;
    }

  if (!ret)
    set_system_error (error, error_code);

  tr_free (wide_path);

  return ret;
}

static void
create_temp_path (char      * path_template,
                  void     (* callback) (const char * path, void * param, tr_error ** error),
                  void      * callback_param,
                  tr_error ** error)
{
  char * path;
  size_t path_size;
  int attempt;
  tr_error * my_error = NULL;

  assert (path_template != NULL);
  assert (callback != NULL);

  path = tr_strdup (path_template);
  path_size = strlen (path);

  assert (path_size > 0);

  for (attempt = 0; attempt < 100; ++attempt)
    {
      size_t i = path_size;

      while (i > 0 && path_template[i - 1] == 'X')
        {
          const int c = tr_rand_int (26 + 26 + 10);
          path[i - 1] = c < 26 ? c + 'A' : (c < 26 + 26 ? (c - 26) + 'a' : (c - 26 - 26) + '0');
          --i;
        }

      assert (path_size >= i + 6);

      tr_error_clear (&my_error);

      (*callback) (path, callback_param, &my_error);

      if (my_error == NULL)
        break;
    }

  if (my_error != NULL)
    tr_error_propagate(error, &my_error);
  else
    memcpy (path_template, path, path_size);

  tr_free (path);
}

bool
tr_sys_path_exists (const char  * path,
                    tr_error   ** error)
{
  bool ret = false;
  wchar_t * wide_path;
  HANDLE handle = INVALID_HANDLE_VALUE;

  assert (path != NULL);

  wide_path = path_to_native_path (path);

  if (wide_path != NULL)
    {
      DWORD attributes = GetFileAttributesW (wide_path);
      if (attributes != INVALID_FILE_ATTRIBUTES)
        {
          if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            {
              handle = CreateFileW (wide_path, 0, 0, NULL, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS, NULL);

              ret = handle != INVALID_HANDLE_VALUE;
            }
          else
            {
              ret = true;
            }
        }
    }

  if (!ret)
    set_system_error_if_file_found (error, GetLastError ());

  if (handle != INVALID_HANDLE_VALUE)
    CloseHandle (handle);

  tr_free (wide_path);

  return ret;
}

bool
tr_sys_path_get_info (const char        * path,
                      int                 flags,
                      tr_sys_path_info  * info,
                      tr_error         ** error)
{
  bool ret = false;
  wchar_t * wide_path;

  assert (path != NULL);
  assert (info != NULL);

  wide_path = path_to_native_path (path);

  if ((flags & TR_SYS_PATH_NO_FOLLOW) == 0)
    {
      HANDLE handle = INVALID_HANDLE_VALUE;

      if (wide_path != NULL)
        handle = CreateFileW (wide_path, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

      if (handle != INVALID_HANDLE_VALUE)
        {
          tr_error * my_error = NULL;
          ret = tr_sys_file_get_info (handle, info, &my_error);
          if (!ret)
            tr_error_propagate (error, &my_error);
          CloseHandle (handle);
        }
      else
        {
          set_system_error (error, GetLastError ());
        }
    }
  else
    {
      WIN32_FILE_ATTRIBUTE_DATA attributes;

      if (wide_path != NULL)
        ret = GetFileAttributesExW (wide_path, GetFileExInfoStandard, &attributes);

      if (ret)
        stat_to_sys_path_info (attributes.dwFileAttributes, attributes.nFileSizeLow,
                               attributes.nFileSizeHigh, &attributes.ftLastWriteTime,
                               info);
      else
        set_system_error (error, GetLastError ());
    }

  tr_free (wide_path);

  return ret;
}

bool
tr_sys_path_is_relative (const char * path)
{
  assert (path != NULL);

  /* UNC path: `\\...`. */
  if (is_unc_path (path))
    return false;

  /* Local path: `X:` or `X:\...`. */
  if (isalpha (path[0]) && path[1] == ':' && (path[2] == '\0' || is_slash (path[2])))
    return false;

  return true;
}

bool
tr_sys_path_is_same (const char  * path1,
                     const char  * path2,
                     tr_error   ** error)
{
  bool ret = false;
  wchar_t * wide_path1 = NULL;
  wchar_t * wide_path2 = NULL;
  HANDLE handle1 = INVALID_HANDLE_VALUE;
  HANDLE handle2 = INVALID_HANDLE_VALUE;
  BY_HANDLE_FILE_INFORMATION fi1, fi2;

  assert (path1 != NULL);
  assert (path2 != NULL);

  wide_path1 = path_to_native_path (path1);
  if (wide_path1 == NULL)
    goto fail;

  wide_path2 = path_to_native_path (path2);
  if (wide_path2 == NULL)
    goto fail;

  handle1 = CreateFileW (wide_path1, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (handle1 == INVALID_HANDLE_VALUE)
    goto fail;

  handle2 = CreateFileW (wide_path2, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (handle2 == INVALID_HANDLE_VALUE)
    goto fail;

  /* TODO: Use GetFileInformationByHandleEx on >= Server 2012 */

  if (!GetFileInformationByHandle (handle1, &fi1) || !GetFileInformationByHandle (handle2, &fi2))
    goto fail;

  ret = fi1.dwVolumeSerialNumber == fi2.dwVolumeSerialNumber &&
        fi1.nFileIndexHigh == fi2.nFileIndexHigh &&
        fi1.nFileIndexLow  == fi2.nFileIndexLow;

  goto cleanup;

fail:
  set_system_error_if_file_found (error, GetLastError ());

cleanup:
  CloseHandle (handle2);
  CloseHandle (handle1);

  tr_free (wide_path2);
  tr_free (wide_path1);

  return ret;
}

char *
tr_sys_path_resolve (const char  * path,
                     tr_error   ** error)
{
  char * ret = NULL;
  wchar_t * wide_path;
  wchar_t * wide_ret = NULL;
  HANDLE handle = INVALID_HANDLE_VALUE;
  DWORD wide_ret_size;

  assert (path != NULL);

  wide_path = path_to_native_path (path);
  if (wide_path == NULL)
    goto fail;

  handle = CreateFileW (wide_path, FILE_READ_EA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (handle == INVALID_HANDLE_VALUE)
    goto fail;

  wide_ret_size = GetFinalPathNameByHandleW (handle, NULL, 0, 0);
  if (wide_ret_size == 0)
    goto fail;

  wide_ret = tr_new (wchar_t, wide_ret_size);
  if (GetFinalPathNameByHandleW (handle, wide_ret, wide_ret_size, 0) != wide_ret_size - 1)
    goto fail;

  /* Resolved path always begins with "\\?\", so skip those first four chars. */
  ret = tr_win32_native_to_utf8 (wide_ret + 4, -1);
  if (ret != NULL)
    goto cleanup;

fail:
  set_system_error (error, GetLastError ());

  tr_free (ret);
  ret = NULL;

cleanup:
  tr_free (wide_ret);
  tr_free (wide_path);

  if (handle != INVALID_HANDLE_VALUE)
    CloseHandle (handle);

  return ret;
}

char *
tr_sys_path_basename (const char  * path,
                      tr_error   ** error)
{
  if (path == NULL || path[0] == '\0')
    return tr_strdup (".");

  if (!is_valid_path (path))
    {
      set_system_error (error, ERROR_PATH_NOT_FOUND);
      return NULL;
    }

  const char * end = path + strlen (path);
  while (end > path && is_slash (*(end - 1)))
    --end;

  if (end == path)
    return tr_strdup ("/");

  const char * name = end;
  while (name > path && *(name - 1) != ':' && !is_slash (*(name - 1)))
    --name;

  if (name == end)
    return tr_strdup ("/");

  return tr_strndup (name, end - name);
}

char *
tr_sys_path_dirname (const char  * path,
                     tr_error   ** error)
{
  if (path == NULL || path[0] == '\0')
    return tr_strdup (".");

  if (!is_valid_path (path))
    {
      set_system_error (error, ERROR_PATH_NOT_FOUND);
      return NULL;
    }

  const bool is_unc = is_unc_path (path);

  if (is_unc && path[2] == '\0')
    return tr_strdup (path);

  const char * end = path + strlen (path);
  while (end > path && is_slash (*(end - 1)))
    --end;

  if (end == path)
    return tr_strdup ("/");

  const char * name = end;
  while (name > path && *(name - 1) != ':' && !is_slash (*(name - 1)))
    --name;
  while (name > path && is_slash (*(name - 1)))
    --name;

  if (name == path)
    return tr_strdup (is_unc ? "\\\\" : ".");

  if (name > path && *(name - 1) == ':' && *name != '\0' && !is_slash (*name))
    return tr_strdup_printf ("%c:.", path[0]);

  return tr_strndup (path, name - path);
}

bool
tr_sys_path_rename (const char  * src_path,
                    const char  * dst_path,
                    tr_error   ** error)
{
  bool ret = false;
  wchar_t * wide_src_path;
  wchar_t * wide_dst_path;

  assert (src_path != NULL);
  assert (dst_path != NULL);

  wide_src_path = path_to_native_path (src_path);
  wide_dst_path = path_to_native_path (dst_path);

  if (wide_src_path != NULL && wide_dst_path != NULL)
    {
      DWORD flags = MOVEFILE_REPLACE_EXISTING;
      DWORD attributes;

      attributes = GetFileAttributesW (wide_src_path);
      if (attributes != INVALID_FILE_ATTRIBUTES &&
          (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
          flags = 0;
        }
      else
        {
          attributes = GetFileAttributesW (wide_dst_path);
          if (attributes != INVALID_FILE_ATTRIBUTES &&
              (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            flags = 0;
        }

      ret = MoveFileExW (wide_src_path, wide_dst_path, flags);
    }

  if (!ret)
    set_system_error (error, GetLastError ());

  tr_free (wide_dst_path);
  tr_free (wide_src_path);

  return ret;
}

bool
tr_sys_path_remove (const char  * path,
                    tr_error   ** error)
{
  bool ret = false;
  wchar_t * wide_path;

  assert (path != NULL);

  wide_path = path_to_native_path (path);

  if (wide_path != NULL)
    {
      const DWORD attributes = GetFileAttributesW (wide_path);

      if (attributes != INVALID_FILE_ATTRIBUTES)
        {
          if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            ret = RemoveDirectoryW (wide_path);
          else
            ret = DeleteFileW (wide_path);
        }
    }

  if (!ret)
    set_system_error (error, GetLastError ());

  tr_free (wide_path);

  return ret;
}

tr_sys_file_t
tr_sys_file_get_std (tr_std_sys_file_t    std_file,
                     tr_error          ** error)
{
  tr_sys_file_t ret = TR_BAD_SYS_FILE;

  switch (std_file)
    {
    case TR_STD_SYS_FILE_IN:
      ret = GetStdHandle (STD_INPUT_HANDLE);
      break;
    case TR_STD_SYS_FILE_OUT:
      ret = GetStdHandle (STD_OUTPUT_HANDLE);
      break;
    case TR_STD_SYS_FILE_ERR:
      ret = GetStdHandle (STD_ERROR_HANDLE);
      break;
    default:
      assert (0 && "Unknown standard file");
      set_system_error (error, ERROR_INVALID_PARAMETER);
      return TR_BAD_SYS_FILE;
    }

  if (ret == TR_BAD_SYS_FILE)
    set_system_error (error, GetLastError ());
  else if (ret == NULL)
    ret = TR_BAD_SYS_FILE;

  return ret;
}

tr_sys_file_t
tr_sys_file_open (const char  * path,
                  int           flags,
                  int           permissions,
                  tr_error   ** error)
{
  tr_sys_file_t ret;
  DWORD native_access = 0;
  DWORD native_disposition = OPEN_EXISTING;
  DWORD native_flags = FILE_ATTRIBUTE_NORMAL;
  bool success;

  assert (path != NULL);
  assert ((flags & (TR_SYS_FILE_READ | TR_SYS_FILE_WRITE)) != 0);

  (void) permissions;

  if (flags & TR_SYS_FILE_READ)
    native_access |= GENERIC_READ;
  if (flags & TR_SYS_FILE_WRITE)
    native_access |= GENERIC_WRITE;

  if (flags & TR_SYS_FILE_CREATE_NEW)
    native_disposition = CREATE_NEW;
  else if (flags & TR_SYS_FILE_CREATE)
    native_disposition = flags & TR_SYS_FILE_TRUNCATE ? CREATE_ALWAYS : OPEN_ALWAYS;
  else if (flags & TR_SYS_FILE_TRUNCATE)
    native_disposition = TRUNCATE_EXISTING;

  if (flags & TR_SYS_FILE_SEQUENTIAL)
    native_flags |= FILE_FLAG_SEQUENTIAL_SCAN;

  ret = open_file (path, native_access, native_disposition, native_flags, error);

  success = ret != TR_BAD_SYS_FILE;

  if (success && (flags & TR_SYS_FILE_APPEND))
    success = SetFilePointer (ret, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER;

  if (!success)
    {
      if (error == NULL)
        set_system_error (error, GetLastError ());

      CloseHandle (ret);
      ret = TR_BAD_SYS_FILE;
    }

  return ret;
}

static void
file_open_temp_callback (const char  * path,
                         void        * param,
                         tr_error   ** error)
{
  tr_sys_file_t * result = (tr_sys_file_t *) param;

  assert (result != NULL);

  *result = open_file (path,
                       GENERIC_READ | GENERIC_WRITE,
                       CREATE_NEW,
                       FILE_ATTRIBUTE_TEMPORARY,
                       error);
}

tr_sys_file_t
tr_sys_file_open_temp (char      * path_template,
                       tr_error ** error)
{
  tr_sys_file_t ret = TR_BAD_SYS_FILE;

  assert (path_template != NULL);

  create_temp_path (path_template, file_open_temp_callback, &ret, error);

  return ret;
}

bool
tr_sys_file_close (tr_sys_file_t    handle,
                   tr_error      ** error)
{
  bool ret;

  assert (handle != TR_BAD_SYS_FILE);

  ret = CloseHandle (handle);

  if (!ret)
    set_system_error (error, GetLastError ());

  return ret;
}

bool
tr_sys_file_get_info (tr_sys_file_t       handle,
                      tr_sys_path_info  * info,
                      tr_error         ** error)
{
  bool ret;
  BY_HANDLE_FILE_INFORMATION attributes;

  assert (handle != TR_BAD_SYS_FILE);
  assert (info != NULL);

  ret = GetFileInformationByHandle (handle, &attributes);

  if (ret)
    stat_to_sys_path_info (attributes.dwFileAttributes, attributes.nFileSizeLow,
                           attributes.nFileSizeHigh, &attributes.ftLastWriteTime,
                           info);
  else
    set_system_error (error, GetLastError ());

  return ret;
}

bool
tr_sys_file_seek (tr_sys_file_t       handle,
                  int64_t             offset,
                  tr_seek_origin_t    origin,
                  uint64_t          * new_offset,
                  tr_error         ** error)
{
  bool ret = false;
  LARGE_INTEGER native_offset, new_native_pointer;

  TR_STATIC_ASSERT (TR_SEEK_SET == FILE_BEGIN,   "values should match");
  TR_STATIC_ASSERT (TR_SEEK_CUR == FILE_CURRENT, "values should match");
  TR_STATIC_ASSERT (TR_SEEK_END == FILE_END,     "values should match");

  assert (handle != TR_BAD_SYS_FILE);
  assert (origin == TR_SEEK_SET || origin == TR_SEEK_CUR || origin == TR_SEEK_END);

  native_offset.QuadPart = offset;

  if (SetFilePointerEx (handle, native_offset, &new_native_pointer, origin))
    {
      if (new_offset != NULL)
        *new_offset = new_native_pointer.QuadPart;
      ret = true;
    }
  else
    {
      set_system_error (error, GetLastError ());
    }

  return ret;
}

bool
tr_sys_file_read (tr_sys_file_t    handle,
                  void           * buffer,
                  uint64_t         size,
                  uint64_t       * bytes_read,
                  tr_error      ** error)
{
  bool ret = false;
  DWORD my_bytes_read;

  assert (handle != TR_BAD_SYS_FILE);
  assert (buffer != NULL || size == 0);

  if (size > MAXDWORD)
    {
      set_system_error (error, ERROR_INVALID_PARAMETER);
      return false;
    }

  if (ReadFile (handle, buffer, (DWORD)size, &my_bytes_read, NULL))
    {
      if (bytes_read != NULL)
        *bytes_read = my_bytes_read;
      ret = true;
    }
  else
    {
      set_system_error (error, GetLastError ());
    }

  return ret;
}

bool
tr_sys_file_read_at (tr_sys_file_t    handle,
                     void           * buffer,
                     uint64_t         size,
                     uint64_t         offset,
                     uint64_t       * bytes_read,
                     tr_error      ** error)
{
  bool ret = false;
  OVERLAPPED overlapped;
  DWORD my_bytes_read;

  assert (handle != TR_BAD_SYS_FILE);
  assert (buffer != NULL || size == 0);

  if (size > MAXDWORD)
    {
      set_system_error (error, ERROR_INVALID_PARAMETER);
      return false;
    }

  overlapped.Offset = (DWORD)offset;
  offset >>= 32;
  overlapped.OffsetHigh = (DWORD)offset;
  overlapped.hEvent = NULL;

  if (ReadFile (handle, buffer, (DWORD)size, &my_bytes_read, &overlapped))
    {
      if (bytes_read != NULL)
        *bytes_read = my_bytes_read;
      ret = true;
    }
  else
    {
      set_system_error (error, GetLastError ());
    }

  return ret;
}

bool
tr_sys_file_write (tr_sys_file_t    handle,
                   const void     * buffer,
                   uint64_t         size,
                   uint64_t       * bytes_written,
                   tr_error      ** error)
{
  bool ret = false;
  DWORD my_bytes_written;

  assert (handle != TR_BAD_SYS_FILE);
  assert (buffer != NULL || size == 0);

  if (size > MAXDWORD)
    {
      set_system_error (error, ERROR_INVALID_PARAMETER);
      return false;
    }

  if (WriteFile (handle, buffer, (DWORD)size, &my_bytes_written, NULL))
    {
      if (bytes_written != NULL)
        *bytes_written = my_bytes_written;
      ret = true;
    }
  else
    {
      set_system_error (error, GetLastError ());
    }

  return ret;
}

bool
tr_sys_file_write_at (tr_sys_file_t    handle,
                      const void     * buffer,
                      uint64_t         size,
                      uint64_t         offset,
                      uint64_t       * bytes_written,
                      tr_error      ** error)
{
  bool ret = false;
  OVERLAPPED overlapped;
  DWORD my_bytes_written;

  assert (handle != TR_BAD_SYS_FILE);
  assert (buffer != NULL || size == 0);

  if (size > MAXDWORD)
    {
      set_system_error (error, ERROR_INVALID_PARAMETER);
      return false;
    }

  overlapped.Offset = (DWORD)offset;
  offset >>= 32;
  overlapped.OffsetHigh = (DWORD)offset;
  overlapped.hEvent = NULL;

  if (WriteFile (handle, buffer, (DWORD)size, &my_bytes_written, &overlapped))
    {
      if (bytes_written != NULL)
        *bytes_written = my_bytes_written;
      ret = true;
    }
  else
    {
      set_system_error (error, GetLastError ());
    }

  return ret;
}

bool
tr_sys_file_flush (tr_sys_file_t    handle,
                   tr_error      ** error)
{
  bool ret;

  assert (handle != TR_BAD_SYS_FILE);

  ret = FlushFileBuffers (handle);

  if (!ret)
    set_system_error (error, GetLastError ());

  return ret;
}

bool
tr_sys_file_truncate (tr_sys_file_t    handle,
                      uint64_t         size,
                      tr_error      ** error)
{
  bool ret = false;
  FILE_END_OF_FILE_INFO info;

  assert (handle != TR_BAD_SYS_FILE);

  info.EndOfFile.QuadPart = size;

  ret = SetFileInformationByHandle (handle, FileEndOfFileInfo, &info, sizeof (info));

  if (!ret)
    set_system_error (error, GetLastError ());

  return ret;
}

bool
tr_sys_file_prefetch (tr_sys_file_t    handle,
                      uint64_t         offset,
                      uint64_t         size,
                      tr_error      ** error)
{
  bool ret = false;

  assert (handle != TR_BAD_SYS_FILE);
  assert (size > 0);

  (void) handle;
  (void) offset;
  (void) size;
  (void) error;

  /* ??? */

  return ret;
}

bool
tr_sys_file_preallocate (tr_sys_file_t    handle,
                         uint64_t         size,
                         int              flags,
                         tr_error      ** error)
{
  assert (handle != TR_BAD_SYS_FILE);

  if ((flags & TR_SYS_FILE_PREALLOC_SPARSE) != 0)
    {
      DWORD tmp;
      if (!DeviceIoControl (handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &tmp, NULL))
        {
          set_system_error (error, GetLastError ());
          return false;
        }
    }

  return tr_sys_file_truncate (handle, size, error);
}

void *
tr_sys_file_map_for_reading (tr_sys_file_t    handle,
                             uint64_t         offset,
                             uint64_t         size,
                             tr_error      ** error)
{
  void * ret = NULL;
  HANDLE mappingHandle;

  assert (handle != TR_BAD_SYS_FILE);
  assert (size > 0);

  if (size > MAXSIZE_T)
    {
      set_system_error (error, ERROR_INVALID_PARAMETER);
      return false;
    }

  mappingHandle = CreateFileMappingW (handle, NULL, PAGE_READONLY, 0, 0, NULL);

  if (mappingHandle != NULL)
    {
      ULARGE_INTEGER native_offset;

      native_offset.QuadPart = offset;

      ret = MapViewOfFile (mappingHandle, FILE_MAP_READ, native_offset.u.HighPart,
                           native_offset.u.LowPart, (SIZE_T)size);
    }

  if (ret == NULL)
    set_system_error (error, GetLastError ());

  CloseHandle (mappingHandle);

  return ret;
}

bool
tr_sys_file_unmap (const void  * address,
                   uint64_t      size,
                   tr_error   ** error)
{
  bool ret;

  assert (address != NULL);
  assert (size > 0);

  (void) size;

  ret = UnmapViewOfFile (address);

  if (!ret)
    set_system_error (error, GetLastError ());

  return ret;
}

char *
tr_sys_dir_get_current (tr_error ** error)
{
  char * ret = NULL;
  wchar_t * wide_ret = NULL;
  DWORD size;

  size = GetCurrentDirectoryW (0, NULL);

  if (size != 0)
    {
      wide_ret = tr_new (wchar_t, size);
      if (GetCurrentDirectoryW (size, wide_ret) != 0)
        ret = tr_win32_native_to_utf8 (wide_ret, size);
    }

  if (ret == NULL)
    set_system_error (error, GetLastError ());

  tr_free (wide_ret);

  return ret;
}

bool
tr_sys_dir_create (const char  * path,
                   int           flags,
                   int           permissions,
                   tr_error   ** error)
{
  return create_dir (path, flags, permissions, true, error);
}

static void
dir_create_temp_callback (const char  * path,
                          void        * param,
                          tr_error   ** error)
{
  bool * result = (bool *) param;

  assert (result != NULL);

  *result = create_dir (path, 0, 0, false, error);
}

bool
tr_sys_dir_create_temp (char      * path_template,
                        tr_error ** error)
{
  bool ret = false;

  assert (path_template != NULL);

  create_temp_path (path_template, dir_create_temp_callback, &ret, error);

  return ret;
}

tr_sys_dir_t
tr_sys_dir_open (const char  * path,
                 tr_error   ** error)
{
  tr_sys_dir_t ret;
  int pattern_size;

#ifndef __clang__
  /* Clang gives "static_assert expression is not an integral constant expression" error */
  TR_STATIC_ASSERT (TR_BAD_SYS_DIR == NULL, "values should match");
#endif

  assert (path != NULL);

  ret = tr_new (struct tr_sys_dir_win32, 1);
  ret->pattern = path_to_native_path_ex (path, 2, &pattern_size);

  if (ret->pattern != NULL)
    {
      ret->pattern[pattern_size + 0] = L'\\';
      ret->pattern[pattern_size + 1] = L'*';

      ret->find_handle = INVALID_HANDLE_VALUE;
      ret->utf8_name = NULL;
    }
  else
    {
      set_system_error (error, GetLastError ());

      tr_free (ret->pattern);
      tr_free (ret);

      ret = NULL;
    }

  return ret;
}

const char *
tr_sys_dir_read_name (tr_sys_dir_t    handle,
                      tr_error     ** error)
{
  char * ret;
  DWORD error_code = ERROR_SUCCESS;

  assert (handle != TR_BAD_SYS_DIR);

  if (handle->find_handle == INVALID_HANDLE_VALUE)
    {
      handle->find_handle = FindFirstFileW (handle->pattern, &handle->find_data);
      if (handle->find_handle == INVALID_HANDLE_VALUE)
        error_code = GetLastError ();
    }
  else
    {
      if (!FindNextFileW (handle->find_handle, &handle->find_data))
        error_code = GetLastError ();
    }

  if (error_code != ERROR_SUCCESS)
    {
      set_system_error_if_file_found (error, error_code);
      return NULL;
    }

  ret = tr_win32_native_to_utf8 (handle->find_data.cFileName, -1);

  if (ret != NULL)
    {
      tr_free (handle->utf8_name);
      handle->utf8_name = ret;
    }
  else
    {
      set_system_error (error, GetLastError ());
    }

  return ret;
}

bool
tr_sys_dir_close (tr_sys_dir_t    handle,
                  tr_error     ** error)
{
  bool ret;

  assert (handle != TR_BAD_SYS_DIR);

  ret = FindClose (handle->find_handle);

  if (!ret)
    set_system_error (error, GetLastError ());

  tr_free (handle->utf8_name);
  tr_free (handle->pattern);
  tr_free (handle);

  return ret;
}
