// This file Copyright © 2021-2023 Mike Gelfand
// It may be used under the 3-clause BSD (SPDX: BSD-3-Clause).
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t
#include <cstdint> // uint8_t

/**
 * This is a tiny and reusable implementation of alleged RC4 cipher.
 * https://en.wikipedia.org/wiki/RC4
 *
 * The use of RC4 is declining due to security concerns.
 * Popular cryptographic libraries have deprecated or removed it:
 *
 *  - OpenSSL: disabled by default in 1.1, moved to "legacy" in 3.0
 *  - WolfSSL (CyaSSL): disabled by default in 3.4.6
 *  - MbedTLS (PolarSSL): removed in 3.0
 *
 * Nonetheless it's still used in BitTorrent Protocol Encryption
 * https://en.wikipedia.org/wiki/BitTorrent_protocol_encryption,
 * so this header file provides an implementation.
 */
class tr_arc4
{
public:
    constexpr tr_arc4() = default;

    constexpr tr_arc4(void const* key, size_t key_length)
    {
        init(key, key_length);
    }

    constexpr void init(void const* key, size_t key_length)
    {
        for (size_t i = 0; i < 256; ++i)
        {
            s_[i] = static_cast<uint8_t>(i);
        }

        for (size_t i = 0, j = 0; i < 256; ++i)
        {
            j = static_cast<uint8_t>(j + s_[i] + ((uint8_t const*)key)[i % key_length]);
            arc4_swap(i, j);
        }
    }

    constexpr void process(void const* src_data, void* dst_data, size_t data_length)
    {
        for (size_t i = 0; i < data_length; ++i)
        {
            static_cast<uint8_t*>(dst_data)[i] = static_cast<uint8_t const*>(src_data)[i] ^ arc4_next();
        }
    }

    constexpr void discard(size_t length)
    {
        while (length-- > 0)
        {
            arc4_next();
        }
    }

private:
    constexpr void arc4_swap(size_t i, size_t j)
    {
        auto const tmp = s_[i];
        s_[i] = s_[j];
        s_[j] = tmp;
    }

    constexpr uint8_t arc4_next()
    {
        i_ += 1;
        j_ += s_[i_];

        arc4_swap(i_, j_);

        return s_[static_cast<uint8_t>(s_[i_] + s_[j_])];
    }

    std::array<uint8_t, 256> s_ = {};
    uint8_t i_ = 0;
    uint8_t j_ = 0;
};
