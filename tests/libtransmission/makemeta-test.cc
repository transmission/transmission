// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdlib> // mktemp()
#include <cstring> // strlen()
#include <numeric>
#include <string>
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
        size_t n_files = std::max(size_t{ 1U }, static_cast<size_t>(tr_rand_int_weak(DefaultMaxFileCount))),
        size_t max_size = DefaultMaxFileSize)
    {
        auto files = std::vector<std::pair<std::string, std::vector<std::byte>>>{};

        for (size_t i = 0; i < n_files; ++i)
        {
            auto payload = std::vector<std::byte>{};
            // TODO(5.0.0): zero-sized files are disabled in these test
            // because tr_torrent_metainfo discards them, throwing off the
            // builder-to-metainfo comparisons here. tr_torrent_metainfo
            // will behave when BEP52 support is added in Transmission 5.
            static auto constexpr MinFileSize = size_t{ 1U };
            payload.resize(std::max(MinFileSize, static_cast<size_t>(tr_rand_int_weak(max_size))));
            tr_rand_buffer(std::data(payload), std::size(payload));

            auto filename = tr_pathbuf{ top, '/', "test.XXXXXX" };
            createTmpfileWithContents(std::data(filename), std::data(payload), std::size(payload));
            tr_sys_path_native_separators(std::data(filename));

            files.emplace_back(std::make_pair(std::string{ filename.sv() }, payload));
        }

        return files;
    }

    static tr_metainfo_builder makeBuilder(std::string_view top, tr_torrent_metainfo const& metainfo, bool anonymize = false)
    {
        auto builder = tr_metainfo_builder{ top };
        builder.setAnnounceList(tr_announce_list{ metainfo.announceList() });
        builder.setAnonymize(anonymize);
        builder.setComment(metainfo.comment());
        builder.setPrivate(metainfo.isPrivate());
        builder.setSource(metainfo.source());

        auto webseeds = std::vector<std::string>{};
        for (size_t i = 0, n = metainfo.webseedCount(); i < n; ++i)
        {
            webseeds.emplace_back(metainfo.webseed(i));
        }
        builder.setWebseeds(std::move(webseeds));

        return builder;
    }

    static void builderCheck(tr_metainfo_builder const& builder)
    {
        auto metainfo = tr_torrent_metainfo{};
        EXPECT_TRUE(metainfo.parseBenc(builder.benc()));
        EXPECT_EQ(builder.blockInfo().totalSize(), metainfo.totalSize());
        EXPECT_EQ(builder.files().totalSize(), metainfo.files().totalSize());
        EXPECT_EQ(builder.files().fileCount(), metainfo.files().fileCount());
        for (size_t i = 0, n = std::min(builder.files().fileCount(), metainfo.files().fileCount()); i < n; ++i)
        {
            EXPECT_EQ(builder.files().fileSize(i), metainfo.files().fileSize(i));
            EXPECT_EQ(builder.files().path(i), metainfo.files().path(i));
        }
        EXPECT_EQ(builder.name(), metainfo.name());
        EXPECT_EQ(builder.comment(), metainfo.comment());
        EXPECT_EQ(builder.isPrivate(), metainfo.isPrivate());
        EXPECT_EQ(builder.announceList().toString(), metainfo.announceList().toString());
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

    builderCheck(builder);
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

    builderCheck(builder);
}

} // namespace test

} // namespace libtransmission
