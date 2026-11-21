// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// WebRTC signaling orchestrator: non-trickle SDP after ICE gathering completes.
// All maps and callbacks are accessed on Transmission's session event thread.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

#include <fmt/format.h>

#include "libtransmission/crypto-utils.h"
#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket-rtc.h"
#include "libtransmission/session.h"
#include "libtransmission/rtc-signaling.h"
#include "libtransmission/rtc-endpoint.h"
#include "libtransmission/rtc-network.h"

#if defined(_WIN32) && defined(__clang_analyzer__)
// libdatachannel may throw; work around LLVM #44701 for Windows clang-tidy.
#define WORKAROUND_CLANG_TIDY_GH44701
#endif

using namespace std::chrono_literals;

namespace
{
constexpr auto OfferLifetime = 120s;
constexpr auto GatheringLifetime = 20s;
constexpr auto MaxOutstandingPeers = size_t{ 128 };
constexpr auto MaxOffersPerAnnounce = size_t{ 4 };

std::string_view candidate_type_name(rtc::Candidate::Type type)
{
    switch (type)
    {
    case rtc::Candidate::Type::Host:
        return "host";
    case rtc::Candidate::Type::ServerReflexive:
        return "srflx";
    case rtc::Candidate::Type::PeerReflexive:
        return "prflx";
    case rtc::Candidate::Type::Relayed:
        return "relay";
    default:
        return "unknown";
    }
}

std::string_view rtc_state_name(rtc::PeerConnection::State state)
{
    switch (state)
    {
    case rtc::PeerConnection::State::New:
        return "new";
    case rtc::PeerConnection::State::Connecting:
        return "connecting";
    case rtc::PeerConnection::State::Connected:
        return "connected";
    case rtc::PeerConnection::State::Disconnected:
        return "disconnected";
    case rtc::PeerConnection::State::Failed:
        return "failed";
    case rtc::PeerConnection::State::Closed:
        return "closed";
    default:
        return "unknown";
    }
}

std::string_view ice_state_name(rtc::PeerConnection::IceState state)
{
    switch (state)
    {
    case rtc::PeerConnection::IceState::New:
        return "new";
    case rtc::PeerConnection::IceState::Checking:
        return "checking";
    case rtc::PeerConnection::IceState::Connected:
        return "connected";
    case rtc::PeerConnection::IceState::Completed:
        return "completed";
    case rtc::PeerConnection::IceState::Failed:
        return "failed";
    case rtc::PeerConnection::IceState::Disconnected:
        return "disconnected";
    case rtc::PeerConnection::IceState::Closed:
        return "closed";
    default:
        return "unknown";
    }
}

void log_candidate_summary(uint64_t peer_id, std::string_view description_kind, rtc::Description const& description)
{
    auto const candidates = description.candidates();
    auto host = size_t{};
    auto srflx = size_t{};
    auto prflx = size_t{};
    auto relay = size_t{};
    auto unknown = size_t{};
    auto ipv4 = size_t{};
    auto ipv6 = size_t{};
    auto unresolved = size_t{};
    auto udp = size_t{};
    auto tcp = size_t{};
    auto other_transport = size_t{};

    for (auto const& candidate : candidates)
    {
        switch (candidate.type())
        {
        case rtc::Candidate::Type::Host:
            ++host;
            break;
        case rtc::Candidate::Type::ServerReflexive:
            ++srflx;
            break;
        case rtc::Candidate::Type::PeerReflexive:
            ++prflx;
            break;
        case rtc::Candidate::Type::Relayed:
            ++relay;
            break;
        default:
            ++unknown;
            break;
        }

        switch (candidate.family())
        {
        case rtc::Candidate::Family::Ipv4:
            ++ipv4;
            break;
        case rtc::Candidate::Family::Ipv6:
            ++ipv6;
            break;
        default:
            ++unresolved;
            break;
        }

        switch (candidate.transportType())
        {
        case rtc::Candidate::TransportType::Udp:
            ++udp;
            break;
        case rtc::Candidate::TransportType::Unknown:
            ++other_transport;
            break;
        default:
            ++tcp;
            break;
        }
    }

    tr_logAddDebug(
        fmt::format(
            "WebRTC peer={} {} candidates total={} host={} srflx={} prflx={} relay={} unknown={} "
            "ipv4={} ipv6={} unresolved={} udp={} tcp={} other-transport={} end-of-candidates={}",
            peer_id,
            description_kind,
            candidates.size(),
            host,
            srflx,
            prflx,
            relay,
            unknown,
            ipv4,
            ipv6,
            unresolved,
            udp,
            tcp,
            other_transport,
            description.ended()),
        "WebRTC");
}

std::string hash_key(tr_sha1_digest_t const& hash)
{
    return { reinterpret_cast<char const*>(hash.data()), hash.size() };
}

std::string new_offer_id()
{
    auto random = std::array<uint8_t, 10>{};
    tr_rand_buffer(random.data(), random.size());
    static constexpr auto Hex = "0123456789abcdef";
    auto result = std::string{};
    result.reserve(20);
    for (auto const byte : random)
    {
        result.push_back(Hex[byte >> 4U]);
        result.push_back(Hex[byte & 0x0FU]);
    }
    return result;
}
} // namespace

