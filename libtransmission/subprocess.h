// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <map>
#include <string_view>

struct tr_error;

bool tr_spawn_async(
    char const* const* cmd,
    std::map<std::string_view, std::string_view> const& env,
    char const* work_dir,
    tr_error** error);
