/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <stdio.h> /* fopen() */
#include <string.h> /* strcmp() */

#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
#include "resume.h"
#include "torrent.h" /* tr_isTorrent() */
#include "tr-assert.h"
#include "variant.h"

#include "libtransmission-test.h"

/***
****
***/

static tr_session* session = NULL;

#define check_have_none(tor, totalSize) \
    do \
    { \
        tr_stat const* tst = tr_torrentStat(tor); \
        check_int(tst->activity, ==, TR_STATUS_STOPPED); \
        check_int(tst->error, ==, TR_STAT_OK); \
        check_uint(tst->sizeWhenDone, ==, totalSize); \
        check_uint(tst->leftUntilDone, ==, totalSize); \
        check_uint(tor->info.totalSize, ==, totalSize); \
        check_uint(tst->haveValid, ==, 0); \
    } \
    while (0)

static bool testFileExistsAndConsistsOfThisString(tr_torrent const* tor, tr_file_index_t fileIndex, char const* str)
{
    char* path;
    size_t const str_len = strlen(str);
    bool success = false;

    path = tr_torrentFindFile(tor, fileIndex);

    if (path != NULL)
    {
        TR_ASSERT(tr_sys_path_exists(path, NULL));

        size_t contents_len;
        uint8_t* contents = tr_loadFile(path, &contents_len, NULL);

        success = contents != NULL && str_len == contents_len && memcmp(contents, str, contents_len) == 0;

        tr_free(contents);
        tr_free(path);
    }

    return success;
}

static void onRenameDone(tr_torrent* tor UNUSED, char const* oldpath UNUSED, char const* newname UNUSED, int error,
    void* user_data)
{
    *(int*)user_data = error;
}

static int torrentRenameAndWait(tr_torrent* tor, char const* oldpath, char const* newname)
{
    int error = -1;
    tr_torrentRenamePath(tor, oldpath, newname, onRenameDone, &error);

    do
    {
        tr_wait_msec(10);
    }
    while (error == -1);

    return error;
}

static void torrentRemoveAndWait(tr_torrent* tor, int expected_torrent_count)
{
    tr_torrentRemove(tor, false, NULL);

    while (tr_sessionCountTorrents(session) != expected_torrent_count)
    {
        tr_wait_msec(10);
    }
}

/***
****
***/

static void create_single_file_torrent_contents(char const* top)
{
    char* path = tr_buildPath(top, "hello-world.txt", NULL);
    libtest_create_file_with_string_contents(path, "hello, world!\n");
    tr_free(path);
}

static tr_torrent* create_torrent_from_base64_metainfo(tr_ctor* ctor, char const* metainfo_base64)
{
    int err;
    size_t metainfo_len;
    char* metainfo;
    tr_torrent* tor;

    /* create the torrent ctor */
    metainfo = tr_base64_decode_str(metainfo_base64, &metainfo_len);
    TR_ASSERT(metainfo != NULL);
    TR_ASSERT(metainfo_len > 0);
    tr_ctorSetMetainfo(ctor, (uint8_t*)metainfo, metainfo_len);
    tr_ctorSetPaused(ctor, TR_FORCE, true);

    /* create the torrent */
    err = 0;
    tor = tr_torrentNew(ctor, &err, NULL);
    TR_ASSERT(err == 0);

    /* cleanup */
    tr_free(metainfo);
    return tor;
}

