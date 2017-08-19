/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "transmission.h"
#include "session.h"
#include "session-id.h"
#include "utils.h"
#include "version.h"

#undef VERBOSE
#include "libtransmission-test.h"

static int testPeerId(void)
{
    uint8_t peer_id[PEER_ID_LEN + 1];

    for (int i = 0; i < 100000; ++i)
    {
        int val = 0;

        tr_peerIdInit(peer_id);

        check_uint(strlen((char*)peer_id), ==, PEER_ID_LEN);
        check_mem(peer_id, ==, PEERID_PREFIX, 8);

        for (int j = 8; j < PEER_ID_LEN; ++j)
        {
            char tmp[2] = { (char)peer_id[j], '\0' };
            val += strtoul(tmp, NULL, 36);
        }

        check_int(val % 36, ==, 0);
    }

    return 0;
}

static int test_session_id(void)
{
    tr_session_id_t session_id;
    char const* session_id_str_1 = NULL;
    char const* session_id_str_2 = NULL;
    char const* session_id_str_3 = NULL;

    check(!tr_session_id_is_local(NULL));
    check(!tr_session_id_is_local(""));
    check(!tr_session_id_is_local("test"));

    session_id = tr_session_id_new();
    check_ptr(session_id, !=, NULL);

    tr_timeUpdate(0);

    session_id_str_1 = tr_session_id_get_current(session_id);
    check_str(session_id_str_1, !=, NULL);
    check_uint(strlen(session_id_str_1), ==, 48);
    session_id_str_1 = tr_strdup(session_id_str_1);

    check(tr_session_id_is_local(session_id_str_1));

    tr_timeUpdate(60 * 60 - 1);

    check(tr_session_id_is_local(session_id_str_1));

    session_id_str_2 = tr_session_id_get_current(session_id);
    check_str(session_id_str_2, !=, NULL);
    check_uint(strlen(session_id_str_2), ==, 48);
    check_str(session_id_str_2, ==, session_id_str_1);

    tr_timeUpdate(60 * 60);

    check(tr_session_id_is_local(session_id_str_1));

    session_id_str_2 = tr_session_id_get_current(session_id);
    check_str(session_id_str_2, !=, NULL);
    check_uint(strlen(session_id_str_2), ==, 48);
    check_str(session_id_str_2, !=, session_id_str_1);
    session_id_str_2 = tr_strdup(session_id_str_2);

    check(tr_session_id_is_local(session_id_str_2));
    check(tr_session_id_is_local(session_id_str_1));

    tr_timeUpdate(60 * 60 * 2);

    check(tr_session_id_is_local(session_id_str_2));
    check(tr_session_id_is_local(session_id_str_1));

    session_id_str_3 = tr_session_id_get_current(session_id);
    check_str(session_id_str_3, !=, NULL);
    check_uint(strlen(session_id_str_3), ==, 48);
    check_str(session_id_str_3, !=, session_id_str_2);
    check_str(session_id_str_3, !=, session_id_str_1);
    session_id_str_3 = tr_strdup(session_id_str_3);

    check(tr_session_id_is_local(session_id_str_3));
    check(tr_session_id_is_local(session_id_str_2));
    check(!tr_session_id_is_local(session_id_str_1));

    tr_timeUpdate(60 * 60 * 10);

    check(tr_session_id_is_local(session_id_str_3));
    check(tr_session_id_is_local(session_id_str_2));
    check(!tr_session_id_is_local(session_id_str_1));

    check(!tr_session_id_is_local(NULL));
    check(!tr_session_id_is_local(""));
    check(!tr_session_id_is_local("test"));

    tr_session_id_free(session_id);

    check(!tr_session_id_is_local(session_id_str_3));
    check(!tr_session_id_is_local(session_id_str_2));
    check(!tr_session_id_is_local(session_id_str_1));

    tr_free((char*)session_id_str_3);
    tr_free((char*)session_id_str_2);
    tr_free((char*)session_id_str_1);

    return 0;
}

int main(void)
{
    testFunc const tests[] =
    {
        testPeerId,
        test_session_id
    };

    return runTests(tests, NUM_TESTS(tests));
}
