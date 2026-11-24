// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Fully local WebSocket tracker -> ICE -> DataChannel -> BitTorrent transfer.
// No Internet STUN servers, public trackers, or third-party WebTorrent clients.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <limits>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
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

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "libtransmission/announce-list.h"
#include "libtransmission/quark.h"
#include "libtransmission/session.h"
#include "libtransmission/websocket-client.h"
#include "libtransmission/torrent.h"
#include "libtransmission/types.h"
#include "libtransmission/variant.h"
#include "libtransmission/announcer-ws-protocol.h"
#include "libtransmission/rtc-endpoint.h"
#include "libtransmission/transmission.h"

#include "test-fixtures.h"
#include "test-websocket-server.h"

using namespace std::chrono_literals;

class WebTorrentEndToEndTest : public tr::test::SessionTest
{
protected:
    void SetUp() override
    {
        auto* const settings_map = settings()->get_if<tr_variant::Map>();
        settings_map->insert_or_assign(TR_KEY_webtorrent_enabled, true);
        // Loopback tests should never contact a public STUN/TURN service.
        settings_map->insert_or_assign(TR_KEY_webtorrent_ice_servers, tr_variant::Vector{});
        tr::test::SessionTest::SetUp();
        if (!tr_websocket_client::supports_websockets())
        {
            GTEST_SKIP() << "libcurl was built without WS/WSS protocol support";
        }
    }

    void exercise();
};

struct WebTorrentTestTracker
{
    std::mutex mutex;
    std::vector<std::shared_ptr<test_ws::WebSocket>> sockets;
    std::vector<std::shared_ptr<rtc::PeerConnection>> peers;
    std::vector<std::shared_ptr<rtc::DataChannel>> channels;
    std::atomic<bool> saw_announce = false;
    std::atomic<bool> got_handshake = false;
    std::atomic<bool> got_extended_handshake = false;
    bool no_classic_address_hints = false;
    std::string observed_yourip;
    std::atomic<uint64_t> served_bytes = 0;
    std::atomic<uint32_t> piece_count = 0;
    std::atomic<uint64_t> total_size = 0;
    bool handshake_read = false;
    std::vector<std::byte> received;
    std::string const remote_peer_id = "-TEST00-123456789012";
};

namespace
{
uint32_t readU32(std::vector<std::byte> const& bytes, size_t offset)
{
    auto const b0 = static_cast<uint32_t>(std::to_integer<unsigned char>(bytes[offset]));
    auto const b1 = static_cast<uint32_t>(std::to_integer<unsigned char>(bytes[offset + 1U]));
    auto const b2 = static_cast<uint32_t>(std::to_integer<unsigned char>(bytes[offset + 2U]));
    auto const b3 = static_cast<uint32_t>(std::to_integer<unsigned char>(bytes[offset + 3U]));
    return (b0 << 24U) | (b1 << 16U) | (b2 << 8U) | b3;
}

void appendU32(rtc::binary& buffer, uint32_t value)
{
    buffer.push_back(std::byte{ static_cast<unsigned char>((value >> 24U) & 0xFFU) });
    buffer.push_back(std::byte{ static_cast<unsigned char>((value >> 16U) & 0xFFU) });
    buffer.push_back(std::byte{ static_cast<unsigned char>((value >> 8U) & 0xFFU) });
    buffer.push_back(std::byte{ static_cast<unsigned char>(value & 0xFFU) });
}
} // namespace