static int test_single_filename_torrent(void)
{
    uint64_t loaded;
    tr_torrent* tor;
    char* tmpstr;
    size_t const totalSize = 14;
    tr_ctor* ctor;
    tr_stat const* st;

    /* this is a single-file torrent whose file is hello-world.txt, holding the string "hello, world!" */
    ctor = tr_ctorNew(session);
    tor = create_torrent_from_base64_metainfo(ctor,
        "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
        "ZWkxMzU4NTQ5MDk4ZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDY6bGVuZ3RoaTE0ZTQ6bmFtZTE1"
        "OmhlbGxvLXdvcmxkLnR4dDEyOnBpZWNlIGxlbmd0aGkzMjc2OGU2OnBpZWNlczIwOukboJcrkFUY"
        "f6LvqLXBVvSHqCk6Nzpwcml2YXRlaTBlZWU=");
    check(tr_isTorrent(tor));

    /* sanity check the info */
    check_int(tor->info.fileCount, ==, 1);
    check_str(tor->info.files[0].name, ==, "hello-world.txt");
    check(!tor->info.files[0].is_renamed);

    /* sanity check the (empty) stats */
    libttest_blockingTorrentVerify(tor);
    check_have_none(tor, totalSize);

    create_single_file_torrent_contents(tor->currentDir);

    /* sanity check the stats again, now that we've added the file */
    libttest_blockingTorrentVerify(tor);
    st = tr_torrentStat(tor);
    check_int(st->activity, ==, TR_STATUS_STOPPED);
    check_int(st->error, ==, TR_STAT_OK);
    check_uint(st->leftUntilDone, ==, 0);
    check_uint(st->haveUnchecked, ==, 0);
    check_uint(st->desiredAvailable, ==, 0);
    check_uint(st->sizeWhenDone, ==, totalSize);
    check_uint(st->haveValid, ==, totalSize);

    /**
    ***  okay! we've finally put together all the scaffolding to test
    ***  renaming a single-file torrent
    **/

    /* confirm that bad inputs get caught */

    check_int(torrentRenameAndWait(tor, "hello-world.txt", NULL), ==, EINVAL);
    check_int(torrentRenameAndWait(tor, "hello-world.txt", ""), ==, EINVAL);
    check_int(torrentRenameAndWait(tor, "hello-world.txt", "."), ==, EINVAL);
    check_int(torrentRenameAndWait(tor, "hello-world.txt", ".."), ==, EINVAL);
    check_int(torrentRenameAndWait(tor, "hello-world.txt", "hello-world.txt"), ==, 0);
    check_int(torrentRenameAndWait(tor, "hello-world.txt", "hello/world.txt"), ==, EINVAL);

    check(!tor->info.files[0].is_renamed);
    check_str(tor->info.files[0].name, ==, "hello-world.txt");

    /***
    ****  Now try a rename that should succeed
    ***/

    tmpstr = tr_buildPath(tor->currentDir, "hello-world.txt", NULL);
    check(tr_sys_path_exists(tmpstr, NULL));
    check_str(tr_torrentName(tor), ==, "hello-world.txt");
    check_int(torrentRenameAndWait(tor, tor->info.name, "foobar"), ==, 0);
    check(!tr_sys_path_exists(tmpstr, NULL)); /* confirm the old filename can't be found */
    tr_free(tmpstr);
    check(tor->info.files[0].is_renamed); /* confirm the file's 'renamed' flag is set */
    check_str(tr_torrentName(tor), ==, "foobar"); /* confirm the torrent's name is now 'foobar' */
    check_str(tor->info.files[0].name, ==, "foobar"); /* confirm the file's name is now 'foobar' in our struct */
    check_str(strstr(tor->info.torrent, "foobar"), ==, NULL); /* confirm the name in the .torrent file hasn't changed */
    tmpstr = tr_buildPath(tor->currentDir, "foobar", NULL);
    check(tr_sys_path_exists(tmpstr, NULL)); /* confirm the file's name is now 'foobar' on the disk */
    tr_free(tmpstr);
    check(testFileExistsAndConsistsOfThisString(tor, 0, "hello, world!\n")); /* confirm the contents are right */

    /* (while it's renamed: confirm that the .resume file remembers the changes) */
    tr_torrentSaveResume(tor);
    libttest_sync();
    loaded = tr_torrentLoadResume(tor, ~0, ctor, NULL);
    check_str(tr_torrentName(tor), ==, "foobar");
    check_uint((loaded & TR_FR_NAME), !=, 0);

    /***
    ****  ...and rename it back again
    ***/

    tmpstr = tr_buildPath(tor->currentDir, "foobar", NULL);
    check(tr_sys_path_exists(tmpstr, NULL));
    check_int(torrentRenameAndWait(tor, "foobar", "hello-world.txt"), ==, 0);
    check(!tr_sys_path_exists(tmpstr, NULL));
    check(tor->info.files[0].is_renamed);
    check_str(tor->info.files[0].name, ==, "hello-world.txt");
    check_str(tr_torrentName(tor), ==, "hello-world.txt");
    tr_free(tmpstr);
    check(testFileExistsAndConsistsOfThisString(tor, 0, "hello, world!\n"));

    /* cleanup */
    tr_ctorFree(ctor);
    torrentRemoveAndWait(tor, 0);
    return 0;
}

