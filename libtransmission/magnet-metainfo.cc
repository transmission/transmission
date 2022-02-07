// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstring>
#include <string>
#include <string_view>

#include "transmission.h"

#include "crypto-utils.h"
#include "error.h"
#include "error-types.h"
#include "magnet-metainfo.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"
#include "web-utils.h"

using namespace std::literals;

/* this base32 code converted from code by Robert Kaye and Gordon Mohr
 * and is public domain. see http://bitzi.com/publicdomain for more info */
namespace
{
namespace bitzi
{

auto constexpr Base32Lookup = std::array<int, 80>{
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
        int const digit = Base32Lookup[lookup];

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
} // namespace

/***
****
***/

std::string tr_magnet_metainfo::magnet() const
{
    auto s = std::string{};

    s += "magnet:?xt=urn:btih:"sv;
    s += infoHashString();

    if (!std::empty(name_))
    {
        s += "&dn="sv;
        tr_http_escape(s, name_, true);
    }

    for (auto const& tracker : this->announceList())
    {
        s += "&tr="sv;
        tr_http_escape(s, tracker.announce.full, true);
    }

    for (auto const& webseed : webseed_urls_)
    {
        s += "&ws="sv;
        tr_http_escape(s, webseed, true);
    }

    return s;
}

bool tr_magnet_metainfo::parseMagnet(std::string_view magnet_link, tr_error** error)
{
    auto const parsed = tr_urlParse(magnet_link);
    if (!parsed || parsed->scheme != "magnet"sv)
    {
        tr_error_set(error, TR_ERROR_EINVAL, "Error parsing URL"sv);
        return false;
    }

    bool got_checksum = false;
    for (auto const& [key, value] : tr_url_query_view{ parsed->query })
    {
        if (key == "dn"sv)
        {
            this->setName(tr_urlPercentDecode(value));
        }
        else if (key == "tr"sv || tr_strvStartsWith(key, "tr."sv))
        {
            // "tr." explanation @ https://trac.transmissionbt.com/ticket/3341
            this->announce_list_.add(tr_urlPercentDecode(value));
        }
        else if (key == "ws"sv)
        {
            auto const url = tr_urlPercentDecode(value);
            auto const url_sv = tr_strvStrip(url);
            if (tr_urlIsValid(url_sv))
            {
                this->webseed_urls_.emplace_back(url_sv);
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
                case TR_SHA1_DIGEST_STRLEN:
                    this->info_hash_ = tr_sha1_from_string(hash);
                    got_checksum = true;
                    break;

                case 32:
                    bitzi::base32_to_sha1(
                        reinterpret_cast<uint8_t*>(std::data(this->info_hash_)),
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

    info_hash_str_ = tr_sha1_to_string(this->infoHash());

    return got_checksum;
}
