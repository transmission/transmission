// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <cstdint>
#include <cstring>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility> // for std::pair
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

#include "transmission.h"

#include "file.h" // for tr_sys_file_t
#include "net.h" // for tr_address
#include "tr-assert.h"
#include "tr-macros.h"

struct tr_address;

struct BlocklistFile
{
public:
    // Prevent moving to protect the fd_ from accidental destruction
    BlocklistFile(BlocklistFile&&) = delete;
    BlocklistFile(BlocklistFile const&) = delete;
    BlocklistFile& operator=(BlocklistFile const&) = delete;
    BlocklistFile& operator=(BlocklistFile&&) = delete;

    BlocklistFile(char const* filename, bool isEnabled)
        : filename_(filename)
        , is_enabled_(isEnabled)
    {
    }

    [[nodiscard]] constexpr auto const& filename() const noexcept
    {
        return filename_;
    }

    [[nodiscard]] bool exists() const
    {
        return tr_sys_path_exists(filename_.c_str(), nullptr);
    }

    [[nodiscard]] size_t getRuleCount() const
    {
        ensureLoaded();

        return std::size(rules_);
    }

    [[nodiscard]] constexpr bool isEnabled() const noexcept
    {
        return is_enabled_;
    }

    void setEnabled(bool isEnabled)
    {
        is_enabled_ = isEnabled;
    }

    bool hasAddress(tr_address const& addr);

    /// @brief Read the file of ranges, sort and merge, write to our own file, and reload from it
    size_t setContent(char const* filename);

    static std::vector<std::unique_ptr<BlocklistFile>> loadBlocklists(
        std::string_view const blocklist_dir,
        bool const is_enabled);

private:
    using AddressPair = std::pair<tr_address, tr_address>;

    void ensureLoaded() const;

    static void save(std::string_view filename, AddressPair const* pairs, size_t n_pairs);
    static std::vector<AddressPair> parseFile(std::string_view filename);
    static std::optional<AddressPair> parseLine(char const* line);
    static std::optional<AddressPair> parseLine1(std::string_view line);
    static std::optional<AddressPair> parseLine2(std::string_view line);
    static std::optional<AddressPair> parseLine3(char const* line);

    std::string const filename_;

    bool is_enabled_ = false;
    mutable std::vector<AddressPair> rules_;

    static auto constexpr FileFormatVersion = std::array<char, 4>{ 'v', '0', '0', '3' };
    static auto constexpr BinSuffix = std::string_view{ ".bin" };
};
