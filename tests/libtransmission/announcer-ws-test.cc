// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Local WebSocket announce integration. No public tracker or network is needed.
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "libtransmission/announcer-common.h"
#include "libtransmission/announcer-ws.h"
#include "libtransmission/quark.h"
#include "libtransmission/session.h"
#include "libtransmission/websocket-client.h"
#include "libtransmission/announcer-ws-protocol.h"
#include "libtransmission/rtc-signaling.h"

#include "test-fixtures.h"
#include "test-websocket-server.h"

using WsTrackerAnnouncerTest = ::tr::test::SessionTest;
using namespace std::chrono_literals;

TEST_F(WsTrackerAnnouncerTest, DisabledByDefault)
{
    auto announcer = tr_announcer_ws{ session_ };
    auto request = tr_announce_request{};
    request.info_hash.fill(std::byte{ 'A' });
    request.peer_id.fill('B');
    request.announce_url = tr_interned_string{ "ws://127.0.0.1:9000" };

    auto completed = std::atomic<bool>{ false };
    auto accepted = std::atomic<bool>{ true };
    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& response)
                {
                    accepted = response.did_connect;
                    completed = true;
                });
        });

    EXPECT_TRUE(tr::test::waitFor([&] { return completed.load(); }, 2000));
    EXPECT_FALSE(accepted);
}

class WsTrackerAnnouncerEnabledTest : public tr::test::SessionTest
{
protected:
    void SetUp() override
    {
        auto* const settings_map = settings()->get_if<tr_variant::Map>();
        settings_map->insert_or_assign(TR_KEY_webtorrent_enabled, true);
        settings_map->insert_or_assign(TR_KEY_webtorrent_ice_servers, tr_variant::Vector{});
        tr::test::SessionTest::SetUp();
        if (!tr_websocket_client::supports_websockets())
        {
            GTEST_SKIP() << "libcurl was built without WS/WSS protocol support";
        }
    }
};

TEST_F(WsTrackerAnnouncerEnabledTest, AnnounceOverLocalWebSocket)
{
    auto config = test_ws::WebSocketServer::Configuration{};
    config.port = 0;
    config.bindAddress = "127.0.0.1";
    auto server = std::make_shared<test_ws::WebSocketServer>(config);
    ASSERT_TRUE(server->ready()) << server->error();

    auto const expected_hash = std::string(20, 'A');
    auto const response = std::string{ R"({"action":"announce","info_hash":")" } + expected_hash +
        R"(","interval":125,"complete":2,"incomplete":1})";

    auto seen = std::atomic<int>{ 0 };
    auto clients = std::vector<std::shared_ptr<test_ws::WebSocket>>{};
    auto client_mutex = std::mutex{};
    server->onClient(
        [&](std::shared_ptr<test_ws::WebSocket> const& socket)
        {
            {
                auto guard = std::lock_guard{ client_mutex };
                clients.emplace_back(socket);
            }
            auto weak = std::weak_ptr<test_ws::WebSocket>{ socket };
            socket->onMessage(
                [](test_ws::binary const&) {},
                [weak, &seen, response](std::string const& message)
                {
                    if (message.find(R"("action":"announce")") != std::string::npos)
                    {
                        ++seen;
                        if (auto ws = weak.lock())
                        {
                            ws->send(response);
                        }
                    }
                });
        });

    auto announcer = tr_announcer_ws{ session_ };
    auto request = tr_announce_request{};
    request.info_hash.fill(std::byte{ 'A' });
    request.peer_id.fill('B');
    request.announce_url = tr_interned_string{ "ws://127.0.0.1:" + std::to_string(server->port()) };
    request.numwant = 0;

    auto completed = std::atomic<bool>{ false };
    auto succeeded = std::atomic<bool>{ false };
    auto seeders = std::atomic<int64_t>{ -1 };
    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& answer)
                {
                    succeeded = answer.did_connect && answer.errmsg.empty();
                    seeders = answer.seeders.value_or(-1);
                    completed = true;
                });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return completed.load(); }, 12000));
    EXPECT_TRUE(succeeded);
    EXPECT_EQ(seeders, 2);
    EXPECT_EQ(seen.load(), 1);

    session_->run_in_session_thread([&] { announcer.upkeep(); });
    server->onClient(nullptr);
    {
        auto guard = std::lock_guard{ client_mutex };
        for (auto& socket : clients)
        {
            socket->onMessage(nullptr);
            socket->close();
        }
        clients.clear();
    }
    server->stop();
}

