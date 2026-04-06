// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#if __cpp_lib_constexpr_vector >= 201907L
#define TR_CONSTEXPR_VEC constexpr
#else
#define TR_CONSTEXPR_VEC
#endif

#if __cpp_lib_constexpr_string >= 201907L
#define TR_CONSTEXPR_STR constexpr
#else
#define TR_CONSTEXPR_STR
#endif

#if __cplusplus >= 202302L // _MSVC_LANG value for C++23 not available yet
#define TR_CONSTEXPR23 constexpr
#else
#define TR_CONSTEXPR23
#endif

// ---

#ifdef _WIN32
#define TR_IF_WIN32(ThenValue, ElseValue) ThenValue
#else
#define TR_IF_WIN32(ThenValue, ElseValue) ElseValue
#endif

#ifdef __UCLIBC__
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) \
    (__UCLIBC_MAJOR__ > (major) || (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ > (minor)) || \
     (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ == (minor) && __UCLIBC_SUBLEVEL__ >= (micro)))
#else
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) 0
#endif
