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

#include "transmission.h"
#include "tr-macros.h"
#include "tr-assert.h"

/// @brief Implementation of the BitTorrent spec's Bitfield array of bits
struct Bitfield
{
public:
    /***
    ****  life cycle
    ***/
    explicit Bitfield(size_t bit_count);

    Bitfield(): Bitfield(0) {}

    ~Bitfield()
    {
        this->setHasNone();
    }

    /***
    ****
    ***/

    void setHasAll();

    void setHasNone();

    /// @brief Sets one bit
    void setBit(size_t bit);

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
        if (this->hasAll())
        {
            return end - begin;
        }

        if (this->hasNone())
        {
            return 0;
        }

        return this->countRangeImpl(begin, end);
    }

    [[nodiscard]] size_t countBits() const;

    [[nodiscard]] constexpr bool hasAll() const
    {
        return this->bit_count_ != 0 ? (this->true_count_ == this->bit_count_) : this->hint_ == HAS_ALL;
    }

    [[nodiscard]] constexpr bool hasNone() const
    {
        return this->bit_count_ != 0 ? (this->true_count_ == 0) : this->hint_ == HAS_NONE;
    }

    [[nodiscard]] bool readBit(size_t n) const;

    /***
    ****
    ***/

    void setFromFlags(bool const* bytes, size_t n);

    void setFromBitfield(Bitfield const& src);

    void setRaw(void const* newBits, size_t byte_count, bool bounded);

    void* getRaw(size_t* byte_count) const;

    [[nodiscard]] size_t getBitCount() const
    {
        return bit_count_;
    }

private:
    [[nodiscard]] constexpr size_t countArray() const;
    [[nodiscard]] size_t countRangeImpl(size_t begin, size_t end) const;
    static void setBitsInArray(uint8_t* array, size_t bit_count);
    static constexpr size_t getStorageSize(size_t bit_count)
    {
        return (bit_count >> 3) + ((bit_count & 7) != 0 ? 1 : 0);
    }
    void ensureBitsAlloced(size_t n);
    bool ensureNthBitAlloced(size_t nth);
    void freeArray();
    void setTrueCount(size_t n);
    void rebuildTrueCount();
    void incTrueCount(size_t i);
    void decTrueCount(size_t i);

#ifdef TR_ENABLE_ASSERTS
    [[nodiscard]] bool isValid() const;
#endif

    uint8_t* bits_ = nullptr;
    size_t alloc_count_ = 0;
    size_t bit_count_ = 0;
    size_t true_count_ = 0;

    enum OperationMode
    {
        /// @brief Normal operation: storage of bytes contains bits to set or clear
        NORMAL,
        /// @brief If bit_count_==0, storage is inactive, consider all bits to be 1
        HAS_ALL,
        /// @brief If bit_count_==0, storage is inactive, consider all bits to be 0
        HAS_NONE,
    };

    // Special cases for when full or empty but we don't know the bitCount.
    // This occurs when a magnet link's peers send have all / have none
    OperationMode hint_ = NORMAL;
};
