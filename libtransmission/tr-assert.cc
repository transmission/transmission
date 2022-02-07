// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "tr-assert.h"

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

[[noreturn]] bool tr_assert_report(char const* file, int line, char const* message_fmt, ...)
{
    va_list args;
    va_start(args, message_fmt);

    fprintf(stderr, "assertion failed: ");
    vfprintf(stderr, message_fmt, args);
    fprintf(stderr, " (%s:%d)\n", file, line);

    va_end(args);

    abort();
}

#endif
