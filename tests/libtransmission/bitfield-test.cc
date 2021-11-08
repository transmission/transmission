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
        tr_bitfield bf(bit_count);

        for (int j = 0, n = tr_rand_int_weak(bit_count); j < n; ++j)
        {
            bf.set(tr_rand_int_weak(bit_count));
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
            if (bf.test(j))
            {
                ++count1;
            }
        }

        auto const count2 = bf.count(begin, end);
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
        size_t const true_count = std::count(std::begin(flags), std::end(flags), true);
        size_t const n = std::size(flags);
        bool const have_all = true_count == n;
        bool const have_none = true_count == 0;

        auto bf = tr_bitfield(n);
        bf.setFromBools(std::data(flags), std::size(flags));

        EXPECT_EQ(n, bf.size());
        EXPECT_EQ(have_all, bf.hasAll());
        EXPECT_EQ(have_none, bf.hasNone());
        EXPECT_EQ(true_count, bf.count());

        for (size_t i = 0; i < std::size(flags); ++i)
        {
            EXPECT_EQ(flags[i], bf.test(i));
        }
    }
}

TEST(Bitfield, setRaw)
{
    auto constexpr TestByte = uint8_t{ 10 };
    auto constexpr TestByteTrueBits = 2;

    auto const raw = std::vector<uint8_t>(100, TestByte);

    auto bf = tr_bitfield(std::size(raw) * 8);
    bf.setRaw(std::data(raw), std::size(raw));
    EXPECT_EQ(TestByteTrueBits * std::size(raw), bf.count());

    // The first byte of the bitfield corresponds to indices 0 - 7
    // from high bit to low bit, respectively. The next one 8-15, etc.
    // Spare bits at the end are set to zero.
    auto test = uint8_t{};
    for (int i = 0; i < 8; ++i)
    {
        if (bf.test(i))
        {
            test |= (1 << (7 - i));
        }
    }
    EXPECT_EQ(TestByte, test);
    EXPECT_EQ(raw, bf.raw());
}

TEST(Bitfield, bitfields)
{
    unsigned int bitcount = 500;
    tr_bitfield field(bitcount);

    // test tr_bitfield::set()
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 == 0)
        {
            field.set(i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_EQ(field.test(i), (i % 7 == 0));
    }

    /* test tr_bitfield::setRange */
    field.setRange(0, bitcount);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_TRUE(field.test(i));
    }

    /* test tr_bitfield::clearBit */
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 != 0)
        {
            field.unset(i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_EQ(field.test(i), (i % 7 == 0));
    }

    /* test tr_bitfield::clearBitRange in the middle of a boundary */
    field.setRange(0, 64);
    field.unsetRange(4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (i < 4 || i >= 21));
    }

    /* test tr_bitfield::clearBitRange on the boundaries */
    field.setRange(0, 64);
    field.unsetRange(8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (i < 8 || i >= 24));
    }

    /* test tr_bitfield::clearBitRange when begin & end is on the same word */
    field.setRange(0, 64);
    field.unsetRange(4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (i < 4 || i >= 5));
    }

    /* test tr_bitfield::setRange */
    field.unsetRange(0, 64);
    field.setRange(4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (4 <= i && i < 21));
    }

    /* test tr_bitfield::setRange on the boundaries */
    field.unsetRange(0, 64);
    field.setRange(8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (8 <= i && i < 24));
    }

    /* test tr_bitfield::setRange when begin & end is on the same word */
    field.unsetRange(0, 64);
    field.setRange(4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (4 <= i && i < 5));
    }
}

TEST(Bitfield, hasAllNone)
{
    {
        tr_bitfield field(3);

        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(field.hasNone());

        field.set(0);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.unset(0);
        field.set(1);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.unset(1);
        field.set(2);
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.set(0);
        field.set(1);
        EXPECT_TRUE(field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.setHasNone();
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(field.hasNone());

        field.setHasAll();
        EXPECT_TRUE(field.hasAll());
        EXPECT_TRUE(!field.hasNone());
    }

    {
        tr_bitfield field(0);

        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(!field.hasNone());

        field.setHasNone();
        EXPECT_TRUE(!field.hasAll());
        EXPECT_TRUE(field.hasNone());

        field.setHasAll();
        EXPECT_TRUE(field.hasAll());
        EXPECT_TRUE(!field.hasNone());
    }
}
