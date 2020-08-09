/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "crypto-utils.h"
#include "file.h"
#include "resume.h"
#include "torrent.h" // tr_isTorrent()
#include "tr-assert.h"
#include "variant.h"

#include "test-fixtures.h"

#include <array>
#include <cerrno>
#include <cstdio> // fopen()
#include <cstring> // strcmp()
#include <string>

namespace libtransmission
{

namespace test
{

class RenameTest : public SessionTest
{
    static auto constexpr MaxWaitMsec = 3000;

protected:
    void torrentRemoveAndWait(tr_torrent* tor, int expected_torrent_count)
    {
        tr_torrentRemove(tor, false, nullptr);
        auto const test = [this, expected_torrent_count]()
            {
                return tr_sessionCountTorrents(session_) == expected_torrent_count;
            };
        EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    }

    void createSingleFileTorrentContents(char const* top)
    {
        auto const path = makeString(tr_buildPath(top, "hello-world.txt", nullptr));
        createFileWithContents(path, "hello, world!\n");
    }

    void createMultifileTorrentContents(char const* top)
    {
        auto path = makeString(tr_buildPath(top, "Felidae", "Felinae", "Acinonyx", "Cheetah", "Chester", nullptr));
        createFileWithContents(path, "It ain't easy bein' cheesy.\n");

        path = makeString(tr_buildPath(top, "Felidae", "Pantherinae", "Panthera", "Tiger", "Tony", nullptr));
        createFileWithContents(path, "They’re Grrrrreat!\n");

        path = makeString(tr_buildPath(top, "Felidae", "Felinae", "Felis", "catus", "Kyphi", nullptr));
        createFileWithContents(path, "Inquisitive\n");

        path = makeString(tr_buildPath(top, "Felidae", "Felinae", "Felis", "catus", "Saffron", nullptr));
        createFileWithContents(path, "Tough\n");

        sync();
    }

    tr_torrent* createTorrentFromBase64Metainfo(tr_ctor* ctor, char const* metainfo_base64)
    {
        // create the torrent ctor
        size_t metainfo_len;
        auto* metainfo = static_cast<char*>(tr_base64_decode_str(metainfo_base64, &metainfo_len));
        EXPECT_NE(nullptr, metainfo);
        EXPECT_LT(size_t(0), metainfo_len);
        tr_ctorSetMetainfo(ctor, reinterpret_cast<uint8_t const*>(metainfo), metainfo_len);
        tr_ctorSetPaused(ctor, TR_FORCE, true);

        // create the torrent
        auto err = int{};
        auto* tor = tr_torrentNew(ctor, &err, nullptr);
        EXPECT_EQ(0, err);

        // cleanup
        tr_free(metainfo);
        return tor;
    }

    bool testFileExistsAndConsistsOfThisString(tr_torrent const* tor, tr_file_index_t file_index, std::string const& str)
    {
        auto const str_len = str.size();
        auto success = false;

        auto* path = tr_torrentFindFile(tor, file_index);
        if (path != nullptr)
        {
            EXPECT_TRUE(tr_sys_path_exists(path, nullptr));

            size_t contents_len;
            uint8_t* contents = tr_loadFile(path, &contents_len, nullptr);

            success = contents != nullptr && str_len == contents_len && memcmp(contents, str.data(), contents_len) == 0;

            tr_free(contents);
            tr_free(path);
        }

        return success;
    }

    void expectHaveNone(tr_torrent* tor, uint64_t total_size)
    {
        auto const* tst = tr_torrentStat(tor);
        EXPECT_EQ(TR_STATUS_STOPPED, tst->activity);
        EXPECT_EQ(TR_STAT_OK, tst->error);
        EXPECT_EQ(total_size, tst->sizeWhenDone);
        EXPECT_EQ(total_size, tst->leftUntilDone);
        EXPECT_EQ(total_size, tor->info.totalSize);
        EXPECT_EQ(0, tst->haveValid);
    }

