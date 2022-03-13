// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <cstdio>
#include <cstdlib> /* bsearch(), qsort() */
#include <cstring>

#include <fmt/core.h>

#include "transmission.h"

#include "blocklist.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "tr-assert.h"
#include "utils.h"

/***
****  PRIVATE
***/

struct tr_ipv4_range
{
    uint32_t begin;
    uint32_t end;
};

struct tr_blocklistFile
{
    bool isEnabled;
    tr_sys_file_t fd;
    size_t ruleCount;
    uint64_t byteCount;
    char* filename;
    struct tr_ipv4_range* rules;
};

static void blocklistClose(tr_blocklistFile* b)
{
    if (b->rules != nullptr)
    {
        tr_sys_file_unmap(b->rules, b->byteCount, nullptr);
        tr_sys_file_close(b->fd, nullptr);
        b->rules = nullptr;
        b->ruleCount = 0;
        b->byteCount = 0;
        b->fd = TR_BAD_SYS_FILE;
    }
}

static void blocklistLoad(tr_blocklistFile* b)
{
    blocklistClose(b);

    auto info = tr_sys_path_info{};
    if (!tr_sys_path_get_info(b->filename, 0, &info, nullptr))
    {
        return;
    }

    auto const byteCount = info.size;
    if (byteCount == 0)
    {
        return;
    }

    tr_error* error = nullptr;
    auto const fd = tr_sys_file_open(b->filename, TR_SYS_FILE_READ, 0, &error);
    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {errmsg} ({errcode})"),
            fmt::arg("path", b->filename),
            fmt::arg("errmsg", error->message),
            fmt::arg("errcode", error->code)));
        tr_error_free(error);
        return;
    }

    b->rules = static_cast<struct tr_ipv4_range*>(tr_sys_file_map_for_reading(fd, 0, byteCount, &error));
    if (b->rules == nullptr)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {errmsg} ({errcode})"),
            fmt::arg("path", b->filename),
            fmt::arg("errmsg", error->message),
            fmt::arg("errcode", error->code)));
        tr_sys_file_close(fd, nullptr);
        tr_error_free(error);
        return;
    }

    b->fd = fd;
    b->byteCount = byteCount;
    b->ruleCount = byteCount / sizeof(struct tr_ipv4_range);

    char* const base = tr_sys_path_basename(b->filename, nullptr);
    tr_logAddInfo(fmt::format(
        ngettext("Blocklist '{path}' has {count} entry", "Blocklist '{path}' has {count} entries", b->ruleCount),
        fmt::arg("path", base),
        fmt::arg("count", b->ruleCount)));
    tr_free(base);
}

static void blocklistEnsureLoaded(tr_blocklistFile* b)
{
    if (b->rules == nullptr)
    {
        blocklistLoad(b);
    }
}

static int compareAddressToRange(void const* va, void const* vb)
{
    auto const* a = static_cast<uint32_t const*>(va);
    auto const* b = static_cast<struct tr_ipv4_range const*>(vb);

    if (*a < b->begin)
    {
        return -1;
    }

    if (*a > b->end)
    {
        return 1;
    }

    return 0;
}

static void blocklistDelete(tr_blocklistFile* b)
{
    blocklistClose(b);
    tr_sys_path_remove(b->filename, nullptr);
}

/***
****  PACKAGE-VISIBLE
***/

tr_blocklistFile* tr_blocklistFileNew(char const* filename, bool isEnabled)
{
    auto* const b = tr_new0(tr_blocklistFile, 1);
    b->fd = TR_BAD_SYS_FILE;
    b->filename = tr_strdup(filename);
    b->isEnabled = isEnabled;

    return b;
}

char const* tr_blocklistFileGetFilename(tr_blocklistFile const* b)
{
    return b->filename;
}

void tr_blocklistFileFree(tr_blocklistFile* b)
{
    blocklistClose(b);
    tr_free(b->filename);
    tr_free(b);
}

bool tr_blocklistFileExists(tr_blocklistFile const* b)
{
    return tr_sys_path_exists(b->filename, nullptr);
}

int tr_blocklistFileGetRuleCount(tr_blocklistFile const* b)
{
    blocklistEnsureLoaded((tr_blocklistFile*)b);

    return b->ruleCount;
}

