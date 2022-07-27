// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdlib> // mktemp()
#include <cstring> // strlen()
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "file.h"
#include "makemeta.h"
#include "torrent-metainfo.h"
#include "utils.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission
{

namespace test
{

class MakemetaTest : public SandboxedTest
{
protected:
    void testSingleFileImpl(
        tr_torrent_metainfo& metainfo,
        tr_tracker_info const* trackers,
        int const trackerCount,
        char const** webseeds,
        int const webseedCount,
        void const* payload,
        size_t const payloadSize,
        char const* comment,
        bool isPrivate,
        bool anonymize,
        std::string_view source)
    {

        // create a single input file
        auto input_file = tr_pathbuf{ sandboxDir(), '/', "test.XXXXXX" };
        createTmpfileWithContents(std::data(input_file), payload, payloadSize);
        tr_sys_path_native_separators(std::data(input_file));
        auto* builder = tr_metaInfoBuilderCreate(input_file);
        EXPECT_EQ(tr_file_index_t{ 1 }, builder->fileCount);
        EXPECT_EQ(input_file, builder->top);
        EXPECT_EQ(input_file, builder->files[0].filename);
        EXPECT_EQ(payloadSize, builder->files[0].size);
        EXPECT_EQ(payloadSize, builder->totalSize);
        EXPECT_FALSE(builder->isFolder);
        EXPECT_FALSE(builder->abortFlag);

        // have tr_makeMetaInfo() build the torrent file
        auto const torrent_file = tr_pathbuf{ input_file, ".torrent"sv };
        tr_makeMetaInfo(
            builder,
            torrent_file,
            trackers,
            trackerCount,
            webseeds,
            webseedCount,
            comment,
            isPrivate,
            anonymize,
            std::string(source).c_str());
        EXPECT_EQ(isPrivate, builder->isPrivate);
        EXPECT_EQ(anonymize, builder->anonymize);
        EXPECT_EQ(torrent_file, builder->outputFile);
        EXPECT_STREQ(comment, builder->comment);
        EXPECT_EQ(source, builder->source);
        EXPECT_EQ(trackerCount, builder->trackerCount);

        while (!builder->isDone)
        {
            tr_wait_msec(100);
        }
        sync();

        // now let's check our work: parse the  torrent file
        EXPECT_TRUE(metainfo.parseTorrentFile(torrent_file));

        // quick check of some of the parsed metainfo
        EXPECT_EQ(payloadSize, metainfo.totalSize());
        EXPECT_EQ(tr_sys_path_basename(input_file), metainfo.name());
        EXPECT_EQ(comment, metainfo.comment());
        EXPECT_EQ(isPrivate, metainfo.isPrivate());
        EXPECT_EQ(size_t(trackerCount), std::size(metainfo.announceList()));
        EXPECT_EQ(size_t(webseedCount), metainfo.webseedCount());
        EXPECT_EQ(tr_file_index_t{ 1 }, metainfo.fileCount());
        EXPECT_EQ(tr_sys_path_basename(input_file), metainfo.fileSubpath(0));
        EXPECT_EQ(payloadSize, metainfo.fileSize(0));

        // cleanup
        tr_metaInfoBuilderFree(builder);
    }

    void testSingleDirectoryImpl(
        tr_tracker_info const* trackers,
        int const tracker_count,
        char const** webseeds,
        int const webseed_count,
        void const** payloads,
        size_t const* payload_sizes,
        size_t const payload_count,
        char const* comment,
        bool const is_private,
        bool const anonymize,
        char const* source)
    {
        // create the top temp directory
        auto top = tr_pathbuf{ sandboxDir(), '/', "folder.XXXXXX"sv };
        tr_sys_path_native_separators(std::data(top));
        tr_sys_dir_create_temp(std::data(top));

        // build the payload files that go into the top temp directory
        auto files = std::vector<std::string>{};
        files.reserve(payload_count);
        size_t total_size = 0;

        for (size_t i = 0; i < payload_count; i++)
        {
            auto path = fmt::format(FMT_STRING("{:s}/file.{:04}XXXXXX"), top.sv(), i);
            createTmpfileWithContents(std::data(path), payloads[i], payload_sizes[i]);
            tr_sys_path_native_separators(std::data(path));
            files.push_back(path);
            total_size += payload_sizes[i];
        }

        sync();

        // init the builder
        auto* builder = tr_metaInfoBuilderCreate(top);
        EXPECT_FALSE(builder->abortFlag);
        EXPECT_EQ(top, builder->top);
        EXPECT_EQ(payload_count, builder->fileCount);
        EXPECT_EQ(total_size, builder->totalSize);
        EXPECT_TRUE(builder->isFolder);

        for (size_t i = 0; i < builder->fileCount; ++i)
        {
            EXPECT_EQ(files[i], builder->files[i].filename);
            EXPECT_EQ(payload_sizes[i], builder->files[i].size);
        }

        // build the torrent file
        auto const torrent_file = tr_pathbuf{ top, ".torrent"sv };
        tr_makeMetaInfo(
            builder,
            torrent_file.c_str(),
            trackers,
            tracker_count,
            webseeds,
            webseed_count,
            comment,
            is_private,
            anonymize,
            source);
        EXPECT_EQ(is_private, builder->isPrivate);
        EXPECT_EQ(anonymize, builder->anonymize);
        EXPECT_EQ(torrent_file, builder->outputFile);
        EXPECT_STREQ(comment, builder->comment);
        EXPECT_STREQ(source, builder->source);
        EXPECT_EQ(tracker_count, builder->trackerCount);
        auto test = [&builder]()
        {
            return builder->isDone;
        };
        EXPECT_TRUE(waitFor(test, 5000));
        sync();

        // now let's check our work: parse the  torrent file
        auto metainfo = tr_torrent_metainfo{};
        EXPECT_TRUE(metainfo.parseTorrentFile(torrent_file));

        // quick check of some of the parsed metainfo
        EXPECT_EQ(total_size, metainfo.totalSize());
        EXPECT_EQ(tr_sys_path_basename(top), metainfo.name());
        EXPECT_EQ(comment, metainfo.comment());
        EXPECT_EQ(source, metainfo.source());
        EXPECT_EQ(payload_count, metainfo.fileCount());
        EXPECT_EQ(is_private, metainfo.isPrivate());
        EXPECT_EQ(size_t(tracker_count), std::size(metainfo.announceList()));

        // cleanup
        tr_metaInfoBuilderFree(builder);
    }

    void testSingleDirectoryRandomPayloadImpl(
        tr_tracker_info const* trackers,
        int const tracker_count,
        char const** webseeds,
        int const webseed_count,
        size_t const max_file_count,
        size_t const max_file_size,
        char const* comment,
        bool const is_private,
        bool const anonymize,
        char const* source)
    {
        // build random payloads
        size_t const payload_count = 1 + tr_rand_int_weak(max_file_count);
        auto** payloads = tr_new0(void*, payload_count);
        auto* payload_sizes = tr_new0(size_t, payload_count);

        for (size_t i = 0; i < payload_count; i++)
        {
            size_t const n = 1 + tr_rand_int_weak(max_file_size);
            payloads[i] = tr_new(char, n);
            tr_rand_buffer(payloads[i], n);
            payload_sizes[i] = n;
        }

        // run the test
        testSingleDirectoryImpl(
            trackers,
            tracker_count,
            webseeds,
            webseed_count,
            const_cast<void const**>(payloads),
            payload_sizes,
            payload_count,
            comment,
            is_private,
            anonymize,
            source);

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
    auto const payload = std::string{ "Hello, World!\n" };
    char const* const comment = "This is the comment";
    bool const is_private = false;
    bool const anonymize = false;
    auto metainfo = tr_torrent_metainfo{};
    testSingleFileImpl(
        metainfo,
        trackers.data(),
        tracker_count,
        nullptr,
        0,
        payload.data(),
        payload.size(),
        comment,
        is_private,
        anonymize,
        "TESTME"sv);
}

TEST_F(MakemetaTest, webseed)
{
    auto trackers = std::vector<tr_tracker_info>{};
    auto webseeds = std::vector<char const*>{};

    webseeds.emplace_back("https://www.example.com/linux.iso");

    auto const payload = std::string{ "Hello, World!\n" };
    char const* const comment = "This is the comment";
    bool const is_private = false;
    bool const anonymize = false;
    auto metainfo = tr_torrent_metainfo{};
    testSingleFileImpl(
        metainfo,
        std::data(trackers),
        std::size(trackers),
        std::data(webseeds),
        std::size(webseeds),
        payload.data(),
        payload.size(),
        comment,
        is_private,
        anonymize,
        "TESTME"sv);
}

TEST_F(MakemetaTest, singleFileDifferentSourceFlags)
{
    auto trackers = std::array<tr_tracker_info, 16>{};
    auto tracker_count = int{};
    trackers[tracker_count].tier = tracker_count;
    trackers[tracker_count].announce = const_cast<char*>("udp://tracker.openbittorrent.com:80");
    ++tracker_count;
    trackers[tracker_count].tier = tracker_count;
    trackers[tracker_count].announce = const_cast<char*>("udp://tracker.publicbt.com:80");
    ++tracker_count;
    auto const payload = std::string{ "Hello, World!\n" };
    char const* const comment = "This is the comment";
    bool const is_private = false;
    bool const anonymize = false;

    auto metainfo_foobar = tr_torrent_metainfo{};
    testSingleFileImpl(
        metainfo_foobar,
        trackers.data(),
        tracker_count,
        nullptr,
        0,
        payload.data(),
        payload.size(),
        comment,
        is_private,
        anonymize,
        "FOOBAR"sv);

    auto metainfo_testme = tr_torrent_metainfo{};
    testSingleFileImpl(
        metainfo_testme,
        trackers.data(),
        tracker_count,
        nullptr,
        0,
        payload.data(),
        payload.size(),
        comment,
        is_private,
        anonymize,
        "TESTME"sv);

    EXPECT_NE(metainfo_foobar.infoHash(), metainfo_testme.infoHash());
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
    bool const anonymize = false;
    char const* const source = "TESTME";

    for (size_t i = 0; i < 10; ++i)
    {
        testSingleDirectoryRandomPayloadImpl(
            trackers.data(),
            tracker_count,
            nullptr,
            0,
            DefaultMaxFileCount,
            DefaultMaxFileSize,
            comment,
            is_private,
            anonymize,
            source);
    }
}

} // namespace test

} // namespace libtransmission
