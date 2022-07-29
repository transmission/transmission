// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib> // bsearch()
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
    if (rules_ != nullptr)
    {
        tr_sys_file_unmap(rules_, byte_count_);
        tr_sys_file_close(fd_);
        rules_ = nullptr;
        rule_count_ = 0;
        byte_count_ = 0;
        fd_ = TR_BAD_SYS_FILE;
    }
}

void BlocklistFile::load()
{
    close();

    auto info = tr_sys_path_info{};
    if (!tr_sys_path_get_info(getFilename(), 0, &info))
    {
        return;
    }

    auto const byteCount = info.size;
    if (byteCount == 0)
    {
        return;
    }

    tr_error* error = nullptr;
    auto const fd = tr_sys_file_open(getFilename(), TR_SYS_FILE_READ, 0, &error);
    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", getFilename()),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
        return;
    }

    rules_ = static_cast<struct IPv4Range*>(tr_sys_file_map_for_reading(fd, 0, byteCount, &error));
    if (rules_ == nullptr)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", getFilename()),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_sys_file_close(fd);
        tr_error_free(error);
        return;
    }

    fd_ = fd;
    byte_count_ = byteCount;
    rule_count_ = byteCount / sizeof(IPv4Range);

    tr_logAddInfo(fmt::format(
        ngettext("Blocklist '{path}' has {count} entry", "Blocklist '{path}' has {count} entries", rule_count_),
        fmt::arg("path", tr_sys_path_basename(getFilename())),
        fmt::arg("count", rule_count_)));
}

void BlocklistFile::ensureLoaded()
{
    if (rules_ == nullptr)
    {
        load();
    }
}

// TODO: unused
//static void blocklistDelete(tr_blocklistFile* b)
//{
//    blocklistClose(b);
//    tr_sys_path_remove(b->filename, nullptr);
//}

/***
****  PACKAGE-VISIBLE
***/

bool BlocklistFile::hasAddress(tr_address const& addr)
{
    TR_ASSERT(tr_address_is_valid(&addr));

    if (!is_enabled_ || !addr.isIPv4())
    {
        return false;
    }

    ensureLoaded();

    if (rules_ == nullptr || rule_count_ == 0)
    {
        return false;
    }

    auto const needle = ntohl(addr.addr.addr4.s_addr);

    // std::binary_search works differently and requires a less-than comparison
    // and two arguments of the same type. std::bsearch is the right choice.
    auto const* range = static_cast<IPv4Range const*>(
        std::bsearch(&needle, rules_, rule_count_, sizeof(IPv4Range), IPv4Range::compareAddressToRange));

    return range != nullptr;
}

/*
 * P2P plaintext format: "comment:x.x.x.x-y.y.y.y"
 * http://wiki.phoenixlabs.org/wiki/P2P_Format
 * https://en.wikipedia.org/wiki/PeerGuardian#P2P_plaintext_format
 */
bool BlocklistFile::parseLine1(std::string_view line, struct IPv4Range* range)
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
        range->begin_ = ntohl(addr->addr.addr4.s_addr);
    }
    else
    {
        return false;
    }
    line = line.substr(pos + 1);

    // parse the trailing 'y.y.y.y'
    if (auto const addr = tr_address::fromString(line); addr)
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
 * DAT / eMule format: "000.000.000.000 - 000.255.255.255 , 000 , invalid ip"a
 * https://sourceforge.net/p/peerguardian/wiki/dev-blocklist-format-dat/
 */
bool BlocklistFile::parseLine2(std::string_view line, struct IPv4Range* range)
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
 * CIDR notation: "0.0.0.0/8", IPv4 only
 * https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing#CIDR_notation
 */
bool BlocklistFile::parseLine3(char const* line, IPv4Range* range)
{
    unsigned int ip[4];
    unsigned int pflen = 0;
    uint32_t ip_u = 0;
    uint32_t mask = 0xffffffff;

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

bool BlocklistFile::parseLine(char const* line, IPv4Range* range)
{
    return parseLine1(line, range) || parseLine2(line, range) || parseLine3(line, range);
}

bool BlocklistFile::compareAddressRangesByFirstAddress(IPv4Range const& a, IPv4Range const& b)
{
    return a.begin_ < b.begin_;
}

size_t BlocklistFile::setContent(char const* filename)
{
    int inCount = 0;
    char line[2048];
    tr_error* error = nullptr;

    if (filename == nullptr)
    {
        return 0;
    }

    auto const in = tr_sys_file_open(filename, TR_SYS_FILE_READ, 0, &error);
    if (in == TR_BAD_SYS_FILE)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", filename),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
        return 0;
    }

    close();

    auto const out = tr_sys_file_open(
        getFilename(),
        TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE,
        0666,
        &error);
    if (out == TR_BAD_SYS_FILE)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", getFilename()),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
        tr_sys_file_close(in);
        return 0;
    }

    /* load the rules into memory */
    std::vector<IPv4Range> ranges;
    while (tr_sys_file_read_line(in, line, sizeof(line)))
    {
        IPv4Range range = {};

        ++inCount;

        if (!parseLine(line, &range))
        {
            /* don't try to display the actual lines - it causes issues */
            tr_logAddWarn(fmt::format(_("Couldn't parse line: '{line}'"), fmt::arg("line", inCount)));
            continue;
        }

        ranges.push_back(range);
    }

    if (!std::empty(ranges)) // sort and merge
    {
        size_t keep = 0; // index in ranges

        std::sort(std::begin(ranges), std::end(ranges), BlocklistFile::compareAddressRangesByFirstAddress);

        // merge
        for (auto const& r : ranges)
        {
            if (ranges[keep].end_ < r.begin_)
            {
                ranges[++keep] = r;
            }
            else if (ranges[keep].end_ < r.end_)
            {
                ranges[keep].end_ = r.end_;
            }
        }

        TR_ASSERT_MSG(keep + 1 <= std::size(ranges), "Can shrink `ranges` or leave intact, but not grow");
        ranges.resize(keep + 1);

#ifdef TR_ENABLE_ASSERTS
        assertValidRules(ranges);
#endif
    }

    if (!tr_sys_file_write(out, ranges.data(), sizeof(IPv4Range) * std::size(ranges), nullptr, &error))
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't save '{path}': {error} ({error_code})"),
            fmt::arg("path", getFilename()),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
    }
    else
    {
        tr_logAddInfo(fmt::format(
            ngettext("Blocklist '{path}' has {count} entry", "Blocklist '{path}' has {count} entries", rule_count_),
            fmt::arg("path", tr_sys_path_basename(getFilename())),
            fmt::arg("count", rule_count_)));
    }

    tr_sys_file_close(out);
    tr_sys_file_close(in);

    load();

    return std::size(ranges);
}

#ifdef TR_ENABLE_ASSERTS
void BlocklistFile::assertValidRules(std::vector<IPv4Range> const& ranges)
{
    for (auto const& r : ranges)
    {
        TR_ASSERT(r.begin_ <= r.end_);
    }

    for (size_t i = 1; i < std::size(ranges); ++i)
    {
        TR_ASSERT(ranges[i - 1].end_ < ranges[i].begin_);
    }
}
#endif
