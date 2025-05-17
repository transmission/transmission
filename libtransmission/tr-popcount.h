// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_POPCNT_H
#define TR_POPCNT_H

#include <array>
#include <cstdint>
#include <type_traits>

/* Avoid defining irrelevant helpers that might interfere with other
 * preprocessor logic. */
#if (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L) || __cplusplus >= 202002L
#define TR_HAVE_STD_POPCOUNT
#endif

#if defined(TR_HAVE_STD_POPCOUNT)
#include <bit>
#endif

template<typename T>
struct tr_popcnt
{
    static_assert(std::is_integral_v<T> != 0, "Can only popcnt integral types");

    static_assert(sizeof(T) <= sizeof(uint64_t), "Unsupported size");

    /* Needed regularly to avoid sign extension / get unsigned shift behavior.
     */
    using unsigned_T = typename std::make_unsigned_t<T>;

    /* Sanity tests. */
    static_assert(sizeof(unsigned_T) == sizeof(T), "Unsigned type somehow smaller than signed type");
    static_assert(std::is_integral_v<unsigned_T> != 0, "Unsigned type somehow non integral");

#if defined(TR_HAVE_STD_POPCOUNT)
    /* If we have std::popcount just use that. */
    static constexpr unsigned count(T v)
    {
        auto const unsigned_v = static_cast<unsigned_T>(v);
        return static_cast<unsigned>(std::popcount(unsigned_v));
    }
#else
    /* Generic implementation. */
    static constexpr unsigned count(T v)
    {
        /* To avoid signed shifts. */
        auto unsigned_v = static_cast<unsigned_T>(v);

        if constexpr (sizeof(T) <= sizeof(uint16_t))
        {
            /* Use LUT for small sizes. In reality we only need half a
             * byte for each value if ever hit a case where perf is
             * limited by severe bottleneck on L1D this can be
             * optimized. */
            constexpr auto PopcntLut = std::array<uint8_t, 256>{
                0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2,
                3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
                3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5,
                6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
                3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4,
                5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6,
                6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
            };
            if constexpr (sizeof(T) == sizeof(uint8_t))
            {
                return PopcntLut[unsigned_v];
            }
            else
            {
                return PopcntLut[unsigned_v & 0xFF] + PopcntLut[unsigned_v >> 8];
            }
        }
        else
        {
            /* for larger sizes use implementation described here:
             * http://en.wikipedia.org/wiki/Hamming_weight#Efficient_implementation
             */
            constexpr auto M1 = static_cast<unsigned_T>(0x5555555555555555LL);
            constexpr auto M2 = static_cast<unsigned_T>(0x3333333333333333LL);
            constexpr auto M4 = static_cast<unsigned_T>(0x0F0F0F0F0F0F0F0FLL);
            constexpr auto H01 = static_cast<unsigned_T>(0x0101010101010101LL);

            unsigned_v = unsigned_v - ((unsigned_v >> 1) & M1);
            unsigned_v = (unsigned_v & M2) + ((unsigned_v >> 2) & M2);
            unsigned_v = (unsigned_v + (unsigned_v >> 4)) & M4;

            unsigned_v = unsigned_v * H01;
            return unsigned_v >> (8 * sizeof(T) - 8);
        }
    }
#endif
};

#endif
