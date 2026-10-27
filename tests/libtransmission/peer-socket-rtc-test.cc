// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra-semi"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <rtc/rtc.hpp>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "libtransmission/error.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket-rtc.h"

#include "test-fixtures.h"

using namespace std::chrono_literals;

class PeerSocketRtcTest : public ::tr::test::TransmissionTest
{
protected:
    void SetUp() override
    {
        rtc::InitLogger(rtc::LogLevel::Warning);
    }
};

TEST_F(PeerSocketRtcTest, BidirectionalBitTorrentHandshake)
{
    rtc::Configuration config1;
    rtc::Configuration config2;

    auto pc1 = std::make_shared<rtc::PeerConnection>(config1);
    auto pc2 = std::make_shared<rtc::PeerConnection>(config2);

    // Direct loopback signaling
    pc1->onLocalDescription([pc2](rtc::Description sdp) { pc2->setRemoteDescription(sdp); });
    pc2->onLocalDescription([pc1](rtc::Description sdp) { pc1->setRemoteDescription(sdp); });
    pc1->onLocalCandidate([pc2](rtc::Candidate cand) { pc2->addRemoteCandidate(cand); });
    pc2->onLocalCandidate([pc1](rtc::Candidate cand) { pc1->addRemoteCandidate(cand); });

    std::shared_ptr<rtc::DataChannel> dc2;
    std::mutex cv_m;
    std::condition_variable cv;
    bool dc1_opened = false;
    bool dc2_opened = false;

    pc2->onDataChannel(
        [&](std::shared_ptr<rtc::DataChannel> dc)
        {
            dc2 = dc;
            dc2->onOpen(
                [&]()
                {
                    std::lock_guard lock(cv_m);
                    dc2_opened = true;
                    cv.notify_all();
                });
        });

    auto dc1 = pc1->createDataChannel("webtorrent");
    dc1->onOpen(
        [&]()
        {
            std::lock_guard lock(cv_m);
            dc1_opened = true;
            cv.notify_all();
        });

    // Wait for DataChannels to open
    {
        std::unique_lock lock(cv_m);
        ASSERT_TRUE(cv.wait_for(lock, 10s, [&]() { return dc1_opened && dc2_opened; }));
    }

    auto const addr1 = *tr_socket_address::from_string("127.0.0.1:10001");
    auto const addr2 = *tr_socket_address::from_string("127.0.0.1:10002");

    auto sock1 = tr_peer_socket_rtc::create(addr1, dc1);
    auto sock2 = tr_peer_socket_rtc::create(addr2, dc2);

    EXPECT_TRUE(sock1->is_rtc());
    EXPECT_FALSE(sock1->is_tcp());
    EXPECT_FALSE(sock1->is_utp());
    EXPECT_TRUE(sock2->is_rtc());

    // Setup read callbacks
    bool sock2_read_ready = false;
    sock2->set_read_cb(
        [&]()
        {
            std::lock_guard lock(cv_m);
            sock2_read_ready = true;
            cv.notify_all();
        });

    sock1->set_write_enabled(true);
    sock2->set_read_enabled(true);

    // 1. Send handshake from sock1 to sock2
    std::string const handshake_data = std::string("\x13", 1) + "BitTorrent protocol" + std::string(8, '\0') +
        "12345678901234567890" + // info_hash
        "-TR4000-012345678901"; // peer_id

    tr_peer_socket::PeerBuffer outbuf;
    outbuf.add(handshake_data.data(), handshake_data.size());

    tr_error err;
    auto const n_written = sock1->try_write(outbuf, handshake_data.size(), &err);
    EXPECT_EQ(n_written, handshake_data.size());

    // Wait for read_cb on sock2
    {
        std::unique_lock lock(cv_m);
        ASSERT_TRUE(cv.wait_for(lock, 10s, [&]() { return sock2_read_ready; }));
    }

    tr_peer_socket::PeerBuffer inbuf;
    auto const n_read = sock2->try_read(inbuf, 1024, &err);
    EXPECT_EQ(n_read, handshake_data.size());

    std::string const received(reinterpret_cast<char const*>(inbuf.data()), inbuf.size());
    EXPECT_EQ(received, handshake_data);

    // 2. Send handshake response back from sock2 to sock1
    bool sock1_read_ready = false;
    sock1->set_read_cb(
        [&]()
        {
            std::lock_guard lock(cv_m);
            sock1_read_ready = true;
            cv.notify_all();
        });

    sock2->set_write_enabled(true);
    sock1->set_read_enabled(true);

    std::string const handshake_reply = std::string("\x13", 1) + "BitTorrent protocol" + std::string(8, '\0') +
        "12345678901234567890" + // info_hash
        "-WW0100-abcdefghijkl"; // peer_id

    outbuf.add(handshake_reply.data(), handshake_reply.size());
    auto const n_written2 = sock2->try_write(outbuf, handshake_reply.size(), &err);
    EXPECT_EQ(n_written2, handshake_reply.size());

    // Wait for read_cb on sock1
    {
        std::unique_lock lock(cv_m);
        ASSERT_TRUE(cv.wait_for(lock, 10s, [&]() { return sock1_read_ready; }));
    }

    inbuf.drain(inbuf.size());
    auto const n_read2 = sock1->try_read(inbuf, 1024, &err);
    EXPECT_EQ(n_read2, handshake_reply.size());

    std::string const received2(reinterpret_cast<char const*>(inbuf.data()), inbuf.size());
    EXPECT_EQ(received2, handshake_reply);
}

