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
struct tr_bitfield
{
public:
    /***
    ****  life cycle
    ***/
    explicit tr_bitfield(size_t bit_count);

    tr_bitfield(): tr_bitfield(0) {}

    ~tr_bitfield()
    {
        this->setHasNone();
    }

    /***
    ****
    ***/

    void setHasAll();

    void setHasNone();

    void add(size_t bit);

    /// @brief Sets bit range [begin, end) to 1
    void addRange(size_t begin, size_t end);

    void rem(size_t bit);

    /// @brief Clears bit range [begin, end) to 0
    void remRange(size_t begin, size_t end);

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

    [[nodiscard]] size_t countTrueBits() const;

    [[nodiscard]] constexpr bool hasAll() const
    {
        return this->bit_count != 0 ? (this->true_count == this->bit_count) : this->have_all_hint;
    }

    [[nodiscard]] constexpr bool hasNone() const
    {
        return this->bit_count != 0 ? (this->true_count == 0) : this->have_none_hint;
    }

    [[nodiscard]] bool has(size_t n) const;

    /***
    ****
    ***/

    void setFromFlags(bool const* bytes, size_t n);

    void setFromBitfield(tr_bitfield const*);

    void setRaw(void const* newBits, size_t byte_count, bool bounded);

    void* getRaw(size_t* byte_count) const;

    [[nodiscard]] size_t getBitCount() const
    {
        return bit_count;
    }

private:
    [[nodiscard]] constexpr size_t countArray() const;
    [[nodiscard]] size_t countRangeImpl(size_t begin, size_t end) const;
    static void set_all_true(uint8_t* array, size_t bit_count);
    static constexpr size_t get_bytes_needed(size_t bit_count)
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

    uint8_t* bits = nullptr;
    size_t alloc_count = 0;
    size_t bit_count = 0;
    size_t true_count = 0;

    /* Special cases for when full or empty but we don't know the bitCount.
       This occurs when a magnet link's peers send have all / have none */
    bool have_all_hint = false;
    bool have_none_hint = false;
};
