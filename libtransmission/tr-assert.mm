// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <fmt/format.h>

#include "tr-assert.h"

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

// macOS implementation of tr_assert_report() that provides the message in the crash report
// This replaces the generic implementation of the function in tr-assert.cc

[[noreturn]] bool tr_assert_report(std::string_view file, int line, std::string_view message)
{
    auto const message = fmt::format(FMT_STRING("assertion failed: {:s} ({:s}:{:d})"), buffer, file, line);
    [NSException raise:NSInternalInconsistencyException format:@"%s", message.c_str()];

    // We should not reach this anyway, but it helps mark the function as propertly noreturn
    // (the Objective-C NSException method does not).
    abort();
}

#endif
