// This file Copyright © 2005-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstdint> // uint64_t
#include <optional>
#include <utility> // std::pair

#include "transmission.h"

#include "file.h" // tr_sys_file_t

struct tr_session;

// a cache of torrents' local data file handles used while reading/writing local data
class tr_open_files
{
public:
    [[nodiscard]] std::optional<tr_sys_file_t> get(tr_torrent_id_t tor_id, tr_file_index_t file_num, bool writable);

    [[nodiscard]] std::optional<tr_sys_file_t> get(
        tr_torrent_id_t tor_id,
        tr_file_index_t file_num,
        bool writable,
        std::string_view filename,
        tr_preallocation_mode mode,
        uint64_t file_size);

    void closeAll();
    void closeTorrent(tr_torrent_id_t tor_id);
    void closeFile(tr_torrent_id_t tor_id, tr_file_index_t file_num);

private:
    using Key = std::pair<tr_torrent_id_t, tr_file_index_t>;

    [[nodiscard]] static Key makeKey(tr_torrent_id_t tor_id, tr_file_index_t file_num)
    {
        return std::make_pair(tor_id, file_num);
    }

    struct Entry
    {
        ~Entry();
        void close();

        Key key_;
        tr_sys_file_t fd_ = TR_BAD_SYS_FILE;
        bool writable_ = false;
        mutable uint64_t sequence_ = 0;
    };

    static auto constexpr MaxOpenFiles = 32;
    using Files = std::array<Entry, MaxOpenFiles>;

    Files::iterator find(Key const& key);

    Entry& getFreeSlot();

    Files files_;
};
