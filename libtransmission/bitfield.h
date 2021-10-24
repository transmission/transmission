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

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tr-assert.h"

/**
 * @brief Implementation of the BitTorrent spec's Bitfield array of bits.
 *
 * This is for tracking what pieces a peer has.
 *
 * Also treats "have all" and "have none" as special cases:
 * - This reduces overhead. No need to allocate an array or do lookups
 *   if you know all bits have the same value
 * - This makes peers that send 'have all' or 'have none' useful even 
 *   if you don't know how many pieces there are, e.g. when you have a
 *   magnet link and haven't finished getting its metainfo yet.
 */
class tr_bitfield
{
public:
    explicit tr_bitfield(size_t bit_count);
    ~tr_bitfield() = default;

    void setHasAll();
    void setHasNone();

    // set one or more bits
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
    void setFromBools(bool const* bytes, size_t n);

    // "raw" here is in BEP0003 format: "The first byte of the bitfield
    // corresponds to indices 0 - 7 from high bit to low bit, respectively.
    // The next one 8-15, etc. Spare bits at the end are set to zero.
    void setRaw(uint8_t const* bits, size_t byte_count, bool bounded);
    std::vector<uint8_t> raw() const;

    [[nodiscard]] bool hasAll() const
    {
        return have_all_hint_ || (bit_count_ > 0 && bit_count_ == true_count_);
    }

    [[nodiscard]] bool hasNone() const
    {
        return have_none_hint_ || (bit_count_ > 0 && true_count_ == 0);
    }

    [[nodiscard]] bool test(size_t bit) const
    {
        return hasAll() || (!hasNone() && testFlag(bit));
    }

    [[nodiscard]] size_t count() const
    {
        return true_count_;
    }

    [[nodiscard]] size_t count(size_t begin, size_t end) const;

    [[nodiscard]] size_t size() const
    {
        return bit_count_;
    }

#ifdef TR_ENABLE_ASSERTS
    bool assertValid() const;
#endif

private:
    std::vector<uint8_t> flags_;
    [[nodiscard]] size_t countFlags() const;
    [[nodiscard]] size_t countFlags(size_t begin, size_t end) const;
    [[nodiscard]] bool testFlag(size_t bit) const;

    void ensureBitsAlloced(size_t n);
    [[nodiscard]] bool ensureNthBitAlloced(size_t nth);
    void freeArray();

    void setTrueCount(size_t n);
    void rebuildTrueCount();
    void incrementTrueCount(size_t inc);
    void decrementTrueCount(size_t dec);

    size_t bit_count_ = 0;
    size_t true_count_ = 0;

    /* Special cases for when full or empty but we don't know the bitCount.
       This occurs when a magnet link's peers send have all / have none */
    bool have_all_hint_ = false;
    bool have_none_hint_ = false;
};
