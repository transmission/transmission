/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* memset() */

#include "transmission.h"
#include "history.h"

#include "libtransmission-test.h"

static int test1(void)
{
    tr_recentHistory h;

    memset(&h, 0, sizeof(tr_recentHistory));

    tr_historyAdd(&h, 10000, 1);
    check_int((int)tr_historyGet(&h, 12000, 1000), ==, 0);
    check_int((int)tr_historyGet(&h, 12000, 3000), ==, 1);
    check_int((int)tr_historyGet(&h, 12000, 5000), ==, 1);
    tr_historyAdd(&h, 20000, 1);
    check_int((int)tr_historyGet(&h, 22000, 1000), ==, 0);
    check_int((int)tr_historyGet(&h, 22000, 3000), ==, 1);
    check_int((int)tr_historyGet(&h, 22000, 15000), ==, 2);
    check_int((int)tr_historyGet(&h, 22000, 20000), ==, 2);

    return 0;
}

MAIN_SINGLE_TEST(test1)
