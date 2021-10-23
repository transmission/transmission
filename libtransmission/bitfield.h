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
#include <cstddef>
#include <cstdint>
#include <vector>

#include "tr-assert.h"

/** @brief Implementation of the BitTorrent spec's Bitfield array of bits */
class tr_bitfield
{
public:
    tr_bitfield(size_t bit_count);
    ~tr_bitfield() = default;

    void setHasAll();
    void setHasNone();
    void set(size_t bit, bool value = true);
    void setRange(size_t begin, size_t end, bool value = true);
    void unset(size_t bit)
    {
        set(bit, false);
    }
    void unsetRange(size_t begin, size_t end)
    {
        setRange(begin, end, false);
    }

    void setFromFlags(bool const* bytes, size_t n);
    void setRaw(uint8_t const* bits, size_t byte_count, bool bounded);
    std::vector<uint8_t> raw() const;

    bool hasAll() const
    {
        return this->have_all_hint || (this->bit_count > 0 && this->bit_count == this->true_count);
    }
    bool hasNone() const
    {
        return this->have_none_hint || (this->bit_count > 0 && this->true_count == 0);
    }
    bool test(size_t bit) const;
    size_t count() const
    {
        return this->true_count;
    }
    size_t count(size_t begin, size_t end) const;

    size_t size() const
    {
        return this->bit_count;
    }

#ifdef TR_ENABLE_ASSERTS
    bool assertValid() const;
#endif

private:
    size_t recount() const;
    size_t recount(size_t begin, size_t end) const;

    void ensureBitsAlloced(size_t n);
    bool ensureNthBitAlloced(size_t nth);
    void freeArray();

    void setTrueCount(size_t n);
    void rebuildTrueCount();
    void incrementTrueCount(size_t inc);
    void decrementTrueCount(size_t dec);

    std::vector<uint8_t> bits;
    size_t bit_count = 0;
    size_t true_count = 0;

    /* Special cases for when full or empty but we don't know the bitCount.
       This occurs when a magnet link's peers send have all / have none */
    bool have_all_hint = false;
    bool have_none_hint = false;
};
