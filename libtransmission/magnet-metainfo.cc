/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstring>
#include <string>
#include <string_view>

#include "transmission.h"

#include "crypto-utils.h"
#include "error-types.h"
#include "magnet-metainfo.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"
#include "web-utils.h"

using namespace std::literals;

/* this base32 code converted from code by Robert Kaye and Gordon Mohr
 * and is public domain. see http://bitzi.com/publicdomain for more info */
namespace bitzi
{

int constexpr base32Lookup[] = {
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

void base32_to_sha1(uint8_t* out, char const* in, size_t const inlen)
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

} // namespace bitzi

/***
****
***/

std::string tr_magnet_metainfo::magnet() const
{
    auto s = std::string{};

    s += "magnet:?xt=urn:btih:"sv;
    s += infoHashString();

    if (!std::empty(name))
    {
        s += "&dn="sv;
        tr_http_escape(s, name, true);
    }

    for (auto const& it : trackers)
    {
        s += "&tr="sv;
        tr_http_escape(s, tr_quark_get_string_view(it.second.announce_url), true);
    }

    for (auto const& webseed : webseed_urls)
    {
        s += "&ws="sv;
        tr_http_escape(s, webseed, true);
    }

    return s;
}

bool tr_magnet_metainfo::addTracker(tr_tracker_tier_t tier, std::string_view announce_sv)
{
    announce_sv = tr_strvStrip(announce_sv);

    if (!tr_urlIsValidTracker(announce_sv))
    {
        return false;
    }

    auto buf = std::string{};
    auto const announce_url = tr_quark_new(announce_sv);
    auto const scrape_url = convertAnnounceToScrape(buf, announce_sv) ? tr_quark_new(buf) : TR_KEY_NONE;
    this->trackers.insert({ tier, { announce_url, scrape_url, tier } });
    return true;
}

bool tr_magnet_metainfo::parseMagnet(std::string_view magnet_link, tr_error** error)
{
    auto const parsed = tr_urlParse(magnet_link);
    if (!parsed || parsed->scheme != "magnet"sv)
    {
        tr_error_set(error, TR_ERROR_EINVAL, "Error parsing URL");
        return false;
    }

    bool got_checksum = false;
    auto tier = tr_tracker_tier_t{ 0 };
    for (auto const& [key, value] : tr_url_query_view{ parsed->query })
    {
        if (key == "dn"sv)
        {
            this->name = tr_urlPercentDecode(value);
        }
        else if (key == "tr"sv || tr_strvStartsWith(key, "tr."sv))
        {
            // "tr." explanation @ https://trac.transmissionbt.com/ticket/3341
            addTracker(tier++, tr_urlPercentDecode(value));
        }
        else if (key == "ws"sv)
        {
            auto const url = tr_urlPercentDecode(value);
            auto const url_sv = tr_strvStrip(url);
            if (tr_urlIsValid(url_sv))
            {
                this->webseed_urls.emplace_back(url_sv);
            }
        }
        else if (key == "xt"sv)
        {
            auto constexpr ValPrefix = "urn:btih:"sv;
            if (tr_strvStartsWith(value, ValPrefix))
            {
                auto const hash = value.substr(std::size(ValPrefix));
                switch (std::size(hash))
                {
                case 40:
                    tr_hex_to_sha1(std::data(this->info_hash), std::data(hash));
                    got_checksum = true;
                    break;

                case 32:
                    bitzi::base32_to_sha1(
                        reinterpret_cast<uint8_t*>(std::data(this->info_hash)),
                        std::data(hash),
                        std::size(hash));
                    got_checksum = true;
                    break;

                default:
                    break;
                }
            }
        }
    }

    return got_checksum;
}

bool tr_magnet_metainfo::convertAnnounceToScrape(std::string& out, std::string_view in)
{
    // To derive the scrape URL use the following steps:
    // Begin with the announce URL. Find the last '/' in it.
    // If the text immediately following that '/' isn't 'announce'
    // it will be taken as a sign that that tracker doesn't support
    // the scrape convention. If it does, substitute 'scrape' for
    // 'announce' to find the scrape page.
    auto constexpr oldval = "/announce"sv;
    auto pos = in.rfind(oldval.front());
    if (pos != in.npos && in.find(oldval, pos) == pos)
    {
        auto const prefix = in.substr(0, pos);
        auto const suffix = in.substr(pos + std::size(oldval));
        tr_buildBuf(out, prefix, "/scrape"sv, suffix);
        return true;
    }

    // some torrents with UDP announce URLs don't have /announce
    if (tr_strvStartsWith(in, "udp:"sv))
    {
        out = in;
        return true;
    }

    return false;
}

void tr_magnet_metainfo::toVariant(tr_variant* top) const
{
    tr_variantInitDict(top, 4);

    // announce list
    auto n = std::size(this->trackers);
    if (n == 1)
    {
        tr_variantDictAddStr(top, TR_KEY_announce, tr_quark_get_string_view(std::begin(this->trackers)->second.announce_url));
    }
    else
    {
        auto* list = tr_variantDictAddList(top, TR_KEY_announce_list, n);
        for (auto const& pair : this->trackers)
        {
            tr_variantListAddStr(tr_variantListAddList(list, 1), tr_quark_get_string_view(pair.second.announce_url));
        }
    }

    // webseeds
    n = std::size(this->webseed_urls);
    if (n != 0)
    {
        tr_variant* list = tr_variantDictAddList(top, TR_KEY_url_list, n);

        for (auto& url : this->webseed_urls)
        {
            tr_variantListAddStr(list, url);
        }
    }

    // nonstandard keys
    auto* const d = tr_variantDictAddDict(top, TR_KEY_magnet_info, 2);

    tr_variantDictAddRaw(d, TR_KEY_info_hash, std::data(this->info_hash), std::size(this->info_hash));

    if (!std::empty(this->name))
    {
        tr_variantDictAddStr(d, TR_KEY_display_name, this->name);
    }
}