    int torrentRenameAndWait(tr_torrent* tor, char const* oldpath, char const* newname)
    {
        auto const on_rename_done = [] (
            tr_torrent* /*tor*/, char const* /*oldpath*/,
            char const* /*newname*/, int error,
            void* user_data) noexcept
        {
            *static_cast<int*>(user_data) = error;
        };

        int error = -1;
        tr_torrentRenamePath(tor, oldpath, newname, on_rename_done, &error);
        auto test = [&error]() { return error != -1; };
        EXPECT_TRUE(waitFor(test, MaxWaitMsec));
        return error;
    }
};

TEST_F(RenameTest, singleFilenameTorrent)
{
    uint64_t loaded;
    static auto constexpr TotalSize = size_t{ 14 };

    // this is a single-file torrent whose file is hello-world.txt, holding the string "hello, world!"
    auto* ctor = tr_ctorNew(session_);
    auto* tor = createTorrentFromBase64Metainfo(ctor,
        "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
        "ZWkxMzU4NTQ5MDk4ZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDY6bGVuZ3RoaTE0ZTQ6bmFtZTE1"
        "OmhlbGxvLXdvcmxkLnR4dDEyOnBpZWNlIGxlbmd0aGkzMjc2OGU2OnBpZWNlczIwOukboJcrkFUY"
        "f6LvqLXBVvSHqCk6Nzpwcml2YXRlaTBlZWU=");
    EXPECT_TRUE(tr_isTorrent(tor));

    // sanity check the info
    EXPECT_EQ(tr_file_index_t{ 1 }, tor->info.fileCount);
    EXPECT_STREQ("hello-world.txt", tor->info.files[0].name);
    EXPECT_FALSE(tor->info.files[0].is_renamed);

    // sanity check the (empty) stats
    blockingTorrentVerify(tor);
    expectHaveNone(tor, TotalSize);

    createSingleFileTorrentContents(tor->currentDir);

    // sanity check the stats again, now that we've added the file
    blockingTorrentVerify(tor);
    auto const* st = tr_torrentStat(tor);
    EXPECT_EQ(TR_STATUS_STOPPED, st->activity);
    EXPECT_EQ(TR_STAT_OK, st->error);
    EXPECT_EQ(0, st->leftUntilDone);
    EXPECT_EQ(0, st->haveUnchecked);
    EXPECT_EQ(0, st->desiredAvailable);
    EXPECT_EQ(TotalSize, st->sizeWhenDone);
    EXPECT_EQ(TotalSize, st->haveValid);

    /**
    ***  okay! we've finally put together all the scaffolding to test
    ***  renaming a single-file torrent
    **/

    // confirm that bad inputs get caught

    EXPECT_EQ(EINVAL, torrentRenameAndWait(tor, "hello-world.txt", nullptr));
    EXPECT_EQ(EINVAL, torrentRenameAndWait(tor, "hello-world.txt", ""));
    EXPECT_EQ(EINVAL, torrentRenameAndWait(tor, "hello-world.txt", "."));
    EXPECT_EQ(EINVAL, torrentRenameAndWait(tor, "hello-world.txt", ".."));
    EXPECT_EQ(0, torrentRenameAndWait(tor, "hello-world.txt", "hello-world.txt"));
    EXPECT_EQ(EINVAL, torrentRenameAndWait(tor, "hello-world.txt", "hello/world.txt"));

    EXPECT_FALSE(tor->info.files[0].is_renamed);
    EXPECT_STREQ("hello-world.txt", tor->info.files[0].name);

    /***
    ****  Now try a rename that should succeed
    ***/

    auto tmpstr = makeString(tr_buildPath(tor->currentDir, "hello-world.txt", nullptr));
    EXPECT_TRUE(tr_sys_path_exists(tmpstr.c_str(), nullptr));
    EXPECT_STREQ("hello-world.txt", tr_torrentName(tor));
    EXPECT_EQ(0, torrentRenameAndWait(tor, tor->info.name, "foobar"));
    EXPECT_FALSE(tr_sys_path_exists(tmpstr.c_str(), nullptr)); // confirm the old filename can't be found
    EXPECT_TRUE(tor->info.files[0].is_renamed); // confirm the file's 'renamed' flag is set
    EXPECT_STREQ("foobar", tr_torrentName(tor)); // confirm the torrent's name is now 'foobar'
    EXPECT_STREQ("foobar", tor->info.files[0].name); // confirm the file's name is now 'foobar' in our struct
    EXPECT_STREQ(nullptr, strstr(tor->info.torrent, "foobar")); // confirm the name in the .torrent file hasn't changed
    tmpstr = makeString(tr_buildPath(tor->currentDir, "foobar", nullptr));
    EXPECT_TRUE(tr_sys_path_exists(tmpstr.c_str(), nullptr)); // confirm the file's name is now 'foobar' on the disk
    EXPECT_TRUE(testFileExistsAndConsistsOfThisString(tor, 0, "hello, world!\n")); // confirm the contents are right

    // (while it's renamed: confirm that the .resume file remembers the changes)
    tr_torrentSaveResume(tor);
    sync();
    loaded = tr_torrentLoadResume(tor, ~0, ctor, nullptr);
    EXPECT_STREQ("foobar", tr_torrentName(tor));
    EXPECT_NE(decltype(loaded) { 0 }, (loaded & TR_FR_NAME));

    /***
    ****  ...and rename it back again
    ***/

    tmpstr = makeString(tr_buildPath(tor->currentDir, "foobar", nullptr));
    EXPECT_TRUE(tr_sys_path_exists(tmpstr.c_str(), nullptr));
    EXPECT_EQ(0, torrentRenameAndWait(tor, "foobar", "hello-world.txt"));
    EXPECT_FALSE(tr_sys_path_exists(tmpstr.c_str(), nullptr));
    EXPECT_TRUE(tor->info.files[0].is_renamed);
    EXPECT_STREQ("hello-world.txt", tor->info.files[0].name);
    EXPECT_STREQ("hello-world.txt", tr_torrentName(tor));
    EXPECT_TRUE(testFileExistsAndConsistsOfThisString(tor, 0, "hello, world!\n"));

    // cleanup
    tr_ctorFree(ctor);
    torrentRemoveAndWait(tor, 0);
}

/***
****
****
****
***/

TEST_F(RenameTest, multifileTorrent)
{
    char* str;
    auto constexpr TotalSize = size_t{ 67 };
    auto const expected_files = std::array<std::string, 4>
    {
        "Felidae/Felinae/Acinonyx/Cheetah/Chester",
        "Felidae/Felinae/Felis/catus/Kyphi",
        "Felidae/Felinae/Felis/catus/Saffron",
        "Felidae/Pantherinae/Panthera/Tiger/Tony"
    };
    auto const expected_contents = std::array<std::string, 4>
    {
        "It ain't easy bein' cheesy.\n",
        "Inquisitive\n",
        "Tough\n",
        "They’re Grrrrreat!\n"
    };

    auto* ctor = tr_ctorNew(session_);
    auto* tor = createTorrentFromBase64Metainfo(ctor,
        "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
        "ZWkxMzU4NTU1NDIwZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDU6ZmlsZXNsZDY6bGVuZ3RoaTI4"
        "ZTQ6cGF0aGw3OkZlbGluYWU4OkFjaW5vbnl4NzpDaGVldGFoNzpDaGVzdGVyZWVkNjpsZW5ndGhp"
        "MTJlNDpwYXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNTpLeXBoaWVlZDY6bGVuZ3RoaTZlNDpw"
        "YXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNzpTYWZmcm9uZWVkNjpsZW5ndGhpMjFlNDpwYXRo"
        "bDExOlBhbnRoZXJpbmFlODpQYW50aGVyYTU6VGlnZXI0OlRvbnllZWU0Om5hbWU3OkZlbGlkYWUx"
        "MjpwaWVjZSBsZW5ndGhpMzI3NjhlNjpwaWVjZXMyMDp27buFkmy8ICfNX4nsJmt0Ckm2Ljc6cHJp"
        "dmF0ZWkwZWVl");
    EXPECT_TRUE(tr_isTorrent(tor));
    auto const* files = tor->info.files;

    // sanity check the info
    EXPECT_STREQ("Felidae", tor->info.name);
    EXPECT_EQ(TotalSize, tor->info.totalSize);
    EXPECT_EQ(tr_file_index_t{ 4 }, tor->info.fileCount);

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(expected_files[i], files[i].name);
    }

