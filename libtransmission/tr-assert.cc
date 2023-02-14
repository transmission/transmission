// This file Copyright © 2017-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "tr-assert.h"

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

[[noreturn]] bool tr_assert_report(std::string_view file, long line, std::string_view message)
{
    std::cerr << "assertion failed: " << message << " (" << file << ':' << line << ')' << std::endl;
    abort();
}

#endif
