/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

/* thanks amc1! */

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <ctype.h> /* isprint() */
#include <stdlib.h> /* strtol() */
#include <string.h>

#include "transmission.h"
#include "clients.h"
#include "utils.h" /* tr_snprintf(), tr_strlcpy() */

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

void three_digit_formatter(char* buf, size_t buflen, std::string_view name, char const* digits)
{
    tr_snprintf(buf, buflen, "%*.*s %d.%d.%d", int(std::size(name)), int(std::size(name)), std::data(name), charint(digits[3]), charint(digits[4]), charint(digits[5]));
}

void three_digits(char* buf, size_t buflen, char const* name, uint8_t const* digits) // FIXME: should be removed when done
{
    tr_snprintf(buf, buflen, "%s %d.%d.%d", name, charint(digits[0]), charint(digits[1]), charint(digits[2]));
}

void four_digit_formatter(char* buf, size_t buflen, std::string_view name, char const* digits)
{
    tr_snprintf(
        buf,
        buflen,
        "%*.*s %d.%d.%d.%d",
        int(std::size(name)),
        int(std::size(name)),
        std::data(name),
        charint(digits[3]),
        charint(digits[4]),
        charint(digits[5]),
        charint(digits[6]));
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

void mainline_style(char* buf, size_t buflen, char const* name, uint8_t const* id)
{
    if (id[4] == '-' && id[6] == '-')
    {
        tr_snprintf(buf, buflen, "%s %c.%c.%c", name, id[1], id[3], id[5]);
    }
    else if (id[5] == '-')
    {
        tr_snprintf(buf, buflen, "%s %c.%c%c.%c", name, id[1], id[3], id[4], id[6]);
    }
}

constexpr bool isMainlineStyle(uint8_t const* peer_id)
{
    /**
     * One of the following styles will be used:
     *   Mx-y-z--
     *   Mx-yy-z-
     */
    return peer_id[2] == '-' && peer_id[7] == '-' && (peer_id[4] == '-' || peer_id[5] == '-');
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

void no_version_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    TR_UNUSED(id);

    auto const len = std::min(buflen - 1, std::size(name));
    std::copy_n(std::begin(name), len, buf);
    buf[len] = '\0';
}

void transmission_formatter(char* buf, size_t buflen, std::string_view name, char const* chid)
{
    if (strncmp(chid + 3, "000", 3) == 0) // very old client style: -TR0006- is 0.6
    {
        tr_snprintf(buf, buflen, "%*.*s 0.%c", int(std::size(name)), int(std::size(name)), std::data(name), chid[6]);
    }
    else if (strncmp(chid + 3, "00", 2) == 0) // previous client style: -TR0072- is 0.72
    {
        tr_snprintf(buf, buflen, "%*.*s 0.%02d", int(std::size(name)), int(std::size(name)), std::data(name), strint(chid + 5, 2));
    }
    else // current client style: -TR111Z- is 1.11+ */
    {
        tr_snprintf(
            buf,
            buflen,
            "%*.*s %d.%02d%s",
            int(std::size(name)),
            int(std::size(name)),
            std::data(name),
            strint(chid + 3, 1),
            strint(chid + 4, 2),
            (chid[6] == 'Z' || chid[6] == 'X') ? "+" : "");
    }
}

void ktorrent_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    if (id[5] == 'D')
    {
        tr_snprintf(buf, buflen, "%*.*s %d.%d Dev %d", int(std::size(name)), int(std::size(name)), std::data(name), charint(id[3]), charint(id[4]), charint(id[6]));
    }
    else if (id[5] == 'R')
    {
        tr_snprintf(buf, buflen, "%*.*s %d.%d RC %d", int(std::size(name)), int(std::size(name)), std::data(name), charint(id[3]), charint(id[4]), charint(id[6]));
    }
    else
    {
        three_digit_formatter(buf, buflen, name, id);
    }
}

void utorrent_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    if (id[7] == '-')
    {
        tr_snprintf(
            buf,
            buflen,
            "%*.*s %d.%d.%d%s",
            int(std::size(name)),
            int(std::size(name)),
            std::data(name),
            strint(id + 3, 1),
            strint(id + 4, 1),
            strint(id + 5, 1),
            getMnemonicEnd(id[6]));
    }
    else // uTorrent replaces the trailing dash with an extra digit for longer version numbers
    {
        tr_snprintf(
            buf,
            buflen,
            "%*.*s %d.%d.%d%s",
            int(std::size(name)),
            int(std::size(name)),
            std::data(name),
            strint(id + 3, 1),
            strint(id + 4, 1),
            strint(id + 5, 2),
            getMnemonicEnd(id[7]));
    }
}

