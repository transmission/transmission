/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

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

static void zeroes_completeness_func(tr_torrent* torrent UNUSED, tr_completeness completeness, bool wasRunning UNUSED,
    void* user_data)
{
    *(tr_completeness*)user_data = completeness;
}

#define check_file_location(tor, i, expected_path) \
    do \
    { \
        char* path = tr_torrentFindFile(tor, i); \
        char* expected = expected_path; \
        check_str(path, ==, expected); \
        tr_free(expected); \
        tr_free(path); \
    } \
    while (0)

struct test_incomplete_dir_data
{
    tr_session* session;
    tr_torrent* tor;
    tr_block_index_t block;
    tr_piece_index_t pieceIndex;
    uint32_t offset;
    struct evbuffer* buf;
    bool done;
};

static void test_incomplete_dir_threadfunc(void* vdata)
{
    struct test_incomplete_dir_data* data = vdata;
    tr_cacheWriteBlock(data->session->cache, data->tor, 0, data->offset, data->tor->blockSize, data->buf);
    tr_torrentGotBlock(data->tor, data->block);
    data->done = true;
}

static int test_incomplete_dir_impl(char const* incomplete_dir, char const* download_dir)
{
    tr_session* session;
    tr_torrent* tor;
    tr_completeness completeness;
    tr_completeness const completeness_unset = -1;
    time_t const deadline = time(NULL) + 300;
    tr_variant settings;

    /* init the session */
    tr_variantInitDict(&settings, 3);
    tr_variantDictAddStr(&settings, TR_KEY_download_dir, download_dir);
    tr_variantDictAddStr(&settings, TR_KEY_incomplete_dir, incomplete_dir);
    tr_variantDictAddBool(&settings, TR_KEY_incomplete_dir_enabled, true);
    session = libttest_session_init(&settings);
    tr_variantFree(&settings);
    download_dir = tr_sessionGetDownloadDir(session);
    incomplete_dir = tr_sessionGetIncompleteDir(session);

    /* init an incomplete torrent.
       the test zero_torrent will be missing its first piece */
    tor = libttest_zero_torrent_init(session);
    libttest_zero_torrent_populate(tor, false);
    check_uint(tr_torrentStat(tor)->leftUntilDone, ==, tor->info.pieceSize);
    check_file_location(tor, 0, tr_strdup_printf("%s/%s.part", incomplete_dir, tor->info.files[0].name));
    check_file_location(tor, 1, tr_buildPath(incomplete_dir, tor->info.files[1].name, NULL));
    check_uint(tr_torrentStat(tor)->leftUntilDone, ==, tor->info.pieceSize);

    completeness = completeness_unset;
    tr_torrentSetCompletenessCallback(tor, zeroes_completeness_func, &completeness);

    /* now finish writing it */
    {
        tr_block_index_t first;
        tr_block_index_t last;
        char* zero_block = tr_new0(char, tor->blockSize);
        struct test_incomplete_dir_data data;

        data.session = session;
        data.tor = tor;
        data.pieceIndex = 0;
        data.buf = evbuffer_new();

        tr_torGetPieceBlockRange(tor, data.pieceIndex, &first, &last);

        for (tr_block_index_t block_index = first; block_index <= last; ++block_index)
        {
            evbuffer_add(data.buf, zero_block, tor->blockSize);
            data.block = block_index;
            data.done = false;
            data.offset = data.block * tor->blockSize;
            tr_runInEventThread(session, test_incomplete_dir_threadfunc, &data);

            do
            {
                tr_wait_msec(50);
            }
            while (!data.done);
        }

        evbuffer_free(data.buf);
        tr_free(zero_block);
    }

    libttest_blockingTorrentVerify(tor);
    check_uint(tr_torrentStat(tor)->leftUntilDone, ==, 0);

    while (completeness == completeness_unset && time(NULL) <= deadline)
    {
        tr_wait_msec(50);
    }

    check_int(completeness, ==, TR_SEED);

    for (tr_file_index_t file_index = 0; file_index < tor->info.fileCount; ++file_index)
    {
        check_file_location(tor, file_index, tr_buildPath(download_dir, tor->info.files[file_index].name, NULL));
    }

    /* cleanup */
    tr_torrentRemove(tor, true, tr_sys_path_remove);
    libttest_session_close(session);
    return 0;
}

static int test_incomplete_dir(void)
{
    int rv;

    /* test what happens when incompleteDir is a subdir of downloadDir*/
    if ((rv = test_incomplete_dir_impl("Downloads/Incomplete", "Downloads")) != 0)
    {
        return rv;
    }

    /* test what happens when downloadDir is a subdir of incompleteDir */
    if ((rv = test_incomplete_dir_impl("Downloads", "Downloads/Complete")) != 0)
    {
        return rv;
    }

    /* test what happens when downloadDir and incompleteDir are siblings */
    if ((rv = test_incomplete_dir_impl("Incomplete", "Downloads")) != 0)
    {
        return rv;
    }

    return 0;
}

/***
****
***/

static int test_set_location(void)
{
    int state;
    char* target_dir;
    tr_torrent* tor;
    tr_session* session;
    time_t const deadline = time(NULL) + 300;

    /* init the session */
    session = libttest_session_init(NULL);
    target_dir = tr_buildPath(tr_sessionGetConfigDir(session), "target", NULL);
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, NULL);

    /* init a torrent. */
    tor = libttest_zero_torrent_init(session);
    libttest_zero_torrent_populate(tor, true);
    libttest_blockingTorrentVerify(tor);
    check_uint(tr_torrentStat(tor)->leftUntilDone, ==, 0);

    /* now move it */
    state = -1;
    tr_torrentSetLocation(tor, target_dir, true, NULL, &state);

    while (state == TR_LOC_MOVING && time(NULL) <= deadline)
    {
        tr_wait_msec(50);
    }

    check_int(state, ==, TR_LOC_DONE);

    /* confirm the torrent is still complete after being moved */
    libttest_blockingTorrentVerify(tor);
    check_uint(tr_torrentStat(tor)->leftUntilDone, ==, 0);

    /* confirm the filest really got moved */
    libttest_sync();

    for (tr_file_index_t file_index = 0; file_index < tor->info.fileCount; ++file_index)
    {
        check_file_location(tor, file_index, tr_buildPath(target_dir, tor->info.files[file_index].name, NULL));
    }

    /* cleanup */
    tr_free(target_dir);
    tr_torrentRemove(tor, true, tr_sys_path_remove);
    libttest_session_close(session);
    return 0;
}

/***
****
***/

int main(void)
{
    testFunc const tests[] =
    {
        test_incomplete_dir,
        test_set_location
    };

    return runTests(tests, NUM_TESTS(tests));
}
