// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// WebSocket tracker client. All mutable actor state runs on the session thread;
// libcurl network callbacks only enqueue work on that thread.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include <fmt/format.h>

#include "libtransmission/announcer-common.h"
#include "libtransmission/announcer-ws.h"
#include "libtransmission/log.h"
#include "libtransmission/session.h"
#include "libtransmission/announcer-ws-protocol.h"
#include "libtransmission/rtc-signaling.h"
#include "libtransmission/websocket-client.h"

using namespace std::chrono_literals;

namespace
{
std::string hash_key(tr_sha1_digest_t const& hash)
{
    return { reinterpret_cast<char const*>(hash.data()), hash.size() };
}

std::string peer_key(tr_peer_id_t const& id)
{
    return { id.data(), id.size() };
}

tr_announce_response response_from(tr_announce_request const& request, bool const connected, std::string error = {})
{
    auto response = tr_announce_response{};
    response.info_hash = request.info_hash;
    response.did_connect = connected;
    response.errmsg = std::move(error);
    return response;
}

constexpr auto ResponseTimeout = 25s;
constexpr auto MaxPendingPerSocket = size_t{ 64 };

std::string announce_payload(tr_announce_request const& request, std::vector<tr_ws_tracker::Offer> offers)
{
    auto payload = tr_ws_tracker::Announce{};
    payload.info_hash = hash_key(request.info_hash);
    payload.peer_id = peer_key(request.peer_id);
    payload.event = tr_announce_event_get_string(request.event);
    payload.uploaded = request.up;
    payload.downloaded = request.down;
    payload.left = request.leftUntilComplete;
    payload.numwant = static_cast<int64_t>(offers.size());
    payload.offers = std::move(offers);
    return tr_ws_tracker::encode_announce(payload);
}
} // namespace

struct tr_announcer_ws::State : std::enable_shared_from_this<State>
{
    struct Pending
    {
        tr_announce_request request;
        tr_announce_response_func callback;
        std::string payload;
        std::chrono::steady_clock::time_point deadline;
    };

    struct Connection : std::enable_shared_from_this<Connection>
    {
        Connection(std::shared_ptr<State> const& parent, std::string tracker_url, std::optional<std::string> active_proxy_url)
            : owner{ parent }
            , url{ std::move(tracker_url) }
            , proxy_url{ std::move(active_proxy_url) }
        {
        }

        Connection(Connection const&) = delete;
        Connection& operator=(Connection const&) = delete;
        Connection(Connection&&) = delete;
        Connection& operator=(Connection&&) = delete;

        ~Connection()
        {
            if (ws)
            {
                ws->close();
            }
        }

        void dispatch(std::function<void(Connection&)> callback)
        {
            auto const state = owner.lock();
            if (!state || state->closing)
            {
                return;
            }
            auto const weak = weak_from_this();
            state->session->queue_session_thread(
                [weak, fn = std::move(callback)]()
                {
                    if (auto const strong = weak.lock())
                    {
                        if (auto const parent = strong->owner.lock(); parent && !parent->closing)
                        {
                            fn(*strong);
                        }
                    }
                });
        }

        void start()
        {
            auto const weak = weak_from_this();
            ws = std::make_shared<tr_websocket_client>(
                url,
                proxy_url,
                tr_websocket_client::Callbacks{
                    .opened =
                        [weak]()
                    {
                        if (auto self = weak.lock())
                        {
                            self->dispatch([](Connection& connection) { connection.on_open(); });
                        }
                    },
                    .message =
                        [weak](std::string message)
                    {
                        if (auto self = weak.lock())
                        {
                            self->dispatch([text = std::move(message)](Connection& connection)
                                           { connection.on_message(text); });
                        }
                    },
                    .writable =
                        [weak]()
                    {
                        if (auto self = weak.lock())
                        {
                            self->dispatch([](Connection& connection) { connection.flush(); });
                        }
                    },
                    .closed =
                        [weak](std::string reason)
                    {
                        if (auto self = weak.lock())
                        {
                            self->dispatch([reason = std::move(reason)](Connection& connection)
                                           { connection.on_closed(reason); });
                        }
                    },
                });
            ws->open();
        }

