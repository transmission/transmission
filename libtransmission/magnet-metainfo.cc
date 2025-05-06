// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdint> // uint8_t
#include <cstring>
#include <iterator> // back_inserter
#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "libtransmission/crypto-utils.h"
#include "libtransmission/error-types.h"
#include "libtransmission/error.h"
#include "libtransmission/magnet-metainfo.h"
#include "libtransmission/tr-macros.h" // for tr_sha1_digest_t
#include "libtransmission/tr-strbuf.h" // for tr_urlbuf
#include "libtransmission/utils.h"
#include "libtransmission/web-utils.h"

using namespace std::literals;

namespace
{

auto constexpr Base32HashStrLen = size_t{ 32 };

/* this base32 code converted from code by Robert Kaye and Gordon Mohr
 * and is public domain. see http://bitzi.com/publicdomain for more info */
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
    size_t const outlen = 20;

    memset(out, 0, 20);

    size_t index = 0;
    size_t offset = 0;
    for (size_t i = 0; i < inlen; ++i)
    {
        int const lookup = in[i] - '0';

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

std::optional<tr_sha1_digest_t> parseBase32Hash(std::string_view sv)
{
    if (std::size(sv) != Base32HashStrLen)
    {
        return {};
    }

    if (!std::all_of(
            std::begin(sv),
            std::end(sv),
            [](unsigned char ch)
            { return '0' <= ch && ch < '0' + std::size(bitzi::Base32Lookup) && bitzi::Base32Lookup[ch - '0'] != 0xFF; }))
    {
        return {};
    }

    auto digest = tr_sha1_digest_t{};
    bitzi::base32_to_sha1(reinterpret_cast<uint8_t*>(std::data(digest)), std::data(sv), std::size(sv));
    return digest;
}

std::optional<tr_sha1_digest_t> parseHash(std::string_view sv)
{
    // https://www.bittorrent.org/beps/bep_0009.html
    // Is the info-hash hex encoded, for a total of 40 characters.
    // For compatibility with existing links in the wild, clients
    // should also support the 32 character base32 encoded info-hash.

    if (auto const hash = tr_sha1_from_string(sv); hash)
    {
        return hash;
    }
    if (auto const hash = parseBase32Hash(sv); hash)
    {
        return hash;
    }

    return {};
}

std::optional<tr_sha256_digest_t> parseHash2(std::string_view sv)
{
    // https://www.bittorrent.org/beps/bep_0009.html
    // Is the info-hash v2 hex encoded and tag removed, for a total of 64 characters.

    if (auto const hash = tr_sha256_from_string(sv); hash)
    {
        return hash;
    }

    return {};
}

} // namespace

// ---

std::string tr_magnet_metainfo::magnet() const
{
    auto buf = tr_urlbuf{ "magnet:?xt=urn:btih:"sv, info_hash_string() };

    if (!std::empty(name_))
    {
        buf += "&dn="sv;
        tr_urlPercentEncode(std::back_inserter(buf), name_);
    }

    for (auto const& tracker : this->announce_list())
    {
        buf += "&tr="sv;
        tr_urlPercentEncode(std::back_inserter(buf), tracker.announce.sv());
    }

    for (auto const& webseed : webseed_urls_)
    {
        buf += "&ws="sv;
        tr_urlPercentEncode(std::back_inserter(buf), webseed);
    }

    return std::string{ buf.sv() };
}

void tr_magnet_metainfo::set_name(std::string_view name)
{
    name_ = tr_strv_convert_utf8(name);
}

void tr_magnet_metainfo::add_webseed(std::string_view webseed)
{
    if (!tr_urlIsValid(webseed))
    {
        return;
    }

    auto& urls = webseed_urls_;

    if (auto const it = std::find(std::begin(urls), std::end(urls), webseed); it != std::end(urls))
    {
        return;
    }

    urls.emplace_back(webseed);
}

bool tr_magnet_metainfo::parseMagnet(std::string_view magnet_link, tr_error* error)
{
    magnet_link = tr_strv_strip(magnet_link);
    if (auto const hash = parseHash(magnet_link); hash)
    {
        return parseMagnet(fmt::format("magnet:?xt=urn:btih:{:s}", tr_sha1_to_string(*hash)));
    }

    auto const parsed = tr_urlParse(magnet_link);
    if (!parsed || parsed->scheme != "magnet"sv)
    {
        if (error != nullptr)
        {
            error->set(TR_ERROR_EINVAL, "Error parsing URL"sv);
        }

        return false;
    }

    bool got_hash = false;
    for (auto const& [key, value] : tr_url_query_view{ parsed->query })
    {
        if (key == "dn"sv)
        {
            this->set_name(tr_urlPercentDecode(value));
        }
        else if (key == "tr"sv || tr_strv_starts_with(key, "tr."sv))
        {
            // "tr." explanation @ https://trac.transmissionbt.com/ticket/3341
            this->announce_list_.add(tr_urlPercentDecode(value));
        }
        else if (key == "ws"sv)
        {
            auto const url = tr_urlPercentDecode(value);
            auto const url_sv = tr_strv_strip(url);
            if (tr_urlIsValid(url_sv))
            {
                this->webseed_urls_.emplace_back(url_sv);
            }
        }
        else if (static auto constexpr ValPrefix = "urn:btih:"sv; key == "xt"sv && tr_strv_starts_with(value, ValPrefix))
        {
            // v1 info-hash
            if (auto const hash = parseHash(value.substr(std::size(ValPrefix))); hash)
            {
                this->info_hash_ = *hash;
                got_hash = true;
            }
        }
        else if (static auto constexpr ValPrefix2 = "urn:btmh:1220"sv; key == "xt"sv && tr_strv_starts_with(value, ValPrefix2))
        {
            // v2 info-hash
            // The 1220 tag identifies the hash as sha256, removing tag before sending to parseHash2
            if (auto const hash = parseHash2(value.substr(std::size(ValPrefix2))); hash)
            {
                this->info_hash2_ = *hash;
            }
        }
    }

    info_hash_str_ = tr_sha1_to_string(this->info_hash());

    if (std::empty(name()))
    {
        this->set_name(info_hash_str_);
    }

    return got_hash;
}
