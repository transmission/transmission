/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cstring> /* memset */

#include "transmission.h"
#include "bitfield.h"
#include "tr-assert.h"
#include "utils.h" /* tr_new0() */

/****
*****
****/

static constexpr int8_t const trueBitCount[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, //
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, //
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, //
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, //
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, //
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, //
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, //
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, //
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, //
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, //
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8, //
};

constexpr size_t tr_bitfield::countArray() const
{
    size_t ret = 0;
    size_t i = this->alloc_count;

    while (i > 0)
    {
        ret += trueBitCount[this->bits[--i]];
    }

    return ret;
}

size_t tr_bitfield::countRangeImpl(size_t begin, size_t end) const
{
    size_t ret = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    if (this->bit_count == 0)
    {
        return 0;
    }

    if (first_byte >= this->alloc_count)
    {
        return 0;
    }

    TR_ASSERT(begin < end);
    TR_ASSERT(this->bits != nullptr);

    if (first_byte == last_byte)
    {
        uint8_t val = this->bits[first_byte];

        int i = begin - (first_byte * 8);
        val <<= i;
        val >>= i;
        i = (last_byte + 1) * 8 - end;
        val >>= i;
        val <<= i;

        ret += trueBitCount[val];
    }
    else
    {
        size_t const walk_end = std::min(this->alloc_count, last_byte);

        /* first byte */
        size_t const first_shift = begin - (first_byte * 8);
        uint8_t val = this->bits[first_byte];
        val <<= first_shift;
        val >>= first_shift;
        ret += trueBitCount[val];

        /* middle bytes */
        for (size_t i = first_byte + 1; i < walk_end; ++i)
        {
            ret += trueBitCount[this->bits[i]];
        }

        /* last byte */
        if (last_byte < this->alloc_count)
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

bool tr_bitfield::has(size_t n) const
{
    if (this->hasAll())
    {
        return true;
    }

    if (this->hasNone())
    {
        return false;
    }

    if (n >> 3U >= this->alloc_count)
    {
        return false;
    }

    return (this->bits[n >> 3U] << (n & 7U) & 0x80) != 0;
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

bool tr_bitfield::isValid() const
{
    TR_ASSERT((this->alloc_count == 0) == (this->bits == nullptr));
    TR_ASSERT(this->bits == nullptr || this->true_count == this->countArray());

    return true;
}

#endif

size_t tr_bitfield::countTrueBits() const
{
    TR_ASSERT(this->isValid());

    return this->true_count;
}

void tr_bitfield::set_all_true(uint8_t* array, size_t bit_count)
{
    uint8_t const val = 0xFF;
    size_t const n = get_bytes_needed(bit_count);

    if (n > 0)
    {
        memset(array, val, n - 1);

        array[n - 1] = val << (n * 8 - bit_count);
    }
}

void* tr_bitfield::getRaw(size_t* byte_count) const
{
    TR_ASSERT(this->bit_count > 0);

    size_t const n = get_bytes_needed(this->bit_count);
    uint8_t* newBits = tr_new0(uint8_t, n);

    if (this->alloc_count != 0)
    {
        TR_ASSERT(this->alloc_count <= n);
        std::memcpy(newBits, this->bits, this->alloc_count);
    }
    else if (this->hasAll())
    {
        set_all_true(newBits, this->bit_count);
    }

    *byte_count = n;
    return newBits;
}

void tr_bitfield::ensureBitsAlloced(size_t n)
{
    size_t bytes_needed;
    bool const has_all = this->hasAll();

    if (has_all)
    {
        bytes_needed = get_bytes_needed(std::max(n, this->true_count));
    }
    else
    {
        bytes_needed = get_bytes_needed(n);
    }

    if (this->alloc_count < bytes_needed)
    {
        this->bits = tr_renew(uint8_t, this->bits, bytes_needed);
        std::memset(this->bits + this->alloc_count, 0, bytes_needed - this->alloc_count);
        this->alloc_count = bytes_needed;

        if (has_all)
        {
            set_all_true(this->bits, this->true_count);
        }
    }
}

bool tr_bitfield::ensureNthBitAlloced(size_t nth)
{
    // count is zero-based, so we need to allocate nth+1 bits before setting the nth
    if (nth == SIZE_MAX)
    {
        return false;
    }

    this->ensureBitsAlloced(nth + 1);
    return true;
}

void tr_bitfield::freeArray()
{
    tr_free(this->bits);
    this->bits = nullptr;
    this->alloc_count = 0;
}

void tr_bitfield::setTrueCount(size_t n)
{
    TR_ASSERT(this->bit_count == 0 || n <= this->bit_count);

    this->true_count = n;

    if (this->hasAll() || this->hasNone())
    {
        this->freeArray();
    }

    TR_ASSERT(this->isValid());
}

void tr_bitfield::rebuildTrueCount()
{
    this->setTrueCount(this->countArray());
}

void tr_bitfield::incTrueCount(size_t i)
{
    TR_ASSERT(this->bit_count == 0 || i <= this->bit_count);
    TR_ASSERT(this->bit_count == 0 || this->true_count <= this->bit_count - i);

    this->setTrueCount(this->true_count + i);
}

void tr_bitfield::decTrueCount(size_t i)
{
    TR_ASSERT(this->bit_count == 0 || i <= this->bit_count);
    TR_ASSERT(this->bit_count == 0 || this->true_count >= i);

    this->setTrueCount(this->true_count - i);
}

/****
*****
****/

tr_bitfield::tr_bitfield(size_t bit_count)
{
    this->bit_count = bit_count;
    this->true_count = 0;
    this->bits = nullptr;
    this->alloc_count = 0;
    this->have_all_hint = false;
    this->have_none_hint = false;

    TR_ASSERT(this->isValid());
}

void tr_bitfield::setHasNone()
{
    this->freeArray();
    this->true_count = 0;
    this->have_all_hint = false;
    this->have_none_hint = true;

    TR_ASSERT(this->isValid());
}

void tr_bitfield::setHasAll()
{
    this->freeArray();
    this->true_count = this->bit_count;
    this->have_all_hint = true;
    this->have_none_hint = false;

    TR_ASSERT(this->isValid());
}

void tr_bitfield::setFromBitfield(tr_bitfield const* src)
{
    if (src->hasAll())
    {
        this->setHasAll();
    }
    else if (src->hasNone())
    {
        this->setHasNone();
    }
    else
    {
        this->setRaw(src->bits, src->alloc_count, true);
    }
}

void tr_bitfield::setRaw(void const* newBits, size_t byte_count, bool bounded)
{
    this->freeArray();
    this->true_count = 0;

    if (bounded)
    {
        byte_count = std::min(byte_count, get_bytes_needed(this->bit_count));
    }

    this->bits = static_cast<uint8_t*>(tr_memdup(newBits, byte_count));
    this->alloc_count = byte_count;

    if (bounded)
    {
        /* ensure the excess newBits are set to '0' */
        int const excess_bit_count = byte_count * 8 - this->bit_count;

        TR_ASSERT(excess_bit_count >= 0);
        TR_ASSERT(excess_bit_count <= 7);

        if (excess_bit_count != 0)
        {
            this->bits[this->alloc_count - 1] &= 0xff << excess_bit_count;
        }
    }

    this->rebuildTrueCount();
}

void tr_bitfield::setFromFlags(bool const* flags, size_t n)
{
    size_t trueCount = 0;

    this->freeArray();
    this->ensureBitsAlloced(n);

    for (size_t i = 0; i < n; ++i)
    {
        if (flags[i] && this->bits != nullptr)
        {
            ++trueCount;
            this->bits[i >> 3U] |= (0x80 >> (i & 7U));
        }
    }

    this->setTrueCount(trueCount);
}

void tr_bitfield::add(size_t nth)
{
    if (!this->has(nth) && this->ensureNthBitAlloced(nth))
    {
        size_t const offset = nth >> 3U;

        if ((this->bits != nullptr) && (offset < this->alloc_count))
        {
            this->bits[offset] |= 0x80 >> (nth & 7U);
            this->incTrueCount(1);
        }
    }
}

void tr_bitfield::addRange(size_t begin, size_t end)
{
    size_t sb;
    size_t eb;
    unsigned char sm;
    unsigned char em;
    size_t const diff = (end - begin) - this->countRange(begin, end);

    if (diff == 0)
    {
        return;
    }

    end--;

    if (end >= this->bit_count || begin > end)
    {
        return;
    }

    sb = begin >> 3;
    sm = ~(0xff << (8 - (begin & 7)));
    eb = end >> 3;
    em = 0xff << (7 - (end & 7));

    if (!this->ensureNthBitAlloced(end))
    {
        return;
    }

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
            std::memset(this->bits + sb, 0xff, eb - sb);
        }
    }

    this->incTrueCount(diff);
}

void tr_bitfield::rem(size_t nth)
{
    TR_ASSERT(this->isValid());

    if (this->has(nth) && this->ensureNthBitAlloced(nth))
    {
        this->bits[nth >> 3U] &= 0xff7f >> (nth & 7U);
        this->decTrueCount(1);
    }
}

void tr_bitfield::remRange(size_t begin, size_t end)
{
    size_t sb;
    size_t eb;
    unsigned char sm;
    unsigned char em;
    size_t const diff = this->countRange(begin, end);

    if (diff == 0)
    {
        return;
    }

    end--;

    if (end >= this->bit_count || begin > end)
    {
        return;
    }

    sb = begin >> 3;
    sm = 0xff << (8 - (begin & 7));
    eb = end >> 3;
    em = ~(0xff << (7 - (end & 7)));

    if (!this->ensureNthBitAlloced(end))
    {
        return;
    }

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
            std::memset(this->bits + sb, 0, eb - sb);
        }
    }

    this->decTrueCount(diff);
}
