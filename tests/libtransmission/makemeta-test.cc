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
#include "makemeta.h"
#include "utils.h" // tr_free()

#include "test-fixtures.h"

#include <array>
#include <cstdlib> // mktemp()
#include <cstring> // strlen()
#include <string>

namespace libtransmission
{

namespace test
{

class MakemetaTest : public SandboxedTest
{
protected:
    void testSingleFileImpl(tr_tracker_info const* trackers, int const trackerCount, void const* payload,
        size_t const payloadSize, char const* comment, bool isPrivate)
    {
        // char* sandbox;
        tr_info inf;

        // create a single input file
        auto input_file = makeString(tr_buildPath(sandboxDir().data(), "test.XXXXXX", nullptr));
        createTmpfileWithContents(input_file, payload, payloadSize);
        tr_sys_path_native_separators(&input_file.front());
        auto* builder = tr_metaInfoBuilderCreate(input_file.c_str());
        EXPECT_EQ(tr_file_index_t{ 1 }, builder->fileCount);
        EXPECT_STREQ(input_file.c_str(), builder->top);
        EXPECT_STREQ(input_file.c_str(), builder->files[0].filename);
        EXPECT_EQ(payloadSize, builder->files[0].size);
        EXPECT_EQ(payloadSize, builder->totalSize);
        EXPECT_FALSE(builder->isFolder);
        EXPECT_FALSE(builder->abortFlag);

        // have tr_makeMetaInfo() build the .torrent file
        auto* torrent_file = tr_strdup_printf("%s.torrent", input_file.data());
        tr_makeMetaInfo(builder, torrent_file, trackers, trackerCount, comment, isPrivate);
        EXPECT_EQ(isPrivate, builder->isPrivate);
        EXPECT_STREQ(torrent_file, builder->outputFile);
        EXPECT_STREQ(comment, builder->comment);
        EXPECT_EQ(trackerCount, builder->trackerCount);

        while (!builder->isDone)
        {
            tr_wait_msec(100);
        }

        // now let's check our work: parse the  .torrent file
        auto* ctor = tr_ctorNew(nullptr);
        sync();
        tr_ctorSetMetainfoFromFile(ctor, torrent_file);
        auto const parse_result = tr_torrentParse(ctor, &inf);
        EXPECT_EQ(TR_PARSE_OK, parse_result);

        // quick check of some of the parsed metainfo
        EXPECT_EQ(payloadSize, inf.totalSize);
        EXPECT_EQ(makeString(tr_sys_path_basename(input_file.data(), nullptr)), inf.name);
        EXPECT_STREQ(comment, inf.comment);
        EXPECT_EQ(tr_file_index_t{ 1 }, inf.fileCount);
        EXPECT_EQ(isPrivate, inf.isPrivate);
        EXPECT_FALSE(inf.isFolder);
        EXPECT_EQ(trackerCount, inf.trackerCount);

        // cleanup
        tr_free(torrent_file);
        tr_ctorFree(ctor);
        tr_metainfoFree(&inf);
        tr_metaInfoBuilderFree(builder);
    }

