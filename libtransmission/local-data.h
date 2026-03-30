// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include <small/vector.hpp>

#include "libtransmission/constants.h"
#include "libtransmission/error-types.h"
#include "libtransmission/types.h"

class tr_open_files;
class tr_torrents;

namespace tr
{

class LocalData
{
public:
    using BlockData = small::max_size_vector<uint8_t, TrBlockSize>;

    using OnRead = std::function<
        void(tr_torrent_id_t, tr_byte_span_t byte_span, tr_error const& error, std::unique_ptr<BlockData> data)>;

    using OnTest = std::function<
        void(tr_torrent_id_t, tr_piece_index_t piece, tr_error const& error, std::optional<tr_sha1_digest_t> hash)>;

    using OnWrite = std::function<void(tr_torrent_id_t, tr_byte_span_t byte_span, tr_error const& error)>;

    using OnMove = std::function<void(tr_torrent_id_t, tr_error const& error)>;

    class Backend
    {
    public:
        virtual ~Backend() = default;

        [[nodiscard]] virtual tr_error_code_t read(tr_torrent_id_t tor_id, tr_byte_span_t byte_span, BlockData& setme) = 0;
        [[nodiscard]] virtual tr_error_code_t test_piece(
            tr_torrent_id_t tor_id,
            tr_piece_index_t piece,
            tr_sha1_digest_t& setme_hash) = 0;
        [[nodiscard]] virtual tr_error_code_t write(
            tr_torrent_id_t tor_id,
            tr_byte_span_t byte_span,
            BlockData const& data) = 0;
        [[nodiscard]] virtual tr_error_code_t move(
            tr_torrent_id_t id,
            std::string_view old_parent,
            std::string_view parent,
            std::string_view parent_name) = 0;
        [[nodiscard]] virtual tr_error_code_t remove(tr_torrent_id_t id, tr_torrent_remove_func remove_func) = 0;
        virtual void rename(
            tr_torrent_id_t id,
            std::string_view oldpath,
            std::string_view newname,
            tr_torrent_rename_done_func callback) = 0;
        virtual void close_all() = 0;
        virtual void close_torrent(tr_torrent_id_t tor_id) = 0;
        virtual void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num) = 0;
    };

    explicit LocalData(tr_torrents const& torrents, tr_open_files& open_files, size_t worker_count = {});
    explicit LocalData(std::unique_ptr<Backend> backend, size_t worker_count = {});

    LocalData(LocalData const&) = delete;
    LocalData(LocalData&&) = delete;
    LocalData& operator=(LocalData const&) = delete;
    LocalData& operator=(LocalData&&) = delete;

    ~LocalData();

    void read(tr_torrent_id_t, tr_byte_span_t byte_span, OnRead on_read);
    void test_piece(tr_torrent_id_t, tr_piece_index_t piece, OnTest on_test);
    void write(tr_torrent_id_t, tr_byte_span_t byte_span, std::unique_ptr<BlockData> data, OnWrite on_write);
    void close_torrent(tr_torrent_id_t);
    void close_file(tr_torrent_id_t, tr_file_index_t);
    void close_all();
    void move(tr_torrent_id_t, std::string_view old_parent, std::string_view parent, std::string_view parent_name, OnMove);
    void remove(tr_torrent_id_t, tr_torrent_remove_func remove_func);
    void rename(tr_torrent_id_t, std::string_view oldpath, std::string_view newname, tr_torrent_rename_done_func callback);
    void shutdown();
    [[nodiscard]] uint64_t enqueued_write_bytes() const;

private:
    std::unique_ptr<Backend> backend_;
};

} // namespace tr