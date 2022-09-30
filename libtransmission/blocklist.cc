// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib> // bsearch()
#include <fstream>
#include <string_view>
#include <vector>

#include <fmt/core.h>

#include "transmission.h"

#include "blocklist.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "utils.h"

/***
****  PRIVATE
***/

void BlocklistFile::close()
{
    rules_.clear();
}

void BlocklistFile::ensureLoaded() const
{
    if (!std::empty(rules_))
    {
        return;
    }

    auto in = std::ifstream{ filename_, std::ios_base::in | std::ios_base::binary };
    if (!in)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename_),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
        return;
    }

    auto file_info = tr_sys_path_get_info(filename_);
    auto zeroes_count = 0;
    auto max_zeroes = 0;

    static auto constexpr RangeSize = sizeof(AddressRange);
    if (file_info->size >= RangeSize)
    {
        std::array<char, 40> first_struct = {};

        in.read(reinterpret_cast<char*>(&first_struct), std::size(first_struct));
        in.clear();
        in.seekg(0, std::ios::beg);

        for (auto const struct_byte : first_struct)
        {
            if (struct_byte != 0)
            {
                zeroes_count = 0;
            }
            else
            {
                ++zeroes_count;

                if (zeroes_count > max_zeroes)
                {
                    max_zeroes = zeroes_count;
                }
            }
        }
    }

    // Check for old blocklist file format
    // Old struct size was 8 bytes (2 IPv4), new struct size is 40 bytes (2 IPv4, 2 IPv6)
    //
    // If we encounter less than 4 continuous bytes containing 0 we are using old file format
    // (as the new format guarantees at least 2 empty IPv4 OR 2 empty IPv6)
    // If we confirm using old style convert to new style and rewrite blocklist file
    if ((file_info->size >= 40 && max_zeroes < 4) || (file_info->size % 8 == 0 && file_info->size % 40 != 0))
    {
        auto range = AddressRange{};
        while (in.read(reinterpret_cast<char*>(&range), 8))
        {
            rules_.emplace_back(range);
        }

        tr_logAddInfo(_("Rewriting old blocklist file format to new format"));

        RewriteBlocklistFile();
    }

    else
    {
        auto range = AddressRange{};
        while (in.read(reinterpret_cast<char*>(&range), sizeof(range)))
        {
            rules_.emplace_back(range);
        }
    }

    tr_logAddInfo(fmt::format(
        ngettext("Blocklist '{path}' has {count} entry", "Blocklist '{path}' has {count} entries", std::size(rules_)),
        fmt::arg("path", tr_sys_path_basename(filename_)),
        fmt::arg("count", std::size(rules_))));
}

/***
****  PACKAGE-VISIBLE
***/

bool BlocklistFile::hasAddress(tr_address const& addr)
{
    TR_ASSERT(tr_address_is_valid(&addr));

    if (!is_enabled_)
    {
        return false;
    }

    ensureLoaded();

    if (std::empty(rules_))
    {
        return false;
    }

    if (addr.isIPv4())
    {
        auto const needle = ntohl(addr.addr.addr4.s_addr);

        // std::binary_search works differently and requires a less-than comparison
        // and two arguments of the same type. std::bsearch is the right choice.
        auto const* range = static_cast<AddressRange const*>(std::bsearch(
            &needle,
            std::data(rules_),
            std::size(rules_),
            sizeof(AddressRange),
            AddressRange::compareIPv4AddressToRange));

        return range != nullptr;
    }

    if (addr.isIPv6())
    {
        auto const needle = addr.addr.addr6;

        auto const* range = static_cast<AddressRange const*>(std::bsearch(
            &needle,
            std::data(rules_),
            std::size(rules_),
            sizeof(AddressRange),
            AddressRange::compareIPv6AddressToRange));

        return range != nullptr;
    }

    return false;
}

/*
 * P2P plaintext format: "comment:x.x.x.x-y.y.y.y" / "comment:x:x:x:x:x:x:x:x-x:x:x:x:x:x:x:x"
 * https://web.archive.org/web/20100328075307/http://wiki.phoenixlabs.org/wiki/P2P_Format
 * https://en.wikipedia.org/wiki/PeerGuardian#P2P_plaintext_format
 */
