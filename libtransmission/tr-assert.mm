// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "tr-assert.h"

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

// macOS implementation of tr_assert_report() that provides the message in the crash report
// This replaces the generic implementation of the function in tr-assert.cc

[[noreturn]] bool tr_assert_report(char const* file, int line, char const* message_fmt, ...)
{
    char buffer[1024];
    va_list args;

    va_start(args, message_fmt);
    vsnprintf(buffer, sizeof(buffer), message_fmt, args);
    va_end(args);

    [NSException raise:NSInternalInconsistencyException format:@"assertion failed: %s (%s:%d)", buffer, file, line];

    // We should not reach this anyway, but it helps mark the function as propertly noreturn
    // (the Objective-C NSException method does not).
    abort();
}

#endif