        void submit(Pending pending)
        {
            tr_logAddDebug(
                fmt::format(
                    "WSS announce requested: event={}, numwant={}",
                    tr_announce_event_get_string(pending.request.event),
                    pending.request.numwant),
                "WebSocketTracker");
            if (closed)
            {
                pending.callback(response_from(pending.request, false, "WebSocket connection closed"));
                return;
            }
            if (waiting.size() + outstanding_count() >= MaxPendingPerSocket)
            {
                pending.callback(response_from(pending.request, false, "Too many pending WebSocket announces"));
                return;
            }
            // Non-trickle ICE requires gathered candidates before announcing.
            if (pending.request.event != TR_ANNOUNCE_EVENT_STOPPED && pending.request.numwant > 0)
            {
                auto const parent = owner.lock();
                if (!parent)
                {
                    pending.callback(response_from(pending.request, false, "WebRTC signaling unavailable"));
                    return;
                }
                auto const shared = std::make_shared<Pending>(std::move(pending));
                auto const weak = weak_from_this();
                parent->signaling->make_offers(
                    shared->request.info_hash,
                    url,
                    static_cast<size_t>(std::min<int64_t>(shared->request.numwant, 4)),
                    [weak, shared](std::vector<tr_ws_tracker::Offer> offers)
                    {
                        tr_logAddDebug(fmt::format("WebRTC offers gathered: {}", offers.size()), "WebSocketTracker");
                        if (auto connection = weak.lock(); connection && !connection->closed)
                        {
                            shared->payload = announce_payload(shared->request, std::move(offers));
                            shared->deadline = std::chrono::steady_clock::now() + ResponseTimeout;
                            connection->enqueue(std::move(*shared));
                        }
                        else
                        {
                            shared->callback(response_from(shared->request, false, "WebSocket closed before ICE gathering"));
                        }
                    });
            }
            else
            {
                enqueue(std::move(pending));
            }
        }

        void enqueue(Pending pending)
        {
            auto const hash = hash_key(pending.request.info_hash);
            if (pending.request.event == TR_ANNOUNCE_EVENT_STOPPED)
            {
                registrations.erase(hash);
            }
            else
            {
                registrations[hash] = peer_key(pending.request.peer_id);
            }
            waiting.emplace_back(std::move(pending));
            flush();
        }

        void send_answer(std::string payload)
        {
            if (closed || answer_queue.size() >= MaxPendingPerSocket)
            {
                return;
            }
            answer_queue.emplace_back(std::move(payload));
            flush();
        }

        size_t outstanding_count() const
        {
            return in_flight.size();
        }

        void on_open()
        {
            if (closed)
            {
                return;
            }
            tr_logAddDebug("WSS tracker connected", "WebSocketTracker");
            ready = true;
            flush();
        }

        void flush()
        {
            if (!ready || closed)
            {
                return;
            }
            if (auto parent = owner.lock(); !parent || parent->session->settings().proxy_url != proxy_url)
            {
                on_closed("WebSocket tracker proxy settings changed");
                return;
            }

            while (!answer_queue.empty())
            {
                if (!ws->send(answer_queue.front()))
                {
                    return;
                }
                tr_logAddTrace("WSS answer delivered to tracker socket", "WebSocketTracker");
                answer_queue.pop_front();
            }
            // Tracker announce replies have no request ID. Serialize requests
            // for the same info hash, while allowing other torrents to progress.
            while (!waiting.empty())
            {
                auto const it = std::ranges::find_if(
                    waiting,
                    [this](Pending const& pending) { return !in_flight.contains(hash_key(pending.request.info_hash)); });
                if (it == waiting.end())
                {
                    break;
                }
                if (!ws->send(it->payload))
                {
                    return; // Retry after buffered-amount-low.
                }

                tr_logAddTrace("WSS announce delivered to tracker socket", "WebSocketTracker");
                auto submitted = std::move(*it);
                waiting.erase(it);
                // Each serialized announce gets its own full response window.
                submitted.deadline = std::chrono::steady_clock::now() + ResponseTimeout;
                if (submitted.request.event == TR_ANNOUNCE_EVENT_STOPPED)
                {
                    submitted.callback(response_from(submitted.request, true));
                }
                else
                {
                    in_flight.emplace(hash_key(submitted.request.info_hash), std::move(submitted));
                }
            }
        }

