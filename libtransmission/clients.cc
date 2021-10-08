/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

/* thanks amc1! */

#include <iostream> // FIXME do not commit

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <ctype.h> /* isprint() */
#include <stdlib.h> /* strtol() */
#include <string.h>
#include <tuple>

#include "transmission.h"
#include "clients.h"
#include "utils.h" /* tr_snprintf(), tr_strlcpy() */

using namespace std::literals; // "foo"sv

namespace
{

constexpr int charint(uint8_t ch)
{
    if ('0' <= ch && ch <= '9')
    {
        return ch - '0';
    }

    if ('A' <= ch && ch <= 'Z')
    {
        return 10 + ch - 'A';
    }

    if ('a' <= ch && ch <= 'z')
    {
        return 36 + ch - 'a';
    }

    return 0;
}

constexpr std::optional<int> getShadowInt(uint8_t ch)
{
    auto constexpr str = std::string_view{ "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz.-" };
    auto const pos = str.find(ch);
    return pos != std::string_view::npos ? pos : std::optional<int>{};
}

constexpr std::optional<int> getFDMInt(uint8_t ch)
{
    auto constexpr str = std::string_view{ "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_.!~*()" };
    auto const pos = str.find(ch);
    return pos != std::string_view::npos ? pos : std::optional<int>{};
}

int strint(void const* pch, int span)
{
    char tmp[64];
    memcpy(tmp, pch, span);
    tmp[span] = '\0';
    return strtol(tmp, nullptr, 0);
}

constexpr char const* getMnemonicEnd(uint8_t ch)
{
    switch (ch)
    {
    case 'b':
    case 'B':
        return " (Beta)";

    case 'd':
        return " (Debug)";

    case 'x':
    case 'X':
    case 'Z':
        return " (Dev)";

    default:
        return "";
    }
}

void three_digits(char* buf, size_t buflen, char const* name, uint8_t const* digits) // FIXME: should be removed when done
{
    tr_snprintf(buf, buflen, "%s %d.%d.%d", name, charint(digits[0]), charint(digits[1]), charint(digits[2]));
}

void four_digits(char* buf, size_t buflen, char const* name, uint8_t const* digits) // FIXME: should be removed when done
{
    tr_snprintf(
        buf,
        buflen,
        "%s %d.%d.%d.%d",
        name,
        charint(digits[0]),
        charint(digits[1]),
        charint(digits[2]),
        charint(digits[3]));
}

void two_major_two_minor_formatter(char* buf, size_t buflen, std::string_view name, char const* digits)
{
    tr_snprintf(buf, buflen, "%*.*s %d.%02d", int(std::size(name)), int(std::size(name)), std::data(name), strint(digits + 3, 2), strint(digits + 5, 2));
}

void two_major_two_minor(char* buf, size_t buflen, char const* name, uint8_t const* digits) // FIXME: should be removed when done
{
    tr_snprintf(buf, buflen, "%s %d.%02d", name, strint(digits, 2), strint(digits + 2, 2));
}

void no_version(char* buf, size_t buflen, char const* name)
{
    tr_strlcpy(buf, name, buflen);
}

bool decodeBitCometClient(char* buf, size_t buflen, uint8_t const* id)
{
    char const* chid = (char const*)id;
    char const* mod = nullptr;

    if (strncmp(chid, "exbc", 4) == 0)
    {
        mod = "";
    }
    else if (strncmp(chid, "FUTB", 4) == 0)
    {
        mod = " (Solidox Mod) ";
    }
    else if (strncmp(chid, "xUTB", 4) == 0)
    {
        mod = " (Mod 2) ";
    }
    else
    {
        return false;
    }

    bool const is_bitlord = strncmp(chid + 6, "LORD", 4) == 0;
    char const* const name = (is_bitlord) ? "BitLord " : "BitComet ";
    int const major = id[4];
    int const minor = id[5];

    /**
     * Bitcomet, and older versions of BitLord, are of the form x.yy.
     * Bitcoment 1.0 and onwards are of the form x.y.
     */
    if (is_bitlord && major > 0)
    {
        tr_snprintf(buf, buflen, "%s%s%d.%d", name, mod, major, minor);
    }
    else
    {
        tr_snprintf(buf, buflen, "%s%s%d.%02d", name, mod, major, minor);
    }

    return true;
}

using format_func = void (*)(char* buf, size_t buflen, std::string_view name, char const* id);

constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, char ch)
{
    if (buflen >= 2)
    {
        *buf++ = ch;
    }
    *buf = '\0';
    return { buf, buflen - 1 };
}

constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, int n)
{
    while (n >= 10)
    {
        auto mod = n;
        auto multiplier = 1;
        while (mod > 10)
        {
            mod /= 10;
            multiplier *= 10;
        }
        std::tie(buf, buflen) = buf_append(buf, buflen, char('0' + mod));
        n -= mod * multiplier;
    }
    return buf_append(buf, buflen, char('0' + n));
}

constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, std::string_view name)
{
    auto const len = std::min(buflen - 1, std::size(name));
    for (size_t i = 0; i < len; ++i)
    {
        *buf++ = name[i];
    }
    *buf = '\0';
    return { buf, buflen - len };
}

template<typename T, typename... ArgTypes>
constexpr std::pair<char*, size_t> buf_append(char* buf, size_t buflen, T const t, ArgTypes... args)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, t);
    return buf_append(buf, buflen, args...);
}

constexpr void three_digit_formatter(char* buf, size_t buflen, std::string_view name, char const* digits)
{
    buf_append(buf, buflen, name, ' ', charint(digits[3]), '.', charint(digits[4]), '.', charint(digits[5]));
}

constexpr void four_digit_formatter(char* buf, size_t buflen, std::string_view name, char const* digits)
{
    buf_append(buf, buflen, name, ' ', charint(digits[3]), '.', charint(digits[4]), '.', charint(digits[5]), '.', charint(digits[6]));
}

constexpr void no_version_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    TR_UNUSED(id);
    buf_append(buf, buflen, name);
}

void transmission_formatter(char* buf, size_t buflen, std::string_view name, char const* chid)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ');

    if (strncmp(chid + 3, "000", 3) == 0) // very old client style: -TR0006- is 0.6
    {
        tr_snprintf(buf, buflen, "0.%c", chid[6]);
    }
    else if (strncmp(chid + 3, "00", 2) == 0) // previous client style: -TR0072- is 0.72
    {
        tr_snprintf(buf, buflen, "0.%02d", strint(chid + 5, 2));
    }
    else // current client style: -TR111Z- is 1.11+ */
    {
        tr_snprintf(
            buf,
            buflen,
            "%d.%02d%s",
            strint(chid + 3, 1),
            strint(chid + 4, 2),
            (chid[6] == 'Z' || chid[6] == 'X') ? "+" : "");
    }
}

void constexpr ktorrent_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    if (id[5] == 'D')
    {
        buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), " Dev "sv, charint(id[6]));
    }
    else if (id[5] == 'R')
    {
        buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), " RC "sv, charint(id[6]));
    }
    else
    {
        three_digit_formatter(buf, buflen, name, id);
    }
}

constexpr void utorrent_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    if (id[7] == '-')
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], std::string_view(getMnemonicEnd(id[6]))); // TODO: make getMnemonicEnd return a string_view
    }
    else // uTorrent replaces the trailing dash with an extra digit for longer version numbers
    {
        buf_append(buf, buflen, name, ' ', id[3], '.', id[4], '.', id[5], id[6], std::string_view(getMnemonicEnd(id[6])));
    }
}

void fdm_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    auto const c = getFDMInt(id[5]);
    if (c)
    {
        std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), '.', *c);
    }
    else
    {
        std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), '.', 'x');
    }
}

constexpr void xfplay_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
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

constexpr void ctorrent_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), '.', id[5], id[6]);
}

constexpr void bitbuddy_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], id[5], id[6]);
}

constexpr void bitrocket_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[4], ' ', '(', id[5], id[6], ')');
}

constexpr void folx_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', 'x');
}

void bits_on_wheels_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    if (strncmp(&id[4], "A0B", 3) == 0)
    {
        buf_append(buf, buflen, name, " 1.0.5"sv);
    }
    else if (strncmp(&id[4], "A0C", 3) == 0)
    {
        buf_append(buf, buflen, name, " 1.0.6"sv);
    }
    else
    {
        buf_append(buf, buflen, name, ' ', id[4], '.', id[5], '.', id[6]);
    }
}

constexpr void mediaget_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]));
}

constexpr void picotorrent_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', charint(id[3]), '.', id[4], id[5], '.', charint(id[6]));
}

void xtorrent_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    std::tie(buf, buflen) = buf_append(buf, buflen, name, ' ', charint(id[3]), '.', charint(id[4]), " ("sv);
    tr_snprintf(buf, buflen, "%d)", strint(id + 5, 2));
}

void mainline_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
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

constexpr void amazon_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', id[3], '.', id[5], '.', id[7]);
}

constexpr void opera_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    buf_append(buf, buflen, name, ' ', std::string_view(id+2, 4));
}

struct Client
{
    std::string_view begins_with;
    std::string_view name;
    format_func formatter;
};

