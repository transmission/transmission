/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <string.h>

#ifndef WIN32
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <unistd.h>
#else
 #include <windows.h>
#endif

#include "transmission.h"
#include "error.h"
#include "file.h"

#include "libtransmission-test.h"

#ifndef WIN32
 #define NATIVE_PATH_SEP "/"
#else
 #define NATIVE_PATH_SEP "\\"
#endif

static tr_session * session;

static char *
create_test_dir (const char * name)
{
  char * const test_dir = tr_buildPath (tr_sessionGetConfigDir (session), name, NULL);
  tr_mkdirp (test_dir, 0777);
  return test_dir;
}

static bool
create_symlink (const char * dst_path, const char * src_path, bool dst_is_dir)
{
#ifndef WIN32

  (void) dst_is_dir;

  return symlink (src_path, dst_path) != -1;

#else

  wchar_t * wide_src_path;
  wchar_t * wide_dst_path;
  bool ret = false;

  wide_src_path = tr_win32_utf8_to_native (src_path, -1);
  wide_dst_path = tr_win32_utf8_to_native (dst_path, -1);

  ret = CreateSymbolicLinkW (wide_dst_path, wide_src_path,
                             dst_is_dir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);

  tr_free (wide_dst_path);
  tr_free (wide_src_path);

  return ret;

#endif
}

static bool
create_hardlink (const char * dst_path, const char * src_path)
{
#ifndef WIN32

  return link (src_path, dst_path) != -1;

#else

  wchar_t * wide_src_path = tr_win32_utf8_to_native (src_path, -1);
  wchar_t * wide_dst_path = tr_win32_utf8_to_native (dst_path, -1);

  bool ret = CreateHardLinkW (wide_dst_path, wide_src_path, NULL);

  tr_free (wide_dst_path);
  tr_free (wide_src_path);

  return ret;

#endif
}

static void
clear_path_info (tr_sys_path_info * info)
{
  info->type = (tr_sys_path_type_t)-1;
  info->size = (uint64_t)-1;
  info->last_modified_at = (time_t)-1;
}

static bool
path_contains_no_symlinks (const char * path)
{
  const char * p = path;

  while (*p != '\0')
    {
      tr_sys_path_info info;
      char * pathPart;
      const char * slashPos = strchr (p, '/');

#ifdef WIN32

      const char * backslashPos = strchr (p, '\\');
      if (slashPos == NULL || (backslashPos != NULL && backslashPos < slashPos))
        slashPos = backslashPos;

#endif

      if (slashPos == NULL)
        slashPos = p + strlen (p) - 1;

      pathPart = tr_strndup (path, slashPos - path + 1);

      if (!tr_sys_path_get_info (pathPart, TR_SYS_PATH_NO_FOLLOW, &info, NULL) ||
          (info.type != TR_SYS_PATH_IS_FILE && info.type != TR_SYS_PATH_IS_DIRECTORY))
        {
          tr_free (pathPart);
          return false;
        }

      tr_free (pathPart);

      p = slashPos + 1;
    }

  return true;
}