struct tr_rtc_signaling::State : std::enable_shared_from_this<State>
{
    struct Batch;
    struct Peer;

    explicit State(tr_session* value)
        : session{ value }
    {
    }

    State(State const&) = delete;
    State& operator=(State const&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;

    ~State()
    {
        closing = true;
        peers.clear();
        batches.clear();
    }

    tr_session* const session;
    std::atomic<bool> closing = false;
    std::map<std::string, std::shared_ptr<Peer>> peers;
    std::vector<std::shared_ptr<Batch>> batches;
    uint64_t next_diagnostic_peer = 0;

    struct Batch
    {
        size_t remaining = 0;
        bool completed = false;
        std::chrono::steady_clock::time_point deadline;
        std::vector<tr_ws_tracker::Offer> offers;
        tr_rtc_signaling::OffersReady callback;

        void complete()
        {
            if (completed)
            {
                return;
            }
            completed = true;
            auto fn = std::move(callback);
            fn(std::move(offers));
        }
    };

    struct Peer : std::enable_shared_from_this<Peer>
    {
        Peer(
            std::shared_ptr<State> const& parent,
            std::string key,
            std::string tracker,
            std::string hash,
            bool is_incoming,
            std::optional<std::string> bind_address)
            : owner{ parent }
            , diagnostic_id{ ++parent->next_diagnostic_peer }
            , id{ std::move(key) }
            , tracker_url{ std::move(tracker) }
            , info_hash{ std::move(hash) }
            , incoming{ is_incoming }
            , pc{ std::make_shared<rtc::PeerConnection>(ice_config(parent->session, std::move(bind_address))) }
            , created{ std::chrono::steady_clock::now() }
        {
        }

        Peer(Peer const&) = delete;
        Peer& operator=(Peer const&) = delete;
        Peer(Peer&&) = delete;
        Peer& operator=(Peer&&) = delete;

        ~Peer()
        {
#ifndef WORKAROUND_CLANG_TIDY_GH44701
            try
#endif
            {
                pc->resetCallbacks();
                if (dc)
                {
                    dc->onOpen(nullptr);
                }
                pc->close();
            }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
            catch (std::exception const&) // NOLINT(bugprone-empty-catch)
            {
                // May already be closed.
            }
#endif
        }

        static rtc::Configuration ice_config(tr_session const* session, std::optional<std::string> bind_address)
        {
            auto config = rtc::Configuration{};
            config.bindAddress = std::move(bind_address);
            for (auto const& server : session->settings().webtorrent_ice_servers)
            {
                if (server.starts_with("stun:") || server.starts_with("turn:") || server.starts_with("turns:"))
                {
                    config.iceServers.emplace_back(server);
                }
            }
            return config;
        }

        void log_ice_configuration() const
        {
            auto const* const config = pc->config();
            if (config == nullptr)
            {
                return;
            }

            auto stun = size_t{};
            auto turn_udp = size_t{};
            auto turn_tcp = size_t{};
            auto turn_tls = size_t{};
            for (auto const& server : config->iceServers)
            {
                if (server.type == rtc::IceServer::Type::Stun)
                {
                    ++stun;
                }
                else
                {
                    switch (server.relayType)
                    {
                    case rtc::IceServer::RelayType::TurnUdp:
                        ++turn_udp;
                        break;
                    case rtc::IceServer::RelayType::TurnTcp:
                        ++turn_tcp;
                        break;
                    case rtc::IceServer::RelayType::TurnTls:
                        ++turn_tls;
                        break;
                    }
                }
            }

            tr_logAddDebug(
                fmt::format(
                    "WebRTC peer={} direction={} ICE config stun={} turn-udp={} turn-tcp={} turn-tls={} "
                    "policy={} ice-tcp={} udp-mux={} explicit-bind={} port-range={}-{}",
                    diagnostic_id,
                    incoming ? "incoming" : "outgoing",
                    stun,
                    turn_udp,
                    turn_tcp,
                    turn_tls,
                    config->iceTransportPolicy == rtc::TransportPolicy::Relay ? "relay" : "all",
                    config->enableIceTcp,
                    config->enableIceUdpMux,
                    config->bindAddress.has_value(),
                    config->portRangeBegin,
                    config->portRangeEnd),
                "WebRTC");
        }

        void dispatch(std::function<void(Peer&)> callback)
        {
            auto const parent = owner.lock();
            if (!parent || parent->closing)
            {
                return;
            }
            auto const weak = weak_from_this();
            parent->session->queue_session_thread(
                [weak, callback = std::move(callback)]()
                {
                    if (auto peer = weak.lock())
                    {
                        if (auto current_owner = peer->owner.lock(); current_owner && !current_owner->closing)
                        {
                            callback(*peer);
                        }
                    }
                });
        }

        void init()
        {
            log_ice_configuration();
            auto const weak = weak_from_this();
            pc->onGatheringStateChange(
                [weak](rtc::PeerConnection::GatheringState state)
                {
                    if (auto peer = weak.lock())
                    {
                        peer->dispatch(
                            [state](Peer& p)
                            {
                                tr_logAddDebug(
                                    fmt::format(
                                        "WebRTC peer={} gathering-state={}",
                                        p.diagnostic_id,
                                        state == rtc::PeerConnection::GatheringState::Complete       ? "complete" :
                                            state == rtc::PeerConnection::GatheringState::InProgress ? "in-progress" :
                                                                                                       "new"),
                                    "WebRTC");
                                if (state == rtc::PeerConnection::GatheringState::Complete)
                                {
                                    p.gathering_complete();
                                }
                            });
                    }
                });
            pc->onIceStateChange(
                [weak](rtc::PeerConnection::IceState state)
                {
                    if (auto peer = weak.lock())
                    {
                        peer->dispatch(
                            [state](Peer& p)
                            {
                                p.last_ice_state = ice_state_name(state);
                                tr_logAddDebug(
                                    fmt::format("WebRTC peer={} ice-state={}", p.diagnostic_id, p.last_ice_state),
                                    "WebRTC");
                                if (state == rtc::PeerConnection::IceState::Connected ||
                                    state == rtc::PeerConnection::IceState::Completed)
                                {
                                    if (!p.reject_blocklisted_endpoint())
                                    {
                                        p.log_selected_pair();
                                    }
                                }
                            });
                    }
                });
            pc->onStateChange(
                [weak](rtc::PeerConnection::State state)
                {
                    if (auto peer = weak.lock())
                    {
                        peer->dispatch(
                            [state](Peer& p)
                            {
                                p.last_connection_state = rtc_state_name(state);
                                tr_logAddDebug(
                                    fmt::format(
                                        "WebRTC peer={} direction={} connection-state={}",
                                        p.diagnostic_id,
                                        p.incoming ? "incoming" : "outgoing",
                                        p.last_connection_state),
                                    "WebRTC");
                                if (state == rtc::PeerConnection::State::Connected)
                                {
                                    if (p.reject_blocklisted_endpoint())
                                    {
                                        return;
                                    }
                                    p.connected_once = true;
                                    p.log_selected_pair();
                                }
                                if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed)
                                {
                                    p.remove(
                                        state == rtc::PeerConnection::State::Failed ? "peer-connection-failed" :
                                                                                      "peer-connection-closed");
                                }
                            });
                    }
                });
            if (incoming)
            {
                pc->onDataChannel(
                    [weak](std::shared_ptr<rtc::DataChannel> channel)
                    {
                        if (auto peer = weak.lock())
                        {
                            peer->dispatch([channel = std::move(channel)](Peer& p) { p.attach_channel(channel); });
                        }
                    });
            }
        }

        bool reject_blocklisted_endpoint()
        {
            auto const parent = owner.lock();
            if (!parent)
            {
                return false;
            }
            auto const remote = pc->remoteAddress();
            auto const endpoint = remote ? tr_rtc_endpoint::parse(*remote) : std::nullopt;
            if (!endpoint || !parent->session->blocklist().contains(endpoint->address()))
            {
                return false;
            }
            tr_logAddDebug(fmt::format("WebRTC peer={} selected ICE endpoint is blocklisted", diagnostic_id), "WebRTC");
            remove("selected-endpoint-blocklisted");
            return true;
        }

        [[nodiscard]] tr_error filter_remote_candidates(rtc::Description& description)
        {
            auto const parent = owner.lock();
            if (!parent)
            {
                return { ECANCELED, "WebRTC session closed" };
            }
            auto const candidates = description.candidates().size();
            auto const removed = tr_rtc_network::filter_blocklisted_candidates(
                description,
                [&](tr_address const& address) { return parent->session->blocklist().contains(address); });
            if (removed != 0)
            {
                tr_logAddDebug(
                    fmt::format("WebRTC peer={} removed {} blocklisted ICE candidates", diagnostic_id, removed),
                    "WebRTC");
                if (removed == candidates)
                {
                    return { EACCES, "All remote ICE candidates are blocklisted" };
                }
            }
            return {};
        }

        void log_selected_pair()
        {
            if (selected_pair_reported)
            {
                return;
            }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
            try
#endif
            {
                auto local = rtc::Candidate{};
                auto remote = rtc::Candidate{};
                if (!pc->getSelectedCandidatePair(&local, &remote))
                {
                    return;
                }
                selected_pair_reported = true;
                selected_local_type = candidate_type_name(local.type());
                selected_remote_type = candidate_type_name(remote.type());
                tr_logAddDebug(
                    fmt::format(
                        "WebRTC peer={} selected-pair local={} remote={}",
                        diagnostic_id,
                        candidate_type_name(local.type()),
                        candidate_type_name(remote.type())),
                    "WebRTC");
            }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
            catch (std::exception const&)
            {
                tr_logAddDebug(fmt::format("WebRTC peer={} selected-pair unavailable", diagnostic_id), "WebRTC");
            }
#endif
        }

        void start_outgoing(std::shared_ptr<Batch> const& batch_in)
        {
            tr_logAddDebug("WebRTC outgoing offer started", "WebRTC");
            batch = batch_in;
            dc = pc->createDataChannel("webtorrent");
            attach_channel(dc);
        }

        [[nodiscard]] tr_error start_incoming(
            tr_ws_tracker::Signal const& signal,
            std::string our_peer,
            tr_rtc_signaling::SendAnswer send)
        {
            tr_logAddDebug(fmt::format("WebRTC peer={} incoming offer processing", diagnostic_id), "WebRTC");
            remote_offer_id = signal.offer_id;
            remote_peer_id = signal.peer_id;
            our_peer_id = std::move(our_peer);
            send_answer = std::move(send);
            // libdatachannel creates the local answer automatically when the remote offer is applied.
            failure_stage = "offer-parse";
            auto remote_offer = rtc::Description(signal.sdp, rtc::Description::Type::Offer);
            log_candidate_summary(diagnostic_id, "remote-offer", remote_offer);
            if (auto error = filter_remote_candidates(remote_offer); error)
            {
                failure_stage = "offer-blocklisted";
                return error;
            }
            failure_stage = "offer-apply-and-auto-answer";
            pc->setRemoteDescription(std::move(remote_offer));
            tr_logAddDebug(
                fmt::format("WebRTC peer={} incoming remote offer accepted; automatic answer started", diagnostic_id),
                "WebRTC");
            return {};
        }

        void gathering_complete()
        {
            tr_logAddDebug("WebRTC ICE gathering complete", "WebRTC");
            if (description_sent)
            {
                return;
            }
            auto const desc = pc->localDescription();
            if (!desc)
            {
                tr_logAddWarn("WebRTC ICE gathering completed without local SDP", "WebRTC");
                return;
            }
            description_sent = true;
            log_candidate_summary(diagnostic_id, incoming ? "local-answer" : "local-offer", *desc);
            auto const sdp = desc->generateSdp();
            if (incoming)
            {
                tr_logAddDebug("WebRTC incoming answer ready", "WebRTC");
                if (send_answer)
                {
                    send_answer(tr_ws_tracker::encode_answer(info_hash, our_peer_id, remote_peer_id, remote_offer_id, sdp));
                }
            }
            else if (auto pending = batch.lock())
            {
                tr_logAddTrace("WebRTC outgoing offer ready", "WebRTC");
                pending->offers.emplace_back(tr_ws_tracker::Offer{ .offer_id = id, .sdp = sdp });
                if (pending->remaining > 0)
                {
                    --pending->remaining;
                }
                if (pending->remaining == 0)
                {
                    pending->complete();
                }
            }
        }

        void attach_channel(std::shared_ptr<rtc::DataChannel> channel)
        {
            if (attached || !channel)
            {
                return;
            }
            dc = std::move(channel);
            auto const weak = weak_from_this();
            dc->onOpen(
                [weak]()
                {
                    if (auto peer = weak.lock())
                    {
                        peer->dispatch([](Peer& p) { p.on_open(); });
                    }
                });
            if (dc->isOpen())
            {
                on_open();
            }
        }

        void on_open()
        {
            if (attached || !dc || !dc->isOpen())
            {
                return;
            }
            auto const parent = owner.lock();
            if (!parent || parent->closing)
            {
                return;
            }
            // Use the selected ICE remote endpoint for BitTorrent peer identity.
            auto const remote_address = pc->remoteAddress();
            auto const socket_address = remote_address ? tr_rtc_endpoint::parse(*remote_address) : std::nullopt;
            if (!socket_address)
            {
                tr_logAddWarn(
                    fmt::format("WebRTC peer={} selected ICE remote endpoint unavailable or invalid", diagnostic_id),
                    "WebRTC");
                remove("invalid-selected-ice-endpoint");
                return;
            }
            if (parent->session->blocklist().contains(socket_address->address()))
            {
                remove("selected-endpoint-blocklisted");
                return;
            }
            attached = true;
            channel_opened = std::chrono::steady_clock::now();
            tr_logAddDebug(
                fmt::format(
                    "WebRTC peer={} data channel open; registering BitTorrent peer (remote={})",
                    diagnostic_id,
                    socket_address->address().is_ipv6() ? "ipv6" : "ipv4"),
                "WebRTC");
            auto const weak_state = std::weak_ptr<State>{ parent };
            auto socket = tr_peer_socket_rtc::create(
                *socket_address,
                dc,
                [weak_state](std::function<void()> callback)
                {
                    if (auto state = weak_state.lock(); state && !state->closing)
                    {
                        state->session->queue_session_thread(std::move(callback));
                    }
                },
                [weak = weak_from_this()](std::string_view reason)
                {
                    if (auto peer = weak.lock())
                    {
                        peer->dispatch([reason](Peer& p) { p.remove(reason); });
                    }
                });
            parent->session->addIncoming(std::move(socket));
            tr_logAddDebug(fmt::format("WebRTC peer={} incoming peer socket submitted to session", diagnostic_id), "WebRTC");
        }

        [[nodiscard]] tr_error on_answer(std::string const& sdp)
        {
            if (incoming || answered || sdp.empty())
            {
                return {};
            }
            failure_stage = "answer-parse";
            auto remote_answer = rtc::Description(sdp, rtc::Description::Type::Answer);
            log_candidate_summary(diagnostic_id, "remote-answer", remote_answer);
            if (auto error = filter_remote_candidates(remote_answer); error)
            {
                failure_stage = "answer-blocklisted";
                return error;
            }
            failure_stage = "answer-apply";
            pc->setRemoteDescription(std::move(remote_answer));
            answered = true;
            tr_logAddDebug(fmt::format("WebRTC peer={} remote answer accepted", diagnostic_id), "WebRTC");
            return {};
        }

        void remove(std::string_view reason = "removed")
        {
            if (auto parent = owner.lock())
            {
                auto it = parent->peers.find(id);
                if (it != parent->peers.end() && it->second.get() == this)
                {
                    auto const now = std::chrono::steady_clock::now();
                    auto const age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - created).count();
                    auto const open_ms = channel_opened ?
                        std::chrono::duration_cast<std::chrono::milliseconds>(now - *channel_opened).count() :
                        0;
                    tr_logAddDebug(
                        fmt::format(
                            "WebRTC lifecycle peer={} direction={} reason={} age-ms={} open-ms={} "
                            "channel-open={} connected={} ice={} connection={} selected={}/{}",
                            diagnostic_id,
                            incoming ? "incoming" : "outgoing",
                            reason,
                            age_ms,
                            open_ms,
                            channel_opened.has_value(),
                            connected_once,
                            last_ice_state,
                            last_connection_state,
                            selected_local_type,
                            selected_remote_type),
                        "WebRTC");
                    parent->peers.erase(it);
                }
            }
        }