auto constexpr Clients = std::array<Client, 111>
{{
    { "-AG", "Ares", four_digit_formatter },
    { "-AR", "Arctic", four_digit_formatter },
    { "-AT", "Artemis", four_digit_formatter },
    { "-AV", "Avicora", four_digit_formatter },
    { "-AX", "BitPump", two_major_two_minor_formatter },
    { "-AZ", "Azureus / Vuze", four_digit_formatter },
    { "-A~", "Ares", three_digit_formatter },
    { "-BB", "BitBuddy", bitbuddy_formatter },
    { "-BC", "BitComet", two_major_two_minor_formatter },
    { "-BE", "BitTorrent SDK", four_digit_formatter },
    { "-BF", "BitFlu", no_version_formatter },
    { "-BG", "BTGetit", four_digit_formatter },
    { "-BH", "BitZilla", four_digit_formatter },
    { "-BI", "BiglyBT", four_digit_formatter },
    { "-BM", "BitMagnet", four_digit_formatter },
    { "-BN", "Baidu Netdisk", no_version_formatter },
    { "-BOW", "Bits on Wheels", bits_on_wheels_formatter },
    { "-BP", "BitTorrent Pro (Azureus + Spyware)", four_digit_formatter },
    { "-BR", "BitRocket", bitrocket_formatter },
    { "-BS", "BTSlave", four_digit_formatter },
    { "-BT", "BitTorrent", utorrent_formatter },
    { "-BW", "BitWombat", four_digit_formatter },
    { "-BX", "BittorrentX", four_digit_formatter },
    { "-CD", "Enhanced CTorrent", two_major_two_minor_formatter },
    { "-CT", "CTorrent", ctorrent_formatter },
    { "-DE", "Deluge", four_digit_formatter },
    { "-DP", "Propagate Data Client", four_digit_formatter },
    { "-EB", "EBit", four_digit_formatter },
    { "-ES", "Electric Sheep", three_digit_formatter },
    { "-FC", "FileCroc", four_digit_formatter },
    { "-FD", "Free Download Manager", fdm_formatter },
    { "-FG", "FlashGet", two_major_two_minor_formatter },
    { "-FL", "Folx", folx_formatter },
    { "-FT", "FoxTorrent/RedSwoosh", four_digit_formatter },
    { "-FW", "FrostWire", three_digit_formatter },
    { "-G3", "G3 Torrent", no_version_formatter },
    { "-GR", "GetRight", four_digit_formatter },
    { "-GS", "GSTorrent", four_digit_formatter },
    { "-HK", "Hekate", four_digit_formatter },
    { "-HL", "Halite", three_digit_formatter },
    { "-HN", "Hydranode", four_digit_formatter },
    { "-KG", "KGet", four_digit_formatter },
    { "-KT", "KTorrent", ktorrent_formatter },
    { "-LC", "LeechCraft", four_digit_formatter },
    { "-LH", "LH-ABC", four_digit_formatter },
    { "-LP", "Lphant", two_major_two_minor_formatter },
    { "-LT", "libtorrent (Rasterbar)", three_digit_formatter },
    { "-LW", "LimeWire", no_version_formatter },
    { "-MG", "MediaGet", mediaget_formatter },
    { "-MK", "Meerkat", four_digit_formatter },
    { "-MO", "MonoTorrent", four_digit_formatter },
    { "-MP", "MooPolice", three_digit_formatter },
    { "-MR", "Miro", four_digit_formatter },
    { "-MT", "Moonlight", four_digit_formatter },
    { "-NX", "Net Transport", four_digit_formatter },
    { "-OS", "OneSwarm", four_digit_formatter },
    { "-OT", "OmegaTorrent", four_digit_formatter },
    { "-PD", "Pando", four_digit_formatter },
    { "-PI", "PicoTorrent", picotorrent_formatter },
    { "-QD", "QQDownload", four_digit_formatter },
    { "-RS", "Rufus", four_digit_formatter },
    { "-RT", "Retriever", four_digit_formatter },
    { "-RZ", "RezTorrent", four_digit_formatter },
    { "-SD", "Thunder", four_digit_formatter },
    { "-SM", "SoMud", four_digit_formatter },
    { "-SS", "SwarmScope", four_digit_formatter },
    { "-ST", "SymTorrent", four_digit_formatter },
    { "-SZ", "Shareaza", four_digit_formatter },
    { "-S~", "Shareaza", four_digit_formatter },
    { "-TN", "Torrent .NET", four_digit_formatter },
    { "-TR", "Transmission", transmission_formatter },
    { "-TS", "TorrentStorm", four_digit_formatter },
    { "-TT", "TuoTu", four_digit_formatter },
    { "-UE", "\xc2\xb5Torrent Embedded", utorrent_formatter },
    { "-UL", "uLeecher!", four_digit_formatter },
    { "-UM", "\xc2\xb5Torrent Mac", utorrent_formatter },
    { "-UT", "\xc2\xb5Torrent", utorrent_formatter },
    { "-UW", "\xc2\xb5Torrent Web", utorrent_formatter },
    { "-VG", "Vagaa", four_digit_formatter },
    { "-WS", "HTTP Seed", no_version_formatter },
    { "-WT", "BitLet", four_digit_formatter },
    { "-WT-", "BitLet", no_version_formatter },
    { "-WW", "WebTorrent", four_digit_formatter },
    { "-WY", "FireTorrent", four_digit_formatter },
    { "-XC", "Xtorrent", xtorrent_formatter },
    { "-XF", "Xfplay", xfplay_formatter },
    { "-XL", "Xunlei", four_digit_formatter },
    { "-XS", "XSwifter", four_digit_formatter },
    { "-XT", "XanTorrent", four_digit_formatter },
    { "-XX", "Xtorrent", xtorrent_formatter },
    { "-ZO", "Zona", four_digit_formatter },
    { "-ZT", "Zip Torrent", four_digit_formatter },
    { "-bk", "BitKitten (libtorrent)", four_digit_formatter },
    { "-lt", "libTorrent (Rakshasa)", three_digit_formatter },
    { "-pb", "pbTorrent", three_digit_formatter },
    { "-qB", "qBittorrent", three_digit_formatter },
    { "-st", "SharkTorrent", four_digit_formatter },
    { "10-------", "JVtorrent", no_version_formatter },
    { "346-", "TorrentTopia", no_version_formatter },
    { "AZ2500BT", "BitTyrant (Azureus Mod)", no_version_formatter },
    { "LIME", "Limewire", no_version_formatter },
    { "M", "BitTorrent", mainline_formatter },
    { "OP", "Opera", opera_formatter },
    { "Pando", "Pando", no_version_formatter },
    { "Q", "Queen Bee", mainline_formatter },
    { "S3", "Amazon S3", amazon_formatter },
    { "a00---0", "Swarmy", no_version_formatter },
    { "a02---0", "Swarmy", no_version_formatter },
    { "aria2-", "aria2", no_version_formatter },
    { "eX", "eXeem", no_version_formatter },
    { "martini", "Martini Man", no_version_formatter },
}};

} // namespace

