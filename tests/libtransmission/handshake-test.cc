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

TEST_F(HandshakeTest, encryptedIncoming)
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
        [](auto const& /*res*/) { std::cerr << "woot" << std::endl; return true; },
        nullptr);
    EXPECT_NE(nullptr, handshake);

    auto payload = tr_base64_decode("Jof7KVCt+K7HD4gHGxzpR+Ow5ARF0MWcZTUtc75/8NA9rY0lE3MqRH7lMTmNwS0RiHLqxmjW6vDgREINiRuE8TK5oWC36/vjIo0007gxKzaJUNiPdsgnSVHf1p9B6vEDEiNyOJipDk0A8/zQYADDn1VTUp4JHZpaEj1FLdxIEDiAFKFs6TxIsMEoJgTHtpNO6Lpavx04bmkpU7b0lTYxCI1jIUOcUwXQyxxv7flnqKrL8l1nN6V0x+1nCYSw/3JbpOkFfIhsU5WQ7AXWOyz8GXpUrirPQ/tVyJ3CtViQxY3bL6PxXB0igoIDQ56PdN2AItFEWIGBMShgC4tpXFzaftc3mcC4FuC3Kla7uXXLmlMkpUWNmn04EWPWjRk6RZ7lFd1a1pEexDYQGZOEK8v8X0gM/K1QSfcmO6JEYyRkHkKa/XZxpwuM4mpV2eqzCI85DTu+wk0P1iNpzskmf9/Mw3Oie7JT/E7N/DMxTHvEvlqWOh9XSva+y14kFRBhDhJMEDmc+xgs/3wplvGEukkQ8VZJqS43cUEYc8MVgBCBW/KLqJQpu05KgrjZKuH+oqVApftL1tBul8agvadyf/YPgUuM7cmvvESiyKQLMz2BZHhRLTZhb6t2cgvUqMc2KxHTcxTYlw=="sv);
    incoming_io->readBufferAdd(std::data(payload), std::size(payload));

    payload = tr_base64_decode("hAgwS1t1vSd4FA9Azr7i8UOaReb7cMkKDalKVKllx7O0V/EISWrZ6UCIgGr08i1s4u0=");
    incoming_io->readBufferAdd(std::data(payload), std::size(payload));

    payload = tr_base64_decode("0IBoywIAj8NB1uCGUW4i0XylZT+eNoOlj8Hsq3VLYcaJOmT9ABePpQgqvzkiTf2UkFwLdkwzgMubk9CPltA6CqvVFUw6Hw5KzxLAAcKC8nrQf+0DLuQihVCp1/DoDS2Zb4b82jEKBQm0khLndmN2/rTFYs08PEtq5fY7JRaam1sTGPb/IWatX3iZFwSj8EqVPRd9H5zz28xXH62oF1YuXzueIaHaBmhY1rQPv3CcWVGUbePXportMQRfMCCTsR+v4qbuTAKTGQIRnZMyRMIoS6roCP5rELSMHhwJFotsgHjsY/Ok6d/JydLRQz0qd3WOT1yZsLJzFRxFL2EfwEiWsvbt91TbV9930VT7HaBpNfPeF24aOc7L");
    incoming_io->readBufferAdd(std::data(payload), std::size(payload));

    evutil_closesocket(sockpair[1]);
    evutil_closesocket(sockpair[0]);
}

} // namespace test
} // namespace libtransmission
