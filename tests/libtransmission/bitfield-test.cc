// This file Copyright (C) 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include <libtransmission/transmission.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/bitfield.h>

#include "gtest/gtest.h"

TEST(Bitfield, count)
{
    auto constexpr IterCount = int{ 10000 };

    for (auto i = 0; i < IterCount; ++i)
    {
        auto const bit_count = 100U + tr_rand_int(1000U);

        // generate a random bitfield
        tr_bitfield bf(bit_count);

        for (size_t j = 0, n = tr_rand_int(bit_count); j < n; ++j)
        {
            bf.set(tr_rand_int(bit_count));
        }

        int begin = tr_rand_int(bit_count);
        int end = 0;
        do
        {
            end = tr_rand_int(bit_count);
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

    auto bf = tr_bitfield{ 0 };
    EXPECT_EQ(0U, bf.count(0, 0));
    EXPECT_EQ(0U, bf.count(0, 1));

    bf = tr_bitfield{ 100 };
    EXPECT_EQ(0U, bf.count(0, 0));
    EXPECT_EQ(0U, bf.count(0, 100));
    bf.setHasAll();
    EXPECT_EQ(0U, bf.count(0, 0));
    EXPECT_EQ(1U, bf.count(0, 1));
    EXPECT_EQ(100U, bf.count(0, 100));
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

    auto raw = std::vector<uint8_t>(100, TestByte);

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

    // check that has-all bitfield gets all-true
    bf = tr_bitfield(std::size(raw) * 8);
    bf.setHasAll();
    raw = bf.raw();
    EXPECT_EQ(std::size(bf) / 8, std::size(raw));
    EXPECT_EQ(std::numeric_limits<unsigned char>::max(), raw[0]);

    // check that the spare bits t the end are zero
    bf = tr_bitfield{ 1 };
    uint8_t const by = std::numeric_limits<uint8_t>::max();
    bf.setRaw(&by, 1);
    EXPECT_TRUE(bf.hasAll());
    EXPECT_FALSE(bf.hasNone());
    EXPECT_EQ(1U, bf.count());
    raw = bf.raw();
    EXPECT_EQ(1U, std::size(raw));
    EXPECT_EQ(1 << 7, raw[0]);
}

TEST(Bitfield, bitfields)
{
    unsigned int const bitcount = 500;
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

    /* test tr_bitfield::setSpan */
    field.setSpan(0, bitcount);

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
    field.setSpan(0, 64);
    field.unsetSpan(4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (i < 4 || i >= 21));
    }

    /* test tr_bitfield::clearBitRange on the boundaries */
    field.setSpan(0, 64);
    field.unsetSpan(8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (i < 8 || i >= 24));
    }

    /* test tr_bitfield::clearBitRange when begin & end is on the same word */
    field.setSpan(0, 64);
    field.unsetSpan(4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (i < 4 || i >= 5));
    }

    /* test tr_bitfield::setSpan */
    field.unsetSpan(0, 64);
    field.setSpan(4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (4 <= i && i < 21));
    }

    /* test tr_bitfield::setSpan on the boundaries */
    field.unsetSpan(0, 64);
    field.setSpan(8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (8 <= i && i < 24));
    }

    /* test tr_bitfield::setSpan when begin & end is on the same word */
    field.unsetSpan(0, 64);
    field.setSpan(4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(field.test(i), (4 <= i && i < 5));
    }

    /* test tr_bitfield::setSpan when end runs beyond the end of the bitfield */
    field.setHasNone();
    field.setSpan(100, 1000);
    EXPECT_FALSE(field.hasNone());
    EXPECT_FALSE(field.hasAll());
    EXPECT_EQ(std::size(field) - 100, field.count());

    /* test tr_bitfield::unsetSpan when it changes nothing */
    field.setHasNone();
    field.unsetSpan(0, 100);
    EXPECT_TRUE(field.hasNone());
    EXPECT_FALSE(field.hasAll());
    EXPECT_EQ(0U, field.count());

    /* test tr_bitfield::setSpan when it changes nothing */
    field.setHasAll();
    field.setSpan(0, 100);
    EXPECT_FALSE(field.hasNone());
    EXPECT_TRUE(field.hasAll());
    EXPECT_EQ(std::size(field), field.count());

    /* test tr_bitfield::setSpan with an invalid span doesn't crash */
    field.setHasAll();
    field.setSpan(0, 0);
    EXPECT_TRUE(field.hasAll());
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

