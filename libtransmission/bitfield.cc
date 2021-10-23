/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

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

static void setAllTrue(uint8_t* array, size_t bit_count)
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

size_t tr_bitfield::recount() const
{
    size_t ret = 0;

    for (auto ch : this->bits)
    {
        ret += trueBitCount[ch];
    }

    return ret;
}

size_t tr_bitfield::recount(size_t begin, size_t end) const
{
    size_t ret = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    if (this->bit_count == 0)
    {
        return 0;
    }

    if (first_byte >= std::size(this->bits))
    {
        return 0;
    }

    TR_ASSERT(begin < end);
    TR_ASSERT(!std::empty(this->bits));

    if (first_byte == last_byte)
    {
        int i;
        uint8_t val = this->bits[first_byte];

        i = begin - (first_byte * 8);
        val <<= i;
        val >>= i;
        i = (last_byte + 1) * 8 - end;
        val >>= i;
        val <<= i;

        ret += trueBitCount[val];
    }
    else
    {
        uint8_t val;
        size_t const walk_end = std::min(std::size(this->bits), last_byte);

        /* first byte */
        size_t const first_shift = begin - (first_byte * 8);
        val = this->bits[first_byte];
        val <<= first_shift;
        val >>= first_shift;
        ret += trueBitCount[val];

        /* middle bytes */
        for (size_t i = first_byte + 1; i < walk_end; ++i)
        {
            ret += trueBitCount[this->bits[i]];
        }

        /* last byte */
        if (last_byte < std::size(this->bits))
        {
            size_t const last_shift = (last_byte + 1) * 8 - end;
            val = this->bits[last_byte];
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

    return recount(begin, end);
}

bool tr_bitfield::test(size_t n) const
{
    if (hasAll())
    {
        return true;
    }

    if (hasNone())
    {
        return false;
    }

    if (n >> 3U >= std::size(this->bits))
    {
        return false;
    }

    bool ret = (this->bits[n >> 3U] << (n & 7U) & 0x80) != 0;
    return ret;
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

bool tr_bitfield::assertValid() const
{
    TR_ASSERT(std::empty(this->bits) || this->true_count == recount());

    return true;
}

#endif

std::vector<uint8_t> tr_bitfield::raw() const
{
    auto const n = getBytesNeeded(this->bit_count);

    if (!std::empty(this->bits))
    {
        return this->bits;
    }

    auto raw = std::vector<uint8_t>(n);

    if (hasAll())
    {
        setAllTrue(std::data(raw), std::size(raw));
    }

    return raw;
}

void tr_bitfield::ensureBitsAlloced(size_t n)
{
    bool const has_all = hasAll();

    size_t const bytes_needed = has_all ? getBytesNeeded(std::max(n, this->true_count)) : getBytesNeeded(n);

    if (std::size(this->bits) < bytes_needed)
    {
        this->bits.resize(bytes_needed);

        if (has_all)
        {
            setAllTrue(std::data(this->bits), this->true_count);
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
    this->bits = std::vector<uint8_t>{};
}

void tr_bitfield::setTrueCount(size_t n)
{
    TR_ASSERT(this->bit_count == 0 || n <= this->bit_count);

    this->true_count = n;
    this->have_all_hint = n == this->bit_count;
    this->have_none_hint = n == 0;

    if (hasAll() || hasNone())
    {
        freeArray();
    }

    TR_ASSERT(assertValid());
}

void tr_bitfield::rebuildTrueCount()
{
    setTrueCount(recount());
}

void tr_bitfield::incrementTrueCount(size_t inc)
{
    TR_ASSERT(this->bit_count == 0 || inc <= this->bit_count);
    TR_ASSERT(this->bit_count == 0 || this->true_count <= this->bit_count - inc);

    setTrueCount(this->true_count + inc);
}

void tr_bitfield::decrementTrueCount(size_t dec)
{
    TR_ASSERT(this->bit_count == 0 || dec <= this->bit_count);
    TR_ASSERT(this->bit_count == 0 || this->true_count >= dec);

    setTrueCount(this->true_count - dec);
}

/****
*****
****/

tr_bitfield::tr_bitfield(size_t bit_count_in)
    : bit_count{ bit_count_in }
{
    TR_ASSERT(assertValid());
}

void tr_bitfield::setHasNone()
{
    freeArray();
    this->true_count = 0;
    this->have_all_hint = false;
    this->have_none_hint = true;

    TR_ASSERT(assertValid());
}

void tr_bitfield::setHasAll()
{
    freeArray();
    this->true_count = this->bit_count;
    this->have_all_hint = true;
    this->have_none_hint = false;

    TR_ASSERT(assertValid());
}

void tr_bitfield::setRaw(uint8_t const* raw, size_t byte_count, bool bounded)
{
    if (bounded)
    {
        byte_count = std::min(byte_count, getBytesNeeded(this->bit_count));
    }

    this->bits = std::vector<uint8_t>(raw, raw + byte_count);

    if (bounded)
    {
        /* ensure the excess bits are set to '0' */
        int const excess_bit_count = byte_count * 8 - this->bit_count;

        TR_ASSERT(excess_bit_count >= 0);
        TR_ASSERT(excess_bit_count <= 7);

        if (excess_bit_count != 0)
        {
            this->bits.back() &= 0xff << excess_bit_count;
        }
    }

    rebuildTrueCount();
}

void tr_bitfield::setFromFlags(bool const* flags, size_t n)
{
    size_t trueCount = 0;

    freeArray();
    ensureBitsAlloced(n);

    for (size_t i = 0; i < n; ++i)
    {
        if (flags[i])
        {
            ++trueCount;
            this->bits[i >> 3U] |= (0x80 >> (i & 7U));
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
        this->bits[nth >> 3U] |= 0x80 >> (nth & 7U);
        incrementTrueCount(1);
    }
    else
    {
        this->bits[nth >> 3U] &= 0xff7f >> (nth & 7U);
        decrementTrueCount(1);
    }
}

/* Sets bit range [begin, end) to 1 */
void tr_bitfield::setRange(size_t begin, size_t end, bool value)
{
    // did anything change?
    size_t const old_count = count(begin, end);
    size_t const new_count = value ? (end - begin) : 0;
    if (old_count == new_count)
    {
        return;
    }

    // bounds check
    if (--end >= this->bit_count || begin > end)
    {
        return;
    }

    size_t sb = begin >> 3;
    size_t const eb = end >> 3;

    if (!ensureNthBitAlloced(end))
    {
        return;
    }

    if (value)
    {
        unsigned char const sm = ~(0xff << (8 - (begin & 7)));
        unsigned char const em = 0xff << (7 - (end & 7));

        if (sb == eb)
        {
            this->bits[sb] |= sm & em;
        }
        else
        {
            this->bits[sb] |= sm;
            this->bits[eb] |= em;

            if (++sb < eb)
            {
                std::fill_n(std::begin(this->bits) + sb, eb - sb, 0xff);
            }
        }

        incrementTrueCount(new_count - old_count);
    }
    else
    {
        unsigned char const sm = 0xff << (8 - (begin & 7));
        unsigned char const em = ~(0xff << (7 - (end & 7)));

        if (sb == eb)
        {
            this->bits[sb] &= sm | em;
        }
        else
        {
            this->bits[sb] &= sm;
            this->bits[eb] &= em;

            if (++sb < eb)
            {
                std::fill_n(std::begin(this->bits) + sb, eb - sb, 0);
            }
        }

        decrementTrueCount(old_count);
    }
}