char* tr_clientForId(char* buf, size_t buflen, void const* id_in)
{
    auto const* id = static_cast<uint8_t const*>(id_in);
    auto const* chid = static_cast<char const*>(id_in);

    *buf = '\0';

    if (id == nullptr)
    {
        return buf;
    }

    struct Compare
    {
        bool operator()(std::string_view const& key, Client const& client) const
        {
            auto const key_lhs = std::string_view{ std::data(key), std::min(std::size(key), std::size(client.begins_with)) };
            auto const ret = key_lhs < client.begins_with;
            // std::cerr << "is [" << key_lhs << " less than " << client.begins_with << ' ' << ret << std::endl;
            return ret;
        }

        bool operator()(Client const& client, std::string_view const& key) const
        {
            auto const key_lhs = std::string_view{ std::data(key), std::min(std::size(key), std::size(client.begins_with)) };
            auto const ret = client.begins_with < key_lhs;
            // std::cerr << "is [" << client.begins_with << " less than " << key_lhs << ' ' << ret << std::endl;
            return ret;
        }
    };

    auto const key = std::string_view{ chid };
    auto const compare = Compare{};
#if 0
    auto constexpr compare = [](std::string_view const& key, Client const& client){
        auto const key_lhs = std::string_view { std::data(key), std::min(std::size(key), std::size(client.begins_with)) };
        return client.begins_with < key_lhs;
    };
#endif
    auto eq = std::equal_range(std::begin(Clients), std::end(Clients), key, compare);
    // std::cerr << "eq.first distance from begin " << std::distance(std::begin(Clients), eq.first) << std::endl;
    // std::cerr << "eq.second distance from begin " << std::distance(std::begin(Clients), eq.second) << std::endl;
    if (eq.first != std::end(Clients) && eq.first != eq.second)
    {
        eq.first->formatter(buf, buflen, eq.first->name, chid);
        std::cerr << "got a match [" << key << "] -> [" << buf << ']' << std::endl;
        return buf;
    }

    if (decodeBitCometClient(buf, buflen, id))
    {
        return buf;
    }

    /* Everything else */
    else if (strncmp(chid, "-ML", 3) == 0)
    {
        tr_snprintf(buf, buflen, "MLDonkey %c%c%c%c%c", id[3], id[4], id[5], id[6], id[7]);
    }
    else if (strncmp(chid, "DNA", 3) == 0)
    {
        tr_snprintf(buf, buflen, "BitTorrent DNA %d.%d.%d", strint(id + 3, 2), strint(id + 5, 2), strint(id + 7, 2));
    }
    else if (strncmp(chid, "Plus", 4) == 0)
    {
        tr_snprintf(buf, buflen, "Plus! v2 %c.%c%c", id[4], id[5], id[6]);
    }
    else if (strncmp(chid, "XBT", 3) == 0)
    {
        tr_snprintf(buf, buflen, "XBT Client %c.%c.%c%s", id[3], id[4], id[5], getMnemonicEnd(id[6]));
    }
    else if (strncmp(chid, "Mbrst", 5) == 0)
    {
        tr_snprintf(buf, buflen, "burst! %c.%c.%c", id[5], id[7], id[9]);
    }
    else if (strncmp(chid, "btpd", 4) == 0)
    {
        tr_snprintf(buf, buflen, "BT Protocol Daemon %c%c%c", id[5], id[6], id[7]);
    }
    else if (strncmp(chid, "BLZ", 3) == 0)
    {
        tr_snprintf(buf, buflen, "Blizzard Downloader %d.%d", id[3] + 1, id[4]);
    }
    else if (strncmp(chid, "-SP", 3) == 0)
    {
        three_digits(buf, buflen, "BitSpirit", id + 3);
    }
    else if ('\0' == id[0] && strncmp(chid + 2, "BS", 2) == 0)
    {
        tr_snprintf(buf, buflen, "BitSpirit %u", (id[1] == 0 ? 1 : id[1]));
    }
    else if (strncmp(chid, "QVOD", 4) == 0)
    {
        four_digits(buf, buflen, "QVOD", id + 4);
    }
    else if (strncmp(chid, "-NE", 3) == 0)
    {
        four_digits(buf, buflen, "BT Next Evolution", id + 3);
    }
    else if (strncmp(chid, "TIX", 3) == 0)
    {
        two_major_two_minor(buf, buflen, "Tixati", id + 3);
    }
    else if (strncmp(chid, "A2", 2) == 0)
    {
        if (id[4] == '-' && id[6] == '-' && id[8] == '-')
        {
            tr_snprintf(buf, buflen, "aria2 %c.%c.%c", id[3], id[5], id[7]);
        }
        else if (id[4] == '-' && id[7] == '-' && id[9] == '-')
        {
            tr_snprintf(buf, buflen, "aria2 %c.%c%c.%c", id[3], id[5], id[6], id[8]);
        }
        else
        {
            no_version(buf, buflen, "aria2");
        }
    }
    else if (strncmp(chid, "-BL", 3) == 0)
    {
        tr_snprintf(buf, buflen, "BitLord %c.%c.%c-%c%c%c", id[3], id[4], id[5], id[6], id[7], id[8]);
    }

    /* Shad0w-style */
    if (tr_str_is_empty(buf))
    {
        auto const a = getShadowInt(id[1]);
        auto const b = getShadowInt(id[2]);
        auto const c = getShadowInt(id[3]);

        if (strchr("AOQRSTU", id[0]) != nullptr && a && b && c)
        {
            char const* name = nullptr;

            switch (id[0])
            {
            case 'A':
                name = "ABC";
                break;

            case 'O':
                name = "Osprey";
                break;

            case 'Q':
                name = "BTQueue";
                break;

            case 'R':
                name = "Tribler";
                break;

            case 'S':
                name = "Shad0w";
                break;

            case 'T':
                name = "BitTornado";
                break;

            case 'U':
                name = "UPnP NAT Bit Torrent";
                break;
            }

            if (name != nullptr)
            {
                tr_snprintf(buf, buflen, "%s %d.%d.%d", name, *a, *b, *c);
                return buf;
            }
        }
    }

    /* No match */
    if (tr_str_is_empty(buf))
    {
        char out[32];
        char* walk = out;

        for (size_t i = 0; i < 8; ++i)
        {
            char const c = chid[i];

            if (isprint((unsigned char)c))
            {
                *walk++ = c;
            }
            else
            {
                tr_snprintf(walk, out + sizeof(out) - walk, "%%%02X", (unsigned int)c);
                walk += 3;
            }
        }

        *walk = '\0';
        tr_strlcpy(buf, out, buflen);
    }

    return buf;
}
