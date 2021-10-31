/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/***
****
***/

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#ifndef __has_extension
#define __has_extension __has_feature
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

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
#define TR_LIKELY(x) __builtin_expect(!!(x), 1)
#define TR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define TR_LIKELY(x) (x)
#define TR_UNLIKELY(x) (x)
#endif

/***
****
***/

#if __has_attribute(__deprecated__) || TR_GNUC_CHECK_VERSION(3, 1)
#define TR_DEPRECATED __attribute__((__deprecated__))
#elif defined(_MSC_VER)
#define TR_DEPRECATED __declspec(deprecated)
#else
#define TR_DEPRECATED
#endif

#if __has_attribute(__format__) || TR_GNUC_CHECK_VERSION(2, 3)
#define TR_GNUC_PRINTF(fmt, args) __attribute__((__format__(printf, fmt, args)))
#else
#define TR_GNUC_PRINTF(fmt, args)
#endif

#if __has_attribute(__nonnull__) || TR_GNUC_CHECK_VERSION(3, 3)
#define TR_GNUC_NONNULL(...) __attribute__((__nonnull__(__VA_ARGS__)))
#else
#define TR_GNUC_NONNULL(...)
#endif

#if __has_attribute(__sentinel__) || TR_GNUC_CHECK_VERSION(4, 3)
#define TR_GNUC_NULL_TERMINATED __attribute__((__sentinel__))
#else
#define TR_GNUC_NULL_TERMINATED
#endif

#if __has_attribute(__hot__) || TR_GNUC_CHECK_VERSION(4, 3)
#define TR_GNUC_HOT __attribute__((__hot__))
#else
#define TR_GNUC_HOT
#endif

#if __has_attribute(__malloc__) || TR_GNUC_CHECK_VERSION(2, 96)
#define TR_GNUC_MALLOC __attribute__((__malloc__))
#else
#define TR_GNUC_MALLOC
#endif

/***
****
***/

/* Only use this macro to suppress false-positive alignment warnings */
#define TR_DISCARD_ALIGN(ptr, type) ((type)(void*)(ptr))

#define TR_INET6_ADDRSTRLEN 46

#define TR_ADDRSTRLEN 64

#define TR_BAD_SIZE ((size_t)-1)

// Mostly to enforce better formatting
#define TR_ARG_TUPLE(...) __VA_ARGS__

// https://www.bittorrent.org/beps/bep_0003.html
// A string of length 20 which this downloader uses as its id. Each
// downloader generates its own id at random at the start of a new
// download. This value will also almost certainly have to be escaped.
auto inline constexpr PEER_ID_LEN = size_t{ 20 };
using tr_peer_id_t = std::array<char, PEER_ID_LEN>;

#define SHA_DIGEST_LENGTH 20

// TODO #1: all arrays of SHA_DIGEST_LENGTH should be replaced with tr_sha1_digest_t
// TODO #2: tr_peer_id_t, tr_sha1_digest_t should be moved into a new 'types.h' header
// TODO #3: this should be an array of std::byte
using tr_sha1_digest_t = std::array<uint8_t, 20>;
