/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "session.h"
#include "utils.h"
#include "version.h"

#undef VERBOSE
#include "libtransmission-test.h"

static int
testPeerId (void)
{
    int i;
    uint8_t peer_id[PEER_ID_LEN+1];

    for (i = 0; i < 100000; ++i)
    {
        int j;
        int val = 0;

        tr_peerIdInit (peer_id);

        check (strlen ((char*)peer_id) == PEER_ID_LEN);
        check (!memcmp (peer_id, PEERID_PREFIX, 8));

        for (j = 8; j < PEER_ID_LEN; ++j)
        {
            char tmp[2] = { peer_id[j], '\0' };
            val += strtoul (tmp, NULL, 36);
        }

        check ((val % 36) == 0);
    }

    return 0;
}

MAIN_SINGLE_TEST (testPeerId)
