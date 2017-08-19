/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "error.h"

#include "libtransmission-test.h"

static int test_error_set(void)
{
    tr_error* err = NULL;

    tr_error_prefix(&err, "error: ");
    check_ptr(err, ==, NULL);

    tr_error_set(&err, 1, "error: %s (%d)", "oops", 2);
    check(err != NULL);
    check_int(err->code, ==, 1);
    check_str(err->message, ==, "error: oops (2)");
    tr_error_clear(&err);
    check_ptr(err, ==, NULL);

    tr_error_set_literal(&err, 2, "oops");
    check_ptr(err, !=, NULL);
    check_int(err->code, ==, 2);
    check_str(err->message, ==, "oops");

    tr_error_prefix(&err, "error: ");
    check_ptr(err, !=, NULL);
    check_int(err->code, ==, 2);
    check_str(err->message, ==, "error: oops");

    tr_error_free(err);

    return 0;
}

static int test_error_propagate(void)
{
    tr_error* err = NULL;
    tr_error* err2 = NULL;

    tr_error_set_literal(&err, 1, "oops");
    check_ptr(err, !=, NULL);
    check_int(err->code, ==, 1);
    check_str(err->message, ==, "oops");

    tr_error_propagate(&err2, &err);
    check_ptr(err2, !=, NULL);
    check_int(err2->code, ==, 1);
    check_str(err2->message, ==, "oops");
    check_ptr(err, ==, NULL);

    tr_error_propagate_prefixed(&err, &err2, "error: ");
    check_ptr(err, !=, NULL);
    check_int(err->code, ==, 1);
    check_str(err->message, ==, "error: oops");
    check_ptr(err2, ==, NULL);

    tr_error_propagate(NULL, &err);
    check_ptr(err, ==, NULL);

    tr_error_free(err2);

    return 0;
}

int main(void)
{
    testFunc const tests[] =
    {
        test_error_set,
        test_error_propagate
    };

    return runTests(tests, NUM_TESTS(tests));
}
