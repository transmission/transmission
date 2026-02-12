// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

/* thanks amc1! */

#include <algorithm>
#include <array>
#include <cctype> /* isprint() */
#include <cstdint> // uint8_t
#include <optional>
#include <ranges>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "lib/base/string-utils.h"

#include "lib/transmission/clients.h"
#include "lib/transmission/types.h"

using namespace std::literals;

namespace
{

template<typename T>
constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, T const& value)
{
    if (buflen == 0)
    {
        return { buf, buflen };
    }

    auto const [out, len] = fmt::format_to_n(buf, buflen, "{}", value);
    auto* const end = buf + std::min(buflen - 1, static_cast<size_t>(out - buf));
    *end = '\0';
    return { end, buflen - (end - buf) };
}

template<typename T, typename... ArgTypes>
constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, T t, ArgTypes... args)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, t);
    return buf_append(buf, buflen, args...);
}

constexpr std::string_view base62str(uint8_t chr)
{
    // clang-format off: 90 characters per column
    auto constexpr Strings = std::array<std::string_view, 256>{
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "0"sv,  "1"sv,
         "2"sv,  "3"sv,  "4"sv,  "5"sv,  "6"sv,  "7"sv,  "8"sv,  "9"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv, "10"sv, "11"sv, "12"sv, "13"sv, "14"sv,
        "15"sv, "16"sv, "17"sv, "18"sv, "19"sv, "20"sv, "21"sv, "22"sv, "23"sv, "24"sv,
        "25"sv, "26"sv, "27"sv, "28"sv, "29"sv, "30"sv, "31"sv, "32"sv, "33"sv, "34"sv,
        "35"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv, "36"sv, "37"sv, "38"sv,
        "39"sv, "40"sv, "41"sv, "42"sv, "43"sv, "44"sv, "45"sv, "46"sv, "47"sv, "48"sv,
        "49"sv, "50"sv, "51"sv, "52"sv, "53"sv, "54"sv, "55"sv, "56"sv, "57"sv, "58"sv,
        "59"sv, "60"sv, "61"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,
         "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv,  "x"sv
    } ;
    // clang-format on

    return Strings[chr];
}

int strint(char const* pch, int span, int base = 10)
{
    auto sv = std::string_view{ pch, static_cast<size_t>(span) };
    return tr_num_parse<int>(sv, nullptr, base).value_or(0);
}

constexpr std::string_view utSuffix(uint8_t ch)
{
    switch (ch)
    {
    case 'b':
    case 'B':
        return " (Beta)"sv;

    case 'd':
        return " (Debug)"sv;

    case 'x':
    case 'X':
    case 'Z':
        return " (Dev)"sv;

    default:
        return ""sv;
    }
}

void two_major_two_minor_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', strint(&id[3], 2), '.');
    *fmt::format_to_n(buf, buflen - 1, "{:02d}", strint(&id[5], 2)).out = '\0';
}

// Shad0w with his experimental BitTorrent implementation and BitTornado
// introduced peer ids that begin with a character which is``T`` in the
// case of BitTornado followed by up to five ascii characters for version
// number, padded with dashes if less than 5, followed by ---. The ascii
// characters denoting version are limited to the following characters:
// 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-
// For example: 'S58B-----'... for Shadow's 5.8.11
std::optional<int> get_shad0w_int(char ch)
{
    auto constexpr Str = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-"sv;

    if (auto const pos = Str.find(ch); pos != std::string_view::npos)
    {
        return pos;
    }

    return {};
}