// A tracker announce reply carries an info hash but no request ID.
// A second announce for that hash must wait without blocking other torrents.
TEST_F(WsTrackerAnnouncerEnabledTest, SerializesSameInfoHashWithoutBlockingOtherTorrents)
{
    auto config = test_ws::WebSocketServer::Configuration{};
    config.port = 0;
    config.bindAddress = "127.0.0.1";
    auto server = std::make_shared<test_ws::WebSocketServer>(config);
    ASSERT_TRUE(server->ready()) << server->error();

    auto const hash_a = std::string(20, 'A');
    auto const hash_b = std::string(20, 'B');
    auto const response_a1 = std::string{ R"({"action":"announce","info_hash":")" } + hash_a +
        R"(","interval":125,"complete":7,"incomplete":1})";
    auto const response_a2 = std::string{ R"({"action":"announce","info_hash":")" } + hash_a +
        R"(","interval":125,"complete":11,"incomplete":1})";
    auto const response_b = std::string{ R"({"action":"announce","info_hash":")" } + hash_b +
        R"(","interval":125,"complete":3,"incomplete":1})";

    auto socket = std::shared_ptr<test_ws::WebSocket>{};
    auto mutex = std::mutex{};
    auto received_a = std::atomic<int>{ 0 };
    auto received_b = std::atomic<int>{ 0 };
    server->onClient(
        [&](std::shared_ptr<test_ws::WebSocket> const& client)
        {
            {
                auto const lock = std::lock_guard{ mutex };
                socket = client;
            }
            auto weak = std::weak_ptr<test_ws::WebSocket>{ client };
            client->onMessage(
                [](test_ws::binary const&) {},
                [weak, &received_a, &received_b, hash_a, hash_b, response_a2, response_b](std::string const& message)
                {
                    if (auto ws = weak.lock())
                    {
                        if (message.find(R"("info_hash":")" + hash_a + "\"") != std::string::npos)
                        {
                            if (++received_a == 2)
                            {
                                ws->send(response_a2);
                            }
                        }
                        else if (message.find(R"("info_hash":")" + hash_b + "\"") != std::string::npos)
                        {
                            ++received_b;
                            ws->send(response_b);
                        }
                    }
                });
        });

    auto announcer = tr_announcer_ws{ session_ };
    auto request_a = tr_announce_request{};
    request_a.info_hash.fill(std::byte{ 'A' });
    request_a.peer_id.fill('C');
    request_a.announce_url = tr_interned_string{ "ws://127.0.0.1:" + std::to_string(server->port()) };
    request_a.numwant = 0;
    auto request_b = request_a;
    request_b.info_hash.fill(std::byte{ 'B' });

    auto completed_a1 = std::atomic<bool>{ false };
    auto completed_a2 = std::atomic<bool>{ false };
    auto completed_b = std::atomic<bool>{ false };
    auto seeders_a1 = std::atomic<int64_t>{ -1 };
    auto seeders_a2 = std::atomic<int64_t>{ -1 };
    auto seeders_b = std::atomic<int64_t>{ -1 };

    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request_a,
                [&](tr_announce_response const& response)
                {
                    seeders_a1 = response.seeders.value_or(-1);
                    completed_a1 = true;
                });
            announcer.announce(
                request_a,
                [&](tr_announce_response const& response)
                {
                    seeders_a2 = response.seeders.value_or(-1);
                    completed_a2 = true;
                });
            announcer.announce(
                request_b,
                [&](tr_announce_response const& response)
                {
                    seeders_b = response.seeders.value_or(-1);
                    completed_b = true;
                });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return received_a.load() == 1 && completed_b; }, 12000));
    EXPECT_EQ(received_b.load(), 1);
    EXPECT_FALSE(completed_a1);
    EXPECT_FALSE(completed_a2);

    {
        auto const lock = std::lock_guard{ mutex };
        ASSERT_TRUE(socket);
        socket->send(response_a1);
    }
    ASSERT_TRUE(tr::test::waitFor([&] { return completed_a1 && completed_a2; }, 12000));
    EXPECT_EQ(received_a.load(), 2);
    EXPECT_EQ(seeders_a1.load(), 7);
    EXPECT_EQ(seeders_a2.load(), 11);
    EXPECT_EQ(seeders_b.load(), 3);

    server->onClient(nullptr);
    {
        auto const lock = std::lock_guard{ mutex };
        if (socket)
        {
            socket->onMessage(nullptr);
            socket->close();
            socket.reset();
        }
    }
    server->stop();
}

