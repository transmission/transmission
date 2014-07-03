/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#if defined (HAVE_CANONICALIZE_FILE_NAME) && !defined (_GNU_SOURCE)
 #define _GNU_SOURCE
#endif

#include <assert.h>
#include <errno.h>
#include <libgen.h> /* basename (), dirname () */
#include <limits.h> /* PATH_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "transmission.h"
#include "file.h"
#include "utils.h"

#ifndef PATH_MAX
 #define PATH_MAX 4096
#endif

static void
set_system_error (tr_error ** error,
                  int         code)
{
  if (error == NULL)
    return;

  tr_error_set_literal (error, code, tr_strerror (code));
}

static void
set_system_error_if_file_found (tr_error ** error,
                                int         code)
{
  if (code != ENOENT)
    set_system_error (error, code);
}

static void
stat_to_sys_path_info (const struct stat * sb,
                       tr_sys_path_info  * info)
{
  if (S_ISREG (sb->st_mode))
    info->type = TR_SYS_PATH_IS_FILE;
  else if (S_ISDIR (sb->st_mode))
    info->type = TR_SYS_PATH_IS_DIRECTORY;
  else
    info->type = TR_SYS_PATH_IS_OTHER;

  info->size = (uint64_t) sb->st_size;
  info->last_modified_at = sb->st_mtime;
}

bool
tr_sys_path_exists (const char  * path,
                    tr_error   ** error)
{
  bool ret;

  assert (path != NULL);

  ret = access (path, F_OK) != -1;

  if (!ret)
    set_system_error_if_file_found (error, errno);

  return ret;
}

bool
tr_sys_path_get_info (const char        * path,
                      int                 flags,
                      tr_sys_path_info  * info,
                      tr_error         ** error)
{
  bool ret;
  struct stat sb;

  assert (path != NULL);
  assert (info != NULL);

  if ((flags & TR_SYS_PATH_NO_FOLLOW) == 0)
    ret = stat (path, &sb) != -1;
  else
    ret = lstat (path, &sb) != -1;

  if (ret)
    stat_to_sys_path_info (&sb, info);
  else
    set_system_error (error, errno);

  return ret;
}

bool
tr_sys_path_is_same (const char  * path1,
                     const char  * path2,
                     tr_error   ** error)
{
  bool ret = false;
  struct stat sb1, sb2;

  assert (path1 != NULL);
  assert (path2 != NULL);

  if (stat (path1, &sb1) != -1 && stat (path2, &sb2) != -1)
    ret = sb1.st_dev == sb2.st_dev && sb1.st_ino == sb2.st_ino;
  else
    set_system_error_if_file_found (error, errno);

  return ret;
}

char *
tr_sys_path_resolve (const char  * path,
                     tr_error   ** error)
{
  char * ret = NULL;
  char * tmp = NULL;

  assert (path != NULL);

#if defined (HAVE_CANONICALIZE_FILE_NAME)

  ret = canonicalize_file_name (path);

#endif

#if defined (_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L

  /* Better safe than sorry: realpath () officially supports NULL as destination
     starting off POSIX.1-2008. */

  if (ret == NULL)
    ret = realpath (path, NULL);

#endif

  if (ret == NULL)
    {
      tmp = tr_new (char, PATH_MAX);
      ret = realpath (path, tmp);
      if (ret != NULL)
        ret = tr_strdup (ret);
    }

  if (ret == NULL)
    set_system_error (error, errno);

  tr_free (tmp);

  return ret;
}

char *
tr_sys_path_basename (const char  * path,
                      tr_error   ** error)
{
  char * ret = NULL;
  char * tmp;

  assert (path != NULL);

  tmp = tr_strdup (path);
  ret = basename (tmp);
  if (ret != NULL)
    ret = tr_strdup (ret);
  else
    set_system_error (error, errno);

  tr_free (tmp);

  return ret;
}

char *
tr_sys_path_dirname (const char  * path,
                     tr_error   ** error)
{
  char * ret = NULL;
  char * tmp;

  assert (path != NULL);

  tmp = tr_strdup (path);
  ret = dirname (tmp);
  if (ret != NULL)
    ret = tr_strdup (ret);
  else
    set_system_error (error, errno);

  tr_free (tmp);

  return ret;
}

bool
tr_sys_path_rename (const char  * src_path,
                    const char  * dst_path,
                    tr_error   ** error)
{
  bool ret;

  assert (src_path != NULL);
  assert (dst_path != NULL);

  ret = rename (src_path, dst_path) != -1;

  if (!ret)
    set_system_error (error, errno);

  return ret;
}

bool
tr_sys_path_remove (const char  * path,
                    tr_error   ** error)
{
  bool ret;

  assert (path != NULL);

  ret = remove (path) != -1;

  if (!ret)
    set_system_error (error, errno);

  return ret;
}
