/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <iterator> // std::back_inserter
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
        std::begin(bits_),
        std::end(bits_),
        0,
        [](auto acc, auto item) { return acc + true_bits_lookup_[item]; });
}

size_t Bitfield::countRangeImpl(size_t begin, size_t end) const
{
    size_t ret = 0;
    size_t const first_byte = begin >> 3U;
    size_t const last_byte = (end - 1) >> 3U;

    if (bit_count_ == 0)
    {
        return 0;
    }

    if (first_byte >= std::size(bits_))
    {
        return 0;
    }

    TR_ASSERT(begin < end);

    if (first_byte == last_byte)
    {
        uint8_t val = bits_[first_byte];

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
        size_t const walk_end = std::min(std::size(bits_), last_byte);

        /* first byte */
        size_t const first_shift = begin - (first_byte * 8);
        uint8_t val = bits_[first_byte];
        val <<= first_shift;
        val >>= first_shift;
        ret += true_bits_lookup_[val];

        /* middle bytes */
        for (size_t i = first_byte + 1; i < walk_end; ++i)
        {
            ret += true_bits_lookup_[bits_[i]];
        }

        /* last byte */
        if (last_byte < std::size(bits_))
        {
            size_t const last_shift = (last_byte + 1) * 8 - end;
            val = bits_[last_byte];
            val >>= last_shift;
            val <<= last_shift;
            ret += true_bits_lookup_[val];
        }
    }

    TR_ASSERT(ret <= (end - begin));
    return ret;
}

bool Bitfield::readBit(size_t n) const
{
    switch (mode_)
    {
    case OperationMode::Normal:
        {
            auto const byte_offset = n >> 3;

            if (byte_offset >= std::size(bits_))
            {
                return false;
            }

            return ((bits_[byte_offset] << (n & 7)) & 0x80) != 0;
        }
    case OperationMode::All:
        return true;
    case OperationMode::None:
        return false;
    case OperationMode::Start:
        TR_UNREACHABLE();
    }
    return false;
}

/***
****
***/

#ifdef TR_ENABLE_ASSERTS

bool Bitfield::isValid() const
{
    TR_ASSERT_MSG(
        std::size(bits_) == 0 || true_count_ == countArray(),
        "Invalid Bitfield state: bits.size()=%zu bit_count=%zu true_count=%zu countArray()=%zu",
        std::size(bits_),
        bit_count_,
        true_count_,
        countArray());

    return true;
}

#endif

size_t Bitfield::countBits() const
{
    TR_ASSERT(isValid());

    return true_count_;
}

void Bitfield::setBitsInArray(std::vector<uint8_t>& array, size_t bit_count)
{
    TR_ASSERT(getStorageSize(bit_count) == std::size(array));

    if (!std::empty(array) && bit_count > 0)
    {
        auto const last_byte_index = getStorageSize(bit_count) - 1;

        std::fill_n(std::begin(array), last_byte_index, 0xFF);
        array[last_byte_index] = 0xFF << (last_byte_index * 8 - bit_count);
    }
}

std::vector<uint8_t> Bitfield::getRaw() const
{
    TR_ASSERT(bit_count_ > 0);

    size_t const n = getStorageSize(bit_count_);
    auto new_bits = std::vector<uint8_t>(n);

    if (!std::empty(bits_))
    {
        TR_ASSERT(std::size(bits_) <= n);
        std::copy(std::cbegin(bits_), std::cend(bits_), std::back_inserter(new_bits));
    }
    else if (hasAll())
    {
        setBitsInArray(new_bits, bit_count_);
    }

    return new_bits;
}

void Bitfield::ensureNthBitFits(size_t n)
{
    TR_ASSERT_MSG(mode_ == OperationMode::Normal, "Can only reallocate storage in Normal mode");

    size_t bytes_needed = getStorageSize(std::max(n, true_count_));

    if (std::size(bits_) < bytes_needed)
    {
        bits_.resize(bytes_needed);
    }
}

/****
*****
****/

Bitfield::Bitfield(size_t bit_count)
{
    bit_count_ = bit_count;
    true_count_ = 0;
    setMode(OperationMode::Normal);

    TR_ASSERT(isValid());
}

