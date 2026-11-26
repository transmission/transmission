// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Local HTTP CONNECT proxy integration for WS tracker signaling.
// A deliberately unresolvable tracker hostname proves traffic used the proxy.
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/util.h>

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

#include "test-fixtures.h"
#include "test-websocket-server.h"

using namespace std::chrono_literals;

namespace
{
class LocalConnectProxy
{
public:
    LocalConnectProxy()
    {
#ifdef _WIN32
        auto wsa = WSADATA{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            error_ = "Unable to initialize Windows sockets";
            return;
        }
        wsa_started_ = true;
#endif
        base_ = event_base_new(); // NOLINT(cppcoreguidelines-prefer-member-initializer): WSAStartup must run first.
        if (base_ == nullptr)
        {
            error_ = "Unable to create proxy event base";
            return;
        }
        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        listener_ = evconnlistener_new_bind(
            base_,
            &LocalConnectProxy::onAccept,
            this,
            LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
            -1,
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr));
        if (listener_ == nullptr)
        {
            error_ = "Unable to bind proxy";
            return;
        }
        auto actual = sockaddr_in{};
        auto length = ev_socklen_t{ sizeof(actual) };
        if (getsockname(evconnlistener_get_fd(listener_), reinterpret_cast<sockaddr*>(&actual), &length) != 0)
        {
            error_ = "Unable to query proxy address";
            return;
        }
        port_ = ntohs(actual.sin_port);
        worker_ = std::thread(
            [this]()
            {
                while (!stopping_)
                {
                    event_base_loop(base_, EVLOOP_NONBLOCK);
                    std::this_thread::sleep_for(5ms);
                }
            });
    }

    ~LocalConnectProxy()
    {
        stopping_ = true;
        if (worker_.joinable())
        {
            worker_.join();
        }
        for (auto const& tunnel : tunnels_)
        {
            if (tunnel->client != nullptr)
            {
                bufferevent_free(tunnel->client);
            }
            if (tunnel->remote != nullptr)
            {
                bufferevent_free(tunnel->remote);
            }
        }
        if (listener_ != nullptr)
        {
            evconnlistener_free(listener_);
        }
        if (base_ != nullptr)
        {
            event_base_free(base_);
        }
#ifdef _WIN32
        if (wsa_started_)
        {
            WSACleanup();
        }
#endif
    }

    LocalConnectProxy(LocalConnectProxy const&) = delete;
    LocalConnectProxy& operator=(LocalConnectProxy const&) = delete;
    LocalConnectProxy(LocalConnectProxy&&) = delete;
    LocalConnectProxy& operator=(LocalConnectProxy&&) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return error_.empty();
    }

    [[nodiscard]] std::string const& error() const noexcept
    {
        return error_;
    }

    [[nodiscard]] uint16_t port() const
    {
        return port_;
    }

    void forwardTo(uint16_t port)
    {
        backend_port_ = port;
    }

    void setReject(bool reject)
    {
        reject_ = reject;
    }

    [[nodiscard]] int connects() const
    {
        return connects_;
    }

    [[nodiscard]] int backendConnections() const
    {
        return backendConnections_;
    }

    [[nodiscard]] int forwardedRequests() const
    {
        return forwardedRequests_;
    }

