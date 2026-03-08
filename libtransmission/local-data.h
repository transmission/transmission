// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <cstdint> // for intX_t, uintX_t
#include <functional>
#include <memory> // for std::unique_ptr
#include <optional>
#include <string_view>
#include <utility> // for std::pair
#include <vector>

#include <small/vector.hpp>

#include "libtransmission/block-info.h"
#include "libtransmission/types.h"

class tr_open_files;
class tr_torrents;

namespace tr
{

class LocalData
{
public:
    using BlockData = small::max_size_vector<uint8_t, tr_block_info::BlockSize>;

    using OnRead = std::function<
        void(tr_torrent_id_t, tr_byte_span_t byte_span, tr_error const& error, std::unique_ptr<BlockData> data)>;

    using OnTest = std::function<
        void(tr_torrent_id_t, tr_piece_index_t piece, tr_error const& error, std::optional<tr_sha1_digest_t> hash)>;

    using OnWrite = std::function<void(tr_torrent_id_t, tr_byte_span_t byte_span, tr_error const& error)>;

    class Backend
    {
    public:
        virtual ~Backend() = default;

        [[nodiscard]] virtual int read(tr_torrent_id_t tor_id, tr_byte_span_t byte_span, BlockData& setme) = 0;
        [[nodiscard]] virtual int testPiece(tr_torrent_id_t tor_id, tr_piece_index_t piece, tr_sha1_digest_t& setme_hash) = 0;
        [[nodiscard]] virtual int write(tr_torrent_id_t tor_id, tr_byte_span_t byte_span, BlockData const& data) = 0;
        [[nodiscard]] virtual int move(
            tr_torrent_id_t id,
            std::string_view old_parent,
            std::string_view parent,
            std::string_view parent_name) = 0;
        [[nodiscard]] virtual int remove(tr_torrent_id_t id, tr_torrent_remove_func remove_func) = 0;
        [[nodiscard]] virtual int rename(tr_torrent_id_t id, std::string_view oldpath, std::string_view newname) = 0;
        virtual void close_torrent(tr_torrent_id_t tor_id) = 0;
        virtual void close_file(tr_torrent_id_t tor_id, tr_file_index_t file_num) = 0;
    };

    explicit LocalData(tr_open_files& open_files, tr_torrents const& torrents, size_t worker_count = {});
    explicit LocalData(std::unique_ptr<Backend> backend, size_t worker_count = {});

    LocalData(LocalData const&) = delete;
    LocalData(LocalData&&) = delete;
    LocalData& operator=(LocalData const&) = delete;
    LocalData& operator=(LocalData&&) = delete;

    ~LocalData();

    // Read a block
    void read(tr_torrent_id_t, tr_byte_span_t byte_span, OnRead on_read);

    // Read a piece and return its SHA1 checksum.
    void testPiece(tr_torrent_id_t, tr_piece_index_t piece, OnTest on_test);

    // Write a block
    void write(tr_torrent_id_t, tr_byte_span_t byte_span, std::unique_ptr<BlockData> data, OnWrite on_write);

    // Close the files in a torrent.
    // Useful eg when a torrent download finishes and we want
    // to reopen the files in read-only mode for seeding.
    void close_torrent(tr_torrent_id_t);

    // Close an individual file in the torrent.
    void close_file(tr_torrent_id_t, tr_file_index_t);

    // See tr_torrent_files::move()
    void move(tr_torrent_id_t, std::string_view old_parent, std::string_view parent, std::string_view parent_name = "");

    // See tr_torrent_files::remove()
    void remove(tr_torrent_id_t, tr_torrent_remove_func remove_func);

    // See tr_torrentRenamePath()
    void rename(tr_torrent_id_t, std::string_view oldpath, std::string_view newname, tr_torrent_rename_done_func callback);

    // Stops accepting new work and blocks until all already-enqueued
    // non-read operations complete.
    void shutdown();

    // Number of bytes pending to be written to disk
    [[nodiscard]] uint64_t enqueued_write_bytes() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tr
