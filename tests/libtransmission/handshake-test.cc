// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstring>
#include <string_view>

#include <event2/util.h>

#include "transmission.h"

#include "handshake.h"
#include "peer-io.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission
{
namespace test
{

using HandshakeTest = SessionTest;

TEST_F(HandshakeTest, helloWorld)
{
    auto const addr = *tr_address::fromString("127.0.0.1"sv);
    auto const now = tr_time();
    auto const peer_socket = tr_peer_socket_tcp_create(0);
    auto const port = tr_port::fromHost(8080);

    auto* const incoming_io = tr_peerIoNewIncoming(session_, &session_->top_bandwidth_, &addr, port, now, peer_socket);
    EXPECT_NE(nullptr, incoming_io);
    tr_peerIoUnref(incoming_io);
}

TEST_F(HandshakeTest, canCreateHandshake)
{
    auto sockpair = std::array<evutil_socket_t, 2>{};
    evutil_socketpair(AF_INET, SOCK_STREAM, 0, std::data(sockpair));

    auto const addr = *tr_address::fromString("127.0.0.1"sv);
    auto const now = tr_time();
    auto const peer_socket = tr_peer_socket_tcp_create(sockpair[0]);
    auto const port = tr_port::fromHost(8080);

    auto* const incoming_io = tr_peerIoNewIncoming(session_, &session_->top_bandwidth_, &addr, port, now, peer_socket);
    EXPECT_NE(nullptr, incoming_io);
    auto* const handshake = tr_handshakeNew(
        incoming_io,
        TR_CLEAR_PREFERRED,
        [](auto const& /*res*/) { return true; },
        nullptr);
    EXPECT_NE(nullptr, handshake);
    tr_handshakeAbort(handshake);
    tr_peerIoUnref(incoming_io);

    evutil_closesocket(sockpair[1]);
    evutil_closesocket(sockpair[0]);
}

} // namespace test
} // namespace libtransmission
