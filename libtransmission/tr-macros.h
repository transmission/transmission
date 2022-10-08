// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t

/***
****
***/

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#ifdef _WIN32
#define TR_IF_WIN32(ThenValue, ElseValue) ThenValue
#else
#define TR_IF_WIN32(ThenValue, ElseValue) ElseValue
#endif

#ifdef __GNUC__
#define TR_GNUC_CHECK_VERSION(major, minor) (__GNUC__ > (major) || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor)))
#else
#define TR_GNUC_CHECK_VERSION(major, minor) 0
#endif

#ifdef __UCLIBC__
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) \
    (__UCLIBC_MAJOR__ > (major) || (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ > (minor)) || \
     (__UCLIBC_MAJOR__ == (major) && __UCLIBC_MINOR__ == (minor) && __UCLIBC_SUBLEVEL__ >= (micro)))
#else
#define TR_UCLIBC_CHECK_VERSION(major, minor, micro) 0
#endif

/***
****
***/

#if __has_builtin(__builtin_expect) || TR_GNUC_CHECK_VERSION(3, 0)
#define TR_LIKELY(x) __builtin_expect((x) ? 1L : 0L, 1L)
#define TR_UNLIKELY(x) __builtin_expect((x) ? 1L : 0L, 0L)
#else
#define TR_LIKELY(x) (x)
#define TR_UNLIKELY(x) (x)
#endif

#define TR_DISABLE_COPY_MOVE(Class) \
    Class& operator=(Class const&) = delete; \
    Class& operator=(Class&&) = delete; \
    Class(Class const&) = delete; \
    Class(Class&&) = delete;

/***
****
***/

#define TR_PATH_DELIMITER '/'

/* Only use this macro to suppress false-positive alignment warnings */
#define TR_DISCARD_ALIGN(ptr, type) ((type)(void*)(ptr))

#define TR_INET6_ADDRSTRLEN 46

#define TR_ADDRSTRLEN 64

// Mostly to enforce better formatting
#define TR_ARG_TUPLE(...) __VA_ARGS__

// https://www.bittorrent.org/beps/bep_0003.html
// A string of length 20 which this downloader uses as its id. Each
// downloader generates its own id at random at the start of a new
// download. This value will also almost certainly have to be escaped.
auto inline constexpr PEER_ID_LEN = size_t{ 20 };
using tr_peer_id_t = std::array<char, PEER_ID_LEN>;

auto inline constexpr TR_SHA1_DIGEST_STRLEN = size_t{ 40 };
using tr_sha1_digest_t = std::array<std::byte, 20>;
using tr_sha1_digest_string_t = std::array<char, TR_SHA1_DIGEST_STRLEN + 1>; // +1 for '\0'

auto inline constexpr TR_SHA256_DIGEST_STRLEN = size_t{ 64 };
using tr_sha256_digest_t = std::array<std::byte, 32>;
using tr_sha256_digest_string_t = std::array<char, TR_SHA256_DIGEST_STRLEN + 1>; // +1 for '\0'