    // sanity check the (empty) stats
    blockingTorrentVerify(tor);
    expectHaveNone(tor, TotalSize);

    // build the local data
    createMultifileTorrentContents(tor->currentDir);

    // sanity check the (full) stats
    blockingTorrentVerify(tor);
    auto const* st = tr_torrentStat(tor);
    EXPECT_EQ(TR_STATUS_STOPPED, st->activity);
    EXPECT_EQ(TR_STAT_OK, st->error);
    EXPECT_EQ(0, st->leftUntilDone);
    EXPECT_EQ(0, st->haveUnchecked);
    EXPECT_EQ(0, st->desiredAvailable);
    EXPECT_EQ(TotalSize, st->sizeWhenDone);
    EXPECT_EQ(TotalSize, st->haveValid);

    /**
    ***  okay! let's test renaming.
    **/

    // rename a leaf...
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus/Kyphi", "placeholder"));
    EXPECT_STREQ("Felidae/Felinae/Felis/catus/placeholder", files[1].name);
    EXPECT_TRUE(testFileExistsAndConsistsOfThisString(tor, 1, "Inquisitive\n"));

    // ...and back again
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus/placeholder", "Kyphi"));
    EXPECT_STREQ("Felidae/Felinae/Felis/catus/Kyphi", files[1].name);
    testFileExistsAndConsistsOfThisString(tor, 1, "Inquisitive\n");

