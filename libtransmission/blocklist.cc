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

void BlocklistFile::ensureLoaded() const
{
    if (!std::empty(rules_))
    {
        return;
    }

    tr_error* error = nullptr;
    auto const file_info = tr_sys_path_get_info(filename_, 0, &error);
    if (error)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename_),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_clear(&error);
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

    bool unsupported_file = false;
    if (file_info->size < std::size(FileFormatVersion))
    {
        unsupported_file = true;
    }
    else if (((file_info->size - std::size(FileFormatVersion)) % sizeof(AddressPair)) != 0)
    {
        unsupported_file = true;
    }
    else
    {
        auto version_string = std::array<char, std::size(FileFormatVersion)>{};
        in.read(std::data(version_string), std::size(version_string));
        unsupported_file = version_string != FileFormatVersion;
    }
    if (unsupported_file)
    {
        tr_sys_path_remove(filename_);
        auto source_file = std::string_view{ filename_ };
        source_file.remove_suffix(std::size(BinSuffix));
        tr_logAddInfo(_("Rewriting old blocklist file format to new format"));
        rules_ = parseFile(source_file);
        save(filename_, std::data(rules_), std::size(rules_));
        return;
    }

    auto range = AddressPair{};
    while (in.read(reinterpret_cast<char*>(&range), sizeof(range)))
    {
        rules_.emplace_back(range);
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

        if (auto const path = tr_pathbuf{ blocklist_dir, '/', name }; tr_strvEndsWith(path, BinSuffix))
        {
            load = path;
        }
        else
        {
            auto const binname = tr_pathbuf{ blocklist_dir, '/', name, BinSuffix };

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

    struct Compare
    {
        [[nodiscard]] auto compare(tr_address const& a, AddressPair const& b) const noexcept // <=>
        {
            if (a < b.first)
            {
                return -1;
            }
            if (b.second < a)
            {
                return 1;
            }
            return 0;
        }

        [[nodiscard]] auto compare(AddressPair const& a, tr_address const& b) const noexcept // <=>
        {
            return -compare(b, a);
        }

        [[nodiscard]] auto operator()(AddressPair const& a, tr_address const& b) const noexcept // <
        {
            return compare(a, b) < 0;
        }

        [[nodiscard]] auto operator()(tr_address const& a, AddressPair const& b) const noexcept // <
        {
            return compare(a, b) < 0;
        }
    };

    return std::binary_search(std::begin(rules_), std::end(rules_), addr, Compare{});
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

std::optional<BlocklistFile::AddressPair> BlocklistFile::parseLine(char const* line)
{
    if (auto range = parseLine1(line); range)
    {
        return range;
    }

    if (auto range = parseLine2(line); range)
    {
        return range;
    }

    if (auto range = parseLine3(line); range)
    {
        return range;
    }

    return {};
}

std::vector<BlocklistFile::AddressPair> BlocklistFile::parseFile(std::string_view filename)
{
    auto in = std::ifstream{ tr_pathbuf{ filename } };
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
    auto ranges = std::vector<AddressPair>{};
    while (std::getline(in, line))
    {
        ++line_number;
        if (auto range = parseLine(line.c_str()); range)
        {
            ranges.push_back(*range);
        }
        else
        {
            // don't try to display the actual lines - it causes issues
            tr_logAddWarn(fmt::format(_("Couldn't parse line: '{line}'"), fmt::arg("line", line_number)));
        }
    }
    in.close();

    if (std::empty(ranges))
    {
        return {};
    }

    // safeguard against some joker swapping the begin & end ranges
    for (auto& range : ranges)
    {
        if (range.first > range.second)
        {
            std::swap(range.first, range.second);
        }
    }

    std::sort(std::begin(ranges), std::end(ranges), [](auto const& a, auto const& b) { return a.first < b.first; });

    // merge overlapping ranges
    size_t keep = 0; // index in ranges
    for (auto const& range : ranges)
    {
        if (ranges[keep].second < range.first)
        {
            ranges[++keep] = range;
        }
        else if (ranges[keep].second < range.second)
        {
            ranges[keep].second = range.second;
        }
    }

    TR_ASSERT_MSG(keep + 1 <= std::size(ranges), "Can shrink `ranges` or leave intact, but not grow");
    ranges.resize(keep + 1);

#ifdef TR_ENABLE_ASSERTS
    for (auto const& r : ranges)
    {
        TR_ASSERT(r.first <= r.second);
    }
    for (size_t i = 1, n = std::size(ranges); i < n; ++i)
    {
        TR_ASSERT(ranges[i - 1].second < ranges[i].first);
    }
#endif

    return ranges;
}

size_t BlocklistFile::setContent(char const* filename)
{
    auto const ranges = parseFile(filename);
    auto const n_ranges = std::size(ranges);

    if (n_ranges != 0U)
    {
        save(filename_, std::data(ranges), std::size(ranges));
        rules_ = ranges;
        ensureLoaded();
    }

    return n_ranges;
}

void BlocklistFile::save(std::string_view filename, AddressPair const* ranges, size_t n_ranges)
{
    auto out = std::ofstream{ tr_pathbuf{ filename }, std::ios_base::out | std::ios_base::trunc | std::ios_base::binary };
    if (!out.is_open())
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
        return;
    }

    if (!out.write(std::data(FileFormatVersion), std::size(FileFormatVersion)) ||
        !out.write(reinterpret_cast<char const*>(ranges), n_ranges * sizeof(AddressPair)))
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't save '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
    }
    else
    {
        tr_logAddInfo(fmt::format(
            ngettext("Blocklist '{path}' has {count} entry", "Blocklist '{path}' has {count} entries", n_ranges),
            fmt::arg("path", tr_sys_path_basename(filename)),
            fmt::arg("count", n_ranges)));
    }

    out.close();
}