/***
****
****
****
***/

static void create_multifile_torrent_contents(char const* top)
{
    char* path;

    path = tr_buildPath(top, "Felidae", "Felinae", "Acinonyx", "Cheetah", "Chester", NULL);
    libtest_create_file_with_string_contents(path, "It ain't easy bein' cheesy.\n");
    tr_free(path);

    path = tr_buildPath(top, "Felidae", "Pantherinae", "Panthera", "Tiger", "Tony", NULL);
    libtest_create_file_with_string_contents(path, "They’re Grrrrreat!\n");
    tr_free(path);

    path = tr_buildPath(top, "Felidae", "Felinae", "Felis", "catus", "Kyphi", NULL);
    libtest_create_file_with_string_contents(path, "Inquisitive\n");
    tr_free(path);

    path = tr_buildPath(top, "Felidae", "Felinae", "Felis", "catus", "Saffron", NULL);
    libtest_create_file_with_string_contents(path, "Tough\n");
    tr_free(path);

    libttest_sync();
}

static int test_multifile_torrent(void)
{
    uint64_t loaded;
    tr_torrent* tor;
    tr_ctor* ctor;
    char* str;
    char* tmp;
    static size_t const totalSize = 67;
    tr_stat const* st;
    tr_file const* files;
    char const* strings[4];
    char const* expected_files[4] =
    {
        "Felidae/Felinae/Acinonyx/Cheetah/Chester",
        "Felidae/Felinae/Felis/catus/Kyphi",
        "Felidae/Felinae/Felis/catus/Saffron",
        "Felidae/Pantherinae/Panthera/Tiger/Tony"
    };
    char const* expected_contents[4] =
    {
        "It ain't easy bein' cheesy.\n",
        "Inquisitive\n",
        "Tough\n",
        "They’re Grrrrreat!\n"
    };

    ctor = tr_ctorNew(session);
    tor = create_torrent_from_base64_metainfo(ctor,
        "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
        "ZWkxMzU4NTU1NDIwZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDU6ZmlsZXNsZDY6bGVuZ3RoaTI4"
        "ZTQ6cGF0aGw3OkZlbGluYWU4OkFjaW5vbnl4NzpDaGVldGFoNzpDaGVzdGVyZWVkNjpsZW5ndGhp"
        "MTJlNDpwYXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNTpLeXBoaWVlZDY6bGVuZ3RoaTZlNDpw"
        "YXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNzpTYWZmcm9uZWVkNjpsZW5ndGhpMjFlNDpwYXRo"
        "bDExOlBhbnRoZXJpbmFlODpQYW50aGVyYTU6VGlnZXI0OlRvbnllZWU0Om5hbWU3OkZlbGlkYWUx"
        "MjpwaWVjZSBsZW5ndGhpMzI3NjhlNjpwaWVjZXMyMDp27buFkmy8ICfNX4nsJmt0Ckm2Ljc6cHJp"
        "dmF0ZWkwZWVl");
    check(tr_isTorrent(tor));
    files = tor->info.files;

    /* sanity check the info */
    check_str(tor->info.name, ==, "Felidae");
    check_uint(tor->info.totalSize, ==, totalSize);
    check_uint(tor->info.fileCount, ==, 4);

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        check_str(files[i].name, ==, expected_files[i]);
    }

    /* sanity check the (empty) stats */
    libttest_blockingTorrentVerify(tor);
    check_have_none(tor, totalSize);

    /* build the local data */
    create_multifile_torrent_contents(tor->currentDir);

    /* sanity check the (full) stats */
    libttest_blockingTorrentVerify(tor);
    st = tr_torrentStat(tor);
    check_int(st->activity, ==, TR_STATUS_STOPPED);
    check_int(st->error, ==, TR_STAT_OK);
    check_uint(st->leftUntilDone, ==, 0);
    check_uint(st->haveUnchecked, ==, 0);
    check_uint(st->desiredAvailable, ==, 0);
    check_uint(st->sizeWhenDone, ==, totalSize);
    check_uint(st->haveValid, ==, totalSize);

    /**
    ***  okay! let's test renaming.
    **/

    /* rename a leaf... */
    check_int(torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus/Kyphi", "placeholder"), ==, 0);
    check_str(files[1].name, ==, "Felidae/Felinae/Felis/catus/placeholder");
    check(testFileExistsAndConsistsOfThisString(tor, 1, "Inquisitive\n"));

    /* ...and back again */
    check_int(torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus/placeholder", "Kyphi"), ==, 0);
    check_str(files[1].name, ==, "Felidae/Felinae/Felis/catus/Kyphi");
    testFileExistsAndConsistsOfThisString(tor, 1, "Inquisitive\n");

    /* rename a branch... */
    check_int(torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus", "placeholder"), ==, 0);
    check_str(files[0].name, ==, expected_files[0]);
    check_str(files[1].name, ==, "Felidae/Felinae/Felis/placeholder/Kyphi");
    check_str(files[2].name, ==, "Felidae/Felinae/Felis/placeholder/Saffron");
    check_str(files[3].name, ==, expected_files[3]);
    check(testFileExistsAndConsistsOfThisString(tor, 1, expected_contents[1]));
    check(testFileExistsAndConsistsOfThisString(tor, 2, expected_contents[2]));
    check(!files[0].is_renamed);
    check(files[1].is_renamed);
    check(files[2].is_renamed);
    check(!files[3].is_renamed);

    /* (while the branch is renamed: confirm that the .resume file remembers the changes) */
    tr_torrentSaveResume(tor);
    /* this is a bit dodgy code-wise, but let's make sure the .resume file got the name */
    tr_free(files[1].name);
    tor->info.files[1].name = tr_strdup("gabba gabba hey");
    loaded = tr_torrentLoadResume(tor, ~0, ctor, NULL);
    check_uint((loaded & TR_FR_FILENAMES), !=, 0);
    check_str(files[0].name, ==, expected_files[0]);
    check_str(files[1].name, ==, "Felidae/Felinae/Felis/placeholder/Kyphi");
    check_str(files[2].name, ==, "Felidae/Felinae/Felis/placeholder/Saffron");
    check_str(files[3].name, ==, expected_files[3]);

    /* ...and back again */
    check_int(torrentRenameAndWait(tor, "Felidae/Felinae/Felis/placeholder", "catus"), ==, 0);

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        check_str(files[i].name, ==, expected_files[i]);
        check(testFileExistsAndConsistsOfThisString(tor, i, expected_contents[i]));
    }

    check(!files[0].is_renamed);
    check(files[1].is_renamed);
    check(files[2].is_renamed);
    check(!files[3].is_renamed);

    /***
    ****  Test it an incomplete torrent...
    ***/

    /* remove the directory Felidae/Felinae/Felis/catus */
    str = tr_torrentFindFile(tor, 1);
    check_str(str, !=, NULL);
    tr_sys_path_remove(str, NULL);
    tr_free(str);
    str = tr_torrentFindFile(tor, 2);
    check_str(str, !=, NULL);
    tr_sys_path_remove(str, NULL);
    tmp = tr_sys_path_dirname(str, NULL);
    tr_sys_path_remove(tmp, NULL);
    tr_free(tmp);
    tr_free(str);
    libttest_sync();
    libttest_blockingTorrentVerify(tor);
    testFileExistsAndConsistsOfThisString(tor, 0, expected_contents[0]);

    for (tr_file_index_t i = 1; i <= 2; ++i)
    {
        str = tr_torrentFindFile(tor, i);
        check_str(str, ==, NULL);
        tr_free(str);
    }

    testFileExistsAndConsistsOfThisString(tor, 3, expected_contents[3]);

    /* rename a branch... */
    check_int(torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus", "foo"), ==, 0);
    check_str(files[0].name, ==, expected_files[0]);
    check_str(files[1].name, ==, "Felidae/Felinae/Felis/foo/Kyphi");
    check_str(files[2].name, ==, "Felidae/Felinae/Felis/foo/Saffron");
    check_str(files[3].name, ==, expected_files[3]);

    /* ...and back again */
    check_int(torrentRenameAndWait(tor, "Felidae/Felinae/Felis/foo", "catus"), ==, 0);

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        check_str(files[i].name, ==, expected_files[i]);
    }

    check_int(torrentRenameAndWait(tor, "Felidae", "gabba"), ==, 0);
    strings[0] = "gabba/Felinae/Acinonyx/Cheetah/Chester";
    strings[1] = "gabba/Felinae/Felis/catus/Kyphi";
    strings[2] = "gabba/Felinae/Felis/catus/Saffron";
    strings[3] = "gabba/Pantherinae/Panthera/Tiger/Tony";

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        check_str(files[i].name, ==, strings[i]);
        testFileExistsAndConsistsOfThisString(tor, i, expected_contents[i]);
    }

    /* rename the root, then a branch, and then a leaf... */
    check_int(torrentRenameAndWait(tor, "gabba", "Felidae"), ==, 0);
    check_int(torrentRenameAndWait(tor, "Felidae/Pantherinae/Panthera/Tiger", "Snow Leopard"), ==, 0);
    check_int(torrentRenameAndWait(tor, "Felidae/Pantherinae/Panthera/Snow Leopard/Tony", "10.6"), ==, 0);
    strings[0] = "Felidae/Felinae/Acinonyx/Cheetah/Chester";
    strings[1] = "Felidae/Felinae/Felis/catus/Kyphi";
    strings[2] = "Felidae/Felinae/Felis/catus/Saffron";
    strings[3] = "Felidae/Pantherinae/Panthera/Snow Leopard/10.6";

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        check_str(files[i].name, ==, strings[i]);
        testFileExistsAndConsistsOfThisString(tor, i, expected_contents[i]);
    }

    tr_ctorFree(ctor);
    torrentRemoveAndWait(tor, 0);

    /**
    ***  Test renaming prefixes (shouldn't work)
    **/

    ctor = tr_ctorNew(session);
    tor = create_torrent_from_base64_metainfo(ctor,
        "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
        "ZWkxMzU4NTU1NDIwZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDU6ZmlsZXNsZDY6bGVuZ3RoaTI4"
        "ZTQ6cGF0aGw3OkZlbGluYWU4OkFjaW5vbnl4NzpDaGVldGFoNzpDaGVzdGVyZWVkNjpsZW5ndGhp"
        "MTJlNDpwYXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNTpLeXBoaWVlZDY6bGVuZ3RoaTZlNDpw"
        "YXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNzpTYWZmcm9uZWVkNjpsZW5ndGhpMjFlNDpwYXRo"
        "bDExOlBhbnRoZXJpbmFlODpQYW50aGVyYTU6VGlnZXI0OlRvbnllZWU0Om5hbWU3OkZlbGlkYWUx"
        "MjpwaWVjZSBsZW5ndGhpMzI3NjhlNjpwaWVjZXMyMDp27buFkmy8ICfNX4nsJmt0Ckm2Ljc6cHJp"
        "dmF0ZWkwZWVl");
    check(tr_isTorrent(tor));
    files = tor->info.files;

    /* rename prefix of top */
    check_int(torrentRenameAndWait(tor, "Feli", "FelidaeX"), ==, EINVAL);
    check_str(tor->info.name, ==, "Felidae");
    check(!files[0].is_renamed);
    check(!files[1].is_renamed);
    check(!files[2].is_renamed);
    check(!files[3].is_renamed);

    /* rename false path */
    check_int(torrentRenameAndWait(tor, "Felidae/FelinaeX", "Genus Felinae"), ==, EINVAL);
    check_str(tor->info.name, ==, "Felidae");
    check(!files[0].is_renamed);
    check(!files[1].is_renamed);
    check(!files[2].is_renamed);
    check(!files[3].is_renamed);

    /***
    ****
    ***/

    /* cleanup */
    tr_ctorFree(ctor);
    torrentRemoveAndWait(tor, 0);
    return 0;
}