private:
    struct Tunnel
    {
        LocalConnectProxy* owner = nullptr;
        bufferevent* client = nullptr;
        bufferevent* remote = nullptr;
        bool connected = false;
    };

    static void relay(bufferevent* from, Tunnel* tunnel)
    {
        auto* const to = from == tunnel->client ? tunnel->remote : tunnel->client;
        if (to != nullptr)
        {
            evbuffer_add_buffer(bufferevent_get_output(to), bufferevent_get_input(from));
        }
    }

    static void onRemoteRead(bufferevent* from, void* ctx)
    {
        relay(from, static_cast<Tunnel*>(ctx));
    }

    static void onClientRead(bufferevent* from, void* ctx)
    {
        auto* const tunnel = static_cast<Tunnel*>(ctx);
        if (tunnel->connected)
        {
            ++tunnel->owner->forwardedRequests_;
            relay(from, tunnel);
            return;
        }

        auto* const input = bufferevent_get_input(from);
        auto const size = evbuffer_get_length(input);
        if (size > size_t{ 16U } * 1024U)
        {
            return;
        }
        auto const* data = reinterpret_cast<char const*>(evbuffer_pullup(input, -1));
        if (data == nullptr)
        {
            return;
        }
        auto const request = std::string_view{ data, size };
        auto const end = request.find("\r\n\r\n");
        if (end == std::string_view::npos)
        {
            return;
        }
        if (!request.starts_with("CONNECT "))
        {
            return;
        }
        ++tunnel->owner->connects_;
        evbuffer_drain(input, end + 4);

        if (tunnel->owner->reject_)
        {
            constexpr auto Response = std::string_view{ "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\n\r\n" };
            bufferevent_write(from, Response.data(), Response.size());
            return;
        }

        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(tunnel->owner->backend_port_);

        tunnel->remote = bufferevent_socket_new(tunnel->owner->base_, -1, BEV_OPT_CLOSE_ON_FREE);
        bufferevent_setcb(tunnel->remote, &onRemoteRead, nullptr, &onEvent, tunnel);
        bufferevent_enable(tunnel->remote, EV_READ | EV_WRITE);
        bufferevent_socket_connect(tunnel->remote, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    static void onEvent(bufferevent* bev, short what, void* ctx)
    {
        auto* const tunnel = static_cast<Tunnel*>(ctx);
        if ((what & BEV_EVENT_CONNECTED) != 0 && bev == tunnel->remote)
        {
            ++tunnel->owner->backendConnections_;
            tunnel->connected = true;
            constexpr auto Response = std::string_view{ "HTTP/1.1 200 Connection Established\r\n\r\n" };
            bufferevent_write(tunnel->client, Response.data(), Response.size());
            relay(tunnel->client, tunnel);
        }
        else if ((what & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) != 0)
        {
            if (tunnel->client != nullptr)
            {
                bufferevent_free(tunnel->client);
                tunnel->client = nullptr;
            }
            if (tunnel->remote != nullptr)
            {
                bufferevent_free(tunnel->remote);
                tunnel->remote = nullptr;
            }
        }
    }

    static void onAccept(evconnlistener* /*listener*/, evutil_socket_t fd, sockaddr* /*address*/, int /*length*/, void* ctx)
    {
        auto* const proxy = static_cast<LocalConnectProxy*>(ctx);
        auto tunnel = std::make_unique<Tunnel>();
        tunnel->owner = proxy;
        tunnel->client = bufferevent_socket_new(proxy->base_, fd, BEV_OPT_CLOSE_ON_FREE);
        if (tunnel->client == nullptr)
        {
            evutil_closesocket(fd);
            return;
        }
        bufferevent_setcb(tunnel->client, &onClientRead, nullptr, &onEvent, tunnel.get());
        bufferevent_enable(tunnel->client, EV_READ | EV_WRITE);
        proxy->tunnels_.emplace_back(std::move(tunnel));
    }

    event_base* base_ = nullptr;
    evconnlistener* listener_ = nullptr;
    std::thread worker_;
    std::vector<std::unique_ptr<Tunnel>> tunnels_;
    std::atomic<bool> stopping_ = false;
    std::atomic<bool> reject_ = false;
    std::atomic<int> connects_ = 0;
    std::atomic<int> backendConnections_ = 0;
    std::atomic<int> forwardedRequests_ = 0;
    uint16_t port_ = 0;
    std::atomic<uint16_t> backend_port_ = 0;
    std::string error_;
#ifdef _WIN32
    bool wsa_started_ = false;
#endif
};

class WsTrackerProxyTest : public tr::test::SessionTest
{
protected:
    void SetUp() override
    {
        proxy_ = std::make_unique<LocalConnectProxy>();
        ASSERT_TRUE(proxy_->ready()) << proxy_->error();
        auto* const map = settings()->get_if<tr_variant::Map>();
        map->insert_or_assign(TR_KEY_webtorrent_enabled, true);
        map->insert_or_assign(TR_KEY_webtorrent_ice_servers, tr_variant::Vector{});
        map->insert_or_assign(TR_KEY_proxy_url, "http://127.0.0.1:" + std::to_string(proxy_->port()));
        tr::test::SessionTest::SetUp();
        if (!tr_websocket_client::supports_websockets())
        {
            GTEST_SKIP() << "libcurl was built without WS/WSS protocol support";
        }
    }

    void TearDown() override
    {
        tr::test::SessionTest::TearDown();
        proxy_.reset();
    }

    std::unique_ptr<LocalConnectProxy> proxy_;
};
} // namespace