        std::weak_ptr<State> owner;
        uint64_t const diagnostic_id;
        std::string const id;
        std::string const tracker_url;
        std::string const info_hash;
        bool const incoming;
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> dc;
        std::weak_ptr<Batch> batch;
        std::chrono::steady_clock::time_point const created;
        std::optional<std::chrono::steady_clock::time_point> channel_opened;
        std::string_view last_ice_state = "new";
        std::string_view last_connection_state = "new";
        std::string_view selected_local_type = "unselected";
        std::string_view selected_remote_type = "unselected";
        bool connected_once = false;
        tr_rtc_signaling::SendAnswer send_answer;
        std::string remote_peer_id;
        std::string remote_offer_id;
        std::string our_peer_id;
        bool answered = false;
        bool description_sent = false;
        bool attached = false;
        bool selected_pair_reported = false;
        std::string_view failure_stage = "create-peer";
    };
};

tr_rtc_signaling::tr_rtc_signaling(tr_session* session)
    : state_{ std::make_shared<State>(session) }
{
}

tr_rtc_signaling::~tr_rtc_signaling()
{
    state_->closing = true;
    state_->peers.clear();
    state_->batches.clear();
}

void tr_rtc_signaling::make_offers(
    tr_sha1_digest_t const& hash,
    std::string const& tracker_url,
    size_t count,
    OffersReady on_ready)
{
    count = std::min(
        { count, MaxOffersPerAnnounce, MaxOutstandingPeers - std::min(MaxOutstandingPeers, state_->peers.size()) });
    tr_logAddDebug(fmt::format("WebRTC offer batch requested: {}", count), "WebRTC");
    if (count == 0)
    {
        on_ready({});
        return;
    }
    auto batch = std::make_shared<State::Batch>();
    batch->remaining = count;
    batch->deadline = std::chrono::steady_clock::now() + GatheringLifetime;
    batch->callback = std::move(on_ready);
    state_->batches.emplace_back(batch);
    auto const hash_str = hash_key(hash);

    for (size_t i = 0; i < count; ++i)
    {
        auto bind_address = std::optional<std::string>{};
        if (auto error = tr_rtc_network::bind_address(
                state_->session->settings().bind_address_ipv4,
                state_->session->settings().bind_address_ipv6,
                state_->next_diagnostic_peer + 1U,
                bind_address);
            error)
        {
            tr_logAddWarn(fmt::format("WebRTC offer rejected: {}", error.message()), "WebRTC");
            --batch->remaining;
            continue;
        }

        auto peer = std::shared_ptr<State::Peer>{};
#ifndef WORKAROUND_CLANG_TIDY_GH44701
        try
#endif
        {
            auto id = new_offer_id();
            while (state_->peers.contains(id))
            {
                id = new_offer_id();
            }
            peer = std::make_shared<State::Peer>(state_, id, tracker_url, hash_str, false, std::move(bind_address));
            state_->peers.emplace(peer->id, peer);
            peer->init();
            peer->start_outgoing(batch);
        }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
        catch (std::exception const&)
        {
            tr_logAddWarn("WebRTC outgoing offer creation failed", "WebRTC");
            if (peer)
            {
                peer->remove("outgoing-offer-create-failed");
            }
            if (batch->remaining != 0)
            {
                --batch->remaining;
            }
        }
#endif
    }
    if (batch->remaining == 0)
    {
        batch->complete();
    }
}

