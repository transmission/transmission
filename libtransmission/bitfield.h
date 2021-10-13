/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <vector>

#include "transmission.h"
#include "tr-macros.h"
#include "tr-assert.h"
#include "span.h"

/// @brief Implementation of the BitTorrent spec's Bitfield array of bits
struct Bitfield
{
public:
    /// @brief State diagram for modes of operation: None -> Normal <==> All
    /// ALL and NONE: Special cases for when full or empty but we don't know the bitCount.
    /// This occurs when a magnet link's peers send have all / have none
    enum struct OperationMode
    {
        /// @brief State at the creation
        Start,
        /// @brief Normal operation: storage of bytes contains bits to set or clear
        Normal,
        /// @brief If bit_count_==0, storage is inactive, consider all bits to be 1
        All,
        /// @brief If bit_count_==0, storage is inactive, consider all bits to be 0
        None,
    };

    /***
    ****  life cycle
    ***/
    explicit Bitfield(size_t bit_count);

    Bitfield()
        : Bitfield(0)
    {
    }

    /// @brief Builds bits from array of boolean flags
    Bitfield(bool const* bytes, size_t n);

    ~Bitfield()
    {
        setMode(OperationMode::None);
    }

    /***
    ****
    ***/

    /// @brief Creates new Bitfield with same count of bits as *this and replaces data from new_bits
    void setFrom(Span<uint8_t> new_bits, bool bounded) {
        *this = Bitfield(new_bits, this->bit_count_, bounded);
    }

    /// @brief Change the state (mode of operation)
    void setMode(OperationMode new_mode);

    /// @brief Sets one bit
    void setBit(size_t bit_index);

    /// @brief Sets bit range [begin, end) to 1
    void setBitRange(size_t begin, size_t end);

    /// @brief Clears one bit
    void clearBit(size_t bit);

    /// @brief Clears bit range [begin, end) to 0
    void clearBitRange(size_t begin, size_t end);

    /***
    ****
    ***/

    [[nodiscard]] size_t countRange(size_t begin, size_t end) const
    {
        if (hasAll())
        {
            return end - begin;
        }

        if (hasNone())
        {
            return 0;
        }

        return countRangeImpl(begin, end);
    }

    [[nodiscard]] size_t countBits() const;

    [[nodiscard]] constexpr bool hasAll() const
    {
        return mode_ == OperationMode::All;
    }

    [[nodiscard]] constexpr bool hasNone() const
    {
        return mode_ == OperationMode::None;
    }

    [[nodiscard]] bool readBit(size_t n) const;

    /***
    ****
    ***/

    [[nodiscard]] std::vector<uint8_t> getRaw() const;

    [[nodiscard]] size_t getBitCount() const
    {
        return bit_count_;
    }

private:
    /// @brief Copies bits from the readonly view new_bits. Use Bitfield::setFrom to access this constructor
    /// @param bounded Whether incoming data is constrained by our memory and bit size
    Bitfield(Span<uint8_t> new_bits, size_t bit_count, bool bounded);

    /// @brief Contains lookup table for how many set bits are there in 0..255
    static std::array<int8_t const, 256> true_bits_lookup_;

    static constexpr size_t getStorageSize(size_t bit_count)
    {
        return 1 + ((bit_count + 7) >> 3);
    }

    [[nodiscard]] size_t countArray() const;
    [[nodiscard]] size_t countRangeImpl(size_t begin, size_t end) const;

    /// @brief Given bit count, sets that many bits in the array, assumes array size is big enough.
    static void setBitsInArray(std::vector<uint8_t>& array, size_t bit_count);

    void ensureNthBitFits(size_t n);

    inline void setTrueCount(size_t n)
    {
        TR_ASSERT(mode_ == OperationMode::Normal);
        TR_ASSERT(n <= bit_count_);

        true_count_ = n;

        TR_ASSERT(isValid());
    }

#ifdef TR_ENABLE_ASSERTS
    [[nodiscard]] bool isValid() const;
#endif

    /// @brief Set the bit
    inline void setBitImpl(size_t bit) {
        TR_ASSERT_MSG(mode_ == OperationMode::Normal, "Can only set bits in Normal operation mode");
        TR_ASSERT(isValid());

        if (!readBit(bit))
        {
            ensureNthBitFits(bit);

            auto const byte_offset = bit >> 3;
            size_t bit_value = size_t { 0x80U } >> (bit & 7);

            bits_[byte_offset] |= bit_value;
            setTrueCount(true_count_ + 1);
        }

        TR_ASSERT(isValid());
    }

    /// @brief Clear the bit
    inline void clearBitImpl(size_t bit) {
        TR_ASSERT_MSG(mode_ == OperationMode::Normal, "Can only set bits in Normal operation mode");
        TR_ASSERT(isValid());

        if (readBit(bit))
        {
            ensureNthBitFits(bit);

            size_t const byte_mask = size_t { 0xFF7FU } >> (bit & 7U);
            bits_[bit >> 3] &= byte_mask;

            TR_ASSERT(true_count_ > 0);
            setTrueCount(true_count_ - 1);
        }

        TR_ASSERT(isValid());
    }

    std::vector<uint8_t> bits_;
    size_t bit_count_ = 0;
    size_t true_count_ = 0;
    OperationMode mode_ = OperationMode::Start;
};
