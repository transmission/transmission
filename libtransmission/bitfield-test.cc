/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* strlen() */
#include "transmission.h"
#include "crypto-utils.h"
#include "bitfield.h"
#include "utils.h" /* tr_free */

#include "gtest/gtest.h"

TEST(Bitfield, count_range) {
    int begin;
    int end;
    int count1;
    int count2;
    int const bitCount = 100 + tr_rand_int_weak(1000);
    tr_bitfield bf;

    /* generate a random bitfield */
    tr_bitfieldConstruct(&bf, bitCount);

    for (int i = 0, n = tr_rand_int_weak(bitCount); i < n; ++i)
    {
        tr_bitfieldAdd(&bf, tr_rand_int_weak(bitCount));
    }

    begin = tr_rand_int_weak(bitCount);

    do
    {
        end = tr_rand_int_weak(bitCount);
    }
    while (end == begin);

    /* ensure end <= begin */
    if (end < begin)
    {
        int const tmp = begin;
        begin = end;
        end = tmp;
    }

    /* test the bitfield */
    count1 = 0;

    for (int i = begin; i < end; ++i)
    {
        if (tr_bitfieldHas(&bf, i))
        {
            ++count1;
        }
    }

    count2 = tr_bitfieldCountRange(&bf, begin, end);
    EXPECT_EQ(count1, count2);

    /* cleanup */
    tr_bitfieldDestruct(&bf);
}

TEST(Bitfields, bitfields)
{
    unsigned int bitcount = 500;
    tr_bitfield field;

    tr_bitfieldConstruct(&field, bitcount);

    /* test tr_bitfieldAdd */
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 == 0)
        {
            tr_bitfieldAdd(&field, i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (i % 7 == 0));
    }

    /* test tr_bitfieldAddRange */
    tr_bitfieldAddRange(&field, 0, bitcount);

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_TRUE(tr_bitfieldHas(&field, i));
    }

    /* test tr_bitfieldRem */
    for (unsigned int i = 0; i < bitcount; i++)
    {
        if (i % 7 != 0)
        {
            tr_bitfieldRem(&field, i);
        }
    }

    for (unsigned int i = 0; i < bitcount; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (i % 7 == 0));
    }

    /* test tr_bitfieldRemRange in the middle of a boundary */
    tr_bitfieldAddRange(&field, 0, 64);
    tr_bitfieldRemRange(&field, 4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (i < 4 || i >= 21));
    }

    /* test tr_bitfieldRemRange on the boundaries */
    tr_bitfieldAddRange(&field, 0, 64);
    tr_bitfieldRemRange(&field, 8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (i < 8 || i >= 24));
    }

    /* test tr_bitfieldRemRange when begin & end is on the same word */
    tr_bitfieldAddRange(&field, 0, 64);
    tr_bitfieldRemRange(&field, 4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (i < 4 || i >= 5));
    }

    /* test tr_bitfieldAddRange */
    tr_bitfieldRemRange(&field, 0, 64);
    tr_bitfieldAddRange(&field, 4, 21);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (4 <= i && i < 21));
    }

    /* test tr_bitfieldAddRange on the boundaries */
    tr_bitfieldRemRange(&field, 0, 64);
    tr_bitfieldAddRange(&field, 8, 24);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (8 <= i && i < 24));
    }

    /* test tr_bitfieldAddRange when begin & end is on the same word */
    tr_bitfieldRemRange(&field, 0, 64);
    tr_bitfieldAddRange(&field, 4, 5);

    for (unsigned int i = 0; i < 64; i++)
    {
        EXPECT_EQ(tr_bitfieldHas(&field, i), (4 <= i && i < 5));
    }

    tr_bitfieldDestruct(&field);
}

TEST(Bitfields, has_all_none)
{
    tr_bitfield field;

    tr_bitfieldConstruct(&field, 3);

    EXPECT_TRUE(!tr_bitfieldHasAll(&field));
    EXPECT_TRUE(tr_bitfieldHasNone(&field));

    tr_bitfieldAdd(&field, 0);
    EXPECT_TRUE(!tr_bitfieldHasAll(&field));
    EXPECT_TRUE(!tr_bitfieldHasNone(&field));

    tr_bitfieldRem(&field, 0);
    tr_bitfieldAdd(&field, 1);
    EXPECT_TRUE(!tr_bitfieldHasAll(&field));
    EXPECT_TRUE(!tr_bitfieldHasNone(&field));

    tr_bitfieldRem(&field, 1);
    tr_bitfieldAdd(&field, 2);
    EXPECT_TRUE(!tr_bitfieldHasAll(&field));
    EXPECT_TRUE(!tr_bitfieldHasNone(&field));

    tr_bitfieldAdd(&field, 0);
    tr_bitfieldAdd(&field, 1);
    EXPECT_TRUE(tr_bitfieldHasAll(&field));
    EXPECT_TRUE(!tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasNone(&field);
    EXPECT_TRUE(!tr_bitfieldHasAll(&field));
    EXPECT_TRUE(tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasAll(&field);
    EXPECT_TRUE(tr_bitfieldHasAll(&field));
    EXPECT_TRUE(!tr_bitfieldHasNone(&field));

    tr_bitfieldDestruct(&field);
    tr_bitfieldConstruct(&field, 0);

    EXPECT_TRUE(!tr_bitfieldHasAll(&field));
    EXPECT_TRUE(!tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasNone(&field);
    EXPECT_TRUE(!tr_bitfieldHasAll(&field));
    EXPECT_TRUE(tr_bitfieldHasNone(&field));

    tr_bitfieldSetHasAll(&field);
    EXPECT_TRUE(tr_bitfieldHasAll(&field));
    EXPECT_TRUE(!tr_bitfieldHasNone(&field));

    tr_bitfieldDestruct(&field);
}
