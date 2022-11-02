// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib> // bsearch()
#include <fstream>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <fmt/core.h>

#include "transmission.h"

#include "blocklist.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "tr-strbuf.h"
#include "utils.h"

using namespace std::literals;

/***
****  PRIVATE
***/

BlocklistFile::AddressRange BlocklistFile::addressPairToRange(AddressPair const& pair)
{
    auto ret = AddressRange{};

    if (auto const& addr = pair.first; addr.isIPv4())
    {
        ret.begin_ = ntohl(addr.addr.addr4.s_addr);
        fmt::print("ipv4 begin {:s} -> {:d}\n", addr.readable(), ret.begin_);
    }
    else // IPv6
    {
        ret.begin6_ = addr.addr.addr6;
        fmt::print("ipv6 begin {:s} -> {:d}\n", addr.readable(), ret.begin_);
    }

    if (auto const& addr = pair.second; addr.isIPv4())
    {
        ret.end_ = ntohl(addr.addr.addr4.s_addr);
        fmt::print("ipv4 end {:s} -> {:d}\n", addr.readable(), ret.end_);
    }
    else // IPv6
    {
        ret.end6_ = addr.addr.addr6;
        fmt::print("ipv6 end {:s} -> {:d}\n", addr.readable(), ret.end_);
    }

    return ret;
}


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

std::vector<std::unique_ptr<BlocklistFile>> BlocklistFile::loadBlocklists(
    std::string_view const blocklist_dir,
    bool const is_enabled)
{
    auto loadme = std::unordered_set<std::string>{};
    auto working_set = std::vector<std::unique_ptr<BlocklistFile>>{};

    // walk the blocklist directory

    auto const odir = tr_sys_dir_open(tr_pathbuf{ blocklist_dir });

    if (odir == TR_BAD_SYS_DIR)
    {
        return working_set;
    }

    char const* name = nullptr;
    while ((name = tr_sys_dir_read_name(odir)) != nullptr)
    {
        auto load = std::string{};

        if (name[0] == '.') /* ignore dotfiles */
        {
            continue;
        }

        if (auto const path = tr_pathbuf{ blocklist_dir, '/', name }; tr_strvEndsWith(path, ".bin"sv))
        {
            load = path;
        }
        else
        {
            auto const binname = tr_pathbuf{ blocklist_dir, '/', name, ".bin"sv };

            if (auto const bininfo = tr_sys_path_get_info(binname); !bininfo)
            {
                // create it
                auto b = BlocklistFile{ binname, is_enabled };
                if (auto const n = b.setContent(path); n > 0)
                {
                    load = binname;
                }
            }
            else if (auto const pathinfo = tr_sys_path_get_info(path);
                     pathinfo && pathinfo->last_modified_at >= bininfo->last_modified_at)
            {
                // update it
                auto const old = tr_pathbuf{ binname, ".old"sv };
                tr_sys_path_remove(old);
                tr_sys_path_rename(binname, old);

                BlocklistFile b(binname, is_enabled);

                if (b.setContent(path) > 0)
                {
                    tr_sys_path_remove(old);
                }
                else
                {
                    tr_sys_path_remove(binname);
                    tr_sys_path_rename(old, binname);
                }
            }
        }

        if (!std::empty(load))
        {
            loadme.emplace(load);
        }
    }

    std::transform(
        std::begin(loadme),
        std::end(loadme),
        std::back_inserter(working_set),
        [&is_enabled](auto const& path) { return std::make_unique<BlocklistFile>(path.c_str(), is_enabled); });

    /* cleanup */
    tr_sys_dir_close(odir);

    return working_set;
}

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
std::optional<BlocklistFile::AddressPair> BlocklistFile::parseLine1(std::string_view line)
{
    // remove leading "comment:"
    auto pos = line.find(':');
    if (pos == std::string_view::npos)
    {
        return {};
    }
    line = line.substr(pos + 1);

    // parse the leading 'x.x.x.x'
    pos = line.find('-');
    if (pos == std::string_view::npos)
    {
        return {};
    }

    auto addrpair = AddressPair{};
    if (auto const addr = tr_address::fromString(line.substr(0, pos)); addr)
    {
        addrpair.first = *addr;
    }
    else
    {
        return {};
    }

    line = line.substr(pos + 1);

    // parse the trailing 'y.y.y.y'
    if (auto const addr = tr_address::fromString(line); addr)
    {
        addrpair.second = *addr;
    }
    else
    {
        return {};
    }

    return addrpair;
}

/*
 * DAT / eMule format: "000.000.000.000 - 000.255.255.255 , 000 , invalid ip"a
 * https://sourceforge.net/p/peerguardian/wiki/dev-blocklist-format-dat/
 */
std::optional<BlocklistFile::AddressPair> BlocklistFile::parseLine2(std::string_view line)
{
    static auto constexpr Delim1 = std::string_view{ " - " };
    static auto constexpr Delim2 = std::string_view{ " , " };

    auto pos = line.find(Delim1);
    if (pos == std::string_view::npos)
    {
        return {};
    }

    auto addrpair = AddressPair{};

    if (auto const addr = tr_address::fromString(line.substr(0, pos)); addr)
    {
        addrpair.first = *addr;
    }
    else
    {
        return {};
    }

    line = line.substr(pos + std::size(Delim1));
    pos = line.find(Delim2);
    if (pos == std::string_view::npos)
    {
        return {};
    }

    if (auto const addr = tr_address::fromString(line.substr(0, pos)); addr)
    {
        addrpair.second = *addr;
    }
    else
    {
        return {};
    }

    return addrpair;
}

