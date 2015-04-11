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
#include <stdio.h> /* fopen() */
#include <string.h> /* strcmp() */

#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
#include "resume.h"
#include "torrent.h" /* tr_isTorrent() */
#include "variant.h"

#include "libtransmission-test.h"

/***
****
***/

static tr_session * session = NULL;

#define check_have_none(tor, totalSize) \
  do { \
    const tr_stat * st = tr_torrentStat(tor); \
    check_int_eq (TR_STATUS_STOPPED, st->activity); \
    check_int_eq (TR_STAT_OK, st->error); \
    check_int_eq (totalSize, st->sizeWhenDone); \
    check_int_eq (totalSize, st->leftUntilDone); \
    check_int_eq (totalSize, tor->info.totalSize); \
    check_int_eq (0, st->haveValid); \
  } while (0)

static bool
testFileExistsAndConsistsOfThisString (const tr_torrent * tor, tr_file_index_t fileIndex, const char * str)
{
  char * path;
  const size_t str_len = strlen (str);
  bool success = false;

  path = tr_torrentFindFile (tor, fileIndex);
  if (path != NULL)
    {
      uint8_t * contents;
      size_t contents_len;

      assert (tr_sys_path_exists (path, NULL));

      contents = tr_loadFile (path, &contents_len, NULL);

      success = contents != NULL
             && (str_len == contents_len)
             && (!memcmp (contents, str, contents_len));

      tr_free (contents);
      tr_free (path);
    }

  return success;
}

static void
onRenameDone (tr_torrent * tor UNUSED, const char * oldpath UNUSED, const char * newname UNUSED, int error, void * user_data)
{
  *(int*)user_data = error;
}

static int
torrentRenameAndWait (tr_torrent * tor,
                      const char * oldpath,
                      const char * newname)
{
  int error = -1;
  tr_torrentRenamePath (tor, oldpath, newname, onRenameDone, &error);
  do {
    tr_wait_msec (10);
  } while (error == -1);
  return error;
}

/***
****
***/

static void
create_single_file_torrent_contents (const char * top)
{
  char * path = tr_buildPath (top, "hello-world.txt", NULL);
  libtest_create_file_with_string_contents (path, "hello, world!\n");
  tr_free (path);
}

static tr_torrent *
create_torrent_from_base64_metainfo (tr_ctor * ctor, const char * metainfo_base64)
{
  int err;
  size_t metainfo_len;
  char * metainfo;
  tr_torrent * tor;

  /* create the torrent ctor */
  metainfo = tr_base64_decode_str (metainfo_base64, &metainfo_len);
  assert (metainfo != NULL);
  assert (metainfo_len > 0);
  tr_ctorSetMetainfo (ctor, (uint8_t*)metainfo, metainfo_len);
  tr_ctorSetPaused (ctor, TR_FORCE, true);

  /* create the torrent */
  err = 0;
  tor = tr_torrentNew (ctor, &err, NULL);
  assert (!err);

  /* cleanup */
  tr_free (metainfo);
  return tor;
}