TEST_F(WsTrackerAnnouncerEnabledTest, ReconnectsAfterTrackerCloses)
{
    auto config = test_ws::WebSocketServer::Configuration{};
    config.port = 0;
    config.bindAddress = "127.0.0.1";
    auto server = std::make_shared<test_ws::WebSocketServer>(config);
    ASSERT_TRUE(server->ready()) << server->error();

    auto const expected_hash = std::string(20, 'A');
    auto const response = std::string{ R"({"action":"announce","info_hash":")" } + expected_hash +
        R"(","interval":125,"complete":2,"incomplete":1})";

    auto connections = std::atomic<int>{ 0 };
    auto replies = std::atomic<int>{ 0 };
    auto clients = std::vector<std::shared_ptr<test_ws::WebSocket>>{};
    auto client_mutex = std::mutex{};
    server->onClient(
        [&](std::shared_ptr<test_ws::WebSocket> const& socket)
        {
            auto const connection_number = ++connections;
            {
                auto guard = std::lock_guard{ client_mutex };
                clients.emplace_back(socket);
            }

            auto weak = std::weak_ptr<test_ws::WebSocket>{ socket };
            socket->onMessage(
                [](test_ws::binary const&) {},
                [weak, connection_number, &replies, response](std::string const& message)
                {
                    if (message.find(R"("action":"announce")") == std::string::npos)
                    {
                        return;
                    }

                    if (auto ws = weak.lock())
                    {
                        if (connection_number == 1)
                        {
                            // Simulate a tracker closing before replying.
                            ws->close();
                        }
                        else
                        {
                            ++replies;
                            ws->send(response);
                        }
                    }
                });
        });

    auto announcer = tr_announcer_ws{ session_ };
    auto request = tr_announce_request{};
    request.info_hash.fill(std::byte{ 'A' });
    request.peer_id.fill('B');
    request.announce_url = tr_interned_string{ "ws://127.0.0.1:" + std::to_string(server->port()) };
    request.numwant = 0;

    auto completed = std::atomic<int>{ 0 };
    auto first_succeeded = std::atomic<bool>{ true };
    auto second_succeeded = std::atomic<bool>{ false };
    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& answer)
                {
                    first_succeeded = answer.did_connect && answer.errmsg.empty();
                    ++completed;
                });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return completed.load() == 1; }, 12000));
    EXPECT_FALSE(first_succeeded);

    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& answer)
                {
                    second_succeeded = answer.did_connect && answer.errmsg.empty();
                    ++completed;
                });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return completed.load() == 2; }, 12000));
    EXPECT_TRUE(second_succeeded);
    EXPECT_EQ(connections.load(), 2);
    EXPECT_EQ(replies.load(), 1);

    session_->run_in_session_thread([&] { announcer.upkeep(); });
    server->onClient(nullptr);
    {
        auto guard = std::lock_guard{ client_mutex };
        for (auto& socket : clients)
        {
            socket->onMessage(nullptr);
            socket->close();
        }
        clients.clear();
    }
    server->stop();
}