static int
test_get_info (void)
{
  char * const test_dir = create_test_dir (__FUNCTION__);
  tr_sys_path_info info;
  tr_error * err = NULL;
  char * path1, * path2;

  path1 = tr_buildPath (test_dir, "a", NULL);
  path2 = tr_buildPath (test_dir, "b", NULL);

  /* Can't get info of non-existent file/directory */
  check (!tr_sys_path_get_info (path1, 0, &info, &err));
  check (err != NULL);
  tr_error_clear (&err);

  libtest_create_file_with_string_contents (path1, "test");

  /* Good file info */
  clear_path_info (&info);
  check (tr_sys_path_get_info (path1, 0, &info, &err));
  check (err == NULL);
  check_int_eq (TR_SYS_PATH_IS_FILE, info.type);
  check_int_eq (4, info.size);
  check (info.last_modified_at >= time(0) - 1 && info.last_modified_at <= time(0));

  tr_sys_path_remove (path1, NULL);

  /* Good directory info */
  tr_mkdirp (path1, 0777);
  clear_path_info (&info);
  check (tr_sys_path_get_info (path1, 0, &info, &err));
  check (err == NULL);
  check_int_eq (TR_SYS_PATH_IS_DIRECTORY, info.type);
  check (info.size != (uint64_t)-1);
  check (info.last_modified_at >= time(0) - 1 && info.last_modified_at <= time(0));
  tr_sys_path_remove (path1, NULL);

  if (create_symlink (path1, path2, false))
    {
      /* Can't get info of non-existent file/directory */
      check (!tr_sys_path_get_info (path1, 0, &info, &err));
      check (err != NULL);
      tr_error_clear (&err);

      libtest_create_file_with_string_contents (path2, "test");

      /* Good file info */
      clear_path_info (&info);
      check (tr_sys_path_get_info (path1, 0, &info, &err));
      check (err == NULL);
      check_int_eq (TR_SYS_PATH_IS_FILE, info.type);
      check_int_eq (4, info.size);
      check (info.last_modified_at >= time(0) - 1 && info.last_modified_at <= time(0));

      tr_sys_path_remove (path2, NULL);

      /* Good directory info */
      tr_mkdirp (path2, 0777);
      clear_path_info (&info);
      check (tr_sys_path_get_info (path1, 0, &info, &err));
      check (err == NULL);
      check_int_eq (TR_SYS_PATH_IS_DIRECTORY, info.type);
      check (info.size != (uint64_t)-1);
      check (info.last_modified_at >= time(0) - 1 && info.last_modified_at <= time(0));

      tr_sys_path_remove (path2, NULL);
      tr_sys_path_remove (path1, NULL);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

  tr_free (path2);
  tr_free (path1);

  tr_free (test_dir);
  return 0;
}

static int
test_path_exists (void)
{
  char * const test_dir = create_test_dir (__FUNCTION__);
  tr_error * err = NULL;
  char * path1, * path2;

  path1 = tr_buildPath (test_dir, "a", NULL);
  path2 = tr_buildPath (test_dir, "b", NULL);

  /* Non-existent file does not exist */
  check (!tr_sys_path_exists (path1, &err));
  check (err == NULL);

  /* Create file and see that it exists */
  libtest_create_file_with_string_contents (path1, "test");
  check (tr_sys_path_exists (path1, &err));
  check (err == NULL);

  tr_sys_path_remove (path1, NULL);

  /* Create directory and see that it exists */
  tr_mkdirp (path1, 0777);
  check (tr_sys_path_exists (path1, &err));
  check (err == NULL);

  tr_sys_path_remove (path1, NULL);

  if (create_symlink (path1, path2, false))
    {
      /* Non-existent file does not exist (via symlink) */
      check (!tr_sys_path_exists (path1, &err));
      check (err == NULL);

      /* Create file and see that it exists (via symlink) */
      libtest_create_file_with_string_contents (path2, "test");
      check (tr_sys_path_exists (path1, &err));
      check (err == NULL);

      tr_sys_path_remove (path2, NULL);

      /* Create directory and see that it exists (via symlink) */
      tr_mkdirp (path2, 0777);
      check (tr_sys_path_exists (path1, &err));
      check (err == NULL);

      tr_sys_path_remove (path2, NULL);
      tr_sys_path_remove (path1, NULL);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

  tr_free (path2);
  tr_free (path1);

  tr_free (test_dir);
  return 0;
}

static int
test_path_is_same (void)
{
  char * const test_dir = create_test_dir (__FUNCTION__);
  tr_error * err = NULL;
  char * path1, * path2, * path3;

  path1 = tr_buildPath (test_dir, "a", NULL);
  path2 = tr_buildPath (test_dir, "b", NULL);
  path3 = tr_buildPath (path2, "c", NULL);

  /* Two non-existent files are not the same */
  check (!tr_sys_path_is_same (path1, path1, &err));
  check (err == NULL);
  check (!tr_sys_path_is_same (path1, path2, &err));
  check (err == NULL);

  /* Two same files are the same */
  libtest_create_file_with_string_contents (path1, "test");
  check (tr_sys_path_is_same (path1, path1, &err));
  check (err == NULL);

  /* Existent and non-existent files are not the same */
  check (!tr_sys_path_is_same (path1, path2, &err));
  check (err == NULL);
  check (!tr_sys_path_is_same (path2, path1, &err));
  check (err == NULL);

  /* Two separate files (even with same content) are not the same */
  libtest_create_file_with_string_contents (path2, "test");
  check (!tr_sys_path_is_same (path1, path2, &err));
  check (err == NULL);

  tr_sys_path_remove (path1, NULL);

  /* Two same directories are the same */
  tr_mkdirp (path1, 0777);
  check (tr_sys_path_is_same (path1, path1, &err));
  check (err == NULL);

  /* File and directory are not the same */
  check (!tr_sys_path_is_same (path1, path2, &err));
  check (err == NULL);
  check (!tr_sys_path_is_same (path2, path1, &err));
  check (err == NULL);

  tr_sys_path_remove (path2, NULL);

  /* Two separate directories are not the same */
  tr_mkdirp (path2, 0777);
  check (!tr_sys_path_is_same (path1, path2, &err));
  check (err == NULL);

  tr_sys_path_remove (path1, NULL);
  tr_sys_path_remove (path2, NULL);

  if (create_symlink (path1, ".", true))
    {
      /* Directory and symlink pointing to it are the same */
      check (tr_sys_path_is_same (path1, test_dir, &err));
      check (err == NULL);
      check (tr_sys_path_is_same (test_dir, path1, &err));
      check (err == NULL);

      /* Non-existent file and symlink are not the same */
      check (!tr_sys_path_is_same (path1, path2, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path2, path1, &err));
      check (err == NULL);

      /* Symlinks pointing to different directories are not the same */
      create_symlink (path2, "..", true);
      check (!tr_sys_path_is_same (path1, path2, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path2, path1, &err));
      check (err == NULL);

      tr_sys_path_remove (path2, NULL);

      /* Symlinks pointing to same directory are the same */
      create_symlink (path2, ".", true);
      check (tr_sys_path_is_same (path1, path2, &err));
      check (err == NULL);

      tr_sys_path_remove (path2, NULL);

      /* Directory and symlink pointing to another directory are not the same */
      tr_mkdirp (path2, 0777);
      check (!tr_sys_path_is_same (path1, path2, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path2, path1, &err));
      check (err == NULL);

      /* Symlinks pointing to same directory are the same */
      create_symlink (path3, "..", true);
      check (tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);

      tr_sys_path_remove (path1, NULL);

      /* File and symlink pointing to directory are not the same */
      libtest_create_file_with_string_contents (path1, "test");
      check (!tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path3, path1, &err));
      check (err == NULL);

      tr_sys_path_remove (path3, NULL);

      /* File and symlink pointing to same file are the same */
      create_symlink (path3, path1, false);
      check (tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);
      check (tr_sys_path_is_same (path3, path1, &err));
      check (err == NULL);

      /* Symlinks pointing to non-existent files are not the same */
      tr_sys_path_remove (path1, NULL);
      create_symlink (path1, "missing", false);
      tr_sys_path_remove (path3, NULL);
      create_symlink (path3, "missing", false);
      check (!tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path3, path1, &err));
      check (err == NULL);

      tr_sys_path_remove (path3, NULL);

      /* Symlinks pointing to same non-existent file are not the same */
      create_symlink (path3, ".." NATIVE_PATH_SEP "missing", false);
      check (!tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path3, path1, &err));
      check (err == NULL);

      /* Non-existent file and symlink pointing to non-existent file are not the same */
      tr_sys_path_remove (path3, NULL);
      check (!tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path3, path1, &err));
      check (err == NULL);

      tr_sys_path_remove (path2, NULL);
      tr_sys_path_remove (path1, NULL);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

  tr_free (path3);
  path3 = tr_buildPath (test_dir, "c", NULL);

  libtest_create_file_with_string_contents (path1, "test");

  if (create_hardlink (path2, path1))
    {
      /* File and hardlink to it are the same */
      check (tr_sys_path_is_same (path1, path2, &err));
      check (err == NULL);

      /* Two hardlinks to the same file are the same */
      create_hardlink (path3, path2);
      check (tr_sys_path_is_same (path2, path3, &err));
      check (err == NULL);
      check (tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);

      tr_sys_path_remove (path2, NULL);

      check (tr_sys_path_is_same (path1, path3, &err));
      check (err == NULL);

      tr_sys_path_remove (path3, NULL);

      /* File and hardlink to another file are not the same */
      libtest_create_file_with_string_contents (path3, "test");
      create_hardlink (path2, path3);
      check (!tr_sys_path_is_same (path1, path2, &err));
      check (err == NULL);
      check (!tr_sys_path_is_same (path2, path1, &err));
      check (err == NULL);

      tr_sys_path_remove (path3, NULL);
      tr_sys_path_remove (path2, NULL);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

  if (create_symlink (path2, path1, false) && create_hardlink (path3, path1))
    {
      check (tr_sys_path_is_same (path2, path3, &err));
      check (err == NULL);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run combined symlink and hardlink tests\n", __FUNCTION__);
    }

  tr_sys_path_remove (path3, NULL);
  tr_sys_path_remove (path2, NULL);
  tr_sys_path_remove (path1, NULL);

  tr_free (path3);
  tr_free (path2);
  tr_free (path1);

  tr_free (test_dir);
  return 0;
}