    // rename a branch...
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus", "placeholder"));
    EXPECT_EQ(expected_files[0], files[0].name);
    EXPECT_STREQ("Felidae/Felinae/Felis/placeholder/Kyphi", files[1].name);
    EXPECT_STREQ("Felidae/Felinae/Felis/placeholder/Saffron", files[2].name);
    EXPECT_EQ(expected_files[3], files[3].name);
    EXPECT_TRUE(testFileExistsAndConsistsOfThisString(tor, 1, expected_contents[1]));
    EXPECT_TRUE(testFileExistsAndConsistsOfThisString(tor, 2, expected_contents[2]));
    EXPECT_FALSE(files[0].is_renamed);
    EXPECT_TRUE(files[1].is_renamed);
    EXPECT_TRUE(files[2].is_renamed);
    EXPECT_FALSE(files[3].is_renamed);

    // (while the branch is renamed: confirm that the .resume file remembers the changes)
    tr_torrentSaveResume(tor);
    // this is a bit dodgy code-wise, but let's make sure the .resume file got the name
    tr_free(files[1].name);
    tor->info.files[1].name = tr_strdup("gabba gabba hey");
    auto const loaded = tr_torrentLoadResume(tor, ~0, ctor, nullptr);
    EXPECT_NE(decltype(loaded) { 0 }, (loaded & TR_FR_FILENAMES));
    EXPECT_EQ(expected_files[0], files[0].name);
    EXPECT_STREQ("Felidae/Felinae/Felis/placeholder/Kyphi", files[1].name);
    EXPECT_STREQ("Felidae/Felinae/Felis/placeholder/Saffron", files[2].name);
    EXPECT_EQ(expected_files[3], files[3].name);

    // ...and back again
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Felinae/Felis/placeholder", "catus"));

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(expected_files[i], files[i].name);
        EXPECT_TRUE(testFileExistsAndConsistsOfThisString(tor, i, expected_contents[i]));
    }

    EXPECT_FALSE(files[0].is_renamed);
    EXPECT_TRUE(files[1].is_renamed);
    EXPECT_TRUE(files[2].is_renamed);
    EXPECT_FALSE(files[3].is_renamed);

    /***
    ****  Test it an incomplete torrent...
    ***/

