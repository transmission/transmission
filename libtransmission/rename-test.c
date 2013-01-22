#include <assert.h>
#include <errno.h>
#include <stdio.h> /* remove() */
#include <string.h> /* strcmp() */
#include <stdio.h>

#include <unistd.h> /* sync() */

#include "transmission.h"
#include "resume.h"
#include "torrent.h" /* tr_isTorrent() */
#include "utils.h" /* tr_mkdirp() */
#include "variant.h"

#include "libtransmission-test.h"

/***
****
***/

#define verify_and_block_until_done(tor) \
  do { \
    tr_torrentVerify (tor); \
    do { \
      tr_wait_msec (10); \
    } while (tor->verifyState != TR_VERIFY_NONE); \
  } while (0)

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

#define check_have_all(tor, totalSize) \
  do { \
    const tr_stat * st = tr_torrentStat(tor); \
    check_int_eq (TR_STATUS_STOPPED, st->activity); \
    check_int_eq (TR_STAT_OK, st->error); \
    check_int_eq (0, st->leftUntilDone); \
    check_int_eq (0, st->haveUnchecked); \
    check_int_eq (0, st->desiredAvailable); \
    check_int_eq (totalSize, st->sizeWhenDone); \
    check_int_eq (totalSize, st->haveValid); \
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

      assert (tr_fileExists (path, NULL));

      contents = tr_loadFile (path, &contents_len);

      success = (str_len == contents_len)
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
create_file_with_contents (const char * path, const char * str)
{
  FILE * fp;
  char * dir;

  dir = tr_dirname (path);
  tr_mkdirp (dir, 0700);
  tr_free (dir);

  remove (path);
  fp = fopen (path, "wb");
  fprintf (fp, "%s", str);
  fclose (fp);

  sync ();
}

static void
create_single_file_torrent_contents (const char * top)
{
  char * path = tr_buildPath (top, "hello-world.txt", NULL);
  create_file_with_contents (path, "hello, world!\n");
  tr_free (path);
}

static tr_torrent *
create_torrent_from_base64_metainfo (tr_ctor * ctor, const char * metainfo_base64)
{
  int err;
  int metainfo_len;
  char * metainfo;
  tr_torrent * tor;

  /* create the torrent ctor */
  metainfo = tr_base64_decode (metainfo_base64, -1, &metainfo_len);
  assert (metainfo != NULL);
  assert (metainfo_len > 0);
  assert (session != NULL);
  tr_ctorSetMetainfo (ctor, (uint8_t*)metainfo, metainfo_len);
  tr_ctorSetPaused (ctor, TR_FORCE, true);

  /* create the torrent */
  err = 0;
  tor = tr_torrentNew (ctor, &err);
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

  /* this is a single-file torrent whose file is hello-world.txt, holding the string "hello, world!" */
  ctor = tr_ctorNew (session);
  tor = create_torrent_from_base64_metainfo (ctor,
    "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
    "ZWkxMzU4NTQ5MDk4ZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDY6bGVuZ3RoaTE0ZTQ6bmFtZTE1"
    "OmhlbGxvLXdvcmxkLnR4dDEyOnBpZWNlIGxlbmd0aGkzMjc2OGU2OnBpZWNlczIwOukboJcrkFUY"
    "f6LvqLXBVvSHqCk6Nzpwcml2YXRlaTBlZWU=");
  check (tr_isTorrent (tor));
  tr_ctorFree (ctor);

  /* sanity check the info */
  check_int_eq (1, tor->info.fileCount);
  check_streq ("hello-world.txt", tor->info.files[0].name);
  check (!tor->info.files[0].is_renamed);

  /* sanity check the (empty) stats */
  verify_and_block_until_done (tor);
  check_have_none (tor, totalSize);

  create_single_file_torrent_contents (tor->downloadDir);

  /* sanity check the stats again, now that we've added the file */
  verify_and_block_until_done (tor);
  check_have_all (tor, totalSize);

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

  tmpstr = tr_buildPath (tor->downloadDir, "hello-world.txt", NULL); 
  check (tr_fileExists (tmpstr, NULL));
  check_streq ("hello-world.txt", tr_torrentName(tor));
  check_int_eq (0, torrentRenameAndWait (tor, "hello-world.txt", "foobar"));
  check (!tr_fileExists (tmpstr, NULL));
  check (tor->info.files[0].is_renamed);
  check_streq ("foobar", tor->info.files[0].name);
  check_streq ("foobar", tr_torrentName(tor));
  check (strstr (tor->info.torrent, "foobar") == NULL);
  check (testFileExistsAndConsistsOfThisString (tor, 0, "hello, world!\n"));
  tr_free (tmpstr);

  /* (while it's renamed: confirm that the .resume file remembers the changes) */
  tr_torrentSaveResume (tor);
  sync ();
  loaded = tr_torrentLoadResume (tor, ~0, ctor);
  check_streq ("foobar", tr_torrentName(tor));
  check ((loaded & TR_FR_NAME) != 0);

  /***
  ****  ...and rename it back again
  ***/

  tmpstr = tr_buildPath (tor->downloadDir, "foobar", NULL); 
  check (tr_fileExists (tmpstr, NULL));
  check_int_eq (0, torrentRenameAndWait (tor, "foobar", "hello-world.txt"));
  check (!tr_fileExists (tmpstr, NULL));
  check (tor->info.files[0].is_renamed);
  check_streq ("hello-world.txt", tor->info.files[0].name);
  check_streq ("hello-world.txt", tr_torrentName(tor));
  tr_free (tmpstr);
  check (testFileExistsAndConsistsOfThisString (tor, 0, "hello, world!\n"));

  /* cleanup */
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
  create_file_with_contents (path, "It ain't easy bein' cheesy.\n");
  tr_free (path);

  path = tr_buildPath (top, "Felidae", "Pantherinae", "Panthera", "Tiger", "Tony", NULL);
  create_file_with_contents (path, "They’re Grrrrreat!\n");
  tr_free (path);

  path = tr_buildPath (top, "Felidae", "Felinae", "Felis", "catus", "Kyphi", NULL);
  create_file_with_contents (path, "Inquisitive\n");
  tr_free (path);

  path = tr_buildPath (top, "Felidae", "Felinae", "Felis", "catus", "Saffron", NULL);
  create_file_with_contents (path, "Tough\n");
  tr_free (path);

  sync ();
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
  const tr_file * files;
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
  verify_and_block_until_done (tor);
  check_have_none (tor, totalSize);

  /* build the local data */
  create_multifile_torrent_contents (tor->downloadDir);

  /* sanity check the (full) stats */
  verify_and_block_until_done (tor);
  check_have_all (tor, totalSize);

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
      check (testFileExistsAndConsistsOfThisString (tor, 1, expected_contents[1]));
    }
  check (files[0].is_renamed == false);
  check (files[1].is_renamed == true);
  check (files[2].is_renamed == true);
  check (files[3].is_renamed == false);

  /***
  ****  Test it an incomplete torrent...
  ***/

  /* remove the directory Felidae/Felinae/Felis/catus */
  str = tr_buildPath (tor->downloadDir, files[1].name, NULL);
  remove (str);
  tr_free (str);
  str = tr_buildPath (tor->downloadDir, files[2].name, NULL);
  remove (str);
  tmp = tr_dirname (str);
  remove (tmp);
  tr_free (tmp);
  tr_free (str);
  verify_and_block_until_done (tor);
  testFileExistsAndConsistsOfThisString (tor, 0, expected_contents[0]);
  check (tr_torrentFindFile (tor, 1) == NULL);
  check (tr_torrentFindFile (tor, 2) == NULL);
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
  check_streq ("gabba/Felinae/Acinonyx/Cheetah/Chester",  files[0].name);
  check_streq ("gabba/Felinae/Felis/catus/Kyphi",         files[1].name);
  check_streq ("gabba/Felinae/Felis/catus/Saffron",       files[2].name);
  check_streq ("gabba/Pantherinae/Panthera/Tiger/Tony", files[3].name);

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

int
main (void)
{
  int ret;
  const testFunc tests[] = { test_single_filename_torrent,
                             test_multifile_torrent };

  libtransmission_test_session_init ();
  ret = runTests (tests, NUM_TESTS (tests));
  libtransmission_test_session_close ();

  return ret;
}