static int
test_single_filename_torrent (void)
{
  uint64_t loaded;
  tr_torrent * tor;
  char * tmpstr;
  const size_t totalSize = 14;
  tr_ctor * ctor;
  const tr_stat * st;

  /* this is a single-file torrent whose file is hello-world.txt, holding the string "hello, world!" */
  ctor = tr_ctorNew (session);
  tor = create_torrent_from_base64_metainfo (ctor,
    "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
    "ZWkxMzU4NTQ5MDk4ZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDY6bGVuZ3RoaTE0ZTQ6bmFtZTE1"
    "OmhlbGxvLXdvcmxkLnR4dDEyOnBpZWNlIGxlbmd0aGkzMjc2OGU2OnBpZWNlczIwOukboJcrkFUY"
    "f6LvqLXBVvSHqCk6Nzpwcml2YXRlaTBlZWU=");
  check (tr_isTorrent (tor));

  /* sanity check the info */
  check_int_eq (1, tor->info.fileCount);
  check_streq ("hello-world.txt", tor->info.files[0].name);
  check (!tor->info.files[0].is_renamed);

  /* sanity check the (empty) stats */
  libttest_blockingTorrentVerify (tor);
  check_have_none (tor, totalSize);

  create_single_file_torrent_contents (tor->currentDir);

  /* sanity check the stats again, now that we've added the file */
  libttest_blockingTorrentVerify (tor);
  st = tr_torrentStat (tor);
  check_int_eq (TR_STATUS_STOPPED, st->activity);
  check_int_eq (TR_STAT_OK, st->error);
  check_int_eq (0, st->leftUntilDone);
  check_int_eq (0, st->haveUnchecked);
  check_int_eq (0, st->desiredAvailable);
  check_int_eq (totalSize, st->sizeWhenDone);
  check_int_eq (totalSize, st->haveValid);

  /**
  ***  okay! we've finally put together all the scaffolding to test
  ***  renaming a single-file torrent
  **/

  /* confirm that bad inputs get caught */

  check_int_eq (EINVAL, torrentRenameAndWait (tor, "hello-world.txt", NULL));
  check_int_eq (EINVAL, torrentRenameAndWait (tor, "hello-world.txt", ""));
  check_int_eq (EINVAL, torrentRenameAndWait (tor, "hello-world.txt", "."));
  check_int_eq (EINVAL, torrentRenameAndWait (tor, "hello-world.txt", ".."));
  check_int_eq (0, torrentRenameAndWait (tor, "hello-world.txt", "hello-world.txt"));
  check_int_eq (EINVAL, torrentRenameAndWait (tor, "hello-world.txt", "hello/world.txt"));

  check (!tor->info.files[0].is_renamed);
  check_streq ("hello-world.txt", tor->info.files[0].name);

  /***
  ****  Now try a rename that should succeed
  ***/

  tmpstr = tr_buildPath (tor->currentDir, "hello-world.txt", NULL);
  check (tr_sys_path_exists (tmpstr, NULL));
  check_streq ("hello-world.txt", tr_torrentName(tor));
  check_int_eq (0, torrentRenameAndWait (tor, tor->info.name, "foobar"));
  check (!tr_sys_path_exists (tmpstr, NULL)); /* confirm the old filename can't be found */
  tr_free (tmpstr);
  check (tor->info.files[0].is_renamed); /* confirm the file's 'renamed' flag is set */
  check_streq ("foobar", tr_torrentName(tor)); /* confirm the torrent's name is now 'foobar' */
  check_streq ("foobar", tor->info.files[0].name); /* confirm the file's name is now 'foobar' in our struct */
  check (strstr (tor->info.torrent, "foobar") == NULL); /* confirm the name in the .torrent file hasn't changed */
  tmpstr = tr_buildPath (tor->currentDir, "foobar", NULL);
  check (tr_sys_path_exists (tmpstr, NULL)); /* confirm the file's name is now 'foobar' on the disk */
  tr_free (tmpstr);
  check (testFileExistsAndConsistsOfThisString (tor, 0, "hello, world!\n")); /* confirm the contents are right */

  /* (while it's renamed: confirm that the .resume file remembers the changes) */
  tr_torrentSaveResume (tor);
  libttest_sync ();
  loaded = tr_torrentLoadResume (tor, ~0, ctor);
  check_streq ("foobar", tr_torrentName(tor));
  check ((loaded & TR_FR_NAME) != 0);

  /***
  ****  ...and rename it back again
  ***/

  tmpstr = tr_buildPath (tor->currentDir, "foobar", NULL);
  check (tr_sys_path_exists (tmpstr, NULL));
  check_int_eq (0, torrentRenameAndWait (tor, "foobar", "hello-world.txt"));
  check (!tr_sys_path_exists (tmpstr, NULL));
  check (tor->info.files[0].is_renamed);
  check_streq ("hello-world.txt", tor->info.files[0].name);
  check_streq ("hello-world.txt", tr_torrentName(tor));
  tr_free (tmpstr);
  check (testFileExistsAndConsistsOfThisString (tor, 0, "hello, world!\n"));

  /* cleanup */
  tr_ctorFree (ctor);
  tr_torrentRemove (tor, false, NULL);
  return 0;
}

/***
****
****
****
***/

static void
create_multifile_torrent_contents (const char * top)
{
  char * path;

  path = tr_buildPath (top, "Felidae", "Felinae", "Acinonyx", "Cheetah", "Chester", NULL);
  libtest_create_file_with_string_contents (path, "It ain't easy bein' cheesy.\n");
  tr_free (path);

  path = tr_buildPath (top, "Felidae", "Pantherinae", "Panthera", "Tiger", "Tony", NULL);
  libtest_create_file_with_string_contents (path, "They’re Grrrrreat!\n");
  tr_free (path);

  path = tr_buildPath (top, "Felidae", "Felinae", "Felis", "catus", "Kyphi", NULL);
  libtest_create_file_with_string_contents (path, "Inquisitive\n");
  tr_free (path);

  path = tr_buildPath (top, "Felidae", "Felinae", "Felis", "catus", "Saffron", NULL);
  libtest_create_file_with_string_contents (path, "Tough\n");
  tr_free (path);

  libttest_sync ();
}