TEST(Bitfield, percent)
{
    auto field = tr_bitfield{ 100 };
    field.setHasAll();
    EXPECT_NEAR(1.0F, field.percent(), 0.01);

    field.setHasNone();
    EXPECT_NEAR(0.0F, field.percent(), 0.01);

    field.setSpan(0, std::size(field) / 2U);
    EXPECT_NEAR(0.5F, field.percent(), 0.01);

    field.setHasNone();
    field.setSpan(0, std::size(field) / 4U);
    EXPECT_NEAR(0.25F, field.percent(), 0.01);
}

TEST(Bitfield, bitwiseOr)
{
    auto a = tr_bitfield{ 100 };
    auto b = tr_bitfield{ 100 };

    a.setHasAll();
    b.setHasNone();
    a |= b;
    EXPECT_TRUE(a.hasAll());

    a.setHasNone();
    b.setHasAll();
    a |= b;
    EXPECT_TRUE(a.hasAll());

    a.setHasNone();
    b.setHasNone();
    a |= b;
    EXPECT_TRUE(a.hasNone());

    a.setHasNone();
    b.setHasNone();
    a.setSpan(0, std::size(a) / 2U);
    b.setSpan(std::size(a) / 2U, std::size(a));
    EXPECT_EQ(0.5, a.percent());
    EXPECT_EQ(0.5, b.percent());
    a |= b;
    EXPECT_EQ(1.0, a.percent());
    EXPECT_TRUE(a.hasAll());

    a.setHasNone();
    b.setHasNone();
    for (size_t i = 0; i < std::size(a); ++i)
    {
        if ((i % 2U) != 0U)
        {
            a.set(i);
        }
        else
        {
            b.set(i);
        }
    }
    EXPECT_NEAR(0.5F, a.percent(), 0.01);
    EXPECT_NEAR(0.5F, b.percent(), 0.01);
    a |= b;
    EXPECT_TRUE(a.hasAll());
}

TEST(Bitfield, bitwiseAnd)
{
    auto a = tr_bitfield{ 100 };
    auto b = tr_bitfield{ 100 };

    a.setHasAll();
    b.setHasNone();
    a &= b;
    EXPECT_TRUE(a.hasNone());

    a.setHasNone();
    b.setHasAll();
    a &= b;
    EXPECT_TRUE(a.hasNone());

    a.setHasAll();
    b.setHasAll();
    a &= b;
    EXPECT_TRUE(a.hasAll());

    a.setHasNone();
    b.setHasNone();
    a.setSpan(0, std::size(a) / 2U);
    b.setSpan(std::size(a) / 2U, std::size(a));
    EXPECT_EQ(0.5, a.percent());
    EXPECT_EQ(0.5, b.percent());
    a &= b;
    EXPECT_TRUE(a.hasNone());

    a.setHasNone();
    b.setHasNone();
    for (size_t i = 0; i < std::size(a); ++i)
    {
        if ((i % 2U) != 0U)
        {
            a.set(i);
        }
        else
        {
            b.set(i);
        }
    }
    a &= b;
    EXPECT_TRUE(a.hasNone());

    a.setHasNone();
    a.setSpan(0U, std::size(a) / 10U);
    b.setHasNone();
    b.setSpan(0U, std::size(a) / 20U);
    a &= b;
    EXPECT_NEAR(0.05F, a.percent(), 0.01);

    a.setHasNone();
    a.setSpan(0U, std::size(a) / 10U);
    b.setHasNone();
    b.setSpan(0U, std::size(a) / 20U);
    b &= a;
    EXPECT_NEAR(0.1F, a.percent(), 0.01);
}
