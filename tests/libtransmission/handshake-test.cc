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

class MediatorMock final : public tr_handshake_mediator
{
public:
    /*
    struct torrent_info
    {
        tr_sha1_digest_t info_hash;
        tr_peer_id_t client_peer_id;
        tr_torrent_id_t id;
        bool is_done;
    };*/

    [[nodiscard]] std::optional<torrent_info> torrentInfo(tr_sha1_digest_t const& info_hash) const override
    {
        fmt::print("{:s}:{:d} torrentInfo info_hash {:s}\n", __FILE__, __LINE__, tr_sha1_to_string(info_hash));
        return {};
    }

    [[nodiscard]] std::optional<torrent_info> torrentInfoFromObfuscated(tr_sha1_digest_t const& info_hash) const override
    {
        fmt::print("{:s}:{:d} torrentInfoFromObfuscated {:s}\n", __FILE__, __LINE__, tr_sha1_to_string(info_hash));
        return {};
    }

    [[nodiscard]] event_base* eventBase() const override
    {
        fmt::print("{:s}:{:d} eventBase\n", __FILE__, __LINE__);
        return nullptr;
    }

    [[nodiscard]] bool isDHTEnabled() const override
    {
        fmt::print("{:s}:{:d} isDHTEnabled\n", __FILE__, __LINE__);
        return false;
    }

    [[nodiscard]] bool isPeerKnownSeed(tr_torrent_id_t tor_id, tr_address addr) const override
    {
        fmt::print("{:s}:{:d} isPeerKnownSeed tor_id {} addr {}\n", __FILE__, __LINE__, tor_id, addr.readable());
        return false;
    }

    void setUTPFailed(tr_sha1_digest_t const& info_hash, tr_address addr) override
    {
        fmt::print("{:s}:{:d} setUTPFailed info_hash {:s} addr {:s}\n", __FILE__, __LINE__, tr_sha1_to_string(info_hash), addr.readable());
    }
};

TEST_F(HandshakeTest, helloWorld)
{
    auto const addr = *tr_address::fromString("127.0.0.1"sv);
    auto const now = tr_time();
    auto const peer_socket = tr_peer_socket_tcp_create(0);
    auto const port = tr_port::fromHost(8080);

    auto mediator = std::make_shared<MediatorMock>();
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
    auto mediator = std::make_shared<MediatorMock>();
    auto* const handshake = tr_handshakeNew(
        mediator,
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
    auto mediator = std::make_shared<MediatorMock>();
    auto* const handshake = tr_handshakeNew(
        mediator,
        incoming_io,
        TR_CLEAR_PREFERRED,
        [](auto const& /*res*/) { std::cerr << "woot" << std::endl; return true; },
        nullptr);
    EXPECT_NE(nullptr, handshake);

    fmt::print("{:s}:{:d} adding payload\n", __FILE__, __LINE__);
    auto payload = tr_base64_decode("MPJX+PAj4UrFQnlq+LeV7zVNhK5CfLL2X7rDLtdb+elvq8yvkljOQhgQqaoStoV10zaoMsn1ZZSsPjr6ezTFsRN6eRyOASXmwr2w22/a+KVP13hTfehmt6LJVJOSWzCfbbw3qwtZLIE7L+3NCaOUGmE8MqydH/q+lq8wVpOUV6SFLax5KokxDVIjVjVCg6nZgDtO9QdmkYwRRkk5KUS3l8VWlCKgIuHIXi7ghZhU+XIuTlHcWjGulUVXL8HFboiiZMWtN8QR80lhxJTJ");
    incoming_io->readBufferAdd(std::data(payload), std::size(payload));

    fmt::print("{:s}:{:d} adding payload\n", __FILE__, __LINE__);
    payload = tr_base64_decode("ZJkmwyPQjhIa9iT9kzJByUKdmqTu5nF8uPwEvimnLvA6j7gp6uS5oDFyUdYugL3Joo9oJJQoUX4WnsPECNEZHy/ZqzmxYQ1oV3k0bGwzo6DatJu5zw8Ky0aAQMX+o0tDeiKF8ImmBiBaPTJ/VpB0/nOxPkGaOVoEKBgDzcvKpVGDZyA5bpwz8HpgoJ0YYoL+sGj4ULagAkdeO/VLrTvHZfaot5j10Nueadq6j0PYc+1OEXDDpZUB7CTPKj2/RyS6Yrs8bRbG1PUx+WUMhZ02dHEMOspeZ8Apko//6zlZvyx691OeEuoP0P/q+aSWqKJCy7Ln2Ue01dH8/vdiE5HnggpBQ/IPxkrbAgfs/eRFb0DaTCrSMjiPR694mrUdfeG6QAybMNhKSuCRsybKno/fptjp+GYwms1ZkGX1Q194BbAgNosIUBdQi2s5za4XybW4FoVfsH5sagmhiXmCcF7U4eNQeBGWqJoBhKL+Z2g38Re6kwNc1OVimTJIZiFfS1Pjco5rvSTFxLsS6uLQwgzTZCLpZgY04cOSD3J2fg45MoiIHa1vZhWzRA7/dptzp9aswtEuhcXwWEIIgQT9qh8DC2V2LVg=");
    incoming_io->readBufferAdd(std::data(payload), std::size(payload));

    fmt::print("{:s}:{:d} adding payload\n", __FILE__, __LINE__);
    payload = tr_base64_decode("91JNHv4wpp1RchzFY5i5XQkYlicqdG9qFE+6tA8SAHRRuIM3VhaA66yfE1FrOuX6oVprCzHuRsUIZo1yOfKIxIowo++4yAn4JNxz6s8YMJdTcruywjvNqbwK1pDK3UNCCDl+VSPj7tC64lgD1PcSEcqwEvSQt4P5afkr5ZQRUb6OyYtDGqQvEpbqYmMExEUBjoNUv0CUIOPBOY50qIPKq0zxyj02dPD0W5VVbgGpntnSyGPbczOtKlkQ7wkPjcODyqsVBtyLeMOHtqA1ORGL68AYVYhvMutem4Cmb34zu962h3x8XYkfxHRzHELdUZ6/hMUaXFxIdjJ50QLBgNVIMxj8CxK2WaBqJEiIZp+WHltLVhYWlcHliSAW6J5y3XqXqwXKeGUKgzNEkXfEQzfy");
    incoming_io->readBufferAdd(std::data(payload), std::size(payload));

    fmt::print("{:s}:{:d} closing\n", __FILE__, __LINE__);
    evutil_closesocket(sockpair[1]);
    evutil_closesocket(sockpair[0]);
}

} // namespace test
} // namespace libtransmission