static int
test_multifile_torrent (void)
{
  tr_file_index_t i;
  uint64_t loaded;
  tr_torrent * tor;
  tr_ctor * ctor;
  char * str;
  char * tmp;
  static const size_t totalSize = 67;
  const tr_stat * st;
  const tr_file * files;
  const char * strings[4];
  const char * expected_files[4] = {
    "Felidae/Felinae/Acinonyx/Cheetah/Chester",
    "Felidae/Felinae/Felis/catus/Kyphi",
    "Felidae/Felinae/Felis/catus/Saffron",
    "Felidae/Pantherinae/Panthera/Tiger/Tony"
  };
  const char * expected_contents[4] = {
   "It ain't easy bein' cheesy.\n",
   "Inquisitive\n",
   "Tough\n",
   "They’re Grrrrreat!\n"
  };

  ctor = tr_ctorNew (session);
  tor = create_torrent_from_base64_metainfo (ctor,
    "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
    "ZWkxMzU4NTU1NDIwZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDU6ZmlsZXNsZDY6bGVuZ3RoaTI4"
    "ZTQ6cGF0aGw3OkZlbGluYWU4OkFjaW5vbnl4NzpDaGVldGFoNzpDaGVzdGVyZWVkNjpsZW5ndGhp"
    "MTJlNDpwYXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNTpLeXBoaWVlZDY6bGVuZ3RoaTZlNDpw"
    "YXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNzpTYWZmcm9uZWVkNjpsZW5ndGhpMjFlNDpwYXRo"
    "bDExOlBhbnRoZXJpbmFlODpQYW50aGVyYTU6VGlnZXI0OlRvbnllZWU0Om5hbWU3OkZlbGlkYWUx"
    "MjpwaWVjZSBsZW5ndGhpMzI3NjhlNjpwaWVjZXMyMDp27buFkmy8ICfNX4nsJmt0Ckm2Ljc6cHJp"
    "dmF0ZWkwZWVl");
  check (tr_isTorrent (tor));
  files = tor->info.files;

  /* sanity check the info */
  check_streq (tor->info.name, "Felidae");
  check_int_eq (totalSize, tor->info.totalSize);
  check_int_eq (4, tor->info.fileCount);
  for (i=0; i<4; ++i)
    check_streq (expected_files[i], files[i].name);

  /* sanity check the (empty) stats */
  libttest_blockingTorrentVerify (tor);
  check_have_none (tor, totalSize);

  /* build the local data */
  create_multifile_torrent_contents (tor->currentDir);

  /* sanity check the (full) stats */
  libttest_blockingTorrentVerify (tor);
  st = tr_torrentStat (tor);
  check_int_eq (TR_STATUS_STOPPED, st->activity);
  check_int_eq (TR_STAT_OK, st->error);
  check_int_eq (0, st->leftUntilDone);
  check_int_eq (0, st->haveUnchecked);
  check_int_eq (0, st->desiredAvailable);
  check_int_eq (totalSize, st->sizeWhenDone);
  check_int_eq (totalSize, st->haveValid);


  /**
  ***  okay! let's test renaming.
  **/

  /* rename a leaf... */
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Felinae/Felis/catus/Kyphi", "placeholder"));
  check_streq (files[1].name, "Felidae/Felinae/Felis/catus/placeholder");
  check (testFileExistsAndConsistsOfThisString (tor, 1, "Inquisitive\n"));

  /* ...and back again */
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Felinae/Felis/catus/placeholder", "Kyphi"));
  check_streq (files[1].name, "Felidae/Felinae/Felis/catus/Kyphi");
  testFileExistsAndConsistsOfThisString (tor, 1, "Inquisitive\n");

  /* rename a branch... */
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Felinae/Felis/catus", "placeholder"));
  check_streq (expected_files[0],                           files[0].name);
  check_streq ("Felidae/Felinae/Felis/placeholder/Kyphi",   files[1].name);
  check_streq ("Felidae/Felinae/Felis/placeholder/Saffron", files[2].name);
  check_streq (expected_files[3],                           files[3].name);
  check (testFileExistsAndConsistsOfThisString (tor, 1, expected_contents[1]));
  check (testFileExistsAndConsistsOfThisString (tor, 2, expected_contents[2]));
  check (files[0].is_renamed == false);
  check (files[1].is_renamed == true);
  check (files[2].is_renamed == true);
  check (files[3].is_renamed == false);

  /* (while the branch is renamed: confirm that the .resume file remembers the changes) */
  tr_torrentSaveResume (tor);
  /* this is a bit dodgy code-wise, but let's make sure the .resume file got the name */
  tr_free (files[1].name);
  tor->info.files[1].name = tr_strdup ("gabba gabba hey");
  loaded = tr_torrentLoadResume (tor, ~0, ctor);
  check ((loaded & TR_FR_FILENAMES) != 0);
  check_streq (expected_files[0],                           files[0].name);
  check_streq ("Felidae/Felinae/Felis/placeholder/Kyphi",   files[1].name);
  check_streq ("Felidae/Felinae/Felis/placeholder/Saffron", files[2].name);
  check_streq (expected_files[3],                           files[3].name);

  /* ...and back again */
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Felinae/Felis/placeholder", "catus"));
  for (i=0; i<4; ++i)
    {
      check_streq (expected_files[i], files[i].name);
      check (testFileExistsAndConsistsOfThisString (tor, i, expected_contents[i]));
    }
  check (files[0].is_renamed == false);
  check (files[1].is_renamed == true);
  check (files[2].is_renamed == true);
  check (files[3].is_renamed == false);

  /***
  ****  Test it an incomplete torrent...
  ***/

  /* remove the directory Felidae/Felinae/Felis/catus */
  str = tr_torrentFindFile (tor, 1);
  check (str != NULL);
  tr_sys_path_remove (str, NULL);
  tr_free (str);
  str = tr_torrentFindFile (tor, 2);
  check (str != NULL);
  tr_sys_path_remove (str, NULL);
  tmp = tr_sys_path_dirname (str, NULL);
  tr_sys_path_remove (tmp, NULL);
  tr_free (tmp);
  tr_free (str);
  libttest_sync ();
  libttest_blockingTorrentVerify (tor);
  testFileExistsAndConsistsOfThisString (tor, 0, expected_contents[0]);
  for (i=1; i<=2; ++i)
    {
      str = tr_torrentFindFile (tor, i);
      check_streq (NULL, str);
      tr_free (str);
    }
  testFileExistsAndConsistsOfThisString (tor, 3, expected_contents[3]);

  /* rename a branch... */
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Felinae/Felis/catus", "foo"));
  check_streq (expected_files[0],                   files[0].name);
  check_streq ("Felidae/Felinae/Felis/foo/Kyphi",   files[1].name);
  check_streq ("Felidae/Felinae/Felis/foo/Saffron", files[2].name);
  check_streq (expected_files[3],                   files[3].name);

  /* ...and back again */
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Felinae/Felis/foo", "catus"));
  for (i=0; i<4; ++i)
    check_streq (expected_files[i], files[i].name);

  check_int_eq (0, torrentRenameAndWait (tor, "Felidae", "gabba"));
  strings[0] = "gabba/Felinae/Acinonyx/Cheetah/Chester";
  strings[1] = "gabba/Felinae/Felis/catus/Kyphi";
  strings[2] = "gabba/Felinae/Felis/catus/Saffron";
  strings[3] = "gabba/Pantherinae/Panthera/Tiger/Tony";
  for (i=0; i<4; ++i)
    {
      check_streq (strings[i], files[i].name);
      testFileExistsAndConsistsOfThisString (tor, i, expected_contents[i]);
    }

  /* rename the root, then a branch, and then a leaf... */
  check_int_eq (0, torrentRenameAndWait (tor, "gabba", "Felidae"));
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Pantherinae/Panthera/Tiger", "Snow Leopard"));
  check_int_eq (0, torrentRenameAndWait (tor, "Felidae/Pantherinae/Panthera/Snow Leopard/Tony", "10.6"));
  strings[0] = "Felidae/Felinae/Acinonyx/Cheetah/Chester";
  strings[1] = "Felidae/Felinae/Felis/catus/Kyphi";
  strings[2] = "Felidae/Felinae/Felis/catus/Saffron";
  strings[3] = "Felidae/Pantherinae/Panthera/Snow Leopard/10.6";
  for (i=0; i<4; ++i)
    {
      check_streq (strings[i], files[i].name);
      testFileExistsAndConsistsOfThisString (tor, i, expected_contents[i]);
    }

  /**
  ***  Test renaming prefixes (shouldn't work)
  **/

  tr_ctorFree (ctor);
  tr_torrentRemove (tor, false, NULL);
  do {
    tr_wait_msec (10);
  } while (0);
  ctor = tr_ctorNew (session);
  tor = create_torrent_from_base64_metainfo (ctor,
    "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
    "ZWkxMzU4NTU1NDIwZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDU6ZmlsZXNsZDY6bGVuZ3RoaTI4"
    "ZTQ6cGF0aGw3OkZlbGluYWU4OkFjaW5vbnl4NzpDaGVldGFoNzpDaGVzdGVyZWVkNjpsZW5ndGhp"
    "MTJlNDpwYXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNTpLeXBoaWVlZDY6bGVuZ3RoaTZlNDpw"
    "YXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNzpTYWZmcm9uZWVkNjpsZW5ndGhpMjFlNDpwYXRo"
    "bDExOlBhbnRoZXJpbmFlODpQYW50aGVyYTU6VGlnZXI0OlRvbnllZWU0Om5hbWU3OkZlbGlkYWUx"
    "MjpwaWVjZSBsZW5ndGhpMzI3NjhlNjpwaWVjZXMyMDp27buFkmy8ICfNX4nsJmt0Ckm2Ljc6cHJp"
    "dmF0ZWkwZWVl");
  check (tr_isTorrent (tor));
  files = tor->info.files;

  /* rename prefix of top */
  check_int_eq (EINVAL, torrentRenameAndWait (tor, "Feli", "FelidaeX"));
  check_streq (tor->info.name, "Felidae");
  check (files[0].is_renamed == false);
  check (files[1].is_renamed == false);
  check (files[2].is_renamed == false);
  check (files[3].is_renamed == false);

  /* rename false path */
  check_int_eq (EINVAL, torrentRenameAndWait (tor, "Felidae/FelinaeX", "Genus Felinae"));
  check_streq (tor->info.name, "Felidae");
  check (files[0].is_renamed == false);
  check (files[1].is_renamed == false);
  check (files[2].is_renamed == false);
  check (files[3].is_renamed == false);

  /***
  ****
  ***/

  /* cleanup */
  tr_ctorFree (ctor);
  tr_torrentRemove (tor, false, NULL);
  return 0;
}

