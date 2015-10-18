/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* mkstemp() */
#include <string.h> /* strcmp() */

#ifndef _WIN32
 #include <unistd.h> /* sync() */
#endif

#include "transmission.h"
#include "crypto-utils.h"
#include "error.h"
#include "file.h"
#include "platform.h" /* TR_PATH_DELIMETER */
#include "torrent.h"
#include "trevent.h"
#include "variant.h"
#include "libtransmission-test.h"

bool verbose = false;

int current_test = 0;

bool
should_print (bool pass)
{
  if (!pass)
    return true;

  if (verbose)
    return true;

  return false;
#ifdef VERBOSE
  return true;
#else
  return false;
#endif
}

bool
check_condition_impl (const char * file, int line, bool condition)
{
  const bool pass = condition;

  if (should_print (pass))
    fprintf (stderr, "%s %s:%d\n", pass?"PASS":"FAIL", file, line);

  return pass;
}

bool
check_streq_impl (const char * file, int line, const char * expected, const char * actual)
{
  const bool pass = !tr_strcmp0 (expected, actual);

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%s\", got \"%s\"\n", file, line, expected?expected:" (null)", actual?actual:" (null)");
  }

  return pass;
}

bool
check_int_eq_impl (const char * file, int line, int64_t expected, int64_t actual)
{
  const bool pass = expected == actual;

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%"PRId64"\", got \"%"PRId64"\"\n", file, line, expected, actual);
  }

  return pass;
}

bool
check_ptr_eq_impl (const char * file, int line, const void * expected, const void * actual)
{
  const bool pass = expected == actual;

  if (should_print (pass)) {
    if (pass)
      fprintf (stderr, "PASS %s:%d\n", file, line);
    else
      fprintf (stderr, "FAIL %s:%d, expected \"%p\", got \"%p\"\n", file, line, expected, actual);
  }

  return pass;
}

int
runTests (const testFunc * const tests, int numTests)
{
  int i;
  int ret;

  (void) current_test; /* Use test even if we don't have any tests to run */

  for (i=0; i<numTests; i++)
    if ((ret = (*tests[i])()))
      return ret;

  return 0; /* All tests passed */
}

/***
****
***/

static char*
tr_getcwd (void)
{
  char * result;
  tr_error * error = NULL;

  result = tr_sys_dir_get_current (&error);

  if (result == NULL)
    {
      fprintf (stderr, "getcwd error: \"%s\"", error->message);
      tr_error_free (error);
      result = tr_strdup ("");
    }

  return result;
}

char *
libtest_sandbox_create (void)
{
  char * path = tr_getcwd ();
  char * sandbox = tr_buildPath (path, "sandbox-XXXXXX", NULL);
  tr_free (path);
  tr_sys_dir_create_temp (sandbox, NULL);
  return sandbox;
}

static void
rm_rf (const char * killme)
{
  tr_sys_path_info info;

  if (tr_sys_path_get_info (killme, 0, &info, NULL))
    {
      tr_sys_dir_t odir;

      if (info.type == TR_SYS_PATH_IS_DIRECTORY &&
          (odir = tr_sys_dir_open (killme, NULL)) != TR_BAD_SYS_DIR)
        {
          const char * name;
          while ((name = tr_sys_dir_read_name (odir, NULL)) != NULL)
            {
              if (strcmp (name, ".") != 0 && strcmp (name, "..") != 0)
                {
                  char * tmp = tr_buildPath (killme, name, NULL);
                  rm_rf (tmp);
                  tr_free (tmp);
                }
            }
          tr_sys_dir_close (odir, NULL);
        }

      if (verbose)
        fprintf (stderr, "cleanup: removing %s\n", killme);

      tr_sys_path_remove (killme, NULL);
    }
}

void
libtest_sandbox_destroy (const char * sandbox)
{
  rm_rf (sandbox);
}

/***
****
***/

#define MEM_K 1024
#define MEM_B_STR   "B"
#define MEM_K_STR "KiB"
#define MEM_M_STR "MiB"
#define MEM_G_STR "GiB"
#define MEM_T_STR "TiB"

#define DISK_K 1000
#define DISK_B_STR  "B"
#define DISK_K_STR "kB"
#define DISK_M_STR "MB"
#define DISK_G_STR "GB"
#define DISK_T_STR "TB"

#define SPEED_K 1000
#define SPEED_B_STR  "B/s"
#define SPEED_K_STR "kB/s"
#define SPEED_M_STR "MB/s"
#define SPEED_G_STR "GB/s"
#define SPEED_T_STR "TB/s"