void fdm_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    auto const c = getFDMInt(id[5]);
    if (c)
    {
        tr_snprintf(buf, buflen, "%*.*s %d.%d.%d", int(std::size(name)), int(std::size(name)), std::data(name), charint(id[3]), charint(id[4]), *c);
    }
    else
    {
        tr_snprintf(buf, buflen, "%*.*s %d.%d.x", int(std::size(name)), int(std::size(name)), std::data(name), charint(id[3]), charint(id[4]));
    }
}

void xfplay_formatter(char* buf, size_t buflen, std::string_view name, char const* id)
{
    if (id[6] == '0')
    {
        three_digit_formatter(buf, buflen, name, id);
    }
    else
    {
        tr_snprintf(buf, buflen, "%*.*s %d.%d.%d", int(std::size(name)), int(std::size(name)), std::data(name), strint(id + 3, 1), strint(id + 4, 1), strint(id + 5, 2));
    }
}

struct Client
{
    std::string_view begins_with;
    std::string_view name;
    format_func formatter;
};

auto constexpr Clients = std::array<Client, 86>
{{
    { "-AG", "Aress", four_digit_formatter },
    { "-AR", "Arctic", four_digit_formatter },
    { "-AT", "Artemis", four_digit_formatter },
    { "-AV", "Avicora", four_digit_formatter },
    { "-AX", "BitPump", two_major_two_minor_formatter },
    { "-AZ", "Azureus / Vuze", four_digit_formatter },
    { "-A~", "Ares", three_digit_formatter },
    { "-BC", "BitComet", two_major_two_minor_formatter },
    { "-BE", "BitTorrent SDK", four_digit_formatter },
    { "-BF", "BitFlu", no_version_formatter },
    { "-BG", "BTGetit", four_digit_formatter },
    { "-BH", "BitZilla", four_digit_formatter },
    { "-BI", "BiglyBT", four_digit_formatter },
    { "-BM", "BitMagnet", four_digit_formatter },
    { "-BN", "Baidu Netdisk", no_version_formatter },
    { "-BP", "BitTorrent Pro (Azureus + Spyware)", four_digit_formatter },
    { "-BS", "BTSlave", four_digit_formatter },
    { "-BT", "BitTorrent", utorrent_formatter },
    { "-BW", "BitWombat", four_digit_formatter },
    { "-BX", "BittorrentX", four_digit_formatter },
    { "-CD", "Enhanced CTorrent", two_major_two_minor_formatter },
    { "-DE", "Deluge", four_digit_formatter },
    { "-DP", "Propagate Data Client", four_digit_formatter },
    { "-EB", "EBit", four_digit_formatter },
    { "-ES", "Electric Sheep", three_digit_formatter },
    { "-FC", "FileCroc", four_digit_formatter },
    { "-FD", "Free Download Manager", fdm_formatter },
    { "-FT", "FoxTorrent/RedSwoosh", four_digit_formatter },
    { "-FW", "FrostWire", three_digit_formatter },
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
    { "-MK", "Meerkat", four_digit_formatter },
    { "-MO", "MonoTorrent", four_digit_formatter },
    { "-MP", "MooPolice", three_digit_formatter },
    { "-MR", "Miro", four_digit_formatter },
    { "-MT", "Moonlight", four_digit_formatter },
    { "-NX", "Net Transport", four_digit_formatter },
    { "-OS", "OneSwarm", four_digit_formatter },
    { "-OT", "OmegaTorrent", four_digit_formatter },
    { "-PD", "Pando", four_digit_formatter },
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
    { "-WW", "WebTorrent", four_digit_formatter },
    { "-WY", "FireTorrent", four_digit_formatter },
    { "-XF", "Xfplay", xfplay_formatter },
    { "-XL", "Xunlei", four_digit_formatter },
    { "-XS", "XSwifter", four_digit_formatter },
    { "-XT", "XanTorrent", four_digit_formatter },
    { "-XX", "Xtorrent", four_digit_formatter },
    { "-ZO", "Zona", four_digit_formatter },
    { "-ZT", "Zip Torrent", four_digit_formatter },
    { "-bk", "BitKitten (libtorrent)", four_digit_formatter },
    { "-lt", "libTorrent (Rakshasa)", three_digit_formatter },
    { "-pb", "pbTorrent", three_digit_formatter },
    { "-qB", "qBittorrent", three_digit_formatter },
    { "-st", "SharkTorrent", four_digit_formatter },
}};

} // namespace

