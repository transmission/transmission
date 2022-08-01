// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdlib> // mktemp()
#include <cstring> // strlen()
#include <string>
#include <numeric>
#include <string_view>
#include <utility>
#include <vector>

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
    static auto constexpr DefaultMaxFileCount = size_t{ 16 };
    static auto constexpr DefaultMaxFileSize = size_t{ 1024 };

    auto makeRandomFiles(
        std::string_view top,
        size_t n_files = tr_rand_int_weak(DefaultMaxFileCount),
        size_t max_size = DefaultMaxFileSize)
    {
        auto files = std::vector<std::pair<std::string, std::vector<std::byte>>>{};

        for (size_t i = 0; i < n_files; ++i)
        {
            auto payload = std::vector<std::byte>{};
            payload.resize(tr_rand_int_weak(max_size));
            tr_rand_buffer(std::data(payload), std::size(payload));

            auto filename = tr_pathbuf{ top, '/', "test.XXXXXX" };
            createTmpfileWithContents(std::data(filename), std::data(payload), std::size(payload));
            tr_sys_path_native_separators(std::data(filename));

            files.emplace_back(std::make_pair(std::string{ filename.sv() }, payload));
        }

        return files;
    }
};

TEST_F(MakemetaTest, singleFile)
{
    auto const files = makeRandomFiles(sandboxDir(), 1);
    auto const [filename, payload] = files.front();

    auto builder = tr_metainfo_builder{ filename };

    auto trackers = tr_announce_list{};
    trackers.add("udp://tracker.openbittorrent.com:80"sv, trackers.nextTier());
    trackers.add("udp://tracker.publicbt.com:80"sv, trackers.nextTier());
    builder.setAnnounceList(std::move(trackers));

    static auto constexpr Comment = "This is the comment"sv;
    builder.setComment(Comment);

    static auto constexpr IsPrivate = false;
    builder.setPrivate(IsPrivate);

    static auto constexpr Anonymize = false;
    builder.setAnonymize(Anonymize);

    tr_error* error = nullptr;
    EXPECT_TRUE(builder.makeChecksums(&error)) << *error;

    // now let's check our work: parse the  torrent file
    auto metainfo = tr_torrent_metainfo{};
    EXPECT_TRUE(metainfo.parseBenc(builder.benc()));

    // quick check of some of the parsed metainfo
    EXPECT_EQ(builder.blockInfo().totalSize(), metainfo.totalSize());
    EXPECT_EQ(builder.name(), metainfo.name());
    EXPECT_EQ(builder.comment(), metainfo.comment());
    EXPECT_EQ(builder.isPrivate(), metainfo.isPrivate());
    EXPECT_EQ(builder.announceList().toString(), metainfo.announceList().toString());
    EXPECT_EQ(2U, std::size(metainfo.announceList()));
    EXPECT_EQ(std::size(payload), metainfo.totalSize());
    EXPECT_EQ(tr_sys_path_basename(filename), metainfo.fileSubpath(0));
}

// webseeds.emplace_back("https://www.example.com/linux.iso");

// "FOOBAR"sv);

TEST_F(MakemetaTest, rewrite)
{
    static auto constexpr Comment = "This is the comment"sv;
    static auto constexpr Source = "This is the source"sv;

    // create the top temp directory
    auto top = tr_pathbuf{ sandboxDir(), '/', "folder.XXXXXX"sv };
    tr_sys_path_native_separators(std::data(top));
    tr_sys_dir_create_temp(std::data(top));

    // build the payload files that go into the top temp directory
    auto const files = makeRandomFiles(top);

    // make the builder
    auto builder = tr_metainfo_builder{ top };
    tr_error* error = nullptr;
    EXPECT_TRUE(builder.makeChecksums(&error));
    EXPECT_EQ(nullptr, error) << *error;
    auto const total_size = std::accumulate(
        std::begin(files),
        std::end(files),
        uint64_t{},
        [](auto sum, auto const& item) { return sum + std::size(item.second); });
    EXPECT_EQ(total_size, builder.blockInfo().totalSize());
    EXPECT_NE(total_size, 0U);
    builder.setComment(Comment);
    builder.setSource(Source);
    auto const is_private = true;
    builder.setPrivate(is_private);
    auto const benc = builder.benc();

    // now let's check our work: parse the metainfo
    auto metainfo = tr_torrent_metainfo{};
    EXPECT_TRUE(metainfo.parseBenc(benc));

    // quick check of some of the parsed metainfo
    EXPECT_EQ(total_size, metainfo.totalSize());
    EXPECT_EQ(Comment, metainfo.comment());
    EXPECT_EQ(Source, metainfo.source());
    EXPECT_EQ(tr_sys_path_basename(top), metainfo.name());
    EXPECT_EQ(is_private, metainfo.isPrivate());
    EXPECT_EQ(builder.isPrivate(), metainfo.isPrivate());
    EXPECT_EQ(builder.files().fileCount(), metainfo.fileCount());
    EXPECT_EQ(builder.announceList().toString(), metainfo.announceList().toString());
}

} // namespace test

} // namespace libtransmission
