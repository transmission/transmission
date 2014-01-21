/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <string.h> /* memset () */

#include "transmission.h"
#include "history.h"

#include "libtransmission-test.h"

static int
test1 (void)
{
    tr_recentHistory h;

    memset (&h, 0, sizeof (tr_recentHistory));

    tr_historyAdd (&h, 10000, 1);
    check_int_eq (0, (int)tr_historyGet (&h, 12000, 1000));
    check_int_eq (1, (int)tr_historyGet (&h, 12000, 3000));
    check_int_eq (1, (int)tr_historyGet (&h, 12000, 5000));
    tr_historyAdd (&h, 20000, 1);
    check_int_eq (0, (int)tr_historyGet (&h, 22000,  1000));
    check_int_eq (1, (int)tr_historyGet (&h, 22000,  3000));
    check_int_eq (2, (int)tr_historyGet (&h, 22000, 15000));
    check_int_eq (2, (int)tr_historyGet (&h, 22000, 20000));

    return 0;
}

MAIN_SINGLE_TEST (test1)
