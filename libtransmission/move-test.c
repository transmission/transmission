/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <string.h> /* strcmp() */
#include <stdio.h>

#include <event2/buffer.h>

#include "transmission.h"
#include "cache.h"
#include "file.h"
#include "resume.h"
#include "trevent.h"
#include "torrent.h" /* tr_isTorrent() */
#include "variant.h"

#include "libtransmission-test.h"

/***
****
***/

static void
zeroes_completeness_func (tr_torrent       * torrent UNUSED,
                          tr_completeness    completeness,
                          bool               wasRunning UNUSED,
                          void             * user_data)
{
  *(tr_completeness*)user_data = completeness;
}

#define check_file_location(tor, i, expected_path) \
  do { \
    char * path = tr_torrentFindFile (tor, i); \
    char * expected = expected_path; \
    check_streq (expected, path); \
    tr_free (expected); \
    tr_free (path); \
  } while (0)

struct test_incomplete_dir_data
{
  tr_session * session;
  tr_torrent * tor;
  tr_block_index_t block;
  tr_piece_index_t pieceIndex;
  uint32_t offset;
  struct evbuffer * buf;
  bool done;
};

static void
test_incomplete_dir_threadfunc (void * vdata)
{
  struct test_incomplete_dir_data * data = vdata;
  tr_cacheWriteBlock (data->session->cache, data->tor, 0, data->offset, data->tor->blockSize, data->buf);
  tr_torrentGotBlock (data->tor, data->block);
  data->done = true;
}

static int
test_incomplete_dir_impl (const char * incomplete_dir, const char * download_dir)
{
  size_t i;
  tr_session * session;
  tr_torrent * tor;
  tr_completeness completeness;
  const tr_completeness completeness_unset = -1;
  const time_t deadline = time(NULL) + 300;
  tr_variant settings;

  /* init the session */
  tr_variantInitDict (&settings, 3);
  tr_variantDictAddStr (&settings, TR_KEY_download_dir, download_dir);
  tr_variantDictAddStr (&settings, TR_KEY_incomplete_dir, incomplete_dir);
  tr_variantDictAddBool (&settings, TR_KEY_incomplete_dir_enabled, true);
  session = libttest_session_init (&settings);
  tr_variantFree (&settings);
  download_dir = tr_sessionGetDownloadDir (session);
  incomplete_dir = tr_sessionGetIncompleteDir (session);

  /* init an incomplete torrent.
     the test zero_torrent will be missing its first piece */
  tor = libttest_zero_torrent_init (session);
  libttest_zero_torrent_populate (tor, false);
  check (tr_torrentStat(tor)->leftUntilDone == tor->info.pieceSize);
  check_file_location (tor, 0, tr_strdup_printf("%s/%s.part", incomplete_dir, tor->info.files[0].name));
  check_file_location (tor, 1, tr_buildPath(incomplete_dir, tor->info.files[1].name, NULL));
  check_int_eq (tor->info.pieceSize, tr_torrentStat(tor)->leftUntilDone);

  completeness = completeness_unset;
  tr_torrentSetCompletenessCallback (tor, zeroes_completeness_func, &completeness);

  /* now finish writing it */
  {
    tr_block_index_t first, last;
    char * zero_block = tr_new0 (char, tor->blockSize);
    struct test_incomplete_dir_data data;

    data.session = session;
    data.tor = tor;
    data.pieceIndex = 0;
    data.buf = evbuffer_new ();

    tr_torGetPieceBlockRange (tor, data.pieceIndex, &first, &last);
    for (i=first; i<=last; ++i)
      {
        evbuffer_add (data.buf, zero_block, tor->blockSize);
        data.block = i;
        data.done = false;
        data.offset = data.block * tor->blockSize;
        tr_runInEventThread (session, test_incomplete_dir_threadfunc, &data);
        do { tr_wait_msec(50); } while (!data.done);
      }

    evbuffer_free (data.buf);
    tr_free (zero_block);
  }

  libttest_blockingTorrentVerify (tor);
  check_int_eq (0, tr_torrentStat(tor)->leftUntilDone);

  while ((completeness==completeness_unset) && (time(NULL)<=deadline))
    tr_wait_msec (50);

  check_int_eq (TR_SEED, completeness);
  for (i=0; i<tor->info.fileCount; ++i)
    check_file_location (tor, i, tr_buildPath (download_dir, tor->info.files[i].name, NULL));

  /* cleanup */
  tr_torrentRemove (tor, true, tr_sys_path_remove);
  libttest_session_close (session);
  return 0;
}

static int
test_incomplete_dir (void)
{
  int rv;

  /* test what happens when incompleteDir is a subdir of downloadDir*/
  if ((rv = test_incomplete_dir_impl ("Downloads/Incomplete", "Downloads")))
    return rv;

  /* test what happens when downloadDir is a subdir of incompleteDir */
  if ((rv = test_incomplete_dir_impl ("Downloads", "Downloads/Complete")))
    return rv;

  /* test what happens when downloadDir and incompleteDir are siblings */
  if ((rv = test_incomplete_dir_impl ("Incomplete", "Downloads")))
    return rv;

  return 0;
}

/***
****
***/

static int
test_set_location (void)
{
  size_t i;
  int state;
  char * target_dir;
  tr_torrent * tor;
  tr_session * session;
  const time_t deadline = time(NULL) + 300;

  /* init the session */
  session = libttest_session_init (NULL);
  target_dir = tr_buildPath (tr_sessionGetConfigDir (session), "target", NULL);
  tr_sys_dir_create (target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, NULL);

  /* init a torrent. */
  tor = libttest_zero_torrent_init (session);
  libttest_zero_torrent_populate (tor, true);
  libttest_blockingTorrentVerify (tor);
  check_int_eq (0, tr_torrentStat(tor)->leftUntilDone);

  /* now move it */
  state = -1;
  tr_torrentSetLocation (tor, target_dir, true, NULL, &state);
  while ((state==TR_LOC_MOVING) && (time(NULL)<=deadline))
    tr_wait_msec (50);
  check_int_eq (TR_LOC_DONE, state);

  /* confirm the torrent is still complete after being moved */
  libttest_blockingTorrentVerify (tor);
  check_int_eq (0, tr_torrentStat(tor)->leftUntilDone);

  /* confirm the filest really got moved */
  libttest_sync ();
  for (i=0; i<tor->info.fileCount; ++i)
    check_file_location (tor, i, tr_buildPath (target_dir, tor->info.files[i].name, NULL));

  /* cleanup */
  tr_free (target_dir);
  tr_torrentRemove (tor, true, tr_sys_path_remove);
  libttest_session_close (session);
  return 0;
}

/***
****
***/

int
main (void)
{
  const testFunc tests[] = { test_incomplete_dir,
                             test_set_location };

  return runTests (tests, NUM_TESTS (tests));
}