#include <iostream> // FIXME do not commit

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

    /* Azureus-style */
    if (id[0] == '-' && id[7] == '-')
    {
        /* */
        if (strncmp(chid + 1, "BB", 2) == 0)
        {
            tr_snprintf(buf, buflen, "BitBuddy %c.%c%c%c", id[3], id[4], id[5], id[6]);
        }
        else if (strncmp(chid + 1, "BR", 2) == 0)
        {
            tr_snprintf(buf, buflen, "BitRocket %c.%c (%c%c)", id[3], id[4], id[5], id[6]);
        }
        else if (strncmp(chid + 1, "CT", 2) == 0)
        {
            tr_snprintf(buf, buflen, "CTorrent %d.%d.%02d", charint(id[3]), charint(id[4]), strint(id + 5, 2));
        }
        else if (strncmp(chid + 1, "XC", 2) == 0 || strncmp(chid + 1, "XX", 2) == 0)
        {
            tr_snprintf(buf, buflen, "Xtorrent %d.%d (%d)", charint(id[3]), charint(id[4]), strint(id + 5, 2));
        }
        else if (strncmp(chid + 1, "BOW", 3) == 0)
        {
            if (strncmp(&chid[4], "A0B", 3) == 0)
            {
                tr_snprintf(buf, buflen, "Bits on Wheels 1.0.5");
            }
            else if (strncmp(&chid[4], "A0C", 3) == 0)
            {
                tr_snprintf(buf, buflen, "Bits on Wheels 1.0.6");
            }
            else
            {
                tr_snprintf(buf, buflen, "Bits on Wheels %c.%c.%c", id[4], id[5], id[5]);
            }
        }
        else if (strncmp(chid + 1, "MG", 2) == 0)
        {
            tr_snprintf(buf, buflen, "MediaGet %d.%02d", charint(id[3]), charint(id[4]));
        }
        else if (strncmp(chid + 1, "PI", 2) == 0)
        {
            tr_snprintf(buf, buflen, "PicoTorrent %d.%d%d.%d", charint(id[3]), charint(id[4]), charint(id[5]), charint(id[6]));
        }
        else if (strncmp(chid + 1, "FL", 2) == 0)
        {
            tr_snprintf(buf, buflen, "Folx %d.x", charint(id[3]));
        }

        if (!tr_str_is_empty(buf))
        {
            return buf;
        }
    }


    /* Mainline */
    if (isMainlineStyle(id))
    {
        if (*id == 'M')
        {
            mainline_style(buf, buflen, "BitTorrent", id);
        }

        if (*id == 'Q')
        {
            mainline_style(buf, buflen, "Queen Bee", id);
        }

        if (!tr_str_is_empty(buf))
        {
            return buf;
        }
    }

    if (decodeBitCometClient(buf, buflen, id))
    {
        return buf;
    }

    /* Clients with no version */
    if (strncmp(chid, "AZ2500BT", 8) == 0)
    {
        no_version(buf, buflen, "BitTyrant (Azureus Mod)");
    }
    else if (strncmp(chid, "LIME", 4) == 0)
    {
        no_version(buf, buflen, "Limewire");
    }
    else if (strncmp(chid, "martini", 7) == 0)
    {
        no_version(buf, buflen, "Martini Man");
    }
    else if (strncmp(chid, "Pando", 5) == 0)
    {
        no_version(buf, buflen, "Pando");
    }
    else if (strncmp(chid, "a00---0", 7) == 0)
    {
        no_version(buf, buflen, "Swarmy");
    }
    else if (strncmp(chid, "a02---0", 7) == 0)
    {
        no_version(buf, buflen, "Swarmy");
    }
    else if (strncmp(chid, "-G3", 3) == 0)
    {
        no_version(buf, buflen, "G3 Torrent");
    }
    else if (strncmp(chid, "10-------", 9) == 0)
    {
        no_version(buf, buflen, "JVtorrent");
    }
    else if (strncmp(chid, "346-", 4) == 0)
    {
        no_version(buf, buflen, "TorrentTopia");
    }
    else if (strncmp(chid, "eX", 2) == 0)
    {
        no_version(buf, buflen, "eXeem");
    }
    else if (strncmp(chid, "aria2-", 6) == 0)
    {
        no_version(buf, buflen, "aria2");
    }
    else if (strncmp(chid, "-WT-", 4) == 0)
    {
        no_version(buf, buflen, "BitLet");
    }
    else if (strncmp(chid, "-FG", 3) == 0)
    {
        two_major_two_minor(buf, buflen, "FlashGet", id + 3);
    }
    /* Everything else */
    else if (strncmp(chid, "S3", 2) == 0 && id[2] == '-' && id[4] == '-' && id[6] == '-')
    {
        tr_snprintf(buf, buflen, "Amazon S3 %c.%c.%c", id[3], id[5], id[7]);
    }
    else if (strncmp(chid, "OP", 2) == 0)
    {
        tr_snprintf(buf, buflen, "Opera (Build %c%c%c%c)", id[2], id[3], id[4], id[5]);
    }
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
