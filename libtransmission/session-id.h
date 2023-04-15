// This file Copyright 2016-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // for size_t
#include <ctime> // for time_t
#include <string_view>

#include "file.h" // tr_sys_file_t

class tr_session_id
{
public:
    using current_time_func_t = time_t (*)();

    explicit tr_session_id(current_time_func_t get_current_time)
        : get_current_time_{ get_current_time }
    {
    }

    tr_session_id(tr_session_id&&) = delete;
    tr_session_id(tr_session_id const&) = delete;
    tr_session_id& operator=(tr_session_id&&) = delete;
    tr_session_id& operator=(tr_session_id const&) = delete;
    ~tr_session_id();

    /**
     * Check if session ID corresponds to session running on the same machine as
     * the caller.
     *
     * This is useful for various behavior alterations, such as transforming
     * relative paths to absolute before passing through RPC, or presenting
     * different UI for local and remote sessions.
     */
    [[nodiscard]] static bool isLocal(std::string_view) noexcept;

    // current session identifier
    [[nodiscard]] std::string_view sv() const noexcept;
    [[nodiscard]] char const* c_str() const noexcept;

private:
    static auto constexpr SessionIdSize = size_t{ 48 };
    static auto constexpr SessionIdDurationSec = time_t{ 60 * 60 }; /* expire in an hour */

    using session_id_t = std::array<char, SessionIdSize + 1>; // add one for '\0'
    static session_id_t make_session_id();

    current_time_func_t const get_current_time_;

    mutable session_id_t current_value_ = {};
    mutable session_id_t previous_value_ = {};
    mutable tr_sys_file_t current_lock_file_ = TR_BAD_SYS_FILE;
    mutable tr_sys_file_t previous_lock_file_ = TR_BAD_SYS_FILE;
    mutable time_t expires_at_ = 0;
};
