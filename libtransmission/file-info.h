// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <string_view>

struct tr_file_info
{
    [[nodiscard]] static std::string sanitizePath(std::string_view path);

    [[nodiscard]] static bool isPortable(std::string_view path)
    {
        return sanitizePath(path) == path;
    }
};