TEST_F(WsTrackerAnnouncerEnabledTest, TimedOutAnnounceReopensTrackerConnection)
{
    auto config = test_ws::WebSocketServer::Configuration{};
    config.port = 0;
    config.bindAddress = "127.0.0.1";
    auto server = std::make_shared<test_ws::WebSocketServer>(config);
    ASSERT_TRUE(server->ready()) << server->error();

    auto const expected_hash = std::string(20, 'A');
    auto const response = std::string{ R"({"action":"announce","info_hash":")" } + expected_hash +
        R"(","interval":125,"complete":3,"incomplete":1})";
    auto connections = std::atomic<int>{ 0 };
    auto announces = std::atomic<int>{ 0 };
    auto clients = std::vector<std::shared_ptr<test_ws::WebSocket>>{};
    auto client_mutex = std::mutex{};
    server->onClient(
        [&](std::shared_ptr<test_ws::WebSocket> const& socket)
        {
            auto const connection_number = ++connections;
            {
                auto guard = std::lock_guard{ client_mutex };
                clients.emplace_back(socket);
            }
            auto weak = std::weak_ptr<test_ws::WebSocket>{ socket };
            socket->onMessage(
                [](test_ws::binary const&) {},
                [weak, connection_number, &announces, response](std::string const& message)
                {
                    if (message.find(R"("action":"announce")") == std::string::npos)
                    {
                        return;
                    }
                    ++announces;
                    if (connection_number > 1)
                    {
                        if (auto ws = weak.lock())
                        {
                            ws->send(response);
                        }
                    }
                    // Leave the first connection open without ever replying.
                });
        });

    auto announcer = tr_announcer_ws{ session_ };
    auto request = tr_announce_request{};
    request.info_hash.fill(std::byte{ 'A' });
    request.peer_id.fill('B');
    request.announce_url = tr_interned_string{ "ws://127.0.0.1:" + std::to_string(server->port()) };
    request.numwant = 0;

    auto first_done = std::atomic<bool>{ false };
    auto first_timed_out = std::atomic<bool>{ false };
    auto second_done = std::atomic<bool>{ false };
    auto second_succeeded = std::atomic<bool>{ false };
    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& answer)
                {
                    first_timed_out = answer.did_timeout;
                    first_done = true;
                });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return announces.load() == 1; }, 12000));
    // This standalone announcer is not registered with the session's upkeep.
    std::this_thread::sleep_for(26s);
    session_->run_in_session_thread([&] { announcer.upkeep(); });
    ASSERT_TRUE(tr::test::waitFor([&] { return first_done.load(); }, 2000));
    EXPECT_TRUE(first_timed_out);

    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& answer)
                {
                    second_succeeded = answer.did_connect && answer.errmsg.empty() && answer.seeders.value_or(-1) == 3;
                    second_done = true;
                });
        });
    ASSERT_TRUE(tr::test::waitFor([&] { return second_done.load(); }, 12000));
    EXPECT_TRUE(second_succeeded);
    EXPECT_EQ(connections.load(), 2);
    EXPECT_EQ(announces.load(), 2);

    server->onClient(nullptr);
    {
        auto guard = std::lock_guard{ client_mutex };
        for (auto& socket : clients)
        {
            socket->onMessage(nullptr);
            socket->close();
        }
        clients.clear();
    }
    server->stop();
}

TEST_F(WsTrackerAnnouncerEnabledTest, ReentrantRetryAfterDisconnectUsesNewConnection)
{
    auto config = test_ws::WebSocketServer::Configuration{};
    config.port = 0;
    config.bindAddress = "127.0.0.1";
    auto server = std::make_shared<test_ws::WebSocketServer>(config);
    ASSERT_TRUE(server->ready()) << server->error();

    auto const hash = std::string(20, 'A');
    auto const response = std::string{ R"({"action":"announce","info_hash":")" } + hash +
        R"(","interval":125,"complete":4,"incomplete":1})";
    auto connections = std::atomic<int>{ 0 };
    auto clients = std::vector<std::shared_ptr<test_ws::WebSocket>>{};
    auto mutex = std::mutex{};
    server->onClient(
        [&](std::shared_ptr<test_ws::WebSocket> const& socket)
        {
            auto const number = ++connections;
            {
                auto guard = std::lock_guard{ mutex };
                clients.emplace_back(socket);
            }
            auto weak = std::weak_ptr<test_ws::WebSocket>{ socket };
            socket->onMessage(
                [](test_ws::binary const&) {},
                [weak, number, response](std::string const& message)
                {
                    if (message.find(R"("action":"announce")") == std::string::npos)
                    {
                        return;
                    }
                    if (auto ws = weak.lock())
                    {
                        if (number == 1)
                        {
                            ws->close();
                        }
                        else
                        {
                            ws->send(response);
                        }
                    }
                });
        });

    auto announcer = tr_announcer_ws{ session_ };
    auto request = tr_announce_request{};
    request.info_hash.fill(std::byte{ 'A' });
    request.peer_id.fill('B');
    request.announce_url = tr_interned_string{ "ws://127.0.0.1:" + std::to_string(server->port()) };
    request.numwant = 0;

    auto first_failed = std::atomic<bool>{ false };
    auto second_succeeded = std::atomic<bool>{ false };
    auto completed = std::atomic<bool>{ false };
    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& first)
                {
                    first_failed = !first.did_connect;
                    // Retry inside the failure callback, before it returns.
                    announcer.announce(
                        request,
                        [&](tr_announce_response const& second)
                        {
                            second_succeeded = second.did_connect && second.errmsg.empty() && second.seeders.value_or(-1) == 4;
                            completed = true;
                        });
                });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return completed.load(); }, 12000));
    EXPECT_TRUE(first_failed);
    EXPECT_TRUE(second_succeeded);
    EXPECT_EQ(connections.load(), 2);

    server->onClient(nullptr);
    {
        auto guard = std::lock_guard{ mutex };
        for (auto& socket : clients)
        {
            socket->onMessage(nullptr);
            socket->close();
        }
        clients.clear();
    }
    server->stop();
}

