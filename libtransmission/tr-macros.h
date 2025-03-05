// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // for std::byte
#include <cstdint> // for uint32_t

// ---

#if __cplusplus >= 202002L || _MSVC_LANG >= 202002L
#define TR_CONSTEXPR20 constexpr
#else
#define TR_CONSTEXPR20
#endif

// Placeholder for future use.
// Can't implement right now because __cplusplus version for C++23 is currently TBD
#define TR_CONSTEXPR23

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

// ---

#define TR_INET6_ADDRSTRLEN 46

#define TR_ADDRSTRLEN 64

// https://www.bittorrent.org/beps/bep_0007.html
// "The client SHOULD include a key parameter in its announces. The key
// should remain the same for a particular infohash during a torrent
// session. Together with the peer_id this allows trackers to uniquely
// identify clients for the purpose of statistics-keeping when they
// announce from multiple IP.
// The key should be generated so it has at least 32bits worth of entropy."
//
// https://www.bittorrent.org/beps/bep_0015.html
// "Clients that resolve hostnames to v4 and v6 and then announce to both
// should use the same [32-bit integer] key for both so that trackers that
// care about accurate statistics-keeping can match the two announces."
using tr_announce_key_t = uint32_t;

// https://www.bittorrent.org/beps/bep_0003.html
// A string of length 20 which this downloader uses as its id. Each
// downloader generates its own id at random at the start of a new
// download. This value will also almost certainly have to be escaped.
using tr_peer_id_t = std::array<char, 20>;

using tr_sha1_digest_t = std::array<std::byte, 20>;

using tr_sha256_digest_t = std::array<std::byte, 32>;