void Bitfield::setMode(Bitfield::OperationMode new_mode)
{
    switch (new_mode)
    {
    case OperationMode::Normal:
        switch (mode_)
        {
        case OperationMode::All:
            {
                // Switching from ALL mode to NORMAL, should set the bits
                mode_ = OperationMode::Normal;
                ensureNthBitFits(bit_count_);
                setBitRangeImpl(0, bit_count_ - 1);
                true_count_ = bit_count_; // switching from mode ALL, all bits are set
                break;
            }
        case OperationMode::None:
            {
                // Switching from ALL mode to NORMAL, should set the bits
                mode_ = OperationMode::Normal;
                ensureNthBitFits(bit_count_);
                clearBitRangeImpl(0, bit_count_ - 1);
                true_count_ = 0; // switching from mode NONE, all bits are not set
                break;
            }
        case OperationMode::Start:
            mode_ = OperationMode::Normal;
            // fall through
        case OperationMode::Normal:
            break;
        }
        break;
    case OperationMode::All:
        clearStorage();
        true_count_ = bit_count_;
        mode_ = OperationMode::All;
        break;
    case OperationMode::None:
        clearStorage();
        true_count_ = 0;
        mode_ = OperationMode::None;
        break;
    case OperationMode::Start:
        TR_UNREACHABLE();
        break;
    }

    TR_ASSERT(isValid());
}

Bitfield::Bitfield(Span<uint8_t> new_bits, size_t bit_count, bool bounded)
    : bit_count_(bit_count)
{
    true_count_ = 0;
    setMode(OperationMode::Normal);

    // Having bounded=true, limits the amount of moved data to available storage size
    size_t byte_count = bounded ? std::min(std::size(new_bits), getStorageSize(bit_count_)) : std::size(new_bits);

    bits_.resize(byte_count);
    std::copy_n(std::begin(new_bits), byte_count, std::begin(bits_));

    if (bounded)
    {
        /* ensure the excess new_bits are set to '0' */
        int const excess_bit_count = bit_count_ & 7;

        if (excess_bit_count != 0)
        {
            bits_[byte_count - 1] &= 0xFF << excess_bit_count;
        }
    }

    setTrueCount(countArray());
}

Bitfield::Bitfield(bool const* flags, size_t n)
    : bit_count_(n)
    , true_count_(std::count(flags, flags + n, true))
{
    if (true_count_ == 0)
    {
        mode_ = OperationMode::None;
    }
    else if (true_count_ == bit_count_)
    {
        mode_ = OperationMode::All;
    }
    else
    {
        mode_ = OperationMode::Normal;
        ensureNthBitFits(n);
        TR_ASSERT(std::size(bits_) >= getStorageSize(n));
        for (size_t index = 0; index < n; ++index)
        {
            if (flags[index])
            {
                bits_[index >> 3] |= (0x80 >> (index & 7));
            }
        }
    }

    TR_ASSERT(isValid());
}

void Bitfield::setBit(size_t bit_index)
{
    switch (mode_)
    {
    case OperationMode::Normal:
        {
            setBitImpl(bit_index);
            break;
        }
    case OperationMode::All:
        TR_ASSERT(bit_index <= bit_count_);
        break;
    case OperationMode::None:
        setMode(OperationMode::Normal);
        setBitImpl(bit_index);
        break;
    case OperationMode::Start:
        TR_UNREACHABLE();
        break;
    }
}

void Bitfield::setBitRange(size_t begin, size_t end)
{
    if (mode_ == OperationMode::All)
    {
        return;
    }

    if (mode_ == OperationMode::None)
    {
        setMode(OperationMode::Normal);
    }

    size_t const true_bits_difference = (end - begin) - countRange(begin, end);

    if (true_bits_difference == 0)
    {
        return;
    }

    end--;

    if (end >= bit_count_ || begin > end)
    {
        return;
    }

    setBitRangeImpl(begin, end);

    TR_ASSERT(true_count_ + true_bits_difference <= bit_count_);
    setTrueCount(true_count_ + true_bits_difference);
}

void Bitfield::clearBit(size_t bit)
{
    TR_ASSERT(isValid());

    switch (mode_)
    {
    case OperationMode::Normal:
        clearBitImpl(bit);
        break;
    case OperationMode::All:
        setMode(OperationMode::Normal);
        clearBitImpl(bit);
        break;
    case OperationMode::None:
        break;
    case OperationMode::Start:
        TR_UNREACHABLE();
        break;
    }
}

void Bitfield::clearBitRange(size_t begin, size_t end)
{
    if (mode_ == OperationMode::None)
    {
        return;
    }

    if (mode_ == OperationMode::All)
    {
        setMode(OperationMode::Normal);
    }

    size_t const true_bits_difference = countRange(begin, end); // all true bits in range will be gone

    if (true_bits_difference == 0)
    {
        return;
    }

    end--;

    if (end >= bit_count_ || begin > end)
    {
        return;
    }

    clearBitRangeImpl(begin, end);

    TR_ASSERT(true_count_ >= true_bits_difference);
    setTrueCount(true_count_ - true_bits_difference);
}