TEST_F(PeerSocketRtcTest, BlockDataTransfer)
{
    rtc::Configuration config;
    auto pc1 = std::make_shared<rtc::PeerConnection>(config);
    auto pc2 = std::make_shared<rtc::PeerConnection>(config);

    pc1->onLocalDescription([pc2](rtc::Description sdp) { pc2->setRemoteDescription(sdp); });
    pc2->onLocalDescription([pc1](rtc::Description sdp) { pc1->setRemoteDescription(sdp); });
    pc1->onLocalCandidate([pc2](rtc::Candidate cand) { pc2->addRemoteCandidate(cand); });
    pc2->onLocalCandidate([pc1](rtc::Candidate cand) { pc1->addRemoteCandidate(cand); });

    std::shared_ptr<rtc::DataChannel> dc2;
    std::mutex cv_m;
    std::condition_variable cv;
    bool dc1_opened = false;
    bool dc2_opened = false;

    pc2->onDataChannel(
        [&](std::shared_ptr<rtc::DataChannel> dc)
        {
            dc2 = dc;
            dc2->onOpen(
                [&]()
                {
                    std::lock_guard lock(cv_m);
                    dc2_opened = true;
                    cv.notify_all();
                });
        });

    auto dc1 = pc1->createDataChannel("webtorrent");
    dc1->onOpen(
        [&]()
        {
            std::lock_guard lock(cv_m);
            dc1_opened = true;
            cv.notify_all();
        });

    {
        std::unique_lock lock(cv_m);
        ASSERT_TRUE(cv.wait_for(lock, 10s, [&]() { return dc1_opened && dc2_opened; }));
    }

    auto const addr1 = *tr_socket_address::from_string("127.0.0.1:10001");
    auto const addr2 = *tr_socket_address::from_string("127.0.0.1:10002");

    auto sock1 = tr_peer_socket_rtc::create(addr1, dc1);
    auto sock2 = tr_peer_socket_rtc::create(addr2, dc2);

    sock1->set_write_enabled(true);
    sock2->set_read_enabled(true);

    // Send a full 16KB BitTorrent block
    std::vector<std::byte> block(TrBlockSize);
    for (size_t i = 0; i < TrBlockSize; ++i)
    {
        block[i] = static_cast<std::byte>(i & 0xFF);
    }

    tr_peer_socket::PeerBuffer outbuf;
    outbuf.add(block.data(), block.size());

    tr_error err;
    auto const written = sock1->try_write(outbuf, block.size(), &err);
    EXPECT_EQ(written, block.size());

    size_t total_received = 0;
    std::vector<std::byte> received_block;
    received_block.reserve(TrBlockSize);

    auto const start_time = std::chrono::steady_clock::now();
    while (total_received < TrBlockSize && std::chrono::steady_clock::now() - start_time < 10s)
    {
        tr_peer_socket::PeerBuffer inbuf;
        auto const n = sock2->try_read(inbuf, TrBlockSize - total_received, &err);
        if (n > 0)
        {
            auto const* p = inbuf.data();
            received_block.insert(received_block.end(), p, p + n);
            total_received += n;
        }
        else
        {
            std::this_thread::sleep_for(10ms);
        }
    }

    EXPECT_EQ(total_received, TrBlockSize);
    EXPECT_EQ(received_block, block);
}

TEST_F(PeerSocketRtcTest, CloseHandling)
{
    rtc::Configuration config;
    auto pc1 = std::make_shared<rtc::PeerConnection>(config);
    auto pc2 = std::make_shared<rtc::PeerConnection>(config);

    pc1->onLocalDescription([pc2](rtc::Description sdp) { pc2->setRemoteDescription(sdp); });
    pc2->onLocalDescription([pc1](rtc::Description sdp) { pc1->setRemoteDescription(sdp); });
    pc1->onLocalCandidate([pc2](rtc::Candidate cand) { pc2->addRemoteCandidate(cand); });
    pc2->onLocalCandidate([pc1](rtc::Candidate cand) { pc1->addRemoteCandidate(cand); });

    std::shared_ptr<rtc::DataChannel> dc2;
    std::mutex cv_m;
    std::condition_variable cv;
    bool dc1_opened = false;
    bool dc2_opened = false;

    pc2->onDataChannel(
        [&](std::shared_ptr<rtc::DataChannel> dc)
        {
            dc2 = dc;
            dc2->onOpen(
                [&]()
                {
                    std::lock_guard lock(cv_m);
                    dc2_opened = true;
                    cv.notify_all();
                });
        });

    auto dc1 = pc1->createDataChannel("webtorrent");
    dc1->onOpen(
        [&]()
        {
            std::lock_guard lock(cv_m);
            dc1_opened = true;
            cv.notify_all();
        });

    {
        std::unique_lock lock(cv_m);
        ASSERT_TRUE(cv.wait_for(lock, 10s, [&]() { return dc1_opened && dc2_opened; }));
    }

    auto const addr1 = *tr_socket_address::from_string("127.0.0.1:10001");
    auto const addr2 = *tr_socket_address::from_string("127.0.0.1:10002");

    auto sock1 = tr_peer_socket_rtc::create(addr1, dc1);
    auto sock2 = tr_peer_socket_rtc::create(addr2, dc2);

    bool error_reported = false;
    sock2->set_error_cb(
        [&]([[maybe_unused]] tr_error const& error)
        {
            std::lock_guard lock(cv_m);
            error_reported = true;
            cv.notify_all();
        });

    // Close dc1
    dc1->close();

    // Wait for sock2 to report closure
    {
        std::unique_lock lock(cv_m);
        EXPECT_TRUE(cv.wait_for(lock, 5s, [&]() { return error_reported; }));
    }

    tr_peer_socket::PeerBuffer inbuf;
    tr_error err;
    auto const n = sock2->try_read(inbuf, 100, &err);
    EXPECT_EQ(n, 0U);
    EXPECT_EQ(err.code(), ENOTCONN);
}