    // remove the directory Felidae/Felinae/Felis/catus
    str = tr_torrentFindFile(tor, 1);
    EXPECT_NE(nullptr, str);
    tr_sys_path_remove(str, nullptr);
    tr_free(str);
    str = tr_torrentFindFile(tor, 2);
    EXPECT_NE(nullptr, str);
    tr_sys_path_remove(str, nullptr);
    auto* tmp = tr_sys_path_dirname(str, nullptr);
    tr_sys_path_remove(tmp, nullptr);
    tr_free(tmp);
    tr_free(str);
    sync();
    blockingTorrentVerify(tor);
    testFileExistsAndConsistsOfThisString(tor, 0, expected_contents[0]);

    for (tr_file_index_t i = 1; i <= 2; ++i)
    {
        str = tr_torrentFindFile(tor, i);
        EXPECT_STREQ(nullptr, str);
        tr_free(str);
    }

    testFileExistsAndConsistsOfThisString(tor, 3, expected_contents[3]);

    // rename a branch...
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Felinae/Felis/catus", "foo"));
    EXPECT_EQ(expected_files[0], files[0].name);
    EXPECT_STREQ("Felidae/Felinae/Felis/foo/Kyphi", files[1].name);
    EXPECT_STREQ("Felidae/Felinae/Felis/foo/Saffron", files[2].name);
    EXPECT_EQ(expected_files[3], files[3].name);

    // ...and back again
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Felinae/Felis/foo", "catus"));

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(expected_files[i], files[i].name);
    }

    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae", "gabba"));
    auto strings = std::array<char const*, 4>{};
    strings[0] = "gabba/Felinae/Acinonyx/Cheetah/Chester";
    strings[1] = "gabba/Felinae/Felis/catus/Kyphi";
    strings[2] = "gabba/Felinae/Felis/catus/Saffron";
    strings[3] = "gabba/Pantherinae/Panthera/Tiger/Tony";

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        EXPECT_STREQ(strings[i], files[i].name);
        testFileExistsAndConsistsOfThisString(tor, i, expected_contents[i]);
    }

    // rename the root, then a branch, and then a leaf...
    EXPECT_EQ(0, torrentRenameAndWait(tor, "gabba", "Felidae"));
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Pantherinae/Panthera/Tiger", "Snow Leopard"));
    EXPECT_EQ(0, torrentRenameAndWait(tor, "Felidae/Pantherinae/Panthera/Snow Leopard/Tony", "10.6"));
    strings[0] = "Felidae/Felinae/Acinonyx/Cheetah/Chester";
    strings[1] = "Felidae/Felinae/Felis/catus/Kyphi";
    strings[2] = "Felidae/Felinae/Felis/catus/Saffron";
    strings[3] = "Felidae/Pantherinae/Panthera/Snow Leopard/10.6";

    for (tr_file_index_t i = 0; i < 4; ++i)
    {
        EXPECT_STREQ(strings[i], files[i].name);
        testFileExistsAndConsistsOfThisString(tor, i, expected_contents[i]);
    }

    tr_ctorFree(ctor);
    torrentRemoveAndWait(tor, 0);

    /**
    ***  Test renaming prefixes (shouldn't work)
    **/

    ctor = tr_ctorNew(session_);
    tor = createTorrentFromBase64Metainfo(ctor,
        "ZDEwOmNyZWF0ZWQgYnkyNTpUcmFuc21pc3Npb24vMi42MSAoMTM0MDcpMTM6Y3JlYXRpb24gZGF0"
        "ZWkxMzU4NTU1NDIwZTg6ZW5jb2Rpbmc1OlVURi04NDppbmZvZDU6ZmlsZXNsZDY6bGVuZ3RoaTI4"
        "ZTQ6cGF0aGw3OkZlbGluYWU4OkFjaW5vbnl4NzpDaGVldGFoNzpDaGVzdGVyZWVkNjpsZW5ndGhp"
        "MTJlNDpwYXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNTpLeXBoaWVlZDY6bGVuZ3RoaTZlNDpw"
        "YXRobDc6RmVsaW5hZTU6RmVsaXM1OmNhdHVzNzpTYWZmcm9uZWVkNjpsZW5ndGhpMjFlNDpwYXRo"
        "bDExOlBhbnRoZXJpbmFlODpQYW50aGVyYTU6VGlnZXI0OlRvbnllZWU0Om5hbWU3OkZlbGlkYWUx"
        "MjpwaWVjZSBsZW5ndGhpMzI3NjhlNjpwaWVjZXMyMDp27buFkmy8ICfNX4nsJmt0Ckm2Ljc6cHJp"
        "dmF0ZWkwZWVl");
    EXPECT_TRUE(tr_isTorrent(tor));
    files = tor->info.files;

    // rename prefix of top
    EXPECT_EQ(EINVAL, torrentRenameAndWait(tor, "Feli", "FelidaeX"));
    EXPECT_STREQ("Felidae", tor->info.name);
    EXPECT_FALSE(files[0].is_renamed);
    EXPECT_FALSE(files[1].is_renamed);
    EXPECT_FALSE(files[2].is_renamed);
    EXPECT_FALSE(files[3].is_renamed);

    // rename false path
    EXPECT_EQ(EINVAL, torrentRenameAndWait(tor, "Felidae/FelinaeX", "Genus Felinae"));
    EXPECT_STREQ("Felidae", tor->info.name);
    EXPECT_FALSE(files[0].is_renamed);
    EXPECT_FALSE(files[1].is_renamed);
    EXPECT_FALSE(files[2].is_renamed);
    EXPECT_FALSE(files[3].is_renamed);

    /***
    ****
    ***/

    // cleanup
    tr_ctorFree(ctor);
    torrentRemoveAndWait(tor, 0);
}

