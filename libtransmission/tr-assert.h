/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#if !defined(NDEBUG) || defined(TR_FORCE_ASSERTIONS)

#include <stdbool.h>

#ifndef TR_LIKELY
#if defined(__GNUC__)
#define TR_LIKELY(x) __builtin_expect(!!(x), true)
#else
#define TR_LIKELY(x) (x)
#endif
#endif

#ifndef TR_NORETURN
#if defined(__GNUC__)
#define TR_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define TR_NORETURN __declspec(noreturn)
#else
#define TR_NORETURN
#endif
#endif

#ifndef TR_GNUC_PRINTF
#if defined(__GNUC__)
#define TR_GNUC_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define TR_GNUC_PRINTF(fmt, args)
#endif
#endif

bool TR_NORETURN tr_assert_report(char const* file, int line, char const* message_fmt, ...) TR_GNUC_PRINTF(3, 4);

#define TR_ASSERT(x) (TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, "%s", #x))
#define TR_ASSERT_MSG(x, ...) (TR_LIKELY(x) || tr_assert_report(__FILE__, __LINE__, __VA_ARGS__))

#define TR_ENABLE_ASSERTS

#else

#define TR_ASSERT(x) ((void)0)
#define TR_ASSERT_MSG(x, ...) ((void)0)

#undef TR_ENABLE_ASSERTS

#endif