TEST_F(WsTrackerProxyTest, AnnounceUsesConfiguredHttpConnectProxy)
{
    auto config = test_ws::WebSocketServer::Configuration{};
    config.port = 0;
    config.bindAddress = "127.0.0.1";
    auto server = std::make_shared<test_ws::WebSocketServer>(config);
    ASSERT_TRUE(server->ready()) << server->error();
    proxy_->forwardTo(server->port());

    auto clients = std::vector<std::shared_ptr<test_ws::WebSocket>>{};
    auto clients_mutex = std::mutex{};
    auto tracker_connections = std::atomic<int>{ 0 };
    auto tracker_announces = std::atomic<int>{ 0 };
    auto const response = std::string{ R"({"action":"announce","info_hash":")" } + std::string(20, 'A') +
        R"(","interval":125,"complete":5,"incomplete":2})";
    server->onClient(
        [&](std::shared_ptr<test_ws::WebSocket> const& client)
        {
            ++tracker_connections;
            {
                auto const lock = std::lock_guard{ clients_mutex };
                clients.emplace_back(client);
            }
            auto weak = std::weak_ptr<test_ws::WebSocket>{ client };
            client->onMessage(
                [](test_ws::binary const&) {},
                [weak, response, &tracker_announces](std::string const&)
                {
                    ++tracker_announces;
                    if (auto ws = weak.lock())
                    {
                        ws->send(response);
                    }
                });
        });

    {
        auto announcer = tr_announcer_ws{ session_ };
        auto request = tr_announce_request{};
        request.info_hash.fill(std::byte{ 'A' });
        request.peer_id.fill('B');
        // If the WebSocket bypasses the proxy, DNS resolution must fail.
        request.announce_url = tr_interned_string{ "ws://unresolvable.invalid:54321/announce" };
        request.numwant = 0;

        auto completed = std::atomic<bool>{ false };
        auto succeeded = std::atomic<bool>{ false };
        auto seeders = std::atomic<int64_t>{ -1 };
        auto error = std::string{};
        session_->run_in_session_thread(
            [&]
            {
                announcer.announce(
                    request,
                    [&](tr_announce_response const& result)
                    {
                        seeders = result.seeders.value_or(-1);
                        error = result.errmsg;
                        succeeded = result.did_connect && seeders == 5;
                        completed = true;
                    });
            });
        auto const done = tr::test::waitFor([&] { return completed.load(); }, 12000);
        EXPECT_TRUE(done) << "CONNECT=" << proxy_->connects() << " backend=" << proxy_->backendConnections()
                          << " forwarded=" << proxy_->forwardedRequests() << " tracker=" << tracker_connections
                          << " announces=" << tracker_announces;
        EXPECT_TRUE(succeeded) << "CONNECT=" << proxy_->connects() << " backend=" << proxy_->backendConnections()
                               << " forwarded=" << proxy_->forwardedRequests() << " tracker=" << tracker_connections
                               << " announces=" << tracker_announces << " seeders=" << seeders << " error=" << error;
        EXPECT_EQ(proxy_->connects(), 1);
    }

    server->onClient(nullptr);
    {
        auto const lock = std::lock_guard{ clients_mutex };
        for (auto& client : clients)
        {
            client->onMessage(nullptr);
            client->close();
        }
        clients.clear();
    }
    server->stop();
}

TEST_F(WsTrackerProxyTest, ProxyFailureNeverFallsBackToDirect)
{
    proxy_->setReject(true);

    auto announcer = tr_announcer_ws{ session_ };
    auto request = tr_announce_request{};
    request.info_hash.fill(std::byte{ 'A' });
    request.peer_id.fill('B');
    request.announce_url = tr_interned_string{ "ws://127.0.0.1:12345/announce" };
    request.numwant = 0;

    auto completed = std::atomic<bool>{ false };
    auto succeeded = std::atomic<bool>{ true };
    session_->run_in_session_thread(
        [&]
        {
            announcer.announce(
                request,
                [&](tr_announce_response const& response)
                {
                    succeeded = response.did_connect;
                    completed = true;
                });
        });
    ASSERT_TRUE(tr::test::waitFor([&] { return completed.load(); }, 12000));
    EXPECT_FALSE(succeeded);
    EXPECT_EQ(proxy_->connects(), 1);
}