bool tr_blocklistFileIsEnabled(tr_blocklistFile* b)
{
    return b->isEnabled;
}

void tr_blocklistFileSetEnabled(tr_blocklistFile* b, bool isEnabled)
{
    TR_ASSERT(b != nullptr);

    b->isEnabled = isEnabled;
}

bool tr_blocklistFileHasAddress(tr_blocklistFile* b, tr_address const* addr)
{
    TR_ASSERT(tr_address_is_valid(addr));

    if (!b->isEnabled || addr->type == TR_AF_INET6)
    {
        return false;
    }

    blocklistEnsureLoaded(b);

    if (b->rules == nullptr || b->ruleCount == 0)
    {
        return false;
    }

    auto const needle = ntohl(addr->addr.addr4.s_addr);

    auto const* range = static_cast<struct tr_ipv4_range const*>(
        bsearch(&needle, b->rules, b->ruleCount, sizeof(struct tr_ipv4_range), compareAddressToRange));

    return range != nullptr;
}

/*
 * P2P plaintext format: "comment:x.x.x.x-y.y.y.y"
 * http://wiki.phoenixlabs.org/wiki/P2P_Format
 * http://en.wikipedia.org/wiki/PeerGuardian#P2P_plaintext_format
 */
static bool parseLine1(char const* line, struct tr_ipv4_range* range)
{
    int b[4];
    int e[4];
    char str[64];
    tr_address addr;

    char const* walk = strrchr(line, ':');

    if (walk == nullptr)
    {
        return false;
    }

    ++walk; /* walk past the colon */

    if (sscanf(
            walk,
            "%d.%d.%d.%d-%d.%d.%d.%d",
            TR_ARG_TUPLE(&b[0], &b[1], &b[2], &b[3]),
            TR_ARG_TUPLE(&e[0], &e[1], &e[2], &e[3])) != 8)
    {
        return false;
    }

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", TR_ARG_TUPLE(b[0], b[1], b[2], b[3]));

    if (!tr_address_from_string(&addr, str))
    {
        return false;
    }

    range->begin = ntohl(addr.addr.addr4.s_addr);

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", TR_ARG_TUPLE(e[0], e[1], e[2], e[3]));

    if (!tr_address_from_string(&addr, str))
    {
        return false;
    }

    range->end = ntohl(addr.addr.addr4.s_addr);

    return true;
}

/*
 * DAT format: "000.000.000.000 - 000.255.255.255 , 000 , invalid ip"
 * http://wiki.phoenixlabs.org/wiki/DAT_Format
 */
static bool parseLine2(char const* line, struct tr_ipv4_range* range)
{
    int unk = 0;
    int a[4];
    int b[4];
    char str[32];
    tr_address addr;

    if (sscanf(
            line,
            "%3d.%3d.%3d.%3d - %3d.%3d.%3d.%3d , %3d , ",
            TR_ARG_TUPLE(&a[0], &a[1], &a[2], &a[3]),
            TR_ARG_TUPLE(&b[0], &b[1], &b[2], &b[3]),
            &unk) != 9)
    {
        return false;
    }

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", TR_ARG_TUPLE(a[0], a[1], a[2], a[3]));

    if (!tr_address_from_string(&addr, str))
    {
        return false;
    }

    range->begin = ntohl(addr.addr.addr4.s_addr);

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", TR_ARG_TUPLE(b[0], b[1], b[2], b[3]));

    if (!tr_address_from_string(&addr, str))
    {
        return false;
    }

    range->end = ntohl(addr.addr.addr4.s_addr);

    return true;
}

/*
 * CIDR notation: "0.0.0.0/8", IPv4 only
 * https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing#CIDR_notation
 */
static bool parseLine3(char const* line, struct tr_ipv4_range* range)
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
    range->begin = ip_u & mask;
    range->end = ip_u | (~mask);

    return true;
}

static bool parseLine(char const* line, struct tr_ipv4_range* range)
{
    return parseLine1(line, range) || parseLine2(line, range) || parseLine3(line, range);
}

