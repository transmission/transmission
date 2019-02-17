/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* bsearch(), qsort() */
#include <string.h>

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
    if (b->rules != NULL)
    {
        tr_sys_file_unmap(b->rules, b->byteCount, NULL);
        tr_sys_file_close(b->fd, NULL);
        b->rules = NULL;
        b->ruleCount = 0;
        b->byteCount = 0;
        b->fd = TR_BAD_SYS_FILE;
    }
}

static void blocklistLoad(tr_blocklistFile* b)
{
    tr_sys_file_t fd;
    uint64_t byteCount;
    tr_sys_path_info info;
    char* base;
    tr_error* error = NULL;
    char const* err_fmt = _("Couldn't read \"%1$s\": %2$s");

    blocklistClose(b);

    if (!tr_sys_path_get_info(b->filename, 0, &info, NULL))
    {
        return;
    }

    byteCount = info.size;

    if (byteCount == 0)
    {
        return;
    }

    fd = tr_sys_file_open(b->filename, TR_SYS_FILE_READ, 0, &error);

    if (fd == TR_BAD_SYS_FILE)
    {
        tr_logAddError(err_fmt, b->filename, error->message);
        tr_error_free(error);
        return;
    }

    b->rules = tr_sys_file_map_for_reading(fd, 0, byteCount, &error);

    if (b->rules == NULL)
    {
        tr_logAddError(err_fmt, b->filename, error->message);
        tr_sys_file_close(fd, NULL);
        tr_error_free(error);
        return;
    }

    b->fd = fd;
    b->byteCount = byteCount;
    b->ruleCount = byteCount / sizeof(struct tr_ipv4_range);

    base = tr_sys_path_basename(b->filename, NULL);
    tr_logAddInfo(_("Blocklist \"%s\" contains %zu entries"), base, b->ruleCount);
    tr_free(base);
}

static void blocklistEnsureLoaded(tr_blocklistFile* b)
{
    if (b->rules == NULL)
    {
        blocklistLoad(b);
    }
}

static int compareAddressToRange(void const* va, void const* vb)
{
    uint32_t const* a = va;
    struct tr_ipv4_range const* b = vb;

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
    tr_sys_path_remove(b->filename, NULL);
}

/***
****  PACKAGE-VISIBLE
***/

tr_blocklistFile* tr_blocklistFileNew(char const* filename, bool isEnabled)
{
    tr_blocklistFile* b;

    b = tr_new0(tr_blocklistFile, 1);
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
    return tr_sys_path_exists(b->filename, NULL);
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
    TR_ASSERT(b != NULL);

    b->isEnabled = isEnabled;
}

bool tr_blocklistFileHasAddress(tr_blocklistFile* b, tr_address const* addr)
{
    TR_ASSERT(tr_address_is_valid(addr));

    uint32_t needle;
    struct tr_ipv4_range const* range;

    if (!b->isEnabled || addr->type == TR_AF_INET6)
    {
        return false;
    }

    blocklistEnsureLoaded(b);

    if (b->rules == NULL || b->ruleCount == 0)
    {
        return false;
    }

    needle = ntohl(addr->addr.addr4.s_addr);

    range = bsearch(&needle, b->rules, b->ruleCount, sizeof(struct tr_ipv4_range), compareAddressToRange);

    return range != NULL;
}

/*
 * P2P plaintext format: "comment:x.x.x.x-y.y.y.y"
 * http://wiki.phoenixlabs.org/wiki/P2P_Format
 * http://en.wikipedia.org/wiki/PeerGuardian#P2P_plaintext_format
 */
static bool parseLine1(char const* line, struct tr_ipv4_range* range)
{
    char* walk;
    int b[4];
    int e[4];
    char str[64];
    tr_address addr;

    walk = strrchr(line, ':');

    if (walk == NULL)
    {
        return false;
    }

    ++walk; /* walk past the colon */

    if (sscanf(walk, "%d.%d.%d.%d-%d.%d.%d.%d", &b[0], &b[1], &b[2], &b[3], &e[0], &e[1], &e[2], &e[3]) != 8)
    {
        return false;
    }

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);

    if (!tr_address_from_string(&addr, str))
    {
        return false;
    }

    range->begin = ntohl(addr.addr.addr4.s_addr);

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", e[0], e[1], e[2], e[3]);

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
    int unk;
    int a[4];
    int b[4];
    char str[32];
    tr_address addr;

    if (sscanf(line, "%3d.%3d.%3d.%3d - %3d.%3d.%3d.%3d , %3d , ", &a[0], &a[1], &a[2], &a[3], &b[0], &b[1], &b[2], &b[3],
        &unk) != 9)
    {
        return false;
    }

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);

    if (!tr_address_from_string(&addr, str))
    {
        return false;
    }

    range->begin = ntohl(addr.addr.addr4.s_addr);

    tr_snprintf(str, sizeof(str), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);

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
    unsigned int pflen;
    uint32_t ip_u;
    uint32_t mask = 0xffffffff;

    if (sscanf(line, "%u.%u.%u.%u/%u", &ip[0], &ip[1], &ip[2], &ip[3], &pflen) != 5)
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
    struct tr_ipv4_range const* a = va;
    struct tr_ipv4_range const* b = vb;

    if (a->begin != b->begin)
    {
        return a->begin < b->begin ? -1 : 1;
    }

    return 0;
}

int tr_blocklistFileSetContent(tr_blocklistFile* b, char const* filename)
{
    tr_sys_file_t in;
    tr_sys_file_t out;
    int inCount = 0;
    char line[2048];
    char const* err_fmt = _("Couldn't read \"%1$s\": %2$s");
    struct tr_ipv4_range* ranges = NULL;
    size_t ranges_alloc = 0;
    size_t ranges_count = 0;
    tr_error* error = NULL;

    if (filename == NULL)
    {
        blocklistDelete(b);
        return 0;
    }

    in = tr_sys_file_open(filename, TR_SYS_FILE_READ, 0, &error);

    if (in == TR_BAD_SYS_FILE)
    {
        tr_logAddError(err_fmt, filename, error->message);
        tr_error_free(error);
        return 0;
    }

    blocklistClose(b);

    out = tr_sys_file_open(b->filename, TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE | TR_SYS_FILE_TRUNCATE, 0666, &error);

    if (out == TR_BAD_SYS_FILE)
    {
        tr_logAddError(err_fmt, b->filename, error->message);
        tr_error_free(error);
        tr_sys_file_close(in, NULL);
        return 0;
    }

    /* load the rules into memory */
    while (tr_sys_file_read_line(in, line, sizeof(line), NULL))
    {
        struct tr_ipv4_range range;

        ++inCount;

        if (!parseLine(line, &range))
        {
            /* don't try to display the actual lines - it causes issues */
            tr_logAddError(_("blocklist skipped invalid address at line %d"), inCount);
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

    if (!tr_sys_file_write(out, ranges, sizeof(struct tr_ipv4_range) * ranges_count, NULL, &error))
    {
        tr_logAddError(_("Couldn't save file \"%1$s\": %2$s"), b->filename, error->message);
        tr_error_free(error);
    }
    else
    {
        char* base = tr_sys_path_basename(b->filename, NULL);
        tr_logAddInfo(_("Blocklist \"%s\" updated with %zu entries"), base, ranges_count);
        tr_free(base);
    }

    tr_free(ranges);
    tr_sys_file_close(out, NULL);
    tr_sys_file_close(in, NULL);

    blocklistLoad(b);

    return ranges_count;
}
