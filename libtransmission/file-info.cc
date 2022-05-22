// This file Copyright © 2019-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

#include <event2/util.h> // evutil_ascii_strncasecmp

#include <fmt/core.h>
#include <fmt/format.h>

#include "file-info.h"
#include "utils.h"

using namespace std::literals;

// https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file
[[nodiscard]] static bool isReservedFilename(std::string_view in) noexcept
{
    if (std::empty(in))
    {
        return false;
    }

    if ("ACLNP"sv.find(in.front()) == std::string_view::npos)
    {
        return false;
    }

    auto in_upper = tr_pathbuf{ in };
    std::transform(std::begin(in_upper), std::end(in_upper), std::begin(in_upper), [](auto ch) { return toupper(ch); });
    auto const in_upper_sv = in_upper.sv();

    static auto constexpr ReservedNames = std::array<std::string_view, 22>{
        "AUX"sv,  "CON"sv,  "NUL"sv,  "PRN"sv,  "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv,
        "COM8"sv, "COM9"sv, "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
    };
    if (std::find(std::begin(ReservedNames), std::end(ReservedNames), in_upper_sv) != std::end(ReservedNames))
    {
        return true;
    }

    static auto constexpr ReservedPrefixes = std::array<std::string_view, 22>{
        "AUX."sv, "CON."sv,  "NUL."sv,  "PRN."sv,  "COM1."sv, "COM2"sv,  "COM3."sv, "COM4."sv, "COM5."sv, "COM6."sv, "COM7."sv,
        "COM8"sv, "COM9."sv, "LPT1."sv, "LPT2."sv, "LPT3."sv, "LPT4."sv, "LPT5."sv, "LPT6."sv, "LPT7."sv, "LPT8."sv, "LPT9."sv,
    };
    return std::any_of(
        std::begin(ReservedPrefixes),
        std::end(ReservedPrefixes),
        [in_upper_sv](auto const& prefix) { return tr_strvStartsWith(in_upper_sv, prefix); });
}

// https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
[[nodiscard]] static auto constexpr isBannedChar(char ch) noexcept
{
    switch (ch)
    {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
        return true;
    default:
        return false;
    }
}

static void appendSanitizedComponent(std::string_view in, tr_pathbuf& out)
{
    // remove leading and trailing spaces
    in = tr_strvStrip(in);

    // remove trailing periods
    while (tr_strvEndsWith(in, '.'))
    {
        in.remove_suffix(1);
    }

    if (isReservedFilename(in))
    {
        out.append('_');
    }

    // munge banned characters
    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    std::transform(std::begin(in), std::end(in), std::back_inserter(out), [](auto ch) { return isBannedChar(ch) ? '_' : ch; });
}

void tr_file_info::sanitizePath(std::string_view in, tr_pathbuf& append_me)
{
    auto segment = std::string_view{};
    while (tr_strvSep(&in, &segment, '/'))
    {
        appendSanitizedComponent(segment, append_me);
        append_me.append("/"sv);
    }

    if (auto const n = std::size(append_me); n > 0)
    {
        append_me.resize(n - 1); // remove trailing slash
    }
}

std::string tr_file_info::sanitizePath(std::string_view in)
{
    auto buf = tr_pathbuf{};
    sanitizePath(in, buf);
    return std::string{ buf.sv() };
}
