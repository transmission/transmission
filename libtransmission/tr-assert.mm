// This file Copyright © 2017-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <Foundation/Foundation.h>

#include <cstdlib> // for abort()

#include <fmt/format.h>

#include "tr-assert.h"

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

// macOS implementation of tr_assert_report() that provides the message in the crash report
// This replaces the generic implementation of the function in tr-assert.cc

[[noreturn]] bool tr_assert_report(std::string_view file, long line, std::string_view message)
{
    auto const full_text = fmt::format(FMT_STRING("assertion failed: {:s} ({:s}:{:d})"), message, file, line);
    [NSException raise:NSInternalInconsistencyException format:@"%s", full_text.c_str()];

    // We should not reach this anyway, but it helps mark the function as property noreturn
    // (the Objective-C NSException method does not).
    abort();
}

#endif
