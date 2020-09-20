/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "session.h"
#include "session-id.h"
#include "utils.h"
#include "version.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

TEST(Session, peerId)
{
    auto const peer_id_prefix = std::string { PEERID_PREFIX };

    for (int i = 0; i < 100000; ++i)
    {
        // get a new peer-id
        auto buf = std::array<uint8_t, PEER_ID_LEN + 1>{};
        tr_peerIdInit(buf.data());

        // confirm that it has the right length
        EXPECT_EQ(PEER_ID_LEN, strlen(reinterpret_cast<char const*>(buf.data())));

        // confirm that it begins with peer_id_prefix
        auto const peer_id = std::string(reinterpret_cast<char const*>(buf.data()), PEER_ID_LEN);
        EXPECT_EQ(peer_id_prefix, peer_id.substr(0, peer_id_prefix.size()));

        // confirm that its total is evenly divisible by 36
        int val = 0;
        auto const suffix = peer_id.substr(peer_id_prefix.size());
        for (char const ch : suffix)
        {
            auto const tmp = std::array<char, 2>{ ch, '\0' };
            val += strtoul(tmp.data(), nullptr, 36);
        }

        EXPECT_EQ(0, val % 36);
    }
}

TEST(Session, sessionId)
{
    EXPECT_FALSE(tr_session_id_is_local(nullptr));
    EXPECT_FALSE(tr_session_id_is_local(""));
    EXPECT_FALSE(tr_session_id_is_local("test"));

    auto session_id = tr_session_id_new();
    EXPECT_NE(nullptr, session_id);

    tr_timeUpdate(0);

    auto const* session_id_str_1 = tr_session_id_get_current(session_id);
    EXPECT_NE(nullptr, session_id_str_1);
    EXPECT_EQ(48, strlen(session_id_str_1));
    session_id_str_1 = tr_strdup(session_id_str_1);

    EXPECT_TRUE(tr_session_id_is_local(session_id_str_1));

    tr_timeUpdate(60 * 60 - 1);

    EXPECT_TRUE(tr_session_id_is_local(session_id_str_1));

    auto const* session_id_str_2 = tr_session_id_get_current(session_id);
    EXPECT_NE(nullptr, session_id_str_2);
    EXPECT_EQ(48, strlen(session_id_str_2));
    EXPECT_STREQ(session_id_str_1, session_id_str_2);

    tr_timeUpdate(60 * 60);

    EXPECT_TRUE(tr_session_id_is_local(session_id_str_1));

    session_id_str_2 = tr_session_id_get_current(session_id);
    EXPECT_NE(nullptr, session_id_str_2);
    EXPECT_EQ(48, strlen(session_id_str_2));
    EXPECT_STRNE(session_id_str_1, session_id_str_2);
    session_id_str_2 = tr_strdup(session_id_str_2);

    EXPECT_TRUE(tr_session_id_is_local(session_id_str_2));
    EXPECT_TRUE(tr_session_id_is_local(session_id_str_1));

    tr_timeUpdate(60 * 60 * 2);

    EXPECT_TRUE(tr_session_id_is_local(session_id_str_2));
    EXPECT_TRUE(tr_session_id_is_local(session_id_str_1));

    auto const* session_id_str_3 = tr_session_id_get_current(session_id);
    EXPECT_NE(nullptr, session_id_str_3);
    EXPECT_EQ(48, strlen(session_id_str_3));
    EXPECT_STRNE(session_id_str_2, session_id_str_3);
    EXPECT_STRNE(session_id_str_1, session_id_str_3);
    session_id_str_3 = tr_strdup(session_id_str_3);

    EXPECT_TRUE(tr_session_id_is_local(session_id_str_3));
    EXPECT_TRUE(tr_session_id_is_local(session_id_str_2));
    EXPECT_FALSE(tr_session_id_is_local(session_id_str_1));

    tr_timeUpdate(60 * 60 * 10);

    EXPECT_TRUE(tr_session_id_is_local(session_id_str_3));
    EXPECT_TRUE(tr_session_id_is_local(session_id_str_2));
    EXPECT_FALSE(tr_session_id_is_local(session_id_str_1));

    tr_session_id_free(session_id);

    EXPECT_FALSE(tr_session_id_is_local(session_id_str_3));
    EXPECT_FALSE(tr_session_id_is_local(session_id_str_2));
    EXPECT_FALSE(tr_session_id_is_local(session_id_str_1));

    tr_free(const_cast<char*>(session_id_str_3));
    tr_free(const_cast<char*>(session_id_str_2));
    tr_free(const_cast<char*>(session_id_str_1));
}
