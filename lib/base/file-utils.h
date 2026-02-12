// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <iterator> // for std::data(), std::size()
#include <string_view>
#include <vector>

struct tr_error;

/**
 * @addtogroup utils Utilities
 * @{
 */

bool tr_file_read(std::string_view filename, std::vector<char>& contents, tr_error* error = nullptr);

/**
 * Tries to move a file by renaming, and [optionally] if that fails, by copying.
 *
 * Creates the destination directory if it doesn't exist.
 */
bool tr_file_move(std::string_view oldpath, std::string_view newpath, bool allow_copy, tr_error* error = nullptr);

bool tr_file_save(std::string_view filename, std::string_view contents, tr_error* error = nullptr);

template<typename ContiguousRange>
constexpr auto tr_file_save(std::string_view filename, ContiguousRange const& x, tr_error* error = nullptr)
{
    return tr_file_save(filename, std::string_view{ std::data(x), std::size(x) }, error);
}