bool BlocklistFile::parseLine1(std::string_view line, struct AddressRange* range)
{
    // remove leading "comment:"
    auto pos = line.find(':');
    if (pos == std::string_view::npos)
    {
        return false;
    }
    line = line.substr(pos + 1);

    // parse the leading 'x.x.x.x'
    pos = line.find('-');
    if (pos == std::string_view::npos)
    {
        return false;
    }
    if (auto const addr = tr_address::fromString(line.substr(0, pos)); addr)
    {
        if (addr->isIPv4())
        {
            range->begin_ = ntohl(addr->addr.addr4.s_addr);
        }
        else
        {
            range->begin6_ = addr->addr.addr6;
        }
    }
    else
    {
        return false;
    }
    line = line.substr(pos + 1);

    // parse the trailing 'y.y.y.y'
    if (auto const addr = tr_address::fromString(line); addr)
    {
        if (addr->isIPv4())
        {
            range->end_ = ntohl(addr->addr.addr4.s_addr);
        }
        else
        {
            range->end6_ = addr->addr.addr6;
        }
    }
    else
    {
        return false;
    }

    return true;
}

/*
 * DAT / eMule format: "000.000.000.000 - 000.255.255.255 , 000 , invalid ip"a
 * https://sourceforge.net/p/peerguardian/wiki/dev-blocklist-format-dat/
 */
bool BlocklistFile::parseLine2(std::string_view line, struct AddressRange* range)
{
    static auto constexpr Delim1 = std::string_view{ " - " };
    static auto constexpr Delim2 = std::string_view{ " , " };

    auto pos = line.find(Delim1);
    if (pos == std::string_view::npos)
    {
        return false;
    }

    if (auto const addr = tr_address::fromString(line.substr(0, pos)); addr)
    {
        range->begin_ = ntohl(addr->addr.addr4.s_addr);
    }
    else
    {
        return false;
    }

    line = line.substr(pos + std::size(Delim1));
    pos = line.find(Delim2);
    if (pos == std::string_view::npos)
    {
        return false;
    }

    if (auto const addr = tr_address::fromString(line.substr(0, pos)); addr)
    {
        range->end_ = ntohl(addr->addr.addr4.s_addr);
    }
    else
    {
        return false;
    }

    return true;
}

/*
 * CIDR notation: "0.0.0.0/8", "::/64"
 * https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing#CIDR_notation
 */
bool BlocklistFile::parseLine3(char const* line, AddressRange* range)
{
    auto ip = std::array<unsigned int, 4>{};
    unsigned int pflen = 0;
    uint32_t ip_u = 0;
    uint32_t mask = 0xffffffff;

    // NOLINTNEXTLINE readability-container-data-pointer
    if (sscanf(line, "%u.%u.%u.%u/%u", TR_ARG_TUPLE(&ip[0], &ip[1], &ip[2], &ip[3]), &pflen) != 5)
    {
        return false;
    }

    if (pflen > 32 || ip[0] > 0xff || ip[1] > 0xff || ip[2] > 0xff || ip[3] > 0xff)
    {
        return false;
    }

    /* this is host order */
    mask <<= 32 - pflen;
    ip_u = ip[0] << 24 | ip[1] << 16 | ip[2] << 8 | ip[3];

    /* fill the non-prefix bits the way we need it */
    range->begin_ = ip_u & mask;
    range->end_ = ip_u | (~mask);

    return true;
}

bool BlocklistFile::parseLine(char const* line, AddressRange* range)
{
    return parseLine1(line, range) || parseLine2(line, range) || parseLine3(line, range);
}

bool BlocklistFile::compareAddressRangesByFirstAddress(AddressRange const& a, AddressRange const& b)
{
    if (a.begin_ == 0 && a.end_ == 0)
    {
        // IPv6
        return (memcmp(a.begin6_.s6_addr, b.begin6_.s6_addr, sizeof(a.begin6_.s6_addr)) < 0);
    }

    return a.begin_ < b.begin_;
}