void WebTorrentEndToEndTest::exercise()
{
    auto cfg = test_ws::WebSocketServer::Configuration{};
    cfg.port = 0;
    cfg.bindAddress = "127.0.0.1";
    auto server = std::make_shared<test_ws::WebSocketServer>(cfg);
    ASSERT_TRUE(server->ready()) << server->error();
    auto state = std::make_shared<WebTorrentTestTracker>();

    server->onClient(
        [state](std::shared_ptr<test_ws::WebSocket> const& ws)
        {
            {
                auto guard = std::lock_guard{ state->mutex };
                state->sockets.emplace_back(ws);
            }
            auto weak_ws = std::weak_ptr<test_ws::WebSocket>{ ws };
            ws->onMessage(
                [](test_ws::binary const&) {},
                [state, weak_ws](std::string const& json)
                {
                    auto document = rapidjson::Document{};
                    document.Parse(json.c_str());
                    if (document.HasParseError() || !document.IsObject() || !document.HasMember("action") ||
                        !document["action"].IsString() || std::string_view{ document["action"].GetString() } != "announce" ||
                        !document.HasMember("info_hash") || !document["info_hash"].IsString())
                    {
                        return;
                    }
                    state->saw_announce = true;

                    // A tracker counts reply must use the same binary info_hash.
                    auto response_buffer = rapidjson::StringBuffer{};
                    auto writer = rapidjson::Writer<rapidjson::StringBuffer>{ response_buffer };
                    writer.StartObject();
                    writer.Key("action");
                    writer.String("announce");
                    writer.Key("info_hash");
                    writer.String(document["info_hash"].GetString(), document["info_hash"].GetStringLength());
                    writer.Key("interval");
                    writer.Int(125);
                    writer.Key("complete");
                    writer.Int(1);
                    writer.Key("incomplete");
                    writer.Int(1);
                    writer.EndObject();
                    if (auto socket = weak_ws.lock())
                    {
                        socket->send(std::string{ response_buffer.GetString(), response_buffer.GetSize() });
                    }

                    if (!document.HasMember("offers") || !document["offers"].IsArray() || document["offers"].Empty())
                    {
                        return;
                    }
                    auto const& first = document["offers"][0];
                    if (!first.IsObject() || !first.HasMember("offer") || !first["offer"].IsObject() ||
                        !first["offer"].HasMember("sdp") || !first["offer"]["sdp"].IsString() || !first.HasMember("offer_id") ||
                        !first["offer_id"].IsString() || !document.HasMember("peer_id") || !document["peer_id"].IsString())
                    {
                        return;
                    }

                    auto const hash = tr_ws_tracker::decode_binary(
                        { document["info_hash"].GetString(), document["info_hash"].GetStringLength() });
                    auto const client_id = tr_ws_tracker::decode_binary(
                        { document["peer_id"].GetString(), document["peer_id"].GetStringLength() });
                    auto const offer_id = tr_ws_tracker::decode_binary(
                        { first["offer_id"].GetString(), first["offer_id"].GetStringLength() });
                    if (!hash || !client_id || !offer_id || hash->size() != 20U || client_id->size() != 20U)
                    {
                        return;
                    }
                    auto offer = rtc::Description(first["offer"]["sdp"].GetString(), rtc::Description::Type::Offer);
                    auto config = rtc::Configuration{};
                    // A single bound interface makes the local ICE endpoint unambiguous.
                    for (auto candidate : offer.candidates())
                    {
                        if (candidate.type() == rtc::Candidate::Type::Host && candidate.resolve())
                        {
                            if (auto const address = candidate.address())
                            {
                                config.bindAddress = address;
                                if (auto const ip = tr_address::from_string(*address); ip && ip->is_ipv4())
                                {
                                    break;
                                }
                            }
                        }
                    }
                    if (!config.bindAddress)
                    {
                        return;
                    }

                    auto remote = std::shared_ptr<rtc::PeerConnection>{};
                    {
                        auto guard = std::lock_guard{ state->mutex };
                        if (!state->peers.empty())
                        {
                            return;
                        }
                        remote = std::make_shared<rtc::PeerConnection>(config);
                        state->peers.emplace_back(remote);
                    }
                    remote->onDataChannel(
                        [state, hash = *hash](std::shared_ptr<rtc::DataChannel> const& channel)
                        {
                            {
                                auto guard = std::lock_guard{ state->mutex };
                                state->channels.emplace_back(channel);
                            }
                            auto weak = std::weak_ptr<rtc::DataChannel>{ channel };
                            channel->onOpen(
                                [weak, hash, remote_id = state->remote_peer_id]()
                                {
                                    if (auto dc = weak.lock())
                                    {
                                        auto hs = std::string{};
                                        hs.reserve(68);
                                        hs.push_back(19);
                                        hs += "BitTorrent protocol";
                                        hs.append(8, '\0');
                                        hs[25] = static_cast<char>(0x10); // BEP 10 support
                                        hs += hash;
                                        hs += remote_id;
                                        auto wire = rtc::binary{};
                                        wire.reserve(hs.size());
                                        for (unsigned char const c : hs)
                                        {
                                            wire.emplace_back(static_cast<std::byte>(c));
                                        }
                                        dc->send(std::move(wire));
                                    }
                                });
                            channel->onMessage(
                                [state, weak](rtc::binary message)
                                {
                                    auto replies = std::vector<rtc::binary>{};
                                    {
                                        auto guard = std::lock_guard{ state->mutex };
                                        state->received.insert(state->received.end(), message.begin(), message.end());

                                        if (!state->handshake_read && state->received.size() >= 68U)
                                        {
                                            if (state->received.front() != std::byte{ 19 })
                                            {
                                                return;
                                            }
                                            state->handshake_read = true;
                                            state->got_handshake = true;
                                            state->received.erase(state->received.begin(), state->received.begin() + 68);

                                            auto const count = state->piece_count.load();
                                            auto const n = (count + 7U) / 8U;
                                            auto bits = rtc::binary{};
                                            appendU32(bits, 1U + n);
                                            bits.push_back(std::byte{ 5 });
                                            bits.resize(bits.size() + n, std::byte{ 0 });
                                            for (uint32_t i = 0; i < count; ++i)
                                            {
                                                bits[5U + (i / 8U)] |= std::byte{ static_cast<unsigned char>(
                                                    0x80U >> (i % 8U)) };
                                            }
                                            replies.emplace_back(std::move(bits));
                                        }

                                        // A DataChannel message is not a BitTorrent frame boundary:
                                        // reassemble the handshake and subsequent length-prefixed messages.
                                        while (state->handshake_read && state->received.size() >= 4U)
                                        {
                                            auto const frame_size = readU32(state->received, 0U);
                                            if (frame_size > 1024U * 1024U || state->received.size() < 4U + frame_size)
                                            {
                                                break;
                                            }

                                            // Inspect the actual BEP 10 extension handshake on this ICE connection.
                                            if (frame_size >= 2U && state->received[4U] == std::byte{ 20 } &&
                                                state->received[5U] == std::byte{ 0 })
                                            {
                                                auto const body = std::string_view{ reinterpret_cast<char const*>(
                                                                                        state->received.data() + 6U),
                                                                                    frame_size - 2U };
                                                auto const decoded = tr_variant_serde::benc().inplace().parse(body);
                                                if (decoded)
                                                {
                                                    if (auto const* fields = decoded->get_if<tr_variant::Map>())
                                                    {
                                                        auto const no_hints = !fields->value_if<int64_t>(TR_KEY_p) &&
                                                            !fields->value_if<std::string_view>(TR_KEY_ipv4) &&
                                                            !fields->value_if<std::string_view>(TR_KEY_ipv6);
                                                        auto no_holepunch = true;
                                                        if (auto const* extensions = fields->find_if<tr_variant::Map>(TR_KEY_m))
                                                        {
                                                            no_holepunch = !extensions->value_if<int64_t>(TR_KEY_ut_holepunch);
                                                        }
                                                        state->no_classic_address_hints = no_hints && no_holepunch;
                                                        if (auto const yourip = fields->value_if<std::string_view>(
                                                                TR_KEY_yourip))
                                                        {
                                                            state->observed_yourip = std::string{ *yourip };
                                                        }
                                                        state->got_extended_handshake = true;
                                                    }
                                                }
                                            }

                                            if (frame_size > 0U)
                                            {
                                                auto const message_id = std::to_integer<unsigned char>(state->received[4U]);
                                                if (message_id == 2U)
                                                {
                                                    // Remote client is interested: allow piece requests.
                                                    auto unchoke = rtc::binary{};
                                                    appendU32(unchoke, 1U);
                                                    unchoke.push_back(std::byte{ 1 });
                                                    replies.emplace_back(std::move(unchoke));
                                                }
                                                else if (message_id == 6U && frame_size == 13U)
                                                {
                                                    auto const index = readU32(state->received, 5U);
                                                    auto const begin = readU32(state->received, 9U);
                                                    auto const length = readU32(state->received, 13U);
                                                    auto const absolute = (uint64_t{ index } * 32768U) + begin;
                                                    if (index < state->piece_count.load() && length > 0U && length <= 16384U &&
                                                        uint64_t{ absolute } + length <= state->total_size.load())
                                                    {
                                                        auto block = rtc::binary{};
                                                        appendU32(block, 9U + length);
                                                        block.push_back(std::byte{ 7 });
                                                        appendU32(block, index);
                                                        appendU32(block, begin);
                                                        block.resize(block.size() + length, std::byte{ 0 });
                                                        replies.emplace_back(std::move(block));
                                                        state->served_bytes += length;
                                                    }
                                                }
                                            }
                                            state->received.erase(
                                                state->received.begin(),
                                                state->received.begin() + 4U + frame_size);
                                        }
                                    }

                                    if (auto dc = weak.lock())
                                    {
                                        for (auto& response : replies)
                                        {
                                            dc->send(std::move(response));
                                        }
                                    }
                                },
                                [](std::string const&) {});
                        });

                    auto const weak_remote = std::weak_ptr<rtc::PeerConnection>{ remote };
                    remote->onGatheringStateChange(
                        [weak_remote,
                         weak_ws,
                         hash = *hash,
                         peer_id = *client_id,
                         offer_id = *offer_id,
                         remote_id = state->remote_peer_id](rtc::PeerConnection::GatheringState gathering)
                        {
                            if (gathering != rtc::PeerConnection::GatheringState::Complete)
                            {
                                return;
                            }
                            auto pc = weak_remote.lock();
                            auto ws = weak_ws.lock();
                            if (!pc || !ws)
                            {
                                return;
                            }
                            if (auto desc = pc->localDescription())
                            {
                                ws->send(tr_ws_tracker::encode_answer(hash, remote_id, peer_id, offer_id, desc->generateSdp()));
                            }
                        });

                    // libdatachannel's default automatic negotiation creates the answer here.
                    remote->setRemoteDescription(std::move(offer));
                });
        });

    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(tor, nullptr);
    state->piece_count = tor->piece_count();
    state->total_size = tor->total_size();
    auto local_tracker = tr_announce_list{};
    ASSERT_TRUE(local_tracker.add("ws://127.0.0.1:" + std::to_string(server->port()) + "/"));
    ASSERT_TRUE(tor->set_announce_list(std::move(local_tracker)));
    tr_torrentStartNow(tor);

    EXPECT_TRUE(tr::test::waitFor([state] { return state->saw_announce.load(); }, 15000));
    EXPECT_TRUE(tr::test::waitFor([state] { return state->got_handshake.load(); }, 25000))
        << "A WebRTC answer was accepted, but the BitTorrent handshake did not complete";

    // Verify the peer manager exposes the selected ICE endpoint.
    EXPECT_TRUE(
        tr::test::waitFor(
            [tor]
            {
                auto const peers = tr_torrentPeers(tor);
                return std::ranges::any_of(peers, [](auto const& peer) { return peer.is_webrtc; });
            },
            10000));

    auto local_ice_address = std::optional<std::string>{};
    {
        auto guard = std::lock_guard{ state->mutex };
        if (!state->peers.empty())
        {
            local_ice_address = state->peers.front()->localAddress();
        }
    }
    ASSERT_TRUE(local_ice_address);
    auto const selected = tr_rtc_endpoint::parse(*local_ice_address);
    ASSERT_TRUE(selected);

    EXPECT_TRUE(tr::test::waitFor([state] { return state->got_extended_handshake.load(); }, 10000))
        << "The real WebRTC connection must negotiate the BEP 10 extension handshake";
    {
        auto guard = std::lock_guard{ state->mutex };
        EXPECT_TRUE(state->no_classic_address_hints)
            << "WebRTC must not advertise classic TCP ports, address hints or BEP 55 hole-punch support";

        auto compact = std::vector<std::byte>{};
        selected->address().to_compact(std::back_inserter(compact));
        auto const expected_yourip = std::string{ reinterpret_cast<char const*>(compact.data()), compact.size() };
        EXPECT_EQ(state->observed_yourip, expected_yourip) << "BEP 10 yourip must contain the real selected remote ICE address";
    }
    auto const peers = tr_torrentPeers(tor);
    auto found_rtc = false;
    for (auto const& peer : peers)
    {
        if (!peer.is_webrtc)
        {
            continue;
        }
        found_rtc = true;
        EXPECT_EQ(peer.addr, selected->address().display_name());
        EXPECT_EQ(peer.port, selected->port().host());
        EXPECT_FALSE(peer.supports_holepunch);
        EXPECT_TRUE(peer.is_encrypted); // DTLS, independently of BitTorrent MSE.
    }
    EXPECT_TRUE(found_rtc);

    EXPECT_TRUE(tr::test::waitFor([tor] { return tr_torrentStat(tor).percent_done >= 1.0; }, 60000))
        << "WebRTC peer exchanged the handshake but failed to deliver a verified torrent";
    EXPECT_GE(state->served_bytes.load(), state->total_size.load()) << "The local WebRTC peer did not serve the full payload";

    tr_torrentStop(tor);
    server->onClient(nullptr);
    {
        auto guard = std::lock_guard{ state->mutex };
        for (auto& channel : state->channels)
        {
            channel->onOpen(nullptr);
            channel->onMessage(nullptr);
        }
        for (auto& pc : state->peers)
        {
            pc->resetCallbacks();
            pc->close();
        }
        for (auto& ws : state->sockets)
        {
            ws->onMessage(nullptr);
            ws->close();
        }
        state->channels.clear();
        state->peers.clear();
        state->sockets.clear();
    }
    server->stop();
}

TEST_F(WebTorrentEndToEndTest, WebRtcPeerDownloadsAndVerifiesTorrent)
{
    exercise();
}