        void on_message(std::string_view message)
        {
            if (closed)
            {
                return;
            }
            if (auto parent = owner.lock(); !parent || parent->session->settings().proxy_url != proxy_url)
            {
                on_closed("WebSocket tracker proxy settings changed");
                return;
            }
            auto const signal = tr_ws_tracker::parse_signal(message);
            if (!signal)
            {
                tr_logAddDebug("WSS response ignored: invalid or unsupported signaling message", "WebSocketTracker");
                return;
            }
            tr_logAddDebug(
                signal->kind == tr_ws_tracker::Signal::Kind::Offer      ? "WSS received WebRTC offer" :
                    signal->kind == tr_ws_tracker::Signal::Kind::Answer ? "WSS received WebRTC answer" :
                                                                          "WSS received tracker announce response",
                "WebSocketTracker");
            if (signal->kind == tr_ws_tracker::Signal::Kind::Offer || signal->kind == tr_ws_tracker::Signal::Kind::Answer)
            {
                auto const reg = registrations.find(signal->info_hash);
                if (reg != registrations.end())
                {
                    if (auto const parent = owner.lock())
                    {
                        auto const weak = weak_from_this();
                        parent->signaling->on_signal(
                            *signal,
                            url,
                            reg->second,
                            [weak](std::string answer)
                            {
                                if (auto connection = weak.lock())
                                {
                                    connection->send_answer(std::move(answer));
                                }
                            });
                    }
                }
                return;
            }
            auto it = in_flight.find(signal->info_hash);
            if (it == in_flight.end())
            {
                return;
            }
            auto pending = std::move(it->second);
            in_flight.erase(it);

            tr_logAddTrace("WSS tracker announce response matched pending request", "WebSocketTracker");
            auto response = response_from(pending.request, true);
            response.interval = signal->interval;
            response.seeders = signal->complete;
            response.leechers = signal->incomplete;
            response.errmsg = signal->failure;
            pending.callback(response);
            if (!closed)
            {
                flush();
            }
        }