TEST_F(WsTrackerAnnouncerEnabledTest, InvalidIncomingOffersDoNotExhaustPeerCapacity)
{
    auto signaling = tr_rtc_signaling{ session_ };
    auto invalid = tr_ws_tracker::Signal{};
    invalid.kind = tr_ws_tracker::Signal::Kind::Offer;
    invalid.info_hash = std::string(20, 'A');
    invalid.peer_id = std::string(20, 'B');
    invalid.offer_id = std::string(20, 'C');
    invalid.sdp = "not a valid WebRTC SDP";

    auto const tracker_url = std::string{ "ws://127.0.0.1:9000" };
    auto const our_peer_id = std::string(20, 'D');
    auto hash = tr_sha1_digest_t{};
    hash.fill(std::byte{ 'A' });
    auto offered = std::atomic<int>{ -1 };

    session_->run_in_session_thread(
        [&]
        {
            // More than the 128 outstanding-peer limit. Each malformed
            // description must release its allocation before the next offer.
            for (int i = 0; i < 130; ++i)
            {
                signaling.on_signal(invalid, tracker_url, our_peer_id, [](std::string const&) {});
            }
            signaling.make_offers(
                hash,
                tracker_url,
                1,
                [&](std::vector<tr_ws_tracker::Offer> const& offers) { offered = static_cast<int>(offers.size()); });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return offered.load() >= 0; }, 20000));
    EXPECT_EQ(offered.load(), 1) << "Malformed incoming offers must not exhaust WebRTC peer capacity";
}

// Incoming WebRTC offers must produce an answer once. libdatachannel's default
// automatic negotiation generates an answer when setRemoteDescription is called.
TEST_F(WsTrackerAnnouncerEnabledTest, IncomingOfferProducesAutomaticAnswer)
{
    auto offerer = std::make_shared<rtc::PeerConnection>(rtc::Configuration{});
    auto channel = offerer->createDataChannel("webtorrent");
    ASSERT_TRUE(channel);

    ASSERT_TRUE(
        tr::test::waitFor([&] { return offerer->gatheringState() == rtc::PeerConnection::GatheringState::Complete; }, 20000));
    auto const offer = offerer->localDescription();
    ASSERT_TRUE(offer);

    auto signal = tr_ws_tracker::Signal{};
    signal.kind = tr_ws_tracker::Signal::Kind::Offer;
    signal.info_hash = std::string(20, 'A');
    signal.peer_id = std::string(20, 'B');
    signal.offer_id = std::string(20, 'C');
    signal.sdp = offer->generateSdp();

    auto signaling = tr_rtc_signaling{ session_ };
    auto response = std::string{};
    auto answered = std::atomic<bool>{ false };

    session_->run_in_session_thread(
        [&]
        {
            signaling.on_signal(
                signal,
                "ws://127.0.0.1:9000",
                std::string(20, 'D'),
                [&](std::string wire)
                {
                    response = std::move(wire);
                    answered = true;
                });
        });

    ASSERT_TRUE(tr::test::waitFor([&] { return answered.load(); }, 20000))
        << "A valid incoming offer must result in an automatic SDP answer";

    auto const reply = tr_ws_tracker::parse_signal(response);
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->kind, tr_ws_tracker::Signal::Kind::Answer);
    EXPECT_EQ(reply->offer_id, signal.offer_id);
    EXPECT_FALSE(reply->sdp.empty());
}