static int compareAddressRangesByFirstAddress(void const* va, void const* vb)
{
    auto const* a = static_cast<struct tr_ipv4_range const*>(va);
    auto const* b = static_cast<struct tr_ipv4_range const*>(vb);

    if (a->begin < b->begin)
    {
        return -1;
    }

    if (a->begin > b->begin)
    {
        return 1;
    }

    return 0;
}

int tr_blocklistFileSetContent(tr_blocklistFile* b, char const* filename)
{
    int inCount = 0;
    char line[2048];
    // TODO: should be a vector
    struct tr_ipv4_range* ranges = nullptr;
    size_t ranges_alloc = 0;
    size_t ranges_count = 0;
    tr_error* error = nullptr;

    if (filename == nullptr)
    {
        blocklistDelete(b);
        return 0;
    }

    auto const in = tr_sys_file_open(filename, TR_SYS_FILE_READ, 0, &error);
    if (in == TR_BAD_SYS_FILE)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {errmsg} ({errcode})"),
            fmt::arg("path", filename),
            fmt::arg("errmsg", error->message),
            fmt::arg("errcode", error->code)));
        tr_error_free(error);
        return 0;
    }

    blocklistClose(b);

    auto const out = tr_sys_file_open(b->filename, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0666, &error);
    if (out == TR_BAD_SYS_FILE)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {errmsg} ({errcode})"),
            fmt::arg("path", b->filename),
            fmt::arg("errmsg", error->message),
            fmt::arg("errcode", error->code)));
        tr_error_free(error);
        tr_sys_file_close(in, nullptr);
        return 0;
    }

    /* load the rules into memory */
    while (tr_sys_file_read_line(in, line, sizeof(line), nullptr))
    {
        struct tr_ipv4_range range;

        ++inCount;

        if (!parseLine(line, &range))
        {
            /* don't try to display the actual lines - it causes issues */
            tr_logAddWarn(fmt::format(_("Blocklist skipped invalid address at line {line}"), fmt::arg("line", inCount)));
            continue;
        }

        if (ranges_alloc == ranges_count)
        {
            ranges_alloc += 4096; /* arbitrary */
            ranges = tr_renew(struct tr_ipv4_range, ranges, ranges_alloc);
        }

        ranges[ranges_count++] = range;
    }

    if (ranges_count > 0) /* sort and merge */
    {
        struct tr_ipv4_range* keep = ranges;

        /* sort */
        qsort(ranges, ranges_count, sizeof(struct tr_ipv4_range), compareAddressRangesByFirstAddress);

        /* merge */
        for (size_t i = 1; i < ranges_count; ++i)
        {
            struct tr_ipv4_range const* r = &ranges[i];

            if (keep->end < r->begin)
            {
                *++keep = *r;
            }
            else if (keep->end < r->end)
            {
                keep->end = r->end;
            }
        }

        ranges_count = keep + 1 - ranges;

#ifdef TR_ENABLE_ASSERTS

        /* sanity checks: make sure the rules are sorted in ascending order and don't overlap */
        {
            for (size_t i = 0; i < ranges_count; ++i)
            {
                TR_ASSERT(ranges[i].begin <= ranges[i].end);
            }

            for (size_t i = 1; i < ranges_count; ++i)
            {
                TR_ASSERT(ranges[i - 1].end < ranges[i].begin);
            }
        }

#endif
    }

    if (!tr_sys_file_write(out, ranges, sizeof(struct tr_ipv4_range) * ranges_count, nullptr, &error))
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't save '{path}': {errmsg} ({errcode})"),
            fmt::arg("path", b->filename),
            fmt::arg("errmsg", error->message),
            fmt::arg("errcode", error->code)));
        tr_error_free(error);
    }
    else
    {
        char* base = tr_sys_path_basename(b->filename, nullptr);
        tr_logAddInfo(fmt::format(
            ngettext_(
                "Blocklist '{path}' updated with {count} entry",
                "Blocklist '{path}' updated with {count} entries",
                ranges_count),
            fmt::arg("path", base),
            fmt::arg("count", ranges_count)));
        tr_free(base);
    }

    tr_free(ranges);
    tr_sys_file_close(out, nullptr);
    tr_sys_file_close(in, nullptr);

    blocklistLoad(b);

    return ranges_count;
}
