// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "file-info.h"
#include "utils.h"

using namespace std::literals;

namespace
{

bool isPortableSegment(std::string_view in)
{
    if (std::empty(in))
    {
        return false;
    }

    if ((std::isspace(in.front()) != 0) || (std::isspace(in.back()) != 0))
    {
        return false;
    }

    if (in.back() == '.')
    {
        return false;
    }

    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    static auto constexpr Banned = R"(<>:"/\|?*)";
    if (in.find_first_of(Banned) != std::string_view::npos)
    {
        return false;
    }

    // https://docs.microsoft.com/en-us/windows/desktop/FileIO/naming-a-file
    auto constexpr ReservedNames = std::array<std::string_view, 22>{
        "CON"sv,  "PRN"sv,  "AUX"sv,  "NUL"sv,  "COM1"sv, "COM2"sv, "COM3"sv, "COM4"sv, "COM5"sv, "COM6"sv, "COM7"sv,
        "COM8"sv, "COM9"sv, "LPT1"sv, "LPT2"sv, "LPT3"sv, "LPT4"sv, "LPT5"sv, "LPT6"sv, "LPT7"sv, "LPT8"sv, "LPT9"sv,
    };
    auto const test = [&in](auto const& name)
    {
        return in == name || tr_strvStartsWith(in, tr_strvJoin(name, "."));
    };
    return std::none_of(std::begin(ReservedNames), std::end(ReservedNames), test);
}

} // namespace

bool tr_file_info::isPortable(std::string_view subpath)
{
    auto segment = std::string_view{};
    while (tr_strvSep(&subpath, &segment, '/'))
    {
        if (!isPortableSegment(segment))
        {
            return false;
        }
    }

    return true;
}