tr_session *
libttest_session_init (tr_variant * settings)
{
  size_t len;
  const char * str;
  char * sandbox;
  char * path;
  tr_quark q;
  static bool formatters_inited = false;
  tr_session * session;
  tr_variant local_settings;

  tr_variantInitDict (&local_settings, 10);

  if (settings == NULL)
    settings = &local_settings;

  sandbox = libtest_sandbox_create ();

  if (!formatters_inited)
    {
      formatters_inited = true;
      tr_formatter_mem_init (MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
      tr_formatter_size_init (DISK_K,DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
      tr_formatter_speed_init (SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);
    }

  /* download dir */
  q = TR_KEY_download_dir;
  if (tr_variantDictFindStr (settings, q, &str, &len))
    path = tr_strdup_printf ("%s/%*.*s", sandbox, (int)len, (int)len, str);
  else
    path = tr_buildPath (sandbox, "Downloads", NULL);
  tr_sys_dir_create (path, TR_SYS_DIR_CREATE_PARENTS, 0700, NULL);
  tr_variantDictAddStr (settings, q, path);
  tr_free (path);

  /* incomplete dir */
  q = TR_KEY_incomplete_dir;
  if (tr_variantDictFindStr (settings, q, &str, &len))
    path = tr_strdup_printf ("%s/%*.*s", sandbox, (int)len, (int)len, str);
  else
    path = tr_buildPath (sandbox, "Incomplete", NULL);
  tr_variantDictAddStr (settings, q, path);
  tr_free (path);

  path = tr_buildPath (sandbox, "blocklists", NULL);
  tr_sys_dir_create (path, TR_SYS_DIR_CREATE_PARENTS, 0700, NULL);
  tr_free (path);

  q = TR_KEY_port_forwarding_enabled;
  if (!tr_variantDictFind (settings, q))
    tr_variantDictAddBool (settings, q, false);

  q = TR_KEY_dht_enabled;
  if (!tr_variantDictFind (settings, q))
    tr_variantDictAddBool (settings, q, false);

  q = TR_KEY_message_level;
  if (!tr_variantDictFind (settings, q))
    tr_variantDictAddInt (settings, q, verbose ? TR_LOG_DEBUG : TR_LOG_ERROR);

  session = tr_sessionInit (sandbox, !verbose, settings);

  tr_free (sandbox);
  tr_variantFree (&local_settings);
  return session;
}

void
libttest_session_close (tr_session * session)
{
  char * sandbox;

  sandbox = tr_strdup (tr_sessionGetConfigDir (session));
  tr_sessionClose (session);
  tr_logFreeQueue (tr_logGetQueue ());
  session = NULL;

  libtest_sandbox_destroy (sandbox);
  tr_free (sandbox);
}

/***
****
***/

tr_torrent *
libttest_zero_torrent_init (tr_session * session)
{
  int err;
  size_t metainfo_len;
  char * metainfo;
  const char * metainfo_base64;
  tr_torrent * tor;
  tr_ctor * ctor;

  /*
     1048576 files-filled-with-zeroes/1048576
        4096 files-filled-with-zeroes/4096
         512 files-filled-with-zeroes/512
   */
  metainfo_base64 =
    "ZDg6YW5ub3VuY2UzMTpodHRwOi8vd3d3LmV4YW1wbGUuY29tL2Fubm91bmNlMTA6Y3JlYXRlZCBi"
    "eTI1OlRyYW5zbWlzc2lvbi8yLjYxICgxMzQwNykxMzpjcmVhdGlvbiBkYXRlaTEzNTg3MDQwNzVl"
    "ODplbmNvZGluZzU6VVRGLTg0OmluZm9kNTpmaWxlc2xkNjpsZW5ndGhpMTA0ODU3NmU0OnBhdGhs"
    "NzoxMDQ4NTc2ZWVkNjpsZW5ndGhpNDA5NmU0OnBhdGhsNDo0MDk2ZWVkNjpsZW5ndGhpNTEyZTQ6"
    "cGF0aGwzOjUxMmVlZTQ6bmFtZTI0OmZpbGVzLWZpbGxlZC13aXRoLXplcm9lczEyOnBpZWNlIGxl"
    "bmd0aGkzMjc2OGU2OnBpZWNlczY2MDpRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj"
    "/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv17"
    "26aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJtGEx"
    "Uv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GIQxhJ"
    "tGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZCS1GI"
    "QxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8KT9ZC"
    "S1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9umo/8K"
    "T9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9e9um"
    "o/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRhMVL9"
    "e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMYSbRh"
    "MVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLUYhDGEm0YTFS/XvbpqP/Ck/WQktRiEMY"
    "SbRhMVL9e9umo/8KT9ZCS1GIQxhJtGExUv1726aj/wpP1kJLOlf5A+Tz30nMBVuNM2hpV3wg/103"
    "OnByaXZhdGVpMGVlZQ==";

  /* create the torrent ctor */
  metainfo = tr_base64_decode_str (metainfo_base64, &metainfo_len);
  assert (metainfo != NULL);
  assert (metainfo_len > 0);
  assert (session != NULL);
  ctor = tr_ctorNew (session);
  tr_ctorSetMetainfo (ctor, (uint8_t*)metainfo, metainfo_len);
  tr_ctorSetPaused (ctor, TR_FORCE, true);

  /* create the torrent */
  err = 0;
  tor = tr_torrentNew (ctor, &err, NULL);
  assert (!err);

  /* cleanup */
  tr_free (metainfo);
  tr_ctorFree (ctor);
  return tor;
}

void
libttest_zero_torrent_populate (tr_torrent * tor, bool complete)
{
  tr_file_index_t i;

  for (i=0; i<tor->info.fileCount; ++i)
    {
      int err;
      uint64_t j;
      tr_sys_file_t fd;
      char * path;
      char * dirname;
      const tr_file * file = &tor->info.files[i];

      if (!complete && (i==0))
        path = tr_strdup_printf ("%s%c%s.part", tor->currentDir, TR_PATH_DELIMITER, file->name);
      else
        path = tr_strdup_printf ("%s%c%s", tor->currentDir, TR_PATH_DELIMITER, file->name);
      dirname = tr_sys_path_dirname (path, NULL);
      tr_sys_dir_create (dirname, TR_SYS_DIR_CREATE_PARENTS, 0700, NULL);
      fd = tr_sys_file_open (path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, NULL);
      for (j=0; j<file->length; ++j)
        tr_sys_file_write (fd, ((!complete) && (i==0) && (j<tor->info.pieceSize)) ? "\1" : "\0", 1, NULL, NULL);
      tr_sys_file_close (fd, NULL);

      tr_free (dirname);
      tr_free (path);

      path = tr_torrentFindFile (tor, i);
      assert (path != NULL);
      err = errno;
      assert (tr_sys_path_exists (path, NULL));
      errno = err;
      tr_free (path);
    }

  libttest_sync ();
  libttest_blockingTorrentVerify (tor);

  if (complete)
    assert (tr_torrentStat(tor)->leftUntilDone == 0);
  else
    assert (tr_torrentStat(tor)->leftUntilDone == tor->info.pieceSize);
}

/***
****
***/

static void
onVerifyDone (tr_torrent * tor UNUSED, bool aborted UNUSED, void * done)
{
  *(bool*)done = true;
}

void
libttest_blockingTorrentVerify (tr_torrent * tor)
{
  bool done = false;

  assert (tor->session != NULL);
  assert (!tr_amInEventThread (tor->session));

  tr_torrentVerify (tor, onVerifyDone, &done);
  while (!done)
    tr_wait_msec (10);
}

static void
build_parent_dir (const char* path)
{
  char * dir;
  tr_error * error = NULL;
  const int tmperr = errno;

  dir = tr_sys_path_dirname (path, NULL);
  tr_sys_dir_create (dir, TR_SYS_DIR_CREATE_PARENTS, 0700, &error);
  assert (error == NULL);
  tr_free (dir);

  errno = tmperr;
}

void
libtest_create_file_with_contents (const char* path, const void* payload, size_t n)
{
  tr_sys_file_t fd;
  const int tmperr = errno;

  build_parent_dir (path);

  fd = tr_sys_file_open (path, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0600, NULL);
  tr_sys_file_write (fd, payload, n, NULL, NULL);
  tr_sys_file_close (fd, NULL);

  libttest_sync ();

  errno = tmperr;
}

void
libtest_create_file_with_string_contents (const char * path, const char* str)
{
  libtest_create_file_with_contents (path, str, strlen(str));
}

void
libtest_create_tmpfile_with_contents (char* tmpl, const void* payload, size_t n)
{
  tr_sys_file_t fd;
  const int tmperr = errno;
  uint64_t n_left = n;
  tr_error * error = NULL;

  build_parent_dir (tmpl);

  fd = tr_sys_file_open_temp (tmpl, NULL);
  while (n_left > 0)
    {
      uint64_t n;
      if (!tr_sys_file_write (fd, payload, n_left, &n, &error))
        {
          fprintf (stderr, "Error writing '%s': %s\n", tmpl, error->message);
          tr_error_free (error);
          break;
        }
      n_left -= n;
    }
  tr_sys_file_close (fd, NULL);

  libttest_sync ();

  errno = tmperr;
}

void
libttest_sync (void)
{
#ifndef _WIN32
  sync ();
#endif
}
