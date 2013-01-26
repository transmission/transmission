#include <assert.h>
#include <errno.h>
#include <stdio.h> /* remove() */
#include <string.h> /* strcmp() */
#include <stdio.h>

#include <sys/types.h> /* stat() */
#include <sys/stat.h> /* stat() */
#include <unistd.h> /* stat(), sync() */

#include <event2/buffer.h>

#include "transmission.h"
#include "cache.h"
#include "resume.h"
#include "torrent.h" /* tr_isTorrent() */
#include "utils.h" /* tr_mkdirp() */
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


static int
test_incomplete_dir_is_subdir_of_download_dir (void)
{
  tr_file_index_t i;
  char * path;
  char * incomplete_dir;
  char * expected_path;
  tr_torrent * tor;
  tr_completeness completeness;

  /* init the session */
  libtransmission_test_session_init ();
  incomplete_dir = tr_buildPath (downloadDir, "incomplete", NULL);
  tr_sessionSetIncompleteDir (session, incomplete_dir);
  tr_sessionSetIncompleteDirEnabled (session, true);

  /* init an incomplete torrent */
  tor = libtransmission_test_zero_torrent_init ();
  libtransmission_test_zero_torrent_populate (tor, false);
  check (tr_torrentStat(tor)->leftUntilDone == tor->info.pieceSize);
  path = tr_torrentFindFile (tor, 0);
  expected_path = tr_strdup_printf ("%s/%s.part", incomplete_dir, tor->info.files[0].name);
  check_streq (expected_path, path);
  tr_free (expected_path);
  tr_free (path);
  path = tr_torrentFindFile (tor, 1);
  expected_path = tr_buildPath (incomplete_dir, tor->info.files[1].name, NULL);
  check_streq (expected_path, path);
  tr_free (expected_path);
  tr_free (path);
  check_int_eq (tor->info.pieceSize, tr_torrentStat(tor)->leftUntilDone);

  /* now finish writing it */
  {
    //char * block;
    uint32_t offset;
    tr_block_index_t i;
    tr_block_index_t first;
    tr_block_index_t last;
    char * tobuf;
    struct evbuffer * buf;

    tobuf = tr_new0 (char, tor->blockSize);
    buf = evbuffer_new ();

    tr_torGetPieceBlockRange (tor, 0, &first, &last);
    for (offset=0, i=first; i<=last; ++i, offset+=tor->blockSize)
      {
        evbuffer_add (buf, tobuf, tor->blockSize);
        tr_cacheWriteBlock (session->cache, tor, 0, offset, tor->blockSize, buf);
        tr_torrentGotBlock (tor, i);
      }

    evbuffer_free (buf);
    tr_free (tobuf);
  }

  completeness = -1;
  tr_torrentSetCompletenessCallback (tor, zeroes_completeness_func, &completeness);
  tr_torrentRecheckCompleteness (tor);
  check_int_eq (TR_SEED, completeness);
  sync ();
  for (i=0; i<tor->info.fileCount; ++i)
    {
      path = tr_torrentFindFile (tor, i);
      expected_path = tr_buildPath (downloadDir, tor->info.files[i].name, NULL);
      check_streq (expected_path, path);
      tr_free (expected_path);
      tr_free (path);
    }


  /* cleanup */
  tr_torrentRemove (tor, true, remove);
  libtransmission_test_session_close ();
  tr_free (incomplete_dir);
  return 0;
}


/***
****
***/

int
main (void)
{
  const testFunc tests[] = { test_incomplete_dir_is_subdir_of_download_dir };

  return runTests (tests, NUM_TESTS (tests));
}


