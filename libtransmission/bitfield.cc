/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cstring> // std::memset
#include <iterator> // std::back_inserter (on Windows)
#include <numeric> // std::accumulate

#include "transmission.h"
#include "bitfield.h"
#include "tr-assert.h"
#include "span.h"

/****
*****
****/

std::array<int8_t const, 256> Bitfield::true_bits_lookup_ = {
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

size_t Bitfield::countArray() const
{
    return std::accumulate(
        std::begin(this->bits_),
        std::end(this->bits_),
        0,
        [](auto acc, auto item) { return acc + true_bits_lookup_[item]; });
}

size_t Bitfield::countRangeImpl(size_t begin, size_t end) const
{
    size_t ret = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    if (this->bit_count_ == 0)
    {
        return 0;
    }

    if (first_byte >= std::size(this->bits_))
    {
        return 0;
    }

    TR_ASSERT(begin < end);

    if (first_byte == last_byte)
    {
        uint8_t val = this->bits_[first_byte];

        int i = begin - (first_byte * 8);
        val <<= i;
        val >>= i;
        i = (last_byte + 1) * 8 - end;
        val >>= i;
        val <<= i;

        ret += true_bits_lookup_[val];
    }
    else
    {
        size_t const walk_end = std::min(std::size(this->bits_), last_byte);

        /* first byte */
        size_t const first_shift = begin - (first_byte * 8);
        uint8_t val = this->bits_[first_byte];
        val <<= first_shift;
        val >>= first_shift;
        ret += true_bits_lookup_[val];

        /* middle bytes */
        for (size_t i = first_byte + 1; i < walk_end; ++i)
        {
            ret += true_bits_lookup_[this->bits_[i]];
        }

        /* last byte */
        if (last_byte < std::size(this->bits_))
        {
            size_t const last_shift = (last_byte + 1) * 8 - end;
            val = this->bits_[last_byte];
            val >>= last_shift;
            val <<= last_shift;
            ret += true_bits_lookup_[val];
        }
    }

    TR_ASSERT(ret <= (begin - end));
    return ret;
}

bool Bitfield::readBit(size_t n) const
{
    if (this->hasAll())
    {
        return true;
    }

    if (this->hasNone())
    {
        return false;
    }

    if (n >> 3U >= std::size(this->bits_))
    {
        return false;
    }

    return (this->bits_[n >> 3U] << (n & 7U) & 0x80) != 0;
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

bool Bitfield::isValid() const
{
    TR_ASSERT(std::size(this->bits_) == 0 || this->true_count_ == this->countArray());

    return true;
}

#endif

size_t Bitfield::countBits() const
{
    TR_ASSERT(this->isValid());

    return this->true_count_;
}

void Bitfield::setBitsInArray(std::vector<uint8_t>& array, size_t bit_count)
{
    size_t const byte_count = getStorageSize(bit_count);

    if (byte_count > 0)
    {
        std::fill_n(std::begin(array), byte_count - 1, 0xFF);

        array[byte_count - 1] = 0xFF << (byte_count * 8 - bit_count);
    }
}

std::vector<uint8_t> Bitfield::getRaw() const
{
    TR_ASSERT(this->bit_count_ > 0);

    size_t const n = getStorageSize(this->bit_count_);
    auto new_bits = std::vector<uint8_t>(n);

    if (!std::empty(this->bits_))
    {
        TR_ASSERT(std::size(this->bits_) <= n);
        std::copy(std::cbegin(this->bits_), std::cend(this->bits_), std::back_inserter(new_bits));
    }
    else if (this->hasAll())
    {
        setBitsInArray(new_bits, this->bit_count_);
    }

    return new_bits;
}

void Bitfield::ensureBitsAlloced(size_t n)
{
    size_t bytes_needed;
    bool const has_all = this->hasAll();

    if (has_all)
    {
        bytes_needed = getStorageSize(std::max(n, this->true_count_));
    }
    else
    {
        bytes_needed = getStorageSize(n);
    }

    if (std::size(this->bits_) != bytes_needed)
    {
        // this will not reallocate if bytes_needed is fewer than allocated
        this->bits_.resize(bytes_needed);

        if (has_all)
        {
            setBitsInArray(this->bits_, this->true_count_);
        }
    }
}

bool Bitfield::ensureNthBitAlloced(size_t nth)
{
    // count is zero-based, so we need to allocate nth+1 bits before setting the nth
    if (nth == SIZE_MAX)
    {
        return false;
    }

    this->ensureBitsAlloced(nth + 1);
    return true;
}

void Bitfield::freeArray()
{
    this->bits_.clear();
}

void Bitfield::setTrueCount(size_t n)
{
    TR_ASSERT(this->bit_count_ == 0 || n <= this->bit_count_);

    this->true_count_ = n;

    if (this->hasAll() || this->hasNone())
    {
        this->freeArray();
    }

    TR_ASSERT(this->isValid());
}

void Bitfield::rebuildTrueCount()
{
    this->setTrueCount(this->countArray());
}

void Bitfield::incTrueCount(size_t i)
{
    TR_ASSERT(this->bit_count_ == 0 || i <= this->bit_count_);
    TR_ASSERT(this->bit_count_ == 0 || this->true_count_ <= this->bit_count_ - i);

    this->setTrueCount(this->true_count_ + i);
}

void Bitfield::decTrueCount(size_t i)
{
    TR_ASSERT(this->bit_count_ == 0 || i <= this->bit_count_);
    TR_ASSERT(this->bit_count_ == 0 || this->true_count_ >= i);

    this->setTrueCount(this->true_count_ - i);
}

/****
*****
****/

Bitfield::Bitfield(size_t bit_count)
{
    this->bit_count_ = bit_count;
    this->true_count_ = 0;
    this->hint_ = NORMAL;

    TR_ASSERT(this->isValid());
}

void Bitfield::setHasNone()
{
    this->freeArray();
    this->true_count_ = 0;
    this->hint_ = HAS_NONE;

    TR_ASSERT(this->isValid());
}

void Bitfield::setHasAll()
{
    this->freeArray();
    this->true_count_ = this->bit_count_;
    this->hint_ = HAS_ALL;

    TR_ASSERT(this->isValid());
}

void Bitfield::setRaw(Span<uint8_t> newBits, bool bounded)
{
    this->freeArray();
    this->true_count_ = 0;

    // Having bounded=true, limits the amount of moved data to available storage size
    size_t byte_count = bounded ? std::min(newBits.getSize(), getStorageSize(this->bit_count_)) : newBits.getSize();

    this->bits_.resize(byte_count);
    std::copy(std::begin(newBits), std::end(newBits), std::begin(this->bits_));

    if (bounded)
    {
        /* ensure the excess newBits are set to '0' */
        int const excess_bit_count = byte_count * 8 - this->bit_count_;

        TR_ASSERT(excess_bit_count >= 0);
        TR_ASSERT(excess_bit_count <= 7);

        if (excess_bit_count != 0)
        {
            this->bits_[std::size(this->bits_) - 1] &= 0xFF << excess_bit_count;
        }
    }

    this->rebuildTrueCount();
}

void Bitfield::setFromFlags(bool const* flags, size_t n)
{
    size_t trueCount = 0;

    this->freeArray();
    this->ensureBitsAlloced(n);
    TR_ASSERT(std::size(this->bits_) >= getStorageSize(n));

    for (size_t i = 0; i < n; ++i)
    {
        if (flags[i])
        {
            ++trueCount;
            this->bits_[i >> 3U] |= (0x80 >> (i & 7U));
        }
    }

    this->setTrueCount(trueCount);
}

void Bitfield::setBit(size_t bit)
{
    if (!this->readBit(bit) && this->ensureNthBitAlloced(bit))
    {
        size_t const offset = bit >> 3U;

        if (offset < std::size(this->bits_))
        {
            this->bits_[offset] |= 0x80 >> (bit & 7U);
            this->incTrueCount(1);
        }
    }
}

void Bitfield::setBitRange(size_t begin, size_t end)
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

    if (end >= this->bit_count_ || begin > end)
    {
        return;
    }

    sb = begin >> 3;
    sm = ~(0xFF << (8 - (begin & 7)));
    eb = end >> 3;
    em = 0xFF << (7 - (end & 7));

    if (!this->ensureNthBitAlloced(end))
    {
        return;
    }

    if (sb == eb)
    {
        this->bits_[sb] |= sm & em;
    }
    else
    {
        this->bits_[sb] |= sm;
        this->bits_[eb] |= em;

        if (++sb < eb)
        {
            std::fill_n(std::begin(this->bits_) + sb, eb - sb, 0xFF);
        }
    }

    this->incTrueCount(diff);
}

void Bitfield::clearBit(size_t bit)
{
    TR_ASSERT(this->isValid());

    if (this->readBit(bit) && this->ensureNthBitAlloced(bit))
    {
        this->bits_[bit >> 3U] &= 0xff7f >> (bit & 7U);
        this->decTrueCount(1);
    }
}

void Bitfield::clearBitRange(size_t begin, size_t end)
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

    if (end >= this->bit_count_ || begin > end)
    {
        return;
    }

    sb = begin >> 3;
    sm = 0xFF << (8 - (begin & 7));
    eb = end >> 3;
    em = ~(0xFF << (7 - (end & 7)));

    if (!this->ensureNthBitAlloced(end))
    {
        return;
    }

    if (sb == eb)
    {
        this->bits_[sb] &= sm | em;
    }
    else
    {
        this->bits_[sb] &= sm;
        this->bits_[eb] &= em;

        if (++sb < eb)
        {
            std::fill_n(std::begin(this->bits_) + sb, eb - sb, 0);
        }
    }

    this->decTrueCount(diff);
}
