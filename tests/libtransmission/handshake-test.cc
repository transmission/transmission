// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cassert>
#include <cstring>
#include <string_view>

#include <event2/util.h>

#include "transmission.h"

#include "handshake.h"
#include "peer-io.h"
#include "session.h" // tr_peerIdInit()

#include "test-fixtures.h"

using namespace std::literals;

#ifdef _WIN32
#define LOCAL_SOCKETPAIR_AF AF_INET
#else
#define LOCAL_SOCKETPAIR_AF AF_UNIX
#endif

namespace libtransmission
{
namespace test
{

auto constexpr MaxWaitMsec = int{ 5000 };

using HandshakeTest = SessionTest;

class MediatorMock final : public tr_handshake_mediator
{
public:
    [[nodiscard]] std::optional<torrent_info> torrentInfo(tr_sha1_digest_t const& info_hash) const override
    {
        fmt::print("{:s}:{:d} torrentInfo info_hash {:s}\n", __FILE__, __LINE__, tr_sha1_to_string(info_hash));

        if (auto const iter = torrents.find(info_hash); iter != std::end(torrents))
        {
            return iter->second;
        }

        return {};
    }

    [[nodiscard]] std::optional<torrent_info> torrentInfoFromObfuscated(tr_sha1_digest_t const& obfuscated) const override
    {
        fmt::print("{:s}:{:d} torrentInfoFromObfuscated {:s}\n", __FILE__, __LINE__, tr_sha1_to_string(obfuscated));

        for (auto const& [info_hash, info] : torrents)
        {
            if (obfuscated == *tr_sha1("req2"sv, info.info_hash))
            {
                return info;
            }
        }

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

    [[nodiscard]] size_t pad(void* setme, size_t maxlen) const override
    {
        TR_ASSERT(maxlen > 10);
        auto const len = size_t{ 10 };
        std::fill_n(static_cast<char*>(setme), 10, ' ');
        return len;
    }

    [[nodiscard]] tr_message_stream_encryption::DH::private_key_bigend_t privateKey() const override
    {
        return private_key_;
    }

    void setUTPFailed(tr_sha1_digest_t const& info_hash, tr_address addr) override
    {
        fmt::print(
            "{:s}:{:d} setUTPFailed info_hash {:s} addr {:s}\n",
            __FILE__,
            __LINE__,
            tr_sha1_to_string(info_hash),
            addr.readable());
    }

    void setPrivateKeyFromBase64(std::string_view b64)
    {
        auto const str = tr_base64_decode(b64);
        assert(std::size(str) == std::size(private_key_));
        std::copy_n(reinterpret_cast<std::byte const*>(std::data(str)), std::size(str), std::begin(private_key_));
    }

    std::map<tr_sha1_digest_t, torrent_info> torrents;
    tr_message_stream_encryption::DH::private_key_bigend_t private_key_ = {};
};

template<typename Span>
void sendToPeer(evutil_socket_t sock, Span const& data)
{
    auto const* walk = std::data(data);
    static_assert(sizeof(*walk) == 1);
    size_t len = std::size(data);

    std::cerr << __FILE__ << ':' << __LINE__ << " writing " << len << " bytes" << std::endl;
    while (len > 0)
    {
        auto const n = write(sock, walk, len);
        auto const err = errno;
        std::cerr << __FILE__ << ':' << __LINE__ << " n " << n << std::endl;
        if (n < 0)
        {
            std::cerr << __FILE__ << ':' << __LINE__ << ' ' << tr_strerror(err);
        }
        assert(n >= 0);
        len -= n;
        walk += n;
    }
}

auto constexpr PlaintextProtocolName = "\023BitTorrent protocol"sv;
auto const default_peer_addr = *tr_address::fromString("127.0.0.1"sv);
auto const default_peer_port = tr_port::fromHost(8080);
auto const torrent_we_are_seeding = tr_handshake_mediator::torrent_info{ *tr_sha1("abcde"sv),
                                                                         tr_peerIdInit(),
                                                                         tr_torrent_id_t{ 100 },
                                                                         true };

auto createPeerIo(tr_session* session)
{
    auto sockpair = std::array<evutil_socket_t, 2>{ -1, -1 };
    EXPECT_EQ(0, evutil_socketpair(LOCAL_SOCKETPAIR_AF, SOCK_STREAM, 0, std::data(sockpair))) << tr_strerror(errno);
    auto const now = tr_time();
    auto const peer_socket = tr_peer_socket_tcp_create(sockpair[0]);
    auto* const incoming_io = tr_peerIoNewIncoming(
        session,
        &session->top_bandwidth_,
        &default_peer_addr,
        default_peer_port,
        now,
        peer_socket);
    return std::make_pair(incoming_io, sockpair[1]);
}

auto makeRandomPeerId()
{
    auto peer_id = tr_peer_id_t{};
    tr_rand_buffer(std::data(peer_id), std::size(peer_id));
    auto const peer_id_prefix = "-UW110Q-"sv;
    std::copy(std::begin(peer_id_prefix), std::end(peer_id_prefix), std::begin(peer_id));
    return peer_id;
}

TEST_F(HandshakeTest, incomingPlaintext)
{
    static auto constexpr reserved_bytes = std::array<uint8_t, 8>{ 0, 0, 0, 0, 0, 0, 0, 0 };
    auto const peer_id = makeRandomPeerId();
    auto mediator = std::make_shared<MediatorMock>();
    mediator->torrents.emplace(torrent_we_are_seeding.info_hash, torrent_we_are_seeding);

    auto [incoming_io, sock] = createPeerIo(session_);
    EXPECT_NE(nullptr, incoming_io);
    auto res = std::optional<tr_handshake_result>{};
    auto* const handshake = tr_handshakeNew(
        mediator,
        incoming_io,
        TR_CLEAR_PREFERRED,
        [](auto const& res)
        {
            *static_cast<std::optional<tr_handshake_result>*>(res.userData) = res;
            return true;
        },
        &res);
    EXPECT_NE(nullptr, handshake);

    // The simplest handshake there is. "The handshake starts with character
    // nineteen (decimal) followed by the string 'BitTorrent protocol'.
    // The leading character is a length prefix[.]. After the fixed headers
    // come eight reserved bytes, which are all zero in all current
    // implementations[.] Next comes the 20 byte sha1 hash of the bencoded
    // form of the info value from the metainfo file[.] After the download
    // hash comes the 20-byte peer id which is reported in tracker requests
    // and contained in peer lists in tracker responses.
    sendToPeer(sock, PlaintextProtocolName);
    sendToPeer(sock, reserved_bytes);
    sendToPeer(sock, torrent_we_are_seeding.info_hash);
    sendToPeer(sock, peer_id);

    // wait for the "handshake done" callback to be called
    waitFor([&res]() { return res.has_value(); }, MaxWaitMsec);

    // check the results
    EXPECT_TRUE(res);
    EXPECT_EQ(handshake, res->handshake);
    EXPECT_TRUE(res->isConnected);
    EXPECT_TRUE(res->readAnythingFromPeer);
    EXPECT_EQ(incoming_io, res->io);
    EXPECT_TRUE(res->peer_id);
    EXPECT_EQ(peer_id, res->peer_id);
    EXPECT_TRUE(incoming_io->torrentHash());
    EXPECT_EQ(torrent_we_are_seeding.info_hash, *incoming_io->torrentHash());

    tr_peerIoUnref(incoming_io);
    evutil_closesocket(sock);
}

// The peer input is identical to HandshakeTest.incomingPlaintext,
// but this time we don't recognize the infohash sent by the peer.
TEST_F(HandshakeTest, incomingPlaintextUnknownInfoHash)
{
    static auto constexpr reserved_bytes = std::array<uint8_t, 8>{ 0, 0, 0, 0, 0, 0, 0, 0 };
    auto const peer_id = makeRandomPeerId();
    auto mediator = std::make_shared<MediatorMock>();
    mediator->torrents.emplace(torrent_we_are_seeding.info_hash, torrent_we_are_seeding);

    auto [incoming_io, sock] = createPeerIo(session_);
    EXPECT_NE(nullptr, incoming_io);
    auto res = std::optional<tr_handshake_result>{};
    auto* const handshake = tr_handshakeNew(
        mediator,
        incoming_io,
        TR_CLEAR_PREFERRED,
        [](auto const& res)
        {
            *static_cast<std::optional<tr_handshake_result>*>(res.userData) = res;
            return true;
        },
        &res);
    EXPECT_NE(nullptr, handshake);

    sendToPeer(sock, PlaintextProtocolName);
    sendToPeer(sock, reserved_bytes);
    sendToPeer(sock, *tr_sha1("some other torrent unknown to us"sv));
    sendToPeer(sock, peer_id);

    // wait for the "handshake done" callback to be called
    waitFor([&res]() { return res.has_value(); }, MaxWaitMsec);

    // check the results
    EXPECT_TRUE(res);
    EXPECT_EQ(handshake, res->handshake);
    EXPECT_FALSE(res->isConnected);
    EXPECT_TRUE(res->readAnythingFromPeer);
    EXPECT_EQ(incoming_io, res->io);
    EXPECT_FALSE(res->peer_id);
    EXPECT_FALSE(incoming_io->torrentHash());

    tr_peerIoUnref(incoming_io);
    evutil_closesocket(sock);
}

TEST_F(HandshakeTest, incomingEncrypted)
{
    auto const ubuntu_torrent = tr_handshake_mediator::torrent_info{ *tr_sha1_from_string(
                                                                         "2c6b6858d61da9543d4231a71db4b1c9264b0685"sv),
                                                                     tr_peerIdInit(),
                                                                     tr_torrent_id_t{ 100 },
                                                                     true };

    // static auto constexpr reserved_bytes = std::array<uint8_t, 8>{ 0, 0, 0, 0, 0, 0, 0, 0 };
    auto mediator = std::make_shared<MediatorMock>();
    mediator->torrents.emplace(ubuntu_torrent.info_hash, ubuntu_torrent);
    mediator->setPrivateKeyFromBase64("0EYKCwBWQ4Dg9kX3c5xxjVtBDKw="sv);

    auto [incoming_io, sock] = createPeerIo(session_);
    EXPECT_NE(nullptr, incoming_io);
    auto res = std::optional<tr_handshake_result>{};
    auto* const handshake = tr_handshakeNew(
        mediator,
        incoming_io,
        TR_CLEAR_PREFERRED,
        [](auto const& res)
        {
            *static_cast<std::optional<tr_handshake_result>*>(res.userData) = res;
            return true;
        },
        &res);
    EXPECT_NE(nullptr, handshake);

    sendToPeer(
        sock,
        tr_base64_decode(
            "svkySIFcCsrDTeHjPt516UFbsoR+5vfbe5/m6stE7u5JLZ10kJ19NmP64E10qInn78sCrJgjw1yEHHwrzOcKiRlYvcMotzJMe+SjrFUnaw3KBfn2bcKBhxb/sfM9J7nJ"));
    sendToPeer(
        sock,
        tr_base64_decode(
            "ICAgICAgICAgIKdr4jIBZ4xFfO4xNiRV7Gl2azTSuTFuu06NU1WyRPif018JYeVGwrTPstEPu3V5lmzjtMGVLaL5EErlpJ93Xrz+ea6EIQEUZA+D4jKaV/to9NVi04/1W1A2PHgg+I9puac/i9BsFPcjdQeoVtU73lNCbTDQgTieyjDWmwo="));

    // wait for the "handshake done" callback to be called
    waitFor([&res]() { return res.has_value(); }, MaxWaitMsec);

    static auto constexpr ExpectedPeerId = tr_peer_id_t{ '-', 'T', 'R', '3', '0', '0', 'Z', '-', 'w', '4',
                                                         'b', 'd', '4', 'm', 'k', 'e', 'b', 'k', 'b', 'i' };

    // check the results
    EXPECT_TRUE(res);
    EXPECT_EQ(handshake, res->handshake);
    EXPECT_TRUE(res->isConnected);
    EXPECT_TRUE(res->readAnythingFromPeer);
    EXPECT_EQ(incoming_io, res->io);
    EXPECT_TRUE(res->peer_id);
    EXPECT_EQ(ExpectedPeerId, res->peer_id);
    EXPECT_TRUE(incoming_io->torrentHash());
    EXPECT_EQ(ubuntu_torrent.info_hash, *incoming_io->torrentHash());
    EXPECT_EQ(tr_sha1_to_string(ubuntu_torrent.info_hash), tr_sha1_to_string(*incoming_io->torrentHash()));

    tr_peerIoUnref(incoming_io);
    evutil_closesocket(sock);
}

// The peer input is identical to HandshakeTest.incomingEncrypted,
// but this time we don't recognize the infohash sent by the peer.
TEST_F(HandshakeTest, incomingEncryptedUnknownInfoHash)
{
    auto mediator = std::make_shared<MediatorMock>();
    mediator->setPrivateKeyFromBase64("0EYKCwBWQ4Dg9kX3c5xxjVtBDKw="sv);

    auto [incoming_io, sock] = createPeerIo(session_);
    EXPECT_NE(nullptr, incoming_io);
    auto res = std::optional<tr_handshake_result>{};
    auto* const handshake = tr_handshakeNew(
        mediator,
        incoming_io,
        TR_CLEAR_PREFERRED,
        [](auto const& res)
        {
            *static_cast<std::optional<tr_handshake_result>*>(res.userData) = res;
            return true;
        },
        &res);
    EXPECT_NE(nullptr, handshake);

    sendToPeer(
        sock,
        tr_base64_decode(
            "svkySIFcCsrDTeHjPt516UFbsoR+5vfbe5/m6stE7u5JLZ10kJ19NmP64E10qInn78sCrJgjw1yEHHwrzOcKiRlYvcMotzJMe+SjrFUnaw3KBfn2bcKBhxb/sfM9J7nJ"));
    sendToPeer(
        sock,
        tr_base64_decode(
            "ICAgICAgICAgIKdr4jIBZ4xFfO4xNiRV7Gl2azTSuTFuu06NU1WyRPif018JYeVGwrTPstEPu3V5lmzjtMGVLaL5EErlpJ93Xrz+ea6EIQEUZA+D4jKaV/to9NVi04/1W1A2PHgg+I9puac/i9BsFPcjdQeoVtU73lNCbTDQgTieyjDWmwo="));

    // wait for the "handshake done" callback to be called
    waitFor([&res]() { return res.has_value(); }, MaxWaitMsec);

    // check the results
    EXPECT_TRUE(res);
    EXPECT_EQ(handshake, res->handshake);
    EXPECT_FALSE(res->isConnected);
    EXPECT_TRUE(res->readAnythingFromPeer);
    EXPECT_FALSE(incoming_io->torrentHash());

    tr_peerIoUnref(incoming_io);
    evutil_closesocket(sock);
}

} // namespace test
} // namespace libtransmission