        void fail_expired(std::chrono::steady_clock::time_point const now)
        {
            auto expired = std::vector<Pending>{};
            for (auto it = waiting.begin(); it != waiting.end();)
            {
                if (it->deadline <= now && !in_flight.contains(hash_key(it->request.info_hash)))
                {
                    expired.emplace_back(std::move(*it));
                    it = waiting.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            for (auto it = in_flight.begin(); it != in_flight.end();)
            {
                if (it->second.deadline <= now)
                {
                    expired.emplace_back(std::move(it->second));
                    it = in_flight.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            if (!expired.empty())
            {
                // A tracker response has no request ID, so a late response
                // must not be delivered to a newer announce on this socket.
                on_closed("WebSocket announce response timed out");
            }
            for (auto& pending : expired)
            {
                auto response = response_from(pending.request, true, "WebSocket announce response timed out");
                response.did_timeout = true;
                pending.callback(response);
            }
        }

        void on_closed(std::string const& reason)
        {
            if (closed)
            {
                return;
            }
            // Do not log untrusted WebSocket error details that may contain addresses or credentials.
            tr_logAddWarn(
                reason == "WebSocket announce response timed out" ? "WSS announce response timed out" :
                    reason == "WebSocket tracker closed"          ? "WSS tracker closed the connection" :
                                                                    "WSS connection failed or closed",
                "WebSocketTracker");
            closed = true;
            ready = false;
            auto failed = std::vector<Pending>{};
            while (!waiting.empty())
            {
                failed.emplace_back(std::move(waiting.front()));
                waiting.pop_front();
            }
            for (auto& [hash, pending] : in_flight)
            {
                failed.emplace_back(std::move(pending));
            }
            in_flight.clear();
            answer_queue.clear();
            registrations.clear();
            // Finish the worker before releasing the owning connection reference.
            ws->close();
            // Remove this session before invoking callbacks: a callback may
            // synchronously retry, which must open a new connection.
            if (auto parent = owner.lock())
            {
                auto it = parent->connections.find(url);
                if (it != parent->connections.end() && it->second.get() == this)
                {
                    parent->connections.erase(it);
                }
            }
            for (auto& pending : failed)
            {
                pending.callback(response_from(pending.request, false, reason));
            }
        }

        std::weak_ptr<State> owner;
        std::string const url;
        std::optional<std::string> const proxy_url;
        std::shared_ptr<tr_websocket_client> ws;
        bool ready = false;
        bool closed = false;
        std::deque<Pending> waiting;
        std::map<std::string, Pending> in_flight;
        std::map<std::string, std::string> registrations;
        std::deque<std::string> answer_queue;
    };

    explicit State(tr_session* value)
        : session{ value }
        , signaling{ std::make_unique<tr_rtc_signaling>(value) }
    {
    }

    tr_session* const session;
    std::unique_ptr<tr_rtc_signaling> signaling;
    std::atomic<bool> closing = false;
    std::map<std::string, std::shared_ptr<Connection>> connections;
};

tr_announcer_ws::tr_announcer_ws(tr_session* session)
    : state_{ std::make_shared<State>(session) }
{
}

tr_announcer_ws::~tr_announcer_ws()
{
    state_->closing = true;
    for (auto const& [url, connection] : state_->connections)
    {
        if (connection->ws)
        {
            connection->ws->close();
        }
    }
    state_->connections.clear();
}

void tr_announcer_ws::announce(tr_announce_request const& request, tr_announce_response_func on_response)
{
    if (!state_->session->settings().webtorrent_enabled)
    {
        on_response(response_from(request, false, "WebRTC support is disabled in session settings"));
        return;
    }
    auto const& proxy_url = state_->session->settings().proxy_url;
    auto const url = std::string{ request.announce_url.sv() };
    // Reconnect when the tracker proxy configuration changes.
    if (auto it = state_->connections.find(url); it != state_->connections.end() && it->second->proxy_url != proxy_url)
    {
        it->second->on_closed("WebSocket tracker proxy settings changed");
    }

    auto pending = State::Pending{
        .request = request,
        .callback = std::move(on_response),
        .payload = announce_payload(request, {}),
        .deadline = std::chrono::steady_clock::now() + ResponseTimeout,
    };
    auto [it, inserted] = state_->connections.try_emplace(url);
    if (inserted)
    {
        it->second = std::make_shared<State::Connection>(state_, url, proxy_url);
    }
    auto connection = it->second;
    connection->submit(std::move(pending));
    if (inserted)
    {
        connection->start();
    }
}

void tr_announcer_ws::upkeep()
{
    auto const now = std::chrono::steady_clock::now();
    auto connections = std::vector<std::shared_ptr<State::Connection>>{};
    connections.reserve(state_->connections.size());
    for (auto const& [url, connection] : state_->connections)
    {
        connections.push_back(connection);
    }
    for (auto& connection : connections)
    {
        if (connection->proxy_url != state_->session->settings().proxy_url)
        {
            connection->on_closed("WebSocket tracker proxy settings changed");
        }
        else
        {
            connection->fail_expired(now);
        }
    }
    state_->signaling->upkeep();
}