/***
****
***/

TEST_F(RenameTest, partialFile)
{
    auto constexpr PieceCount = uint32_t { 33 };
    auto constexpr PieceSize = uint32_t { 32768 };
    auto constexpr Length = std::array<uint32_t, 3>{ 1048576, 4096, 512 };
    auto constexpr TotalSize = uint64_t(Length[0]) + Length[1] + Length[2];

    /***
    ****  create our test torrent with an incomplete .part file
    ***/

    auto* tor = zeroTorrentInit();
    EXPECT_EQ(TotalSize, tor->info.totalSize);
    EXPECT_EQ(PieceSize, tor->info.pieceSize);
    EXPECT_EQ(PieceCount, tor->info.pieceCount);
    EXPECT_STREQ("files-filled-with-zeroes/1048576", tor->info.files[0].name);
    EXPECT_STREQ("files-filled-with-zeroes/4096", tor->info.files[1].name);
    EXPECT_STREQ("files-filled-with-zeroes/512", tor->info.files[2].name);

    zeroTorrentPopulate(tor, false);
    auto* fst = tr_torrentFiles(tor, nullptr);
    EXPECT_EQ(Length[0] - PieceSize, fst[0].bytesCompleted);
    EXPECT_EQ(Length[1], fst[1].bytesCompleted);
    EXPECT_EQ(Length[2], fst[2].bytesCompleted);
    tr_torrentFilesFree(fst, tor->info.fileCount);
    auto const* st = tr_torrentStat(tor);
    EXPECT_EQ(TotalSize, st->sizeWhenDone);
    EXPECT_EQ(PieceSize, st->leftUntilDone);

    /***
    ****
    ***/

    EXPECT_EQ(0, torrentRenameAndWait(tor, "files-filled-with-zeroes", "foo"));
    EXPECT_EQ(0, torrentRenameAndWait(tor, "foo/1048576", "bar"));
    auto strings = std::array<char const*, 3>{};
    strings[0] = "foo/bar";
    strings[1] = "foo/4096";
    strings[2] = "foo/512";

    for (tr_file_index_t i = 0; i < 3; ++i)
    {
        EXPECT_STREQ(strings[i], tor->info.files[i].name);
    }

    strings[0] = "foo/bar.part";

    for (tr_file_index_t i = 0; i < 3; ++i)
    {
        char* expected = tr_buildPath(tor->currentDir, strings[i], nullptr);
        char* path = tr_torrentFindFile(tor, i);
        EXPECT_STREQ(expected, path);
        tr_free(path);
        tr_free(expected);
    }

    torrentRemoveAndWait(tor, 0);
}

} // namespace test

} // namespace libtransmission
