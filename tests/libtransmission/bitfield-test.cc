/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>

#include "transmission.h"
#include "crypto-utils.h"
#include "bitfield.h"
#include "utils.h" /* tr_free */

#include "gtest/gtest.h"

TEST(Bitfield, countRange)
{
    auto constexpr IterCount = int{ 10000 };

    for (auto i = 0; i < IterCount; ++i)
    {
        int const bit_count = 100 + tr_rand_int_weak(1000);

        // generate a random bitfield
        Bitfield bf(bit_count);

        for (int j = 0, n = tr_rand_int_weak(bit_count); j < n; ++j)
        {
            bf.setBit(tr_rand_int_weak(bit_count));
        }

        int begin = tr_rand_int_weak(bit_count);
        int end;
        do
        {
            end = tr_rand_int_weak(bit_count);
        } while (end == begin);

        // ensure end <= begin
        if (end < begin)
        {
            int const tmp = begin;
            begin = end;
            end = tmp;
        }

        // test the bitfield
        unsigned long count1 = {};
        for (auto j = begin; j < end; ++j)
        {
            if (bf.readBit(j))
            {
                ++count1;
            }
        }

        auto const count2 = bf.countRange(begin, end);
        EXPECT_EQ(count1, count2);
    }
}

TEST(Bitfield, ctorFromFlagArray)
{
    auto constexpr Tests = std::array<std::array<bool, 10>, 3>{ {
        { false, true, false, true, false, false, true, false, false, true }, // mixed
        { true, true, true, true, true, true, true, true, true, true }, // have all
        { false, false, false, false, false, false, false, false, false, false }, // have none
    } };

    for (auto const& flags : Tests)
    {
        auto bf = Bitfield(std::data(flags), std::size(flags));
        EXPECT_EQ(std::size(flags), bf.getBitCount());
        EXPECT_EQ(size_t(std::count(std::begin(flags), std::end(flags), true)) == std::size(flags), bf.hasAll());
        EXPECT_EQ(size_t(std::count(std::begin(flags), std::end(flags), false)) == std::size(flags), bf.hasNone());
        EXPECT_EQ(std::count(std::begin(flags), std::end(flags), true), bf.countBits());
        for (size_t i = 0; i < std::size(flags); ++i)
        {
            EXPECT_EQ(flags[i], bf.readBit(i));
        }
    }
}

TEST(Bitfield, bitfields)
{
    unsigned int bitcount = 500;
    Bitfield field(bitcount);

    /* test Bitfield::setBit */
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 == 0)
        {
            field.setBit(i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_EQ(field.readBit(i), (i % 7 == 0));
    }

    /* test Bitfield::setBitRange */
    field.setBitRange(0, bitcount);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_TRUE(field.readBit(i));
    }

    /* test Bitfield::clearBit */
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 != 0)
        {
            field.clearBit(i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_EQ(field.readBit(i), (i % 7 == 0));
    }

    /* test Bitfield::clearBitRange in the middle of a boundary */
    field.setBitRange(0, 64);
    field.clearBitRange(4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.readBit(i), (i < 4 || i >= 21));
    }

    /* test Bitfield::clearBitRange on the boundaries */
    field.setBitRange(0, 64);
    field.clearBitRange(8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.readBit(i), (i < 8 || i >= 24));
    }

    /* test Bitfield::clearBitRange when begin & end is on the same word */
    field.setBitRange(0, 64);
    field.clearBitRange(4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.readBit(i), (i < 4 || i >= 5));
    }

    /* test Bitfield::setBitRange */
    field.clearBitRange(0, 64);
    field.setBitRange(4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.readBit(i), (4 <= i && i < 21));
    }

    /* test Bitfield::setBitRange on the boundaries */
    field.clearBitRange(0, 64);
    field.setBitRange(8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.readBit(i), (8 <= i && i < 24));
    }

    /* test Bitfield::setBitRange when begin & end is on the same word */
    field.clearBitRange(0, 64);
    field.setBitRange(4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.readBit(i), (4 <= i && i < 5));
    }
}

TEST(Bitfield, hasAllNone)
{
    {
        Bitfield field(3);

        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(field.hasNone());

        field.setBit(0);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.clearBit(0);
        field.setBit(1);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.clearBit(1);
        field.setBit(2);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.setBit(0);
        field.setBit(1);
        EXPECT_TRUE(field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.setMode(Bitfield::OperationMode::None);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(field.hasNone());

        field.setMode(Bitfield::OperationMode::All);
        EXPECT_TRUE(field.hasAll());
        EXPECT_TRUE(!field.hasNone());
    }

    {
        Bitfield field(0);

        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.setMode(Bitfield::OperationMode::None);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(field.hasNone());

        field.setMode(Bitfield::OperationMode::All);
        EXPECT_TRUE(field.hasAll());
        EXPECT_TRUE(!field.hasNone());
    }
}