/***
****
***/

static int test_partial_file(void)
{
    tr_torrent* tor;
    tr_stat const* st;
    tr_file_stat* fst;
    uint32_t const pieceCount = 33;
    uint32_t const pieceSize = 32768;
    uint32_t const length[] = { 1048576, 4096, 512 };
    uint64_t const totalSize = length[0] + length[1] + length[2];
    char const* strings[3];

    /***
    ****  create our test torrent with an incomplete .part file
    ***/

    tor = libttest_zero_torrent_init(session);
    check_uint(tor->info.totalSize, ==, totalSize);
    check_uint(tor->info.pieceSize, ==, pieceSize);
    check_uint(tor->info.pieceCount, ==, pieceCount);
    check_str(tor->info.files[0].name, ==, "files-filled-with-zeroes/1048576");
    check_str(tor->info.files[1].name, ==, "files-filled-with-zeroes/4096");
    check_str(tor->info.files[2].name, ==, "files-filled-with-zeroes/512");

    libttest_zero_torrent_populate(tor, false);
    fst = tr_torrentFiles(tor, NULL);
    check_uint(fst[0].bytesCompleted, ==, length[0] - pieceSize);
    check_uint(fst[1].bytesCompleted, ==, length[1]);
    check_uint(fst[2].bytesCompleted, ==, length[2]);
    tr_torrentFilesFree(fst, tor->info.fileCount);
    st = tr_torrentStat(tor);
    check_uint(st->sizeWhenDone, ==, totalSize);
    check_uint(st->leftUntilDone, ==, pieceSize);

    /***
    ****
    ***/

    check_int(torrentRenameAndWait(tor, "files-filled-with-zeroes", "foo"), ==, 0);
    check_int(torrentRenameAndWait(tor, "foo/1048576", "bar"), ==, 0);
    strings[0] = "foo/bar";
    strings[1] = "foo/4096";
    strings[2] = "foo/512";

    for (tr_file_index_t i = 0; i < 3; ++i)
    {
        check_str(tor->info.files[i].name, ==, strings[i]);
    }

    strings[0] = "foo/bar.part";

    for (tr_file_index_t i = 0; i < 3; ++i)
    {
        char* expected = tr_buildPath(tor->currentDir, strings[i], NULL);
        char* path = tr_torrentFindFile(tor, i);
        check_str(path, ==, expected);
        tr_free(path);
        tr_free(expected);
    }

    torrentRemoveAndWait(tor, 0);
    return 0;
}

/***
****
***/

int main(void)
{
    testFunc const tests[] =
    {
        test_single_filename_torrent,
        test_multifile_torrent,
        test_partial_file
    };

    session = libttest_session_init(NULL);

    int ret = runTests(tests, NUM_TESTS(tests));

    libttest_session_close(session);

    return ret;
}