    void testSingleDirectoryImpl(tr_tracker_info const* trackers, int const tracker_count, void const** payloads,
        size_t const* payload_sizes, size_t const payload_count, char const* comment,
        bool const is_private)
    {
        // create the top temp directory
        auto* top = tr_buildPath(sandboxDir().data(), "folder.XXXXXX", nullptr);
        tr_sys_path_native_separators(top);
        tr_sys_dir_create_temp(top, nullptr);

        // build the payload files that go into the top temp directory
        auto files = std::vector<std::string>{};
        files.reserve(payload_count);
        size_t total_size = 0;

        for (size_t i = 0; i < payload_count; i++)
        {
            auto tmpl = std::array<char, 16>{};
            tr_snprintf(tmpl.data(), tmpl.size(), "file.%04zu%s", i, "XXXXXX");
            auto path = makeString(tr_buildPath(top, tmpl.data(), nullptr));
            createTmpfileWithContents(path, payloads[i], payload_sizes[i]);
            tr_sys_path_native_separators(&path.front());
            files.push_back(path);
            total_size += payload_sizes[i];
        }

        sync();

        // init the builder
        auto* builder = tr_metaInfoBuilderCreate(top);
        EXPECT_FALSE(builder->abortFlag);
        EXPECT_STREQ(top, builder->top);
        EXPECT_EQ(payload_count, builder->fileCount);
        EXPECT_EQ(total_size, builder->totalSize);
        EXPECT_TRUE(builder->isFolder);

        for (size_t i = 0; i < builder->fileCount; ++i)
        {
            EXPECT_EQ(files[i], builder->files[i].filename);
            EXPECT_EQ(payload_sizes[i], builder->files[i].size);
        }

        // build the .torrent file
        auto* torrent_file = tr_strdup_printf("%s.torrent", top);
        tr_makeMetaInfo(builder, torrent_file, trackers, tracker_count, comment, is_private);
        EXPECT_EQ(is_private, builder->isPrivate);
        EXPECT_STREQ(torrent_file, builder->outputFile);
        EXPECT_STREQ(comment, builder->comment);
        EXPECT_EQ(tracker_count, builder->trackerCount);
        auto test = [&builder]() { return builder->isDone; };
        EXPECT_TRUE(waitFor(test, 5000));
        sync();

        // now let's check our work: parse the  .torrent file
        auto* ctor = tr_ctorNew(nullptr);
        tr_ctorSetMetainfoFromFile(ctor, torrent_file);
        tr_info inf;
        auto parse_result = tr_torrentParse(ctor, &inf);
        EXPECT_EQ(TR_PARSE_OK, parse_result);

        // quick check of some of the parsed metainfo
        EXPECT_EQ(total_size, inf.totalSize);
        auto* tmpstr = tr_sys_path_basename(top, nullptr);
        EXPECT_STREQ(tmpstr, inf.name);
        tr_free(tmpstr);
        EXPECT_STREQ(comment, inf.comment);
        EXPECT_EQ(payload_count, inf.fileCount);
        EXPECT_EQ(is_private, inf.isPrivate);
        EXPECT_EQ(builder->isFolder, inf.isFolder);
        EXPECT_EQ(tracker_count, inf.trackerCount);

        // cleanup
        tr_free(torrent_file);
        tr_ctorFree(ctor);
        tr_metainfoFree(&inf);
        tr_metaInfoBuilderFree(builder);

        tr_free(top);
    }

    void testSingleDirectoryRandomPayloadImpl(tr_tracker_info const* trackers, int const tracker_count,
        size_t const max_file_count, size_t const max_file_size, char const* comment,
        bool const is_private)
    {
        // build random payloads
        size_t payload_count = 1 + tr_rand_int_weak(max_file_count);
        void** payloads = tr_new0(void*, payload_count);
        size_t* payload_sizes = tr_new0(size_t, payload_count);

        for (size_t i = 0; i < payload_count; i++)
        {
            size_t const n = 1 + tr_rand_int_weak(max_file_size);
            payloads[i] = tr_new(char, n);
            tr_rand_buffer(payloads[i], n);
            payload_sizes[i] = n;
        }

        // run the test
        testSingleDirectoryImpl(
            trackers, tracker_count,
            const_cast<void const**>(payloads),
            payload_sizes,
            payload_count,
            comment, is_private);

        // cleanup
        for (size_t i = 0; i < payload_count; i++)
        {
            tr_free(payloads[i]);
        }

        tr_free(payloads);
        tr_free(payload_sizes);
    }
};

TEST_F(MakemetaTest, singleFile)
{
    auto trackers = std::array<tr_tracker_info, 16>{};
    auto tracker_count = int{};
    trackers[tracker_count].tier = tracker_count;
    trackers[tracker_count].announce = const_cast<char*>("udp://tracker.openbittorrent.com:80");
    ++tracker_count;
    trackers[tracker_count].tier = tracker_count;
    trackers[tracker_count].announce = const_cast<char*>("udp://tracker.publicbt.com:80");
    ++tracker_count;
    auto const payload = std::string { "Hello, World!\n" };
    char const* const comment = "This is the comment";
    bool const is_private = false;
    testSingleFileImpl(trackers.data(), tracker_count,
        payload.data(), payload.size(),
        comment, is_private);
}

TEST_F(MakemetaTest, singleDirectoryRandomPayload)
{
    auto constexpr DefaultMaxFileCount = size_t{ 16 };
    auto constexpr DefaultMaxFileSize = size_t{ 1024 };

    auto trackers = std::array<tr_tracker_info, 16>{};
    auto tracker_count = int{};
    trackers[tracker_count].tier = tracker_count;
    trackers[tracker_count].announce = const_cast<char*>("udp://tracker.openbittorrent.com:80");
    ++tracker_count;
    trackers[tracker_count].tier = tracker_count;
    trackers[tracker_count].announce = const_cast<char*>("udp://tracker.publicbt.com:80");
    ++tracker_count;
    char const* const comment = "This is the comment";
    bool const is_private = false;

    for (size_t i = 0; i < 10; ++i)
    {
        testSingleDirectoryRandomPayloadImpl(trackers.data(), tracker_count,
            DefaultMaxFileCount,
            DefaultMaxFileSize,
            comment, is_private);
    }
}

} // namespace test

} // namespace libtransmission