/***
****
***/

static int
test_partial_file (void)
{
  tr_file_index_t i;
  tr_torrent * tor;
  const tr_stat * st;
  tr_file_stat * fst;
  const uint32_t pieceCount = 33;
  const uint32_t pieceSize = 32768;
  const uint32_t length[] = { 1048576, 4096, 512 };
  const uint64_t totalSize = length[0] + length[1] + length[2];
  const char * strings[3];

  /***
  ****  create our test torrent with an incomplete .part file
  ***/

  tor = libttest_zero_torrent_init (session);
  check_int_eq (totalSize, tor->info.totalSize);
  check_int_eq (pieceSize, tor->info.pieceSize);
  check_int_eq (pieceCount, tor->info.pieceCount);
  check_streq ("files-filled-with-zeroes/1048576", tor->info.files[0].name);
  check_streq ("files-filled-with-zeroes/4096",    tor->info.files[1].name);
  check_streq ("files-filled-with-zeroes/512",     tor->info.files[2].name);

  libttest_zero_torrent_populate (tor, false);
  fst = tr_torrentFiles (tor, NULL);
  check_int_eq (length[0] - pieceSize, fst[0].bytesCompleted);
  check_int_eq (length[1],             fst[1].bytesCompleted);
  check_int_eq (length[2],             fst[2].bytesCompleted);
  tr_torrentFilesFree (fst, tor->info.fileCount);
  st = tr_torrentStat (tor);
  check_int_eq (totalSize, st->sizeWhenDone);
  check_int_eq (pieceSize, st->leftUntilDone);

  /***
  ****
  ***/

  check_int_eq (0, torrentRenameAndWait (tor, "files-filled-with-zeroes", "foo"));
  check_int_eq (0, torrentRenameAndWait (tor, "foo/1048576", "bar"));
  strings[0] = "foo/bar";
  strings[1] = "foo/4096";
  strings[2] = "foo/512";
  for (i=0; i<3; ++i)
    {
      check_streq (strings[i], tor->info.files[i].name);
    }

  strings[0] = "foo/bar.part";
  for (i=0; i<3; ++i)
    {
      char * expected = tr_buildPath (tor->currentDir, strings[i], NULL);
      char * path = tr_torrentFindFile (tor, i);
      check_streq (expected, path);
      tr_free (path);
      tr_free (expected);
    }

  tr_torrentRemove (tor, false, NULL);
  return 0;
}

/***
****
***/

int
main (void)
{
  int ret;
  const testFunc tests[] = { test_single_filename_torrent,
                             test_multifile_torrent,
                             test_partial_file };

  session = libttest_session_init (NULL);
  ret = runTests (tests, NUM_TESTS (tests));
  libttest_session_close (session);

  return ret;
}