size_t BlocklistFile::setContent(char const* filename)
{
    if (filename == nullptr)
    {
        return {};
    }

    auto in = std::ifstream{ filename };
    if (!in.is_open())
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
        return {};
    }

    auto line = std::string{};
    auto line_number = size_t{ 0U };
    auto ranges = std::vector<AddressRange>{};
    while (std::getline(in, line))
    {
        ++line_number;
        auto range = AddressRange{};
        if (!parseLine(line.c_str(), &range))
        {
            /* don't try to display the actual lines - it causes issues */
            tr_logAddWarn(fmt::format(_("Couldn't parse line: '{line}'"), fmt::arg("line", line_number)));
            continue;
        }
        ranges.push_back(range);
    }
    in.close();

    if (std::empty(ranges))
    {
        return {};
    }

    size_t keep = 0; // index in ranges

    std::sort(std::begin(ranges), std::end(ranges), BlocklistFile::compareAddressRangesByFirstAddress);

    // merge
    for (auto const& range : ranges)
    {
        if (range.begin_ == 0 && range.end_ == 0)
        {
            // IPv6
            if (memcmp(ranges[keep].end6_.s6_addr, range.begin6_.s6_addr, sizeof(range.begin6_.s6_addr)) < 0)
            {
                ranges[++keep] = range;
            }
            else if (memcmp(ranges[keep].end6_.s6_addr, range.end6_.s6_addr, sizeof(range.begin6_.s6_addr)) < 0)
            {
                ranges[keep].end6_ = range.end6_;
            }
        }
        else
        {
            if (ranges[keep].end_ < range.begin_)
            {
                ranges[++keep] = range;
            }
            else if (ranges[keep].end_ < range.end_)
            {
                ranges[keep].end_ = range.end_;
            }
        }
    }

    TR_ASSERT_MSG(keep + 1 <= std::size(ranges), "Can shrink `ranges` or leave intact, but not grow");
    ranges.resize(keep + 1);

#ifdef TR_ENABLE_ASSERTS
    assertValidRules(ranges);
#endif

    auto out = std::ofstream{ filename_, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary };
    if (!out.is_open())
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename_),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
        return {};
    }

    if (!out.write(reinterpret_cast<char const*>(ranges.data()), std::size(ranges) * sizeof(AddressRange)))
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't save '{path}': {error} ({error_code})"),
            fmt::arg("path", filename_),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
    }
    else
    {
        tr_logAddInfo(fmt::format(
            ngettext("Blocklist '{path}' has {count} entry", "Blocklist '{path}' has {count} entries", std::size(rules_)),
            fmt::arg("path", tr_sys_path_basename(filename_)),
            fmt::arg("count", std::size(rules_))));
    }

    out.close();

    close();
    ensureLoaded();
    return std::size(rules_);
}

void BlocklistFile::RewriteBlocklistFile() const
{
    auto out = std::ofstream{ filename_, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary };
    if (!out.is_open())
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename_),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
        return;
    }

    if (!out.write(reinterpret_cast<char const*>(rules_.data()), std::size(rules_) * sizeof(AddressRange)))
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't save '{path}': {error} ({error_code})"),
            fmt::arg("path", filename_),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
    }

    out.close();
    ensureLoaded();
}

#ifdef TR_ENABLE_ASSERTS
void BlocklistFile::assertValidRules(std::vector<AddressRange> const& ranges)
{
    for (auto const& r : ranges)
    {
        if (r.begin_ == 0 && r.end_ == 0)
        {
            TR_ASSERT(memcmp(r.begin6_.s6_addr, r.end6_.s6_addr, sizeof(r.begin6_.s6_addr)) <= 0);
        }
        else
        {
            TR_ASSERT(r.begin_ <= r.end_);
        }
    }

    auto ranges_IPv6 = std::vector<AddressRange>{};
    auto ranges_IPv4 = std::vector<AddressRange>{};

    for (size_t i = 0; i < std::size(ranges); i++)
    {
        if (ranges[i].begin_ == 0 && ranges[i].end_ == 0)
        {
            ranges_IPv6.push_back(ranges[i]);
        }
        else
        {
            ranges_IPv4.push_back(ranges[i]);
        }
    }

    for (size_t i = 1; i < std::size(ranges_IPv4); ++i)
    {
        TR_ASSERT(ranges_IPv4[i - 1].end_ < ranges_IPv4[i].begin_);
    }

    for (size_t i = 1; i < std::size(ranges_IPv6); ++i)
    {
        auto last_end_address = ranges_IPv6[i - 1].end6_.s6_addr;
        auto start_address = ranges_IPv6[i].begin6_.s6_addr;

        TR_ASSERT(memcmp(last_end_address, start_address, sizeof(&start_address)) > 0);
    }
}
#endif
