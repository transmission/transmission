/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* strlen() */

#include "transmission.h"
#include "quark.h"
#include "libtransmission-test.h"

static int test_static_quarks(void)
{
    for (int i = 0; i < TR_N_KEYS; i++)
    {
        tr_quark q;
        size_t len;
        char const* str;

        str = tr_quark_get_string((tr_quark)i, &len);
        check_uint(len, ==, strlen(str));
        check(tr_quark_lookup(str, len, &q));
        check_int((int)q, ==, i);
    }

    for (int i = 0; i + 1 < TR_N_KEYS; i++)
    {
        size_t len1;
        size_t len2;
        char const* str1;
        char const* str2;

        str1 = tr_quark_get_string((tr_quark)i, &len1);
        str2 = tr_quark_get_string((tr_quark)(i + 1), &len2);

        check_str(str1, <, str2);
    }

    tr_quark const q = tr_quark_new(NULL, TR_BAD_SIZE);
    check_int((int)q, ==, TR_KEY_NONE);
    check_str(tr_quark_get_string(q, NULL), ==, "");

    return 0;
}

MAIN_SINGLE_TEST(test_static_quarks)
