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
#include <ctime>
#include <utility>

#include "transmission.h"

#include "file.h"
#include "net.h"
#include "utils.h"

struct tr_session;

class tr_open_files
{
    using Key = std::pair<tr_torrent_id_t, tr_file_index_t>;

    [[nodiscard]] static Key makeKey(tr_torrent_id_t tor_id, tr_file_index_t file_num)
    {
        return std::make_pair(tor_id, file_num);
    }

public:
    tr_open_files(tr_session* session) noexcept
        : session_{ session }
    {
    }

    [[nodiscard]] std::optional<tr_sys_file_t> get(tr_torrent_id_t torrent_id, tr_file_index_t file_num, bool writable)
        const noexcept
    {
        auto const key = makeKey(torrent_id, file_num);

        for (auto const& file : files_)
        {
            if (file.key != key)
            {
                continue;
            }

            if (writable && !file.writable)
            {
                return {};
            }

            file.last_used_at = tr_time();
            return file.fd;
        }

        return {};
    }

    [[nodiscard]] std::optional<tr_sys_file_t> get(
        tr_torrent_id_t torrent_id,
        tr_file_index_t file_num,
        bool writable,
        std::string_view filename,
        tr_preallocation_mode preallocation_mode,
        uint64_t preallocation_file_size);

    void close(tr_torrent_id_t torrent_id, tr_file_index_t file_num);

    void close(tr_torrent_id_t torrent_id)
    {
        for (auto& file : files_)
        {
            if (file.key.first == torrent_id)
            {
                file.close();
            }
        }
    }

private:
    tr_session* const session_;

    struct Entry
    {
        Key key;
        tr_sys_file_t fd = TR_BAD_SYS_FILE;
        bool writable = false;
        mutable time_t last_used_at = 0;

        ~Entry()
        {
            close();
        }

        void close()
        {
            if (fd != TR_BAD_SYS_FILE)
            {
                tr_sys_file_close(fd);
            }

            *this = {};
        }
    };

    void getFreeSlot();

    static auto constexpr MaxOpenFiles = 32;
    std::array<Entry, MaxOpenFiles> files_;
};
