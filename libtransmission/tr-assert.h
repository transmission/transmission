// This file Copyright © 2017-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

#include <string_view>

#include "tr-macros.h"

[[noreturn]] bool tr_assert_report(std::string_view file, long line, std::string_view message);

#define TR_ASSERT(x) ((void)(TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, #x)))
#define TR_ASSERT_MSG(x, message) ((void)(TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, message)))
#define TR_UNREACHABLE() tr_assert_report(__FILE__, __LINE__, "Unreachable code")

#define TR_ENABLE_ASSERTS

#else

#define TR_ASSERT(x) ((void)0)
#define TR_ASSERT_MSG(x, ...) ((void)0)
#define TR_UNREACHABLE() ((void)0)

#undef TR_ENABLE_ASSERTS

#endif