static int
test_path_resolve (void)
{
  char * const test_dir = create_test_dir (__FUNCTION__);
  tr_error * err = NULL;
  char * path1, * path2;

  path1 = tr_buildPath (test_dir, "a", NULL);
  path2 = tr_buildPath (test_dir, "b", NULL);

  libtest_create_file_with_string_contents (path1, "test");
  if (create_symlink (path2, path1, false))
    {
      char * tmp;

      tmp = tr_sys_path_resolve (path2, &err);
      check (tmp != NULL);
      check (err == NULL);
      check (path_contains_no_symlinks (tmp));
      tr_free (tmp);

      tr_sys_path_remove (path1, NULL);
      tr_mkdirp (path1, 0755);

      tmp = tr_sys_path_resolve (path2, &err);
      check (tmp != NULL);
      check (err == NULL);
      check (path_contains_no_symlinks (tmp));
      tr_free (tmp);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

  tr_sys_path_remove (path2, NULL);
  tr_sys_path_remove (path1, NULL);

  tr_free (path2);
  tr_free (path1);

  tr_free (test_dir);
  return 0;
}

static int
test_path_basename_dirname (void)
{
  tr_error * err = NULL;
  char * name;

  name = tr_sys_path_basename ("/a/b/c", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq ("c", name);
  tr_free (name);

  name = tr_sys_path_basename ("", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq (".", name);
  tr_free (name);

  name = tr_sys_path_dirname ("/a/b/c", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq ("/a/b", name);
  tr_free (name);

  name = tr_sys_path_dirname ("a/b/c", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq ("a/b", name);
  tr_free (name);

  name = tr_sys_path_dirname ("a", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq (".", name);
  tr_free (name);

  name = tr_sys_path_dirname ("", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq (".", name);
  tr_free (name);

#ifdef WIN32

  name = tr_sys_path_basename ("c:\\a\\b\\c", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq ("c", name);
  tr_free (name);

  name = tr_sys_path_dirname ("C:\\a/b\\c", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq ("C:\\a/b", name);
  tr_free (name);

  name = tr_sys_path_dirname ("a/b\\c", &err);
  check (name != NULL);
  check (err == NULL);
  check_streq ("a/b", name);
  tr_free (name);

#endif

  return 0;
}

static int
test_path_rename (void)
{
  char * const test_dir = create_test_dir (__FUNCTION__);
  tr_error * err = NULL;
  char * path1, * path2, * path3;

  path1 = tr_buildPath (test_dir, "a", NULL);
  path2 = tr_buildPath (test_dir, "b", NULL);
  path3 = tr_buildPath (path2, "c", NULL);

  libtest_create_file_with_string_contents (path1, "test");

  /* Preconditions */
  check (tr_sys_path_exists (path1, NULL));
  check (!tr_sys_path_exists (path2, NULL));

  /* Forward rename works */
  check (tr_sys_path_rename (path1, path2, &err));
  check (!tr_sys_path_exists (path1, NULL));
  check (tr_sys_path_exists (path2, NULL));
  check (err == NULL);

  /* Backward rename works */
  check (tr_sys_path_rename (path2, path1, &err));
  check (tr_sys_path_exists (path1, NULL));
  check (!tr_sys_path_exists (path2, NULL));
  check (err == NULL);

  /* Another backward rename [of non-existent file] does not work */
  check (!tr_sys_path_rename (path2, path1, &err));
  check (err != NULL);
  tr_error_clear (&err);

  /* Rename to file which couldn't be created does not work */
  check (!tr_sys_path_rename (path1, path3, &err));
  check (err != NULL);
  tr_error_clear (&err);

  /* Rename of non-existent file does not work */
  check (!tr_sys_path_rename (path3, path2, &err));
  check (err != NULL);
  tr_error_clear (&err);

  libtest_create_file_with_string_contents (path2, "test");

  /* Renaming file does overwrite existing file */
  check (tr_sys_path_rename (path2, path1, &err));
  check (err == NULL);

  tr_mkdirp (path2, 0777);

  /* Renaming file does not overwrite existing directory, and vice versa */
  check (!tr_sys_path_rename (path1, path2, &err));
  check (err != NULL);
  tr_error_clear (&err);
  check (!tr_sys_path_rename (path2, path1, &err));
  check (err != NULL);
  tr_error_clear (&err);

  tr_sys_path_remove (path2, NULL);

  tr_free (path3);
  path3 = tr_buildPath (test_dir, "c", NULL);

  if (create_symlink (path2, path1, false))
    {
      /* Preconditions */
      check (tr_sys_path_exists (path2, NULL));
      check (!tr_sys_path_exists (path3, NULL));
      check (tr_sys_path_is_same (path1, path2, NULL));

      /* Rename of symlink works, files stay the same */
      check (tr_sys_path_rename (path2, path3, &err));
      check (err == NULL);
      check (!tr_sys_path_exists (path2, NULL));
      check (tr_sys_path_exists (path3, NULL));
      check (tr_sys_path_is_same (path1, path3, NULL));

      tr_sys_path_remove (path3, NULL);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run symlink tests\n", __FUNCTION__);
    }

  if (create_hardlink (path2, path1))
    {
      /* Preconditions */
      check (tr_sys_path_exists (path2, NULL));
      check (!tr_sys_path_exists (path3, NULL));
      check (tr_sys_path_is_same (path1, path2, NULL));

      /* Rename of hardlink works, files stay the same */
      check (tr_sys_path_rename (path2, path3, &err));
      check (err == NULL);
      check (!tr_sys_path_exists (path2, NULL));
      check (tr_sys_path_exists (path3, NULL));
      check (tr_sys_path_is_same (path1, path3, NULL));

      tr_sys_path_remove (path3, NULL);
    }
  else
    {
      fprintf (stderr, "WARNING: [%s] unable to run hardlink tests\n", __FUNCTION__);
    }

  tr_sys_path_remove (path1, NULL);

  tr_free (path3);
  tr_free (path2);
  tr_free (path1);

  tr_free (test_dir);
  return 0;
}

static int
test_path_remove (void)
{
  char * const test_dir = create_test_dir (__FUNCTION__);
  tr_error * err = NULL;
  char * path1, * path2, * path3;

  path1 = tr_buildPath (test_dir, "a", NULL);
  path2 = tr_buildPath (test_dir, "b", NULL);
  path3 = tr_buildPath (path2, "c", NULL);

  /* Can't remove non-existent file/directory */
  check (!tr_sys_path_exists (path1, NULL));
  check (!tr_sys_path_remove (path1, &err));
  check (err != NULL);
  check (!tr_sys_path_exists (path1, NULL));
  tr_error_clear (&err);

  /* Removing file works */
  libtest_create_file_with_string_contents (path1, "test");
  check (tr_sys_path_exists (path1, NULL));
  check (tr_sys_path_remove (path1, &err));
  check (err == NULL);
  check (!tr_sys_path_exists (path1, NULL));

  /* Removing empty directory works */
  tr_mkdirp (path1, 0777);
  check (tr_sys_path_exists (path1, NULL));
  check (tr_sys_path_remove (path1, &err));
  check (err == NULL);
  check (!tr_sys_path_exists (path1, NULL));

  /* Removing non-empty directory fails */
  tr_mkdirp (path2, 0777);
  libtest_create_file_with_string_contents (path3, "test");
  check (tr_sys_path_exists (path2, NULL));
  check (tr_sys_path_exists (path3, NULL));
  check (!tr_sys_path_remove (path2, &err));
  check (err != NULL);
  check (tr_sys_path_exists (path2, NULL));
  check (tr_sys_path_exists (path3, NULL));
  tr_error_clear (&err);

  tr_sys_path_remove (path3, NULL);
  tr_sys_path_remove (path2, NULL);

  tr_free (path3);
  tr_free (path2);
  tr_free (path1);

  tr_free (test_dir);
  return 0;
}

int
main (void)
{
  const testFunc tests[] =
    {
      test_get_info,
      test_path_exists,
      test_path_is_same,
      test_path_resolve,
      test_path_basename_dirname,
      test_path_rename,
      test_path_remove
    };
  int ret;

  /* init the session */
  session = libttest_session_init (NULL);

  ret = runTests (tests, NUM_TESTS (tests));

  if (ret == 0)
    libttest_session_close (session);

  return ret;
}
