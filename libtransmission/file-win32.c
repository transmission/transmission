/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <stdlib.h> /* _splitpath_s (), _makepath_s () */

#include "transmission.h"
#include "file.h"
#include "utils.h"

/* MSDN (http://msdn.microsoft.com/en-us/library/2k2xf226.aspx) only mentions
   "i64" suffix for C code, but no warning is issued */
#define DELTA_EPOCH_IN_MICROSECS 11644473600000000ULL

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
      tr_error_set (error, code, "Unknown error: 0x%08x", code);
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

static bool
get_file_info (HANDLE              handle,
               tr_sys_path_info  * info,
               tr_error         ** error);

bool
tr_sys_path_exists (const char  * path,
                    tr_error   ** error)
{
  bool ret = false;
  wchar_t * wide_path;
  HANDLE handle = INVALID_HANDLE_VALUE;

  assert (path != NULL);

  wide_path = tr_win32_utf8_to_native (path, -1);

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

  wide_path = tr_win32_utf8_to_native (path, -1);

  if ((flags & TR_SYS_PATH_NO_FOLLOW) == 0)
    {
      HANDLE handle = INVALID_HANDLE_VALUE;

      if (wide_path != NULL)
        handle = CreateFileW (wide_path, 0, 0, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

      if (handle != INVALID_HANDLE_VALUE)
        {
          tr_error * my_error = NULL;
          ret = get_file_info (handle, info, &my_error);
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

  wide_path1 = tr_win32_utf8_to_native (path1, -1);
  if (wide_path1 == NULL)
    goto fail;

  wide_path2 = tr_win32_utf8_to_native (path2, -1);
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
  HANDLE handle;
  DWORD wide_ret_size;

  assert (path != NULL);

  wide_path = tr_win32_utf8_to_native (path, -1);
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
  char fname[_MAX_FNAME], ext[_MAX_EXT];

  assert (path != NULL);

  /* TODO: Error handling */

  if (_splitpath_s (path, NULL, 0, NULL, 0, fname, sizeof (fname), ext, sizeof (ext)) == 0 &&
      (*fname != '\0' || *ext != '\0'))
    {
      const size_t tmp_len = strlen (fname) + strlen (ext) + 2;
      char * const tmp = tr_new (char, tmp_len);
      if (_makepath_s (tmp, tmp_len, NULL, NULL, fname, ext) == 0)
        return tmp;
      tr_free (tmp);
    }

  return tr_strdup (".");
}

char *
tr_sys_path_dirname (const char  * path,
                     tr_error   ** error)
{
  char drive[_MAX_DRIVE], dir[_MAX_DIR];

  assert (path != NULL);

  /* TODO: Error handling */

  if (_splitpath_s (path, drive, sizeof (drive), dir, sizeof (dir), NULL, 0, NULL, 0) == 0 &&
      (*drive != '\0' || *dir != '\0'))
    {
      const size_t tmp_len = strlen (drive) + strlen (dir) + 2;
      char * const tmp = tr_new (char, tmp_len);
      if (_makepath_s (tmp, tmp_len, drive, dir, NULL, NULL) == 0)
        {
          size_t len = strlen(tmp);
          while (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
            tmp[--len] = '\0';

          return tmp;
        }

      tr_free (tmp);
    }

  return tr_strdup (".");
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

  wide_src_path = tr_win32_utf8_to_native (src_path, -1);
  wide_dst_path = tr_win32_utf8_to_native (dst_path, -1);

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

  wide_path = tr_win32_utf8_to_native (path, -1);

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

static bool
get_file_info (HANDLE              handle,
               tr_sys_path_info  * info,
               tr_error         ** error)
{
  bool ret;
  BY_HANDLE_FILE_INFORMATION attributes;

  assert (handle != INVALID_HANDLE_VALUE);
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
