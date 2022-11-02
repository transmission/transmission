// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // for size_t
#include <optional>
#include <string>
#include <string_view>
#include <utility> // for std::pair
#include <vector>

#include "net.h" // for tr_address

struct tr_address;

namespace libtransmission
{

class Blocklist
{
public:
    static std::vector<Blocklist> loadBlocklists(std::string_view const blocklist_dir, bool const is_enabled);

    static std::optional<Blocklist> saveNew(std::string_view external_file, std::string_view bin_file, bool is_enabled);

    Blocklist() = default;

    Blocklist(std::string_view bin_file, bool is_enabled)
        : bin_file_{ bin_file }
        , is_enabled_{ is_enabled }
    {
    }

    [[nodiscard]] constexpr auto const& binFile() const noexcept
    {
        return bin_file_;
    }

    [[nodiscard]] constexpr bool enabled() const noexcept
    {
        return is_enabled_;
    }

    [[nodiscard]] size_t size() const
    {
        ensureLoaded();

        return std::size(rules_);
    }

    void setEnabled(bool is_enabled) noexcept
    {
        is_enabled_ = is_enabled;
    }

    bool hasAddress(tr_address const& addr) const;

private:
    using AddressPair = std::pair<tr_address, tr_address>;

    static auto constexpr FileFormatVersion = std::array<char, 4>{ 'v', '0', '0', '3' };
    static auto constexpr BinSuffix = std::string_view{ ".bin" };

    static void save(std::string_view filename, AddressPair const* pairs, size_t n_pairs);
    static std::vector<AddressPair> parseFile(std::string_view filename);
    static std::optional<AddressPair> parseLine(char const* line);
    static std::optional<AddressPair> parseLine1(std::string_view line);
    static std::optional<AddressPair> parseLine2(std::string_view line);
    static std::optional<AddressPair> parseLine3(char const* line);

    void ensureLoaded() const;

    mutable std::vector<AddressPair> rules_;

    std::string bin_file_;
    bool is_enabled_ = false;
};

} // namespace libtransmission