bool decodeShad0wClient(char* buf, size_t buflen, std::string_view in)
{
    auto const* const buf_in = buf;

    auto peer_id = std::string_view{ std::data(in), 9 };

    if (std::size(peer_id) != 9 || peer_id[6] != '-' || peer_id[7] != '-' || peer_id[8] != '-')
    {
        return false;
    }
    while (!std::empty(peer_id) && peer_id.back() == '-')
    {
        peer_id.remove_suffix(1);
    }
    auto vals = std::vector<int>{};
    while (std::size(peer_id) > 1)
    {
        auto const num = get_shad0w_int(peer_id.back());
        if (!num)
        {
            return false;
        }
        vals.push_back(*num);
        peer_id.remove_suffix(1);
    }

    auto name = std::string_view{};
    switch (peer_id.front())
    {
    case 'A':
        name = "ABC"sv;
        break;
    case 'O':
        name = "Osprey"sv;
        break;
    case 'Q':
        name = "BTQueue"sv;
        break;
    case 'R':
        name = "Tribler"sv;
        break;
    case 'S':
        name = "Shad0w"sv;
        break;
    case 'T':
        name = "BitTornado"sv;
        break;
    case 'U':
        name = "UPnP NAT Bit Torrent"sv;
        break;
    default:
        return false;
    }

    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ');
    for (auto const num : vals | std::views::reverse)
    {
        std::tie(buf, buflen) = buf_append(buf, buflen, num, '.');
    }
    if (buf > buf_in)
    {
        buf[-1] = '\0'; // remove trailing '.'
    }
    return true;
}

bool decodeBitCometClient(char* buf, size_t buflen, std::string_view peer_id)
{
    // BitComet produces peer ids that consists of four ASCII characters exbc,
    // followed by two bytes x and y, followed by random characters. The version
    // number is x in decimal before the decimal point and y as two decimal
    // digits after the decimal point. BitLord uses the same scheme, but adds
    // LORD after the version bytes. An unofficial patch for BitComet once
    // replaced exbc with FUTB. The encoding for BitComet Peer IDs changed
    // to Azureus-style as of BitComet version 0.59.
    auto mod = std::string_view{};
    if (auto const lead = std::string_view{ std::data(peer_id), std::min(std::size(peer_id), size_t{ 4 }) }; lead == "exbc")
    {
        mod = ""sv;
    }
    else if (lead == "FUTB")
    {
        mod = "(Solidox Mod) "sv;
    }
    else if (lead == "xUTB"sv)
    {
        mod = "(Mod 2) "sv;
    }
    else
    {
        return false;
    }

    bool const is_bitlord = std::string_view(std::data(peer_id) + 6, 4) == "LORD"sv;
    auto const name = is_bitlord ? "BitLord"sv : "BitComet"sv;
    int const major = uint8_t(peer_id[4]);
    int const minor = uint8_t(peer_id[5]);

    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', mod, major, '.');
    *fmt::format_to_n(buf, buflen - 1, "{:02d}", minor).out = '\0';
    return true;
}

using format_func = void (*)(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id);

constexpr void three_digit_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', base62str(id[4]), '.', base62str(id[5]));
}

constexpr void four_digit_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', base62str(id[4]), '.', base62str(id[5]), '.', base62str(id[6]));
}

void no_version_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t /*id*/)
{
    buf_append(buf, buflen, name);
}

// specific clients

constexpr void amazon_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[5], '.', id[7]);
}

constexpr void aria2_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[4] == '-' && id[6] == '-' && id[8] == '-')
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[5], '.', id[7]);
    }
    else if (id[4] == '-' && id[7] == '-' && id[9] == '-')
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[5], id[6], '.', id[8]);
    }
    else
    {
        buf_append(buf, buflen, name);
    }
}

constexpr void bitbuddy_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], id[5], id[6]);
}

constexpr void bitlord_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], '-', std::string_view(&id[6], 3));
}

constexpr void bitrocket_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], ' ', '(', id[5], id[6], ')');
}

void bittorrent_dna_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ');
    *fmt::format_to_n(buf, buflen - 1, "{:d}.{:d}.{:d}", strint(&id[3], 2), strint(&id[5], 2), strint(&id[7], 2)).out = '\0';
}

