// This file Copyright (C) 2026 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/local-data.h>

namespace
{

class StubBackend final : public tr::LocalData::Backend
{
public:
    [[nodiscard]] int read(tr_torrent_id_t, tr_byte_span_t byte_span, tr::LocalData::BlockData& setme) override
    {
        read_span = byte_span;
        setme.assign({ uint8_t{ 1U }, uint8_t{ 2U }, uint8_t{ 3U } });
        return read_err;
    }

    [[nodiscard]] int test_piece(tr_torrent_id_t, tr_piece_index_t piece, tr_sha1_digest_t& setme_hash) override
    {
        tested_piece = piece;
        setme_hash = hash;
        return test_err;
    }

    [[nodiscard]] int write(tr_torrent_id_t, tr_byte_span_t byte_span, tr::LocalData::BlockData const& data) override
    {
        write_span = byte_span;
        last_write.assign(std::begin(data), std::end(data));
        return write_err;
    }

    [[nodiscard]] int move(tr_torrent_id_t, std::string_view old_parent, std::string_view parent, std::string_view parent_name)
        override
    {
        moved_from = std::string{ old_parent };
        moved_to = std::string{ parent };
        moved_name = std::string{ parent_name };
        return move_err;
    }

    [[nodiscard]] int remove(tr_torrent_id_t, tr_torrent_remove_func) override
    {
        remove_called = true;
        return remove_err;
    }

    void rename(tr_torrent_id_t id, std::string_view oldpath, std::string_view newname, tr_torrent_rename_done_func callback)
        override
    {
        renamed_from = std::string{ oldpath };
        renamed_to = std::string{ newname };
        if (callback != nullptr)
        {
            callback(id, oldpath, newname, tr_error{});
        }
    }

    void close_all() override
    {
        close_all_called = true;
    }

    void close_torrent(tr_torrent_id_t tor_id) override
    {
        closed_torrent = tor_id;
    }

    void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num) override
    {
        closed_file = std::pair{ tor_id, file_num };
    }

    int read_err = 0;
    int test_err = 0;
    int write_err = 0;
    int move_err = 0;
    int remove_err = 0;
    bool remove_called = false;
    bool close_all_called = false;
    tr_byte_span_t read_span{};
    tr_byte_span_t write_span{};
    tr_piece_index_t tested_piece = 0;
    tr_sha1_digest_t hash = tr_sha1::digest("local-data-test"sv);
    std::vector<uint8_t> last_write;
    std::string moved_from;
    std::string moved_to;
    std::string moved_name;
    std::string renamed_from;
    std::string renamed_to;
    tr_torrent_id_t closed_torrent = -1;
    std::optional<std::pair<tr_torrent_id_t, tr_file_index_t>> closed_file;
};

} // namespace

TEST(LocalData, ReadRunsInline)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto called = false;
    local_data.read(
        7,
        { .begin = 10U, .end = 13U },
        [&called, raw_backend](tr_torrent_id_t tor_id, tr_byte_span_t byte_span, tr_error const& error, auto data)
        {
            called = true;
            EXPECT_EQ(7, tor_id);
            EXPECT_EQ(raw_backend->read_span.begin, byte_span.begin);
            EXPECT_EQ(raw_backend->read_span.end, byte_span.end);
            EXPECT_FALSE(error);
            ASSERT_NE(nullptr, data);
            EXPECT_EQ((std::vector<uint8_t>{ 1U, 2U, 3U }), std::vector<uint8_t>(std::begin(*data), std::end(*data)));
        });

    EXPECT_TRUE(called);
}

TEST(LocalData, TestPieceRunsInline)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto called = false;
    local_data.test_piece(
        9,
        3,
        [&called, raw_backend](tr_torrent_id_t tor_id, tr_piece_index_t piece, tr_error const& error, auto hash)
        {
            called = true;
            EXPECT_EQ(9, tor_id);
            EXPECT_EQ(raw_backend->tested_piece, piece);
            EXPECT_FALSE(error);
            ASSERT_TRUE(hash.has_value());
            EXPECT_EQ(raw_backend->hash, *hash);
        });

    EXPECT_TRUE(called);
}

TEST(LocalData, WriteRunsInline)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto data = std::make_unique<tr::LocalData::BlockData>();
    data->assign({ uint8_t{ 4U }, uint8_t{ 5U }, uint8_t{ 6U } });

    auto called = false;
    local_data.write(
        11,
        { .begin = 20U, .end = 23U },
        std::move(data),
        [&called, raw_backend](tr_torrent_id_t tor_id, tr_byte_span_t byte_span, tr_error const& error)
        {
            called = true;
            EXPECT_EQ(11, tor_id);
            EXPECT_EQ(raw_backend->write_span.begin, byte_span.begin);
            EXPECT_EQ(raw_backend->write_span.end, byte_span.end);
            EXPECT_FALSE(error);
            EXPECT_EQ((std::vector<uint8_t>{ 4U, 5U, 6U }), raw_backend->last_write);
        });

    EXPECT_TRUE(called);
    EXPECT_EQ(0U, local_data.enqueued_write_bytes());
}

TEST(LocalData, AdminOperationsDelegate)
{
    auto backend = std::make_unique<StubBackend>();
    auto* raw_backend = backend.get();
    auto local_data = tr::LocalData{ std::move(backend) };

    auto move_called = false;
    local_data.move(
        5,
        "/old",
        "/new",
        "name",
        [&move_called](tr_torrent_id_t tor_id, tr_error const& error)
        {
            move_called = true;
            EXPECT_EQ(5, tor_id);
            EXPECT_FALSE(error);
        });
    EXPECT_TRUE(move_called);
    EXPECT_EQ("/old", raw_backend->moved_from);
    EXPECT_EQ("/new", raw_backend->moved_to);
    EXPECT_EQ("name", raw_backend->moved_name);

    auto rename_called = false;
    local_data.rename(
        8,
        "old",
        "new",
        [&rename_called](tr_torrent_id_t tor_id, std::string_view oldpath, std::string_view newname, tr_error const& error)
        {
            rename_called = true;
            EXPECT_EQ(8, tor_id);
            EXPECT_EQ("old", oldpath);
            EXPECT_EQ("new", newname);
            EXPECT_FALSE(error);
        });
    EXPECT_TRUE(rename_called);

    local_data.remove(12, {});
    EXPECT_TRUE(raw_backend->remove_called);

    local_data.close_file(13, 2);
    ASSERT_TRUE(raw_backend->closed_file.has_value());
    EXPECT_EQ(13, raw_backend->closed_file->first);
    EXPECT_EQ(2, raw_backend->closed_file->second);

    local_data.close_torrent(14);
    EXPECT_EQ(14, raw_backend->closed_torrent);

    local_data.close_all();
    EXPECT_TRUE(raw_backend->close_all_called);

    local_data.shutdown();
}