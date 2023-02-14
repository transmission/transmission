// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tr-macros.h"

/**
 * @brief Implementation of the BitTorrent spec's Bitfield array of bits.
 *
 * This is for tracking the pieces a peer has. Its functionality is like
 * a bitset or vector<bool> with some added use cases:
 *
 * - It needs to be able to read/write the left-to-right bitfield format
 *   specified in the bittorrent spec. This is what raw() and getRaw()
 *   are for.
 *
 * - "Have all" is a special case where we know the peer has all the
 *   pieces and don't need to check the bit array. This is useful since
 *   (a) it's very common (i.e. seeds) and saves memory and work of
 *   allocating a bit array and doing lookups, and (b) if we have a
 *   magnet link and haven't gotten the metainfo yet, we may not know
 *   how many pieces there are -- but we can still know "this peer has
 *   all of them".
 *
 * - "Have none" is another special case that has the same advantages
 *   and motivations as "Have all".
 */
class tr_bitfield
{
public:
    explicit tr_bitfield(size_t bit_count);

    void setHasAll() noexcept;
    void setHasNone() noexcept;

    // set one or more bits
    void set(size_t nth, bool value = true);
    void setSpan(size_t begin, size_t end, bool value = true);
    void unset(size_t bit)
    {
        set(bit, false);
    }
    void unsetSpan(size_t begin, size_t end)
    {
        setSpan(begin, end, false);
    }
    void setFromBools(bool const* flags, size_t n);

    // "raw" here is in BEP0003 format: "The first byte of the bitfield
    // corresponds to indices 0 - 7 from high bit to low bit, respectively.
    // The next one 8-15, etc. Spare bits at the end are set to zero."
    void setRaw(uint8_t const* raw, size_t byte_count);
    [[nodiscard]] std::vector<uint8_t> raw() const;

    [[nodiscard]] constexpr bool hasAll() const noexcept
    {
        return have_all_hint_ || (bit_count_ > 0 && bit_count_ == true_count_);
    }

    [[nodiscard]] constexpr bool hasNone() const noexcept
    {
        return have_none_hint_ || (bit_count_ > 0 && true_count_ == 0);
    }

    [[nodiscard]] TR_CONSTEXPR20 bool test(size_t bit) const
    {
        return hasAll() || (!hasNone() && testFlag(bit));
    }

    [[nodiscard]] constexpr size_t count() const noexcept
    {
        return true_count_;
    }

    [[nodiscard]] size_t count(size_t begin, size_t end) const;

    [[nodiscard]] constexpr size_t size() const noexcept
    {
        return bit_count_;
    }

    [[nodiscard]] constexpr size_t empty() const noexcept
    {
        return size() == 0;
    }

    [[nodiscard]] bool isValid() const;

    [[nodiscard]] constexpr auto percent() const noexcept
    {
        if (hasAll())
        {
            return 1.0F;
        }

        if (hasNone() || empty())
        {
            return 0.0F;
        }

        return static_cast<float>(count()) / size();
    }

    tr_bitfield& operator|=(tr_bitfield const& that) noexcept;
    tr_bitfield& operator&=(tr_bitfield const& that) noexcept;

private:
    [[nodiscard]] size_t countFlags() const noexcept;
    [[nodiscard]] size_t countFlags(size_t begin, size_t end) const noexcept;

    [[nodiscard]] TR_CONSTEXPR20 bool testFlag(size_t n) const
    {
        if (n >> 3U >= std::size(flags_))
        {
            return false;
        }

        bool ret = (flags_[n >> 3U] << (n & 7U) & 0x80) != 0;
        return ret;
    }

    void ensureBitsAlloced(size_t n);
    [[nodiscard]] bool ensureNthBitAlloced(size_t nth);

    void freeArray() noexcept
    {
        // move-assign to ensure the reserve memory is cleared
        flags_ = std::vector<uint8_t>{};
    }

    void incrementTrueCount(size_t inc) noexcept;
    void decrementTrueCount(size_t dec) noexcept;
    void setTrueCount(size_t n) noexcept;
    void rebuildTrueCount() noexcept
    {
        setTrueCount(countFlags());
    }

    std::vector<uint8_t> flags_;

    size_t bit_count_ = 0;
    size_t true_count_ = 0;

    /* Special cases for when full or empty but we don't know the bitCount.
       This occurs when a magnet link's peers send have all / have none */
    bool have_all_hint_ = false;
    bool have_none_hint_ = false;
};