void bits_on_wheels_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // Bits on Wheels uses the pattern -BOWxxx-yyyyyyyyyyyy, where y is random
    // (uppercase letters) and x depends on the version.
    // Version 1.0.6 has xxx = A0C.

    if (std::equal(&id[4], &id[7], "A0B"))
    {
        buf_append(buf, buflen, name, " 1.0.5"sv);
    }
    else if (std::equal(&id[4], &id[7], "A0C"))
    {
        buf_append(buf, buflen, name, " 1.0.6"sv);
    }
    else
    {
        buf_append(buf, buflen, name, ' ', id[4], '.', id[5], '.', id[6]);
    }
}

constexpr void blizzard_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', int(id[3] + 1), int(id[4]));
}

constexpr void btpd_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', std::string_view(&id[5], 3));
}

constexpr void burst_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[5], '.', id[7], '.', id[9]);
}

constexpr void ctorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', base62str(id[4]), '.', id[5], id[6]);
}

constexpr void folx_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', 'x');
}

constexpr void ktorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[5] == 'D')
    {
        buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', base62str(id[4]), " Dev "sv, base62str(id[6]));
    }
    else if (id[5] == 'R')
    {
        buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', base62str(id[4]), " RC "sv, base62str(id[6]));
    }
    else
    {
        three_digit_formatter(buf, buflen, name, id);
    }
}

constexpr void mainline_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // Queen Bee uses Bram`s new style:
    // Q1-0-0-- or Q1-10-0- followed by random bytes.

    if (id[4] == '-' && id[6] == '-') // Mx-y-z--
    {
        buf_append(buf, buflen, name, ' ', id[1], '.', id[3], '.', id[5]);
    }
    else if (id[5] == '-') // Mx-yy-z-
    {
        buf_append(buf, buflen, name, ' ', id[1], '.', id[3], id[4], '.', id[6]);
    }
    else
    {
        buf_append(buf, buflen, name);
    }
}

constexpr void mediaget_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', base62str(id[4]));
}

constexpr void mldonkey_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // MLdonkey use the following peer_id scheme: the first characters are
    // -ML followed by a dotted version then a - followed by randomness.
    // e.g. -ML2.7.2-kgjjfkd
    buf_append(buf, buflen, name, ' ', std::string_view(&id[3], 5));
}

constexpr void opera_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    // Opera 8 previews and Opera 9.x releases use the following peer_id
    // scheme: The first two characters are OP and the next four digits equal
    // the build number. All following characters are random lowercase
    // hexadecimal digits.
    buf_append(buf, buflen, name, ' ', std::string_view(&id[2], 4));
}

constexpr void picotorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', id[4], id[5], '.', base62str(id[6]));
}

constexpr void plus_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[4], '.', id[5], id[6]);
}

constexpr void qvod_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', base62str(id[4]), '.', base62str(id[5]), '.', base62str(id[6]), '.', base62str(id[7]));
}

void transmission_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ');

    if (std::equal(&id[3], &id[6], "000")) // very old client style: -TR0006- is 0.6
    {
        *fmt::format_to_n(buf, buflen - 1, "0.{:c}", id[6]).out = '\0';
    }
    else if (std::equal(&id[3], &id[5], "00")) // pre-1.0 style: -TR0072- is 0.72
    {
        *fmt::format_to_n(buf, buflen - 1, "0.{:02d}", strint(&id[5], 2)).out = '\0';
    }
    else if (id[3] <= '3') // style up through 3.00: -TR111Z- is 1.11+
    {
        *fmt::format_to_n(buf, buflen - 1, "{:s}.{:02d}{:s}", base62str(id[3]), strint(&id[4], 2), utSuffix(id[6])).out = '\0';
    }
    else // -TR400X- is 4.0.0 (Beta)"
    {
        buf_append(buf, buflen, base62str(id[3]), '.', base62str(id[4]), '.', base62str(id[5]), utSuffix(id[6]));
    }
}

void utorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[7] == '-')
    {
        buf_append(
            buf,
            buflen,
            name,
            ' ',
            strint(&id[3], 1, 16),
            '.',
            strint(&id[4], 1, 16),
            '.',
            strint(&id[5], 1, 16),
            utSuffix(id[6]));
    }
    else // uTorrent replaces the trailing dash with an extra digit for longer version numbers
    {
        buf_append(
            buf,
            buflen,
            name,
            ' ',
            strint(&id[3], 1, 16),
            '.',
            strint(&id[4], 1, 16),
            '.',
            strint(&id[5], 2, 10),
            utSuffix(id[7]));
    }
}

constexpr void xbt_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], utSuffix(id[6]));
}

constexpr void xfplay_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    if (id[6] == '0')
    {
        three_digit_formatter(buf, buflen, name, id);
    }
    else
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], id[6]);
    }
}

void xtorrent_formatter(char* buf, size_t buflen, std::string_view name, tr_peer_id_t id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', base62str(id[3]), '.', base62str(id[4]), " ("sv);
    *fmt::format_to_n(buf, buflen - 1, "{:d}", strint(&id[5], 2)).out = '\0';
}

struct Client
{
    std::string_view begins_with;
    std::string_view name;
    format_func formatter;
};

auto constexpr Clients = std::array<Client, 131>{ {
    { .begins_with = "-AD", .name = "Advanced Download Manager", .formatter = three_digit_formatter },
    { .begins_with = "-AG", .name = "Ares", .formatter = four_digit_formatter },
    { .begins_with = "-AR", .name = "Arctic", .formatter = four_digit_formatter },
    { .begins_with = "-AT", .name = "Artemis", .formatter = four_digit_formatter },
    { .begins_with = "-AV", .name = "Avicora", .formatter = four_digit_formatter },
    { .begins_with = "-AX", .name = "BitPump", .formatter = two_major_two_minor_formatter },
    { .begins_with = "-AZ", .name = "Azureus / Vuze", .formatter = four_digit_formatter },
    { .begins_with = "-A~", .name = "Ares", .formatter = three_digit_formatter },
    { .begins_with = "-BB", .name = "BitBuddy", .formatter = bitbuddy_formatter },
    { .begins_with = "-BC", .name = "BitComet", .formatter = two_major_two_minor_formatter },
    { .begins_with = "-BE", .name = "BitTorrent SDK", .formatter = four_digit_formatter },
    { .begins_with = "-BF", .name = "BitFlu", .formatter = no_version_formatter },
    { .begins_with = "-BG", .name = "BTGetit", .formatter = four_digit_formatter },
    { .begins_with = "-BH", .name = "BitZilla", .formatter = four_digit_formatter },
    { .begins_with = "-BI", .name = "BiglyBT", .formatter = four_digit_formatter },
    { .begins_with = "-BL", .name = "BitLord", .formatter = bitlord_formatter },
    { .begins_with = "-BM", .name = "BitMagnet", .formatter = four_digit_formatter },
    { .begins_with = "-BN", .name = "Baidu Netdisk", .formatter = no_version_formatter },
    { .begins_with = "-BOW", .name = "Bits on Wheels", .formatter = bits_on_wheels_formatter },
    { .begins_with = "-BP", .name = "BitTorrent Pro (Azureus + Spyware)", .formatter = four_digit_formatter },
    { .begins_with = "-BR", .name = "BitRocket", .formatter = bitrocket_formatter },
    { .begins_with = "-BS", .name = "BTSlave", .formatter = four_digit_formatter },
    { .begins_with = "-BT", .name = "BitTorrent", .formatter = utorrent_formatter },
    { .begins_with = "-BW", .name = "BitTorrent Web", .formatter = utorrent_formatter },
    { .begins_with = "-BX", .name = "BittorrentX", .formatter = four_digit_formatter },
    { .begins_with = "-CD", .name = "Enhanced CTorrent", .formatter = two_major_two_minor_formatter },
    { .begins_with = "-CT", .name = "CTorrent", .formatter = ctorrent_formatter },
    { .begins_with = "-DE", .name = "Deluge", .formatter = four_digit_formatter },
    { .begins_with = "-DP", .name = "Propagate Data Client", .formatter = four_digit_formatter },
    { .begins_with = "-EB", .name = "EBit", .formatter = four_digit_formatter },
    { .begins_with = "-ES", .name = "Electric Sheep", .formatter = three_digit_formatter },
    { .begins_with = "-FC", .name = "FileCroc", .formatter = four_digit_formatter },
    { .begins_with = "-FD", .name = "Free Download Manager", .formatter = three_digit_formatter },
    { .begins_with = "-FG", .name = "FlashGet", .formatter = two_major_two_minor_formatter },
    { .begins_with = "-FL", .name = "Folx", .formatter = folx_formatter },
    { .begins_with = "-FT", .name = "FoxTorrent/RedSwoosh", .formatter = four_digit_formatter },
    { .begins_with = "-FW", .name = "FrostWire", .formatter = three_digit_formatter },
    { .begins_with = "-FX", .name = "Freebox", .formatter = four_digit_formatter },
    { .begins_with = "-G3", .name = "G3 Torrent", .formatter = no_version_formatter },
    { .begins_with = "-GR", .name = "GetRight", .formatter = four_digit_formatter },
    { .begins_with = "-GS", .name = "GSTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-HK", .name = "Hekate", .formatter = four_digit_formatter },
    { .begins_with = "-HL", .name = "Halite", .formatter = three_digit_formatter },
    { .begins_with = "-HN", .name = "Hydranode", .formatter = four_digit_formatter },
    { .begins_with = "-KG", .name = "KGet", .formatter = four_digit_formatter },
    { .begins_with = "-KT", .name = "KTorrent", .formatter = ktorrent_formatter },
    { .begins_with = "-LC", .name = "LeechCraft", .formatter = four_digit_formatter },
    { .begins_with = "-LH", .name = "LH-ABC", .formatter = four_digit_formatter },
    { .begins_with = "-LP", .name = "Lphant", .formatter = two_major_two_minor_formatter },
    { .begins_with = "-LT", .name = "libtorrent (Rasterbar)", .formatter = three_digit_formatter },
    { .begins_with = "-LW", .name = "LimeWire", .formatter = no_version_formatter },
    { .begins_with = "-Lr", .name = "LibreTorrent", .formatter = three_digit_formatter },
    { .begins_with = "-MG", .name = "MediaGet", .formatter = mediaget_formatter },
    { .begins_with = "-MK", .name = "Meerkat", .formatter = four_digit_formatter },
    { .begins_with = "-ML", .name = "MLDonkey", .formatter = mldonkey_formatter },
    { .begins_with = "-MO", .name = "MonoTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-MP", .name = "MooPolice", .formatter = three_digit_formatter },
    { .begins_with = "-MR", .name = "Miro", .formatter = four_digit_formatter },
    { .begins_with = "-MT", .name = "Moonlight", .formatter = four_digit_formatter },
    { .begins_with = "-NE", .name = "BT Next Evolution", .formatter = four_digit_formatter },
    { .begins_with = "-NX", .name = "Net Transport", .formatter = four_digit_formatter },
    { .begins_with = "-OS", .name = "OneSwarm", .formatter = four_digit_formatter },
    { .begins_with = "-OT", .name = "OmegaTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-PD", .name = "Pando", .formatter = four_digit_formatter },
    { .begins_with = "-PI", .name = "PicoTorrent", .formatter = picotorrent_formatter },
    { .begins_with = "-QD", .name = "QQDownload", .formatter = four_digit_formatter },
    { .begins_with = "-QT", .name = "QT 4 Torrent example", .formatter = four_digit_formatter },
    { .begins_with = "-RS", .name = "Rufus", .formatter = four_digit_formatter },
    { .begins_with = "-RT", .name = "Retriever", .formatter = four_digit_formatter },
    { .begins_with = "-RZ", .name = "RezTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-SB", .name = "~Swiftbit", .formatter = four_digit_formatter },
    { .begins_with = "-SD", .name = "Thunder", .formatter = four_digit_formatter },
    { .begins_with = "-SM", .name = "SoMud", .formatter = four_digit_formatter },
    { .begins_with = "-SP", .name = "BitSpirit", .formatter = three_digit_formatter },
    { .begins_with = "-SS", .name = "SwarmScope", .formatter = four_digit_formatter },
    { .begins_with = "-ST", .name = "SymTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-SZ", .name = "Shareaza", .formatter = four_digit_formatter },
    { .begins_with = "-S~", .name = "Shareaza", .formatter = four_digit_formatter },
    { .begins_with = "-TB", .name = "Torch Browser", .formatter = no_version_formatter },
    { .begins_with = "-TN", .name = "Torrent .NET", .formatter = four_digit_formatter },
    { .begins_with = "-TR", .name = "Transmission", .formatter = transmission_formatter },
    { .begins_with = "-TS", .name = "Torrentstorm", .formatter = four_digit_formatter },
    { .begins_with = "-TT", .name = "TuoTu", .formatter = four_digit_formatter },
    { .begins_with = "-UE", .name = "\xc2\xb5Torrent Embedded", .formatter = utorrent_formatter },
    { .begins_with = "-UL", .name = "uLeecher!", .formatter = four_digit_formatter },
    { .begins_with = "-UM", .name = "\xc2\xb5Torrent Mac", .formatter = utorrent_formatter },
    { .begins_with = "-UT", .name = "\xc2\xb5Torrent", .formatter = utorrent_formatter },
    { .begins_with = "-UW", .name = "\xc2\xb5Torrent Web", .formatter = utorrent_formatter },
    { .begins_with = "-VG", .name = "Vagaa", .formatter = four_digit_formatter },
    { .begins_with = "-WS", .name = "HTTP Seed", .formatter = no_version_formatter },
    { .begins_with = "-WT", .name = "BitLet", .formatter = four_digit_formatter },
    { .begins_with = "-WT-", .name = "BitLet", .formatter = no_version_formatter },
    { .begins_with = "-WW", .name = "WebTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-WY", .name = "FireTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-XC", .name = "Xtorrent", .formatter = xtorrent_formatter },
    { .begins_with = "-XF", .name = "Xfplay", .formatter = xfplay_formatter },
    { .begins_with = "-XL", .name = "Xunlei", .formatter = four_digit_formatter },
    { .begins_with = "-XS", .name = "XSwifter", .formatter = four_digit_formatter },
    { .begins_with = "-XT", .name = "XanTorrent", .formatter = four_digit_formatter },
    { .begins_with = "-XX", .name = "Xtorrent", .formatter = xtorrent_formatter },
    { .begins_with = "-ZO", .name = "Zona", .formatter = four_digit_formatter },
    { .begins_with = "-ZT", .name = "Zip Torrent", .formatter = four_digit_formatter },
    { .begins_with = "-bk", .name = "BitKitten (libtorrent)", .formatter = four_digit_formatter },
    { .begins_with = "-lt", .name = "libTorrent (Rakshasa)", .formatter = three_digit_formatter },
    { .begins_with = "-pb", .name = "pbTorrent", .formatter = three_digit_formatter },
    { .begins_with = "-qB", .name = "qBittorrent", .formatter = three_digit_formatter },
    { .begins_with = "-st", .name = "SharkTorrent", .formatter = four_digit_formatter },
    { .begins_with = "10-------", .name = "JVtorrent", .formatter = no_version_formatter },
    { .begins_with = "346-", .name = "TorrentTopia", .formatter = no_version_formatter },
    { .begins_with = "A2", .name = "aria2", .formatter = aria2_formatter },
    { .begins_with = "AZ2500BT", .name = "BitTyrant (Azureus Mod)", .formatter = no_version_formatter },
    { .begins_with = "BLZ", .name = "Blizzard Downloader", .formatter = blizzard_formatter },
    { .begins_with = "DNA", .name = "BitTorrent DNA", .formatter = bittorrent_dna_formatter },
    { .begins_with = "FD6", .name = "Free Download Manager 6", .formatter = no_version_formatter },
    { .begins_with = "LIME", .name = "Limewire", .formatter = no_version_formatter },
    { .begins_with = "M", .name = "BitTorrent", .formatter = mainline_formatter },
    { .begins_with = "Mbrst", .name = "burst!", .formatter = burst_formatter },
    { .begins_with = "OP", .name = "Opera", .formatter = opera_formatter },
    { .begins_with = "Pando", .name = "Pando", .formatter = no_version_formatter },
    { .begins_with = "Plus", .name = "Plus!", .formatter = plus_formatter },
    { .begins_with = "Q", .name = "Queen Bee", .formatter = mainline_formatter },
    { .begins_with = "QVOD", .name = "QVOD", .formatter = qvod_formatter },
    { .begins_with = "S3", .name = "Amazon S3", .formatter = amazon_formatter },
    { .begins_with = "TIX", .name = "Tixati", .formatter = two_major_two_minor_formatter },
    { .begins_with = "XBT", .name = "XBT Client", .formatter = xbt_formatter },
    { .begins_with = "a00---0", .name = "Swarmy", .formatter = no_version_formatter },
    { .begins_with = "a02---0", .name = "Swarmy", .formatter = no_version_formatter },
    { .begins_with = "aria2-", .name = "aria2", .formatter = no_version_formatter },
    { .begins_with = "btpd", .name = "BT Protocol Daemon", .formatter = btpd_formatter },
    { .begins_with = "eX", .name = "eXeem", .formatter = no_version_formatter },
    { .begins_with = "martini", .name = "Martini Man", .formatter = no_version_formatter },
} };

} // namespace

