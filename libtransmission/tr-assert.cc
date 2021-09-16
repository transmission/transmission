/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "tr-assert.h"

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

bool tr_assert_report(char const* file, int line, char const* message_fmt, ...)
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
