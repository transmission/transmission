// This file Copyright 2016-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // for size_t
#include <ctime> // for time_t
#include <string_view>

#include "transmission.h"

#include "file.h" // tr_sys_file_t
#include "tr-strbuf.h" // for tr_pathbuf

class tr_session_id
{
public:
    using current_time_func_t = time_t(*)();

    tr_session_id(current_time_func_t time_func)
        : time_func_{ time_func }
    {
    }

    tr_session_id(tr_session_id&&) = delete;
    tr_session_id(tr_session_id const&) = delete;
    tr_session_id& operator= (tr_session_id&&) = delete;
    tr_session_id& operator= (tr_session_id const&) = delete;
    ~tr_session_id();

    [[nodiscard]] static bool isLocal(std::string_view) noexcept;

    [[nodiscard]] std::string_view sv() const noexcept;

    [[nodiscard]] char const* c_str() const noexcept;

private:
    static auto constexpr SessionIdSize = size_t{ 48 };
    static auto constexpr SessionIdDurationSec = time_t{ 60 * 60 }; /* expire in an hour */

    static void get_lockfile_path(std::string_view session_id, tr_pathbuf&);
    static tr_sys_file_t create_lockfile(std::string_view session_id);
    static void destroy_lockfile(tr_sys_file_t lockfile_fd, std::string_view session_id);

    using session_id_t = std::array<char, SessionIdSize + 1>; // +1 for '\0';
    static session_id_t make_session_id();

    current_time_func_t const time_func_;

    mutable session_id_t current_value_;
    mutable session_id_t previous_value_;
    mutable tr_sys_file_t current_lock_file_ = TR_BAD_SYS_FILE;
    mutable tr_sys_file_t previous_lock_file_ = TR_BAD_SYS_FILE;
    mutable time_t expires_at_ = 0;
};