void tr_clientForId(char* buf, size_t buflen, tr_peer_id_t peer_id)
{
    *buf = '\0';

    auto const key = std::string_view{ std::data(peer_id), std::size(peer_id) };

    if (decodeShad0wClient(buf, buflen, key) || decodeBitCometClient(buf, buflen, key))
    {
        return;
    }

    if (peer_id[0] == '\0' && peer_id[2] == 'B' && peer_id[3] == 'S')
    {
        *fmt::format_to_n(buf, buflen - 1, "BitSpirit {:d}", peer_id[1] == '\0' ? 1 : int(peer_id[1])).out = '\0';
        return;
    }

    struct Compare
    {
        bool operator()(std::string_view const& key, Client const& client) const
        {
            return key.substr(0, std::min(std::size(key), std::size(client.begins_with))) < client.begins_with;
        }
        bool operator()(Client const& client, std::string_view const& key) const
        {
            return client.begins_with < key.substr(0, std::min(std::size(key), std::size(client.begins_with)));
        }
    };

    if (auto const [eq_begin, eq_end] = std::equal_range(std::begin(Clients), std::end(Clients), key, Compare{});
        eq_begin != std::end(Clients) && eq_begin != eq_end)
    {
        eq_begin->formatter(buf, buflen, eq_begin->name, peer_id);
        return;
    }

    // no match
    if (*buf == '\0')
    {
        auto out = std::array<char, 32>{};
        char* walk = std::data(out);
        char const* const begin = walk;
        char const* const end = begin + std::size(out);

        for (size_t i = 0; i < 8; ++i)
        {
            char const c = peer_id[i];

            if (isprint((unsigned char)c) != 0)
            {
                *walk++ = c;
            }
            else
            {
                walk = fmt::format_to_n(walk, end - walk - 1, "%{:02X}", static_cast<unsigned char>(c)).out;
            }
        }

        buf_append(buf, buflen, std::string_view(begin, walk - begin));
    }
}
