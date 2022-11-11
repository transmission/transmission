// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
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
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h" // for _(), tr_strerror(), tr_strvEndsWith()

using namespace std::literals;

namespace libtransmission
{
namespace
{

// A string at the beginning of .bin files to test & make sure we don't load incompatible files
auto constexpr BinContentsPrefix = std::string_view{ "-tr-blocklist-file-format-v3-" };

// In the blocklists directory, the The plaintext source file can be anything, e.g. "level1".
// The pre-parsed, fast-to-load binary file will have a ".bin" suffix e.g. "level1.bin".
auto constexpr BinFileSuffix = std::string_view{ ".bin" };

using address_range_t = std::pair<tr_address, tr_address>;

void save(std::string_view filename, address_range_t const* ranges, size_t n_ranges)
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

    if (!out.write(std::data(BinContentsPrefix), std::size(BinContentsPrefix)) ||
        !out.write(reinterpret_cast<char const*>(ranges), n_ranges * sizeof(*ranges)))
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

namespace ParseHelpers
{
// P2P plaintext format: "comment:x.x.x.x-y.y.y.y" / "comment:x:x:x:x:x:x:x:x-x:x:x:x:x:x:x:x"
// https://web.archive.org/web/20100328075307/http://wiki.phoenixlabs.org/wiki/P2P_Format
// https://en.wikipedia.org/wiki/PeerGuardian#P2P_plaintext_format
std::optional<address_range_t> parsePeerGuardianLine(std::string_view line)
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

    auto addrpair = address_range_t{};
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

// DAT / eMule format: "000.000.000.000 - 000.255.255.255 , 000 , invalid ip"
// https://sourceforge.net/p/peerguardian/wiki/dev-blocklist-format-dat/
std::optional<address_range_t> parseEmuleLine(std::string_view line)
{
    static auto constexpr Delim1 = std::string_view{ " - " };
    static auto constexpr Delim2 = std::string_view{ " , " };

    auto pos = line.find(Delim1);
    if (pos == std::string_view::npos)
    {
        return {};
    }

    auto addrpair = address_range_t{};

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

// CIDR notation: "0.0.0.0/8", "::/64"
// https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing#CIDR_notation
// Example: `10.5.6.7/8` will block the range [10.0.0.0 .. 10.255.255.255]
std::optional<address_range_t> parseCidrLine(std::string_view line)
{
    auto addrpair = address_range_t{};

    auto pos = line.find('/');
    if (pos == std::string_view::npos)
    {
        return {};
    }

    if (auto const addr = tr_address::fromString(line.substr(0, pos)); addr && addr->isIPv4())
    {
        addrpair.first = *addr;
    }
    else
    {
        return {};
    }

    auto const pflen = tr_parseNum<size_t>(line.substr(pos + 1));
    if (!pflen)
    {
        return {};
    }

    auto const mask = uint32_t{ 0xFFFFFFFF } << (32 - *pflen);
    auto const ip_u = htonl(addrpair.first.addr.addr4.s_addr);
    addrpair.first.addr.addr4.s_addr = ntohl(ip_u & mask);
    addrpair.second.addr.addr4.s_addr = ntohl(ip_u | (~mask));
    return addrpair;
}

std::optional<address_range_t> parseLine(std::string_view line)
{
    for (auto const& line_parser : { parsePeerGuardianLine, parseEmuleLine, parseCidrLine })
    {
        if (auto range = line_parser(line); range)
        {
            return range;
        }
    }

    return {};
}
} // namespace ParseHelpers

auto parseFile(std::string_view filename)
{
    using namespace ParseHelpers;

    auto ranges = std::vector<address_range_t>{};

    auto in = std::ifstream{ tr_pathbuf{ filename } };
    if (!in.is_open())
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
        return ranges;
    }

    auto line = std::string{};
    auto line_number = size_t{ 0U };
    while (std::getline(in, line))
    {
        ++line_number;
        if (auto range = parseLine(line); range && (range->first.type == range->second.type))
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
        return ranges;
    }

    // safeguard against some joker swapping the begin & end ranges
    for (auto& range : ranges)
    {
        if (range.first > range.second)
        {
            std::swap(range.first, range.second);
        }
    }

    // sort ranges by start address
    std::sort(std::begin(ranges), std::end(ranges), [](auto const& a, auto const& b) { return a.first < b.first; });

    // merge overlapping ranges
    auto keep = size_t{ 0U };
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
    for (auto const& range : ranges)
    {
        TR_ASSERT(range.first <= range.second);
    }
    for (size_t i = 1, n = std::size(ranges); i < n; ++i)
    {
        TR_ASSERT(ranges[i - 1].second < ranges[i].first);
    }
#endif

    return ranges;
}

auto getFilenamesInDir(std::string_view folder)
{
    auto files = std::vector<std::string>{};

    if (auto const odir = tr_sys_dir_open(tr_pathbuf{ folder }); odir != TR_BAD_SYS_DIR)
    {
        char const* name = nullptr;
        auto const prefix = std::string{ folder } + '/';
        while ((name = tr_sys_dir_read_name(odir)) != nullptr)
        {
            if (name[0] == '.') // ignore dotfiles
            {
                continue;
            }

            files.emplace_back(prefix + name);
        }

        tr_sys_dir_close(odir);
    }

    return files;
}

} // namespace

void Blocklist::ensureLoaded() const
{
    if (!std::empty(rules_))
    {
        return;
    }

    // get the file's size
    tr_error* error = nullptr;
    auto const file_info = tr_sys_path_get_info(bin_file_, 0, &error);
    if (error != nullptr)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", bin_file_),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_clear(&error);
    }
    if (!file_info)
    {
        return;
    }

