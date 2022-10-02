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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

#include "file.h" // for tr_sys_file_t
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

    ~BlocklistFile()
    {
        close();
    }

    [[nodiscard]] constexpr auto& filename() const
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

    [[nodiscard]] constexpr bool isEnabled() const
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

    static std::vector<std::unique_ptr<BlocklistFile>> loadBlocklists(std::string_view const config_dir, bool const is_enabled);

private:
    struct AddressRange
    {
        uint32_t begin_ = 0;
        uint32_t end_ = 0;
        in6_addr begin6_;
        in6_addr end6_;

        /// @brief Used for std::bsearch of an IPv4 address
        static int compareIPv4AddressToRange(void const* va, void const* vb)
        {
            auto const* a = reinterpret_cast<uint32_t const*>(va);
            auto const* b = reinterpret_cast<AddressRange const*>(vb);

            if (*a < b->begin_)
            {
                return -1;
            }

            if (*a > b->end_)
            {
                return 1;
            }

            return 0;
        }

        /// @brief Used for std::bsearch of an IPv6 address
        static int compareIPv6AddressToRange(void const* va, void const* vb)
        {
            auto const* a = reinterpret_cast<in6_addr const*>(va);
            auto const* b = reinterpret_cast<AddressRange const*>(vb);

            if (memcmp(&a->s6_addr, &b->begin6_.s6_addr, sizeof(a->s6_addr)) < 0)
            {
                return -1;
            }

            if (memcmp(&a->s6_addr, &b->end6_.s6_addr, sizeof(a->s6_addr)) > 0)
            {
                return 1;
            }

            return 0;
        }
    };

    void RewriteBlocklistFile() const;
    void ensureLoaded() const;
    void load();
    void close();

    static bool parseLine(char const* line, AddressRange* range);
    static bool compareAddressRangesByFirstAddress(AddressRange const& a, AddressRange const& b);

    static bool parseLine1(std::string_view line, struct AddressRange* range);
    static bool parseLine2(std::string_view line, struct AddressRange* range);
    static bool parseLine3(char const* line, AddressRange* range);

#ifdef TR_ENABLE_ASSERTS
    /// @brief Sanity checks: make sure the rules are sorted in ascending order and don't overlap
    static void assertValidRules(std::vector<AddressRange> const& ranges);
#endif

    std::string const filename_;

    bool is_enabled_ = false;
    mutable std::vector<AddressRange> rules_;
};