void tr_rtc_signaling::on_signal(
    tr_ws_tracker::Signal const& signal,
    std::string const& tracker_url,
    std::string const& our_peer_id,
    SendAnswer send_answer)
{
    if (signal.kind == tr_ws_tracker::Signal::Kind::Answer)
    {
        auto it = state_->peers.find(signal.offer_id);
        if (it == state_->peers.end() || it->second->incoming || it->second->tracker_url != tracker_url ||
            it->second->info_hash != signal.info_hash)
        {
            return;
        }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
        try
#endif
        {
            if (auto error = it->second->on_answer(signal.sdp); error)
            {
                tr_logAddWarn(
                    fmt::format("WebRTC peer={} remote answer rejected: {}", it->second->diagnostic_id, error.message()),
                    "WebRTC");
                it->second->remove("remote-answer-rejected");
            }
        }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
        catch (std::exception const&)
        {
            tr_logAddWarn(
                fmt::format(
                    "WebRTC peer={} remote answer rejected stage={}",
                    it->second->diagnostic_id,
                    it->second->failure_stage),
                "WebRTC");
            it->second->remove("remote-answer-rejected");
        }
#endif
        return;
    }
    if (signal.kind != tr_ws_tracker::Signal::Kind::Offer || signal.peer_id == our_peer_id || our_peer_id.size() != 20U ||
        state_->peers.size() >= MaxOutstandingPeers)
    {
        return;
    }

    auto bind_address = std::optional<std::string>{};
    if (auto error = tr_rtc_network::bind_address(
            state_->session->settings().bind_address_ipv4,
            state_->session->settings().bind_address_ipv6,
            state_->next_diagnostic_peer + 1U,
            bind_address);
        error)
    {
        tr_logAddWarn(fmt::format("WebRTC incoming offer rejected: {}", error.message()), "WebRTC");
        return;
    }

    auto peer = std::shared_ptr<State::Peer>{};
#ifndef WORKAROUND_CLANG_TIDY_GH44701
    try
#endif
    {
        auto id = new_offer_id();
        while (state_->peers.contains(id))
        {
            id = new_offer_id();
        }
        peer = std::make_shared<State::Peer>(state_, id, tracker_url, signal.info_hash, true, std::move(bind_address));
        state_->peers.emplace(peer->id, peer);
        peer->init();
        if (auto error = peer->start_incoming(signal, our_peer_id, std::move(send_answer)); error)
        {
            tr_logAddWarn(
                fmt::format("WebRTC peer={} incoming offer rejected: {}", peer->diagnostic_id, error.message()),
                "WebRTC");
            peer->remove("incoming-offer-rejected");
        }
    }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
    catch (std::exception const&)
    {
        tr_logAddWarn(
            fmt::format(
                "WebRTC incoming offer rejected peer={} stage={}",
                peer ? peer->diagnostic_id : 0,
                peer ? peer->failure_stage : "create-peer"),
            "WebRTC");
        // A rejected SDP must not occupy a pending peer slot.
        if (peer)
        {
            peer->remove("incoming-offer-rejected");
        }
    }
#endif
}

void tr_rtc_signaling::upkeep()
{
    auto const now = std::chrono::steady_clock::now();
    for (auto it = state_->batches.begin(); it != state_->batches.end();)
    {
        auto& batch = *it;
        if (batch->remaining == 0 || batch->deadline <= now)
        {
            if (batch->remaining != 0)
            {
                tr_logAddWarn(fmt::format("WebRTC ICE gathering timed out for {} offers", batch->remaining), "WebRTC");
            }
            batch->complete();
            it = state_->batches.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = state_->peers.begin(); it != state_->peers.end();)
    {
        auto peer = it->second;
        if (!peer->attached && now - peer->created >= OfferLifetime)
        {
            ++it;
            peer->remove("offer-expired");
        }
        else
        {
            ++it;
        }
    }
}