    // open the file
    auto in = std::ifstream{ bin_file_, std::ios_base::in | std::ios_base::binary };
    if (!in)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", bin_file_),
            fmt::arg("error", tr_strerror(errno)),
            fmt::arg("error_code", errno)));
        return;
    }

    // check to see if the file is usable
    bool supported_file = true;
    if (file_info->size < std::size(BinContentsPrefix)) // too small
    {
        supported_file = false;
    }
    else if (((file_info->size - std::size(BinContentsPrefix)) % sizeof(address_range_t)) != 0) // wrong size
    {
        supported_file = false;
    }
    else
    {
        auto tmp = std::array<char, std::size(BinContentsPrefix)>{};
        in.read(std::data(tmp), std::size(tmp));
        supported_file = BinContentsPrefix == std::string_view{ std::data(tmp), std::size(tmp) };
    }

    if (!supported_file)
    {
        // bad binary file; try to rebuild it
        in.close();
        auto const sz_src_file = std::string{ std::data(bin_file_), std::size(bin_file_) - std::size(BinFileSuffix) };
        if (tr_sys_path_exists(sz_src_file))
        {
            rules_ = parseFile(sz_src_file);
            if (!std::empty(rules_))
            {
                tr_logAddInfo(_("Rewriting old blocklist file format to new format"));
                tr_sys_path_remove(bin_file_);
                save(bin_file_, std::data(rules_), std::size(rules_));
            }
        }
        return;
    }

    auto range = address_range_t{};
    rules_.reserve((file_info->size - std::size(BinContentsPrefix) / sizeof(address_range_t)));
    while (in.read(reinterpret_cast<char*>(&range), sizeof(range)))
    {
        rules_.emplace_back(range);
    }

    tr_logAddInfo(fmt::format(
        ngettext("Blocklist '{path}' has {count} entry", "Blocklist '{path}' has {count} entries", std::size(rules_)),
        fmt::arg("path", tr_sys_path_basename(bin_file_)),
        fmt::arg("count", std::size(rules_))));
}

std::vector<Blocklist> Blocklist::loadBlocklists(std::string_view const blocklist_dir, bool const is_enabled)
{
    // check for files that need to be updated
    for (auto const& src_file : getFilenamesInDir(blocklist_dir))
    {
        if (tr_strvEndsWith(src_file, BinFileSuffix))
        {
            continue;
        }

        // ensure this src_file has an up-to-date corresponding bin_file
        auto const src_info = tr_sys_path_get_info(src_file);
        auto const bin_file = tr_pathbuf{ src_file, BinFileSuffix };
        auto const bin_info = tr_sys_path_get_info(bin_file);
        auto const bin_needs_update = src_info && (!bin_info || bin_info->last_modified_at <= src_info->last_modified_at);
        if (bin_needs_update)
        {
            if (auto const ranges = parseFile(src_file); !std::empty(ranges))
            {
                save(bin_file, std::data(ranges), std::size(ranges));
            }
        }
    }

    auto ret = std::vector<Blocklist>{};
    for (auto const& bin_file : getFilenamesInDir(blocklist_dir))
    {
        if (tr_strvEndsWith(bin_file, BinFileSuffix))
        {
            ret.emplace_back(bin_file, is_enabled);
        }
    }
    return ret;
}

bool Blocklist::contains(tr_address const& addr) const
{
    TR_ASSERT(tr_address_is_valid(&addr));

    if (!is_enabled_)
    {
        return false;
    }

    ensureLoaded();

    struct Compare
    {
        [[nodiscard]] static auto compare(tr_address const& a, address_range_t const& b) noexcept // <=>
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

        [[nodiscard]] static auto compare(address_range_t const& a, tr_address const& b) noexcept // <=>
        {
            return -compare(b, a);
        }

        [[nodiscard]] auto operator()(address_range_t const& a, tr_address const& b) const noexcept // <
        {
            return compare(a, b) < 0;
        }

        [[nodiscard]] auto operator()(tr_address const& a, address_range_t const& b) const noexcept // <
        {
            return compare(a, b) < 0;
        }
    };

    return std::binary_search(std::begin(rules_), std::end(rules_), addr, Compare{});
}

std::optional<Blocklist> Blocklist::saveNew(std::string_view external_file, std::string_view bin_file, bool is_enabled)
{
    // if we can't parse the file, do nothing
    auto rules = parseFile(external_file);
    if (std::empty(rules))
    {
        return {};
    }

    // make a copy of `external_file` for our own safekeeping
    auto const src_file = std::string{ std::data(bin_file), std::size(bin_file) - std::size(BinFileSuffix) };
    tr_sys_path_remove(src_file.c_str());
    tr_error* error = nullptr;
    if (!tr_sys_path_copy(tr_pathbuf{ external_file }, src_file.c_str(), &error))
    {
        if (error != nullptr)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't save '{path}': {error} ({error_code})"),
                fmt::arg("path", src_file),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_error_clear(&error);
        }
        return {};
    }

    save(bin_file, std::data(rules), std::size(rules));

    // return a new Blocklist with these rules
    auto ret = Blocklist{ bin_file, is_enabled };
    ret.rules_ = std::move(rules);
    return ret;
}

} // namespace libtransmission