/*
 * CIDR notation: "0.0.0.0/8", "::/64"
 * https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing#CIDR_notation
 */
std::optional<BlocklistFile::AddressPair> BlocklistFile::parseLine3(char const* line)
{
    auto ip = std::array<unsigned int, 4>{};
    unsigned int pflen = 0;
    uint32_t ip_u = 0;
    uint32_t mask = 0xffffffff;

    // NOLINTNEXTLINE readability-container-data-pointer
    if (sscanf(line, "%u.%u.%u.%u/%u", TR_ARG_TUPLE(&ip[0], &ip[1], &ip[2], &ip[3]), &pflen) != 5)
    {
        return {};
    }

    if (pflen > 32 || ip[0] > 0xff || ip[1] > 0xff || ip[2] > 0xff || ip[3] > 0xff)
    {
        return {};
    }

    // this is host order
    mask <<= 32 - pflen;
    ip_u = ip[0] << 24 | ip[1] << 16 | ip[2] << 8 | ip[3];

    // fill the non-prefix bits the way we need it
    auto addrpair = AddressPair{};
    addrpair.first.addr.addr4.s_addr = ntohl(ip_u & mask);
    addrpair.second.addr.addr4.s_addr = ntohl(ip_u | (~mask));
    return addrpair;
}

std::optional<BlocklistFile::AddressRange> BlocklistFile::parseLine(char const* line)
{
    fmt::print("parseLine {:s}\n", line);
    if (auto const addrpair = parseLine1(line); addrpair)
    {
        fmt::print("parseLine1 succeeded: {:s} -> {:s}\n", addrpair->first.readable(), addrpair->second.readable());
        return addressPairToRange(*addrpair);
    }

    if (auto const addrpair = parseLine2(line); addrpair)
    {
        fmt::print("parseLine2 succeeded: {:s} -> {:s}\n", addrpair->first.readable(), addrpair->second.readable());
        return addressPairToRange(*addrpair);
    }

    if (auto const addrpair = parseLine3(line); addrpair)
    {
        fmt::print("parseLine3 succeeded: {:s} -> {:s}\n", addrpair->first.readable(), addrpair->second.readable());
        return addressPairToRange(*addrpair);
    }

    return {};
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
        if (auto const range = parseLine(line.c_str()); range)
        {
            fmt::print("pushing range back\n");
            ranges.push_back(*range);
        }
        else
        {
            // don't try to display the actual lines - it causes issues
            fmt::print(_("Couldn't parse line: '{line}'\n"), fmt::arg("line", line_number));
            tr_logAddWarn(fmt::format(_("Couldn't parse line: '{line}'"), fmt::arg("line", line_number)));
        }
    }
    fmt::print("ranges size {:d}\n", std::size(ranges));
    in.close();

    if (std::empty(ranges))
    {
        return {};
    }

    //separate before sorting
    auto ipv4_ranges = std::vector<AddressRange>{};
    auto ipv6_ranges = std::vector<AddressRange>{};

    for (auto const& range : ranges)
    {
        if (range.begin_ == 0 && range.end_ == 0)
        {
            // IPv6
            ipv6_ranges.emplace_back(range);
        }
        else
        {
            ipv4_ranges.emplace_back(range);
        }
    }

    std::sort(std::begin(ipv4_ranges), std::end(ipv4_ranges), BlocklistFile::compareAddressRangesByFirstAddress);
    std::sort(std::begin(ipv6_ranges), std::end(ipv6_ranges), BlocklistFile::compareAddressRangesByFirstAddress);
    fmt::print("I think we have {:d} ipv4 and {:d} ipv6\n", std::size(ipv4_ranges), std::size(ipv6_ranges));

    // combine sorted
    ranges.clear();
    ranges.insert(ranges.end(), ipv4_ranges.begin(), ipv4_ranges.end());
    ranges.insert(ranges.end(), ipv6_ranges.begin(), ipv6_ranges.end());
    fmt::print("ranges size after sorting {:d}\n", std::size(ranges));

    size_t keep = 0; // index in ranges

    // merge
    for (auto const& range : ranges)
    {
        if (range.begin_ == 0 && range.end_ == 0)
        {
            fmt::print("ipv6\n");
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
            fmt::print("ipv4 {:d} {:d}\n", range.begin_, range.end_);
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
    fmt::print("ranges size after merging {:d}\n", std::size(ranges));

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

    auto ranges_ipv4 = std::vector<AddressRange>{};
    auto ranges_ipv6 = std::vector<AddressRange>{};

    for (size_t i = 0; i < std::size(ranges); i++)
    {
        if (ranges[i].begin_ == 0 && ranges[i].end_ == 0)
        {
            ranges_ipv6.emplace_back(ranges[i]);
        }
        else
        {
            ranges_ipv4.emplace_back(ranges[i]);
        }
    }

    TR_ASSERT(is_sorted(std::begin(ranges_ipv4), std::end(ranges_ipv4), BlocklistFile::compareAddressRangesByFirstAddress));
    TR_ASSERT(is_sorted(std::begin(ranges_ipv6), std::end(ranges_ipv6), BlocklistFile::compareAddressRangesByFirstAddress));
}
#endif
