// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fstream>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#include <libtransmission/torrent-queue.h>
#include <libtransmission/torrent.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

struct TorrentQueueTest : public libtransmission::test::SandboxedTest
{
    class MockMediator final : public tr_torrent_queue::Mediator
    {
    public:
        explicit MockMediator(TorrentQueueTest const& test)
            : test_{ test }
        {
        }

        [[nodiscard]] std::string config_dir() const override
        {
            return test_.sandboxDir();
        }

        [[nodiscard]] std::string store_filename(tr_torrent_id_t id) const override
        {
            if (auto it = test_.torrents_.find(id); it != std::end(test_.torrents_))
            {
                return it->second.store_filename();
            }
            return {};
        }

    private:
        TorrentQueueTest const& test_;
    };

    std::map<tr_torrent_id_t, tr_torrent const&> torrents_;

    MockMediator mediator_{ *this };

    static auto constexpr TorFilenames = std::array{
        "Android-x86 8.1 r6 iso.torrent"sv,
        "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
        "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
        "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv,
    };
};

TEST_F(TorrentQueueTest, addRemoveToFromQueue)
{
    auto queue = tr_torrent_queue{ mediator_ };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    for (auto const& name : TorFilenames)
    {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parse_torrent_file(path));

        auto& tor = owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));
        tor->init_id(std::size(owned));
        torrents_.try_emplace(tor->id(), *tor);
        queue.add(tor->id());
    }

    for (size_t i = 0; i < std::size(owned); ++i)
    {
        EXPECT_EQ(i, queue.get_pos(owned[i]->id()));
    }

    queue.remove(owned[1]->id());
    queue.remove(owned[2]->id());
    owned.erase(std::begin(owned) + 1, std::begin(owned) + 3);
    for (size_t i = 0; i < std::size(owned); ++i)
    {
        EXPECT_EQ(i, queue.get_pos(owned[i]->id()));
    }
}

TEST_F(TorrentQueueTest, setQueuePos)
{
    static auto constexpr QueuePos = std::array{ 1U, 3U, 0U, 2U };

    auto queue = tr_torrent_queue{ mediator_ };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    for (auto const& name : TorFilenames)
    {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parse_torrent_file(path));

        auto& tor = owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));
        tor->init_id(std::size(owned));
        torrents_.try_emplace(tor->id(), *tor);
        queue.add(tor->id());
    }

    for (size_t i = 0; i < std::size(owned); ++i)
    {
        EXPECT_EQ(i, queue.get_pos(owned[i]->id()));
    }

    for (size_t i = 0; i < std::size(owned); ++i)
    {
        auto const id = owned[i]->id();
        auto const pos = QueuePos[i];
        queue.set_pos(id, pos);
        EXPECT_EQ(queue.get_pos(id), pos);
    }

    for (size_t i = 0; i < std::size(owned); ++i)
    {
        EXPECT_EQ(queue.get_pos(owned[i]->id()), QueuePos[i]);
    }
}

TEST_F(TorrentQueueTest, toFromFile)
{
    static auto constexpr ExpectedContents =
        "[\n"
        "    \"70341e8e1fe8778af23f6318ca75a22f8b1f1c05.torrent\",\n"
        "    \"c9a337562cb0360fd6f5ab40fd2b1b81d5325dbd.torrent\",\n"
        "    \"bc26c6bc83d0ca1a7bf9875df1ffc3fed81ff555.torrent\",\n"
        "    \"f09c8d0884590088f4004e010a928f8b6178c2fd.torrent\"\n"
        "]"sv;

    auto queue = tr_torrent_queue{ mediator_ };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    for (auto const& name : TorFilenames)
    {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parse_torrent_file(path));

        auto& tor = owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));
        tor->init_id(std::size(owned));
        torrents_.try_emplace(tor->id(), *tor);
        queue.add(tor->id());
    }

    queue.to_file();

    auto f = std::ifstream{ sandboxDir() + "/queue.json" };
    auto const contents = std::string{ std::istreambuf_iterator{ f }, std::istreambuf_iterator<decltype(f)::char_type>{} };
    EXPECT_EQ(contents, ExpectedContents);
    f.close();

    auto const filenames = queue.from_file();
    ASSERT_EQ(std::size(filenames), std::size(owned));
    for (size_t i = 0; i < std::size(filenames); ++i)
    {
        EXPECT_EQ(filenames[i], owned[i]->store_filename());
    }
}
