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

#define check_file_location(tor, i, expected_path) \
  do { \
    char * path = tr_torrentFindFile (tor, i); \
    char * expected = expected_path; \
    check_streq (expected, path); \
    tr_free (expected); \
    tr_free (path); \
  } while (0)

static int
test_incomplete_dir_is_subdir_of_download_dir (void)
{
  char * incomplete_dir;
  tr_torrent * tor;
  tr_completeness completeness;
  const tr_completeness completeness_unset = -1;
  const time_t deadline = time(NULL) + 5;

  /* init the session */
  libtransmission_test_session_init ();
  incomplete_dir = tr_buildPath (downloadDir, "incomplete", NULL);
  tr_sessionSetIncompleteDir (session, incomplete_dir);
  tr_sessionSetIncompleteDirEnabled (session, true);

  /* init an incomplete torrent.
     the test zero_torrent will be missing its first piece */
  tor = libtransmission_test_zero_torrent_init ();
  libtransmission_test_zero_torrent_populate (tor, false);
  check (tr_torrentStat(tor)->leftUntilDone == tor->info.pieceSize);
  check_file_location (tor, 0, tr_strdup_printf("%s/%s.part", incomplete_dir, tor->info.files[0].name));
  check_file_location (tor, 1, tr_buildPath(incomplete_dir, tor->info.files[1].name, NULL));
  check_int_eq (tor->info.pieceSize, tr_torrentStat(tor)->leftUntilDone);

  /* now finish writing it */
  {
    uint32_t offset;
    tr_block_index_t first, last, i;
    struct evbuffer * buf = evbuffer_new ();
    char * zero_block = tr_new0 (char, tor->blockSize);

    completeness = completeness_unset;
    tr_torrentSetCompletenessCallback (tor, zeroes_completeness_func, &completeness);

    tr_torGetPieceBlockRange (tor, 0, &first, &last);
    for (offset=0, i=first; i<=last; ++i, offset+=tor->blockSize)
      {
        evbuffer_add (buf, zero_block, tor->blockSize);
        tr_cacheWriteBlock (session->cache, tor, 0, offset, tor->blockSize, buf);
        tr_torrentGotBlock (tor, i);
      }
    sync ();

    tr_torrentVerify (tor);
    while ((completeness==completeness_unset) && (time(NULL)<=deadline))
      tr_wait_msec (50);
    check_int_eq (TR_SEED, completeness);
    for (i=0; i<tor->info.fileCount; ++i)
      check_file_location (tor, i, tr_buildPath (downloadDir, tor->info.files[i].name, NULL));

    evbuffer_free (buf);
    tr_free (zero_block);
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


