/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstring> /* strchr() */
#include <cstdio> /* sscanf() */

#include "transmission.h"
#include "crypto-utils.h" /* tr_hex_to_sha1() */
#include "magnet.h"
#include "tr-assert.h"
#include "utils.h"
#include "utils.h"
#include "variant.h"
#include "web-utils.h"

using namespace std::literals;

/***
****
***/

/* this base32 code converted from code by Robert Kaye and Gordon Mohr
 * and is public domain. see http://bitzi.com/publicdomain for more info */

static int constexpr base32Lookup[] = {
    0xFF, 0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, /* '0', '1', '2', '3', '4', '5', '6', '7' */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* '8', '9', ':', ';', '<', '=', '>', '?' */
    0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* '@', 'A', 'B', 'C', 'D', 'E', 'F', 'G' */
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, /* 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O' */
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, /* 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W' */
    0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 'X', 'Y', 'Z', '[', '\', ']', '^', '_' */
    0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g' */
    0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, /* 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o' */
    0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, /* 'p', 'q', 'r', 's', 't', 'u', 'v', 'w' */
    0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF /* 'x', 'y', 'z', '{', '|', '}', '~', 'DEL' */
};

static void base32_to_sha1(uint8_t* out, char const* in, size_t const inlen)
{
    TR_ASSERT(inlen == 32);

    size_t const outlen = 20;

    memset(out, 0, 20);

    size_t index = 0;
    size_t offset = 0;
    for (size_t i = 0; i < inlen; ++i)
    {
        int lookup = in[i] - '0';

        /* Skip chars outside the lookup table */
        if (lookup < 0)
        {
            continue;
        }

        /* If this digit is not in the table, ignore it */
        int const digit = base32Lookup[lookup];

        if (digit == 0xFF)
        {
            continue;
        }

        if (index <= 3)
        {
            index = (index + 5) % 8;

            if (index == 0)
            {
                out[offset] |= digit;
                offset++;

                if (offset >= outlen)
                {
                    break;
                }
            }
            else
            {
                out[offset] |= digit << (8 - index);
            }
        }
        else
        {
            index = (index + 5) % 8;
            out[offset] |= digit >> index;
            offset++;

            if (offset >= outlen)
            {
                break;
            }

            out[offset] |= digit << (8 - index);
        }
    }
}

/***
****
***/

static auto constexpr MaxTrackers = std::size_t{ 64 };
static auto constexpr MaxWebseeds = std::size_t{ 64 };

#include <iostream>

tr_magnet_info* tr_magnetParse(std::string_view magnet_link)
{
    std::cerr << __FILE__ << ':' << __LINE__ << " magnet link [" << magnet_link << ']' << std::endl;

    auto const parsed = tr_urlParse(magnet_link);
    if (!parsed || parsed->scheme != "magnet"sv)
    {
        return nullptr;
    }

    std::cerr << __FILE__ << ':' << __LINE__ << " parsed->scheme [" << parsed->scheme << ']' << std::endl;
    std::cerr << __FILE__ << ':' << __LINE__ << " parsed->query [" << parsed->query << ']' << std::endl;

    bool got_checksum = false;
    size_t trCount = 0;
    size_t wsCount = 0;
    char* tr[MaxTrackers];
    char* ws[MaxWebseeds];
    char* displayName = nullptr;
    uint8_t sha1[SHA_DIGEST_LENGTH];

    std::cerr << __FILE__ << ':' << __LINE__ << " iterating" << std::endl;
    for (auto const [key, value] : tr_url_query_view{ parsed->query })
    {
        std::cerr << __FILE__ << ':' << __LINE__ << " key [" << key << ']' << std::endl;
        std::cerr << __FILE__ << ':' << __LINE__ << " value [" << value << ']' << std::endl;
        if (key == "dn"sv)
        {
            displayName = tr_http_unescape(std::data(value), std::size(value));
        }
        else if ((key == "tr"sv || key.find("tr.") == 0) && (trCount < MaxTrackers))
        {
            // "tr." explanation @ https://trac.transmissionbt.com/ticket/3341
            tr[trCount++] = tr_http_unescape(std::data(value), std::size(value));
        }
        else if ((key == "ws"sv) && (wsCount < MaxWebseeds))
        {
            std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
            ws[wsCount++] = tr_http_unescape(std::data(value), std::size(value));
            std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        }
        else if (key == "xt"sv)
        {
            auto constexpr ValPrefix = "urn:btih:"sv;
            if (value.find(ValPrefix) == 0)
            {
                auto const hash = value.substr(std::size(ValPrefix));
                if (std::size(hash) == 40)
                {
                    tr_hex_to_sha1(sha1, std::data(hash));
                    got_checksum = true;
                }
                else if (std::size(hash) == 32)
                {
                    base32_to_sha1(sha1, std::data(hash), std::size(hash));
                    got_checksum = true;
                }
            }
        }
    }

    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    tr_magnet_info* info = nullptr;

    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    if (got_checksum)
    {
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        info = tr_new0(tr_magnet_info, 1);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        info->displayName = displayName;
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        info->trackerCount = trCount;
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        info->trackers = static_cast<char**>(tr_memdup(tr, sizeof(char*) * trCount));
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        info->webseedCount = wsCount;
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        info->webseeds = static_cast<char**>(tr_memdup(ws, sizeof(char*) * wsCount));
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        memcpy(info->hash, sha1, sizeof(uint8_t) * SHA_DIGEST_LENGTH);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    }
    else
    {
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        for (size_t i = 0; i < trCount; i++)
        {
            std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
            tr_free(tr[i]);
            std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        }

        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        for (size_t i = 0; i < wsCount; i++)
        {
            std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
            tr_free(ws[i]);
            std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        }

        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        tr_free(displayName);
    }

    std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    return info;
}

void tr_magnetFree(tr_magnet_info* info)
{
    if (info != nullptr)
    {
        for (int i = 0; i < info->trackerCount; ++i)
        {
            tr_free(info->trackers[i]);
        }

        tr_free(info->trackers);

        for (int i = 0; i < info->webseedCount; ++i)
        {
            tr_free(info->webseeds[i]);
        }

        tr_free(info->webseeds);

        tr_free(info->displayName);
        tr_free(info);
    }
}

void tr_magnetCreateMetainfo(tr_magnet_info const* info, tr_variant* top)
{
    tr_variantInitDict(top, 4);

    /* announce list */
    if (info->trackerCount == 1)
    {
        tr_variantDictAddStr(top, TR_KEY_announce, info->trackers[0]);
    }
    else
    {
        tr_variant* trackers = tr_variantDictAddList(top, TR_KEY_announce_list, info->trackerCount);

        for (int i = 0; i < info->trackerCount; ++i)
        {
            tr_variantListAddStr(tr_variantListAddList(trackers, 1), info->trackers[i]);
        }
    }

    /* webseeds */
    if (info->webseedCount > 0)
    {
        tr_variant* urls = tr_variantDictAddList(top, TR_KEY_url_list, info->webseedCount);

        for (int i = 0; i < info->webseedCount; ++i)
        {
            tr_variantListAddStr(urls, info->webseeds[i]);
        }
    }

    /* nonstandard keys */
    auto* const d = tr_variantDictAddDict(top, TR_KEY_magnet_info, 2);
    tr_variantDictAddRaw(d, TR_KEY_info_hash, info->hash, 20);

    if (info->displayName != nullptr)
    {
        tr_variantDictAddStr(d, TR_KEY_display_name, info->displayName);
    }
}
