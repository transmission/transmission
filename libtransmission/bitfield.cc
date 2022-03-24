// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <vector>

#include "transmission.h"

#include "bitfield.h"
#include "tr-assert.h"
#include "utils.h" /* tr_new0() */

/****
*****
****/

namespace
{

constexpr size_t getBytesNeeded(size_t bit_count)
{
    return (bit_count >> 3) + ((bit_count & 7) != 0 ? 1 : 0);
}

void setAllTrue(uint8_t* array, size_t bit_count)
{
    uint8_t constexpr Val = 0xFF;
    size_t const n = getBytesNeeded(bit_count);

    if (n > 0)
    {
        std::fill_n(array, n, Val);
        array[n - 1] = Val << (n * 8 - bit_count);
    }
}

constexpr int8_t const trueBitCount[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2,
    3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3,
    3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5,
    6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4,
    3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4,
    5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6,
    6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

} // namespace

/****
*****
****/

size_t tr_bitfield::countFlags() const
{
    size_t ret = 0;

    for (auto ch : flags_)
    {
        ret += trueBitCount[ch];
    }

    return ret;
}

size_t tr_bitfield::countFlags(size_t begin, size_t end) const
{
    size_t ret = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    if (bit_count_ == 0)
    {
        return 0;
    }

    if (first_byte >= std::size(flags_))
    {
        return 0;
    }

    TR_ASSERT(begin < end);
    TR_ASSERT(!std::empty(flags_));

    if (first_byte == last_byte)
    {
        uint8_t val = flags_[first_byte];

        auto i = begin - (first_byte * 8);
        val <<= i;
        val >>= i;
        i = (last_byte + 1) * 8 - end;
        val >>= i;
        val <<= i;

        ret += trueBitCount[val];
    }
    else
    {
        size_t const walk_end = std::min(std::size(flags_), last_byte);

        /* first byte */
        size_t const first_shift = begin - (first_byte * 8);
        uint8_t val = flags_[first_byte];
        val <<= first_shift;
        val >>= first_shift;
        ret += trueBitCount[val];

        /* middle bytes */
        for (size_t i = first_byte + 1; i < walk_end; ++i)
        {
            ret += trueBitCount[flags_[i]];
        }

        /* last byte */
        if (last_byte < std::size(flags_))
        {
            size_t const last_shift = (last_byte + 1) * 8 - end;
            val = flags_[last_byte];
            val >>= last_shift;
            val <<= last_shift;
            ret += trueBitCount[val];
        }
    }

    TR_ASSERT(ret <= (begin - end));
    return ret;
}

size_t tr_bitfield::count(size_t begin, size_t end) const
{
    if (hasAll())
    {
        return end - begin;
    }

    if (hasNone())
    {
        return 0;
    }

    return countFlags(begin, end);
}

bool tr_bitfield::testFlag(size_t n) const
{
    if (n >> 3U >= std::size(flags_))
    {
        return false;
    }

    bool ret = (flags_[n >> 3U] << (n & 7U) & 0x80) != 0;
    return ret;
}

/***
****
***/

bool tr_bitfield::isValid() const
{
    return std::empty(flags_) || true_count_ == countFlags();
}

std::vector<uint8_t> tr_bitfield::raw() const
{
    auto const n = getBytesNeeded(bit_count_);

    if (!std::empty(flags_))
    {
        return flags_;
    }

    auto raw = std::vector<uint8_t>(n);

    if (hasAll())
    {
        setAllTrue(std::data(raw), bit_count_);
    }

    return raw;
}

void tr_bitfield::ensureBitsAlloced(size_t n)
{
    bool const has_all = hasAll();

    size_t const bytes_needed = has_all ? getBytesNeeded(std::max(n, true_count_)) : getBytesNeeded(n);

    if (std::size(flags_) < bytes_needed)
    {
        flags_.resize(bytes_needed);

        if (has_all)
        {
            setAllTrue(std::data(flags_), true_count_);
        }
    }
}

bool tr_bitfield::ensureNthBitAlloced(size_t nth)
{
    // count is zero-based, so we need to allocate nth+1 bits before setting the nth */
    if (nth == SIZE_MAX)
    {
        return false;
    }

    ensureBitsAlloced(nth + 1);
    return true;
}

void tr_bitfield::freeArray()
{
    flags_ = std::vector<uint8_t>{};
}

void tr_bitfield::setTrueCount(size_t n)
{
    TR_ASSERT_MSG(
        bit_count_ == 0 || n <= bit_count_,
        "bit_count_:%zu, n:%zu, std::size(flags_):%zu",
        bit_count_,
        n,
        size_t(std::size(flags_)));

    true_count_ = n;
    have_all_hint_ = n == bit_count_;
    have_none_hint_ = n == 0;

    if (hasAll() || hasNone())
    {
        freeArray();
    }

    TR_ASSERT(isValid());
}

void tr_bitfield::rebuildTrueCount()
{
    setTrueCount(countFlags());
}

void tr_bitfield::incrementTrueCount(size_t inc)
{
    TR_ASSERT(bit_count_ == 0 || inc <= bit_count_);
    TR_ASSERT(bit_count_ == 0 || true_count_ <= bit_count_ - inc);

    setTrueCount(true_count_ + inc);
}

void tr_bitfield::decrementTrueCount(size_t dec)
{
    TR_ASSERT(bit_count_ == 0 || dec <= bit_count_);
    TR_ASSERT(bit_count_ == 0 || true_count_ >= dec);

    setTrueCount(true_count_ - dec);
}

/****
*****
****/

tr_bitfield::tr_bitfield(size_t bit_count)
    : bit_count_{ bit_count }
{
    TR_ASSERT(isValid());
}

void tr_bitfield::setHasNone()
{
    freeArray();
    true_count_ = 0;
    have_all_hint_ = false;
    have_none_hint_ = true;

    TR_ASSERT(isValid());
}

void tr_bitfield::setHasAll()
{
    freeArray();
    true_count_ = bit_count_;
    have_all_hint_ = true;
    have_none_hint_ = false;

    TR_ASSERT(isValid());
}

void tr_bitfield::setRaw(uint8_t const* raw, size_t byte_count)
{
    flags_.assign(raw, raw + byte_count);

    // ensure any excess bits at the end of the array are set to '0'.
    if (byte_count == getBytesNeeded(bit_count_))
    {
        auto const excess_bit_count = byte_count * 8 - bit_count_;

        TR_ASSERT(excess_bit_count <= 7);

        if (excess_bit_count != 0)
        {
            flags_.back() &= 0xff << excess_bit_count;
        }
    }

    rebuildTrueCount();
}

void tr_bitfield::setFromBools(bool const* flags, size_t n)
{
    size_t trueCount = 0;

    freeArray();
    ensureBitsAlloced(n);

    for (size_t i = 0; i < n; ++i)
    {
        if (flags[i])
        {
            ++trueCount;
            flags_[i >> 3U] |= (0x80 >> (i & 7U));
        }
    }

    setTrueCount(trueCount);
}

void tr_bitfield::set(size_t nth, bool value)
{
    if (test(nth) == value)
    {
        return;
    }

    if (!ensureNthBitAlloced(nth))
    {
        return;
    }

    if (value)
    {
        flags_[nth >> 3U] |= 0x80 >> (nth & 7U);
        incrementTrueCount(1);
    }
    else
    {
        flags_[nth >> 3U] &= 0xff7f >> (nth & 7U);
        decrementTrueCount(1);
    }
}

/* Sets bit range [begin, end) to 1 */
void tr_bitfield::setSpan(size_t begin, size_t end, bool value)
{
    // bounds check
    end = std::min(end, bit_count_);
    if (end == 0 || begin >= end)
    {
        return;
    }

    // did anything change?
    size_t const old_count = count(begin, end);
    size_t const new_count = value ? (end - begin) : 0;
    if (old_count == new_count)
    {
        return;
    }

    --end;
    if (!ensureNthBitAlloced(end))
    {
        return;
    }

    auto walk = begin >> 3;
    auto const last_byte = end >> 3;

    if (value)
    {
        unsigned char const first_mask = ~(0xff << (8 - (begin & 7)));
        unsigned char const last_mask = 0xff << (7 - (end & 7));

        if (walk == last_byte)
        {
            flags_[walk] |= first_mask & last_mask;
        }
        else
        {
            flags_[walk] |= first_mask;
            flags_[last_byte] |= last_mask;

            if (++walk < last_byte)
            {
                std::fill_n(std::data(flags_) + walk, last_byte - walk, 0xff);
            }
        }

        incrementTrueCount(new_count - old_count);
    }
    else
    {
        unsigned char const first_mask = 0xff << (8 - (begin & 7));
        unsigned char const last_mask = ~(0xff << (7 - (end & 7)));

        if (walk == last_byte)
        {
            flags_[walk] &= first_mask | last_mask;
        }
        else
        {
            flags_[walk] &= first_mask;
            flags_[last_byte] &= last_mask;

            if (++walk < last_byte)
            {
                std::fill_n(std::data(flags_) + walk, last_byte - walk, 0);
            }
        }

        decrementTrueCount(old_count);
    }
}
