/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "error.h"

#include "gtest/gtest.h"

TEST(Error, errorSet)
{
    tr_error* err = nullptr;

    tr_error_prefix(&err, "error: ");
    EXPECT_EQ(nullptr, err);

    tr_error_set(&err, 1, "error: %s (%d)", "oops", 2);
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(1, err->code);
    EXPECT_STREQ("error: oops (2)", err->message);
    tr_error_clear(&err);
    EXPECT_EQ(nullptr, err);

    tr_error_set_literal(&err, 2, "oops");
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(2, err->code);
    EXPECT_STREQ("oops", err->message);

    tr_error_prefix(&err, "error: ");
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(2, err->code);
    EXPECT_STREQ("error: oops", err->message);

    tr_error_free(err);
}

TEST(Error, propagate)
{
    tr_error* err = nullptr;
    tr_error* err2 = nullptr;

    tr_error_set_literal(&err, 1, "oops");
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(1, err->code);
    EXPECT_STREQ("oops", err->message);

    tr_error_propagate(&err2, &err);
    EXPECT_NE(nullptr, err2);
    EXPECT_EQ(1, err2->code);
    EXPECT_STREQ("oops", err2->message);
    EXPECT_EQ(nullptr, err);

    tr_error_propagate_prefixed(&err, &err2, "error: ");
    EXPECT_NE(nullptr, err);
    EXPECT_EQ(1, err->code);
    EXPECT_STREQ("error: oops", err->message);
    EXPECT_EQ(nullptr, err2);

    tr_error_propagate(nullptr, &err);
    EXPECT_EQ(nullptr, err);
    EXPECT_EQ(nullptr, err2);
}
