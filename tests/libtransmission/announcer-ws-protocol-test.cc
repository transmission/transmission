// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string>
#include <string_view>

#include <rapidjson/document.h>

#include "libtransmission/web-utils.h"
#include "libtransmission/announcer-ws-protocol.h"
#include "test-fixtures.h"

using WsTrackerProtocolTest = ::tr::test::TransmissionTest;
using namespace std::literals;

TEST_F(WsTrackerProtocolTest, BinaryIdentifiersRoundTrip)
{
    auto id = std::string(20, '\0');
    for (int i = 0; i < 20; ++i)
    {
        id[i] = static_cast<char>(i * 13);
    }
    auto const encoded = tr_ws_tracker::encode_binary(id);
    auto const decoded = tr_ws_tracker::decode_binary(encoded);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(id, *decoded);
    EXPECT_FALSE(tr_ws_tracker::decode_binary("\xC4\x80")); // U+0100 cannot be a binary ID.
}

TEST_F(WsTrackerProtocolTest, AnnouncesUseBinaryIdsAndOfferArray)
{
    auto a = tr_ws_tracker::Announce{};
    a.info_hash = std::string(20, '\0');
    a.info_hash[0] = static_cast<char>(255);
    a.peer_id = "-TR4000-123456789012";
    a.uploaded = 10;
    a.downloaded = 20;
    a.left = 30;
    a.numwant = 1;
    a.event = "started";
    a.offers = { { .offer_id = "01234567890123456789", .sdp = "v=0\\r\\n" } };

    auto d = rapidjson::Document{};
    auto const text = tr_ws_tracker::encode_announce(a);
    d.Parse(text.c_str());
    ASSERT_FALSE(d.HasParseError());
    ASSERT_TRUE(d.IsObject());
    EXPECT_STREQ(d["action"].GetString(), "announce");
    EXPECT_EQ(d["info_hash"].GetStringLength(), 21U); // Latin-1 U+00FF is 2 UTF-8 bytes.
    EXPECT_EQ(d["offers"].Size(), 1U);
    EXPECT_TRUE(d["offers"][0]["offer"].IsObject());
    EXPECT_EQ(d["numwant"].GetInt(), 1);
}

TEST_F(WsTrackerProtocolTest, ParsesInboundOffersAndAnswers)
{
    auto a = tr_ws_tracker::Announce{};
    a.info_hash = std::string(20, 'A');
    a.peer_id = std::string(20, 'B');
    auto const encoded = tr_ws_tracker::encode_answer(
        a.info_hash,
        a.peer_id,
        std::string(20, 'C'),
        "abcdefghijklmnopqrst",
        "v=0");
    auto s = tr_ws_tracker::parse_signal(encoded);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->kind, tr_ws_tracker::Signal::Kind::Answer);
    EXPECT_EQ(s->peer_id, a.peer_id);
    EXPECT_EQ(s->offer_id, "abcdefghijklmnopqrst");
    EXPECT_EQ(s->sdp, "v=0");

    auto const offered =
        R"({"action":"announce","info_hash":"AAAAAAAAAAAAAAAAAAAA","peer_id":"BBBBBBBBBBBBBBBBBBBB","offer_id":"abcdefghijklmnopqrst","offer":{"type":"offer","sdp":"v=0"}})"sv;
    s = tr_ws_tracker::parse_signal(offered);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->kind, tr_ws_tracker::Signal::Kind::Offer);
}

TEST_F(WsTrackerProtocolTest, RejectsMalformedAndOversizedMessages)
{
    EXPECT_FALSE(tr_ws_tracker::parse_signal("{}"));
    EXPECT_FALSE(tr_ws_tracker::parse_signal(R"({"action":"announce","info_hash":"bad"})"));
    EXPECT_FALSE(tr_ws_tracker::parse_signal(std::string((1024U * 1024U) + 1, 'x')));
    EXPECT_FALSE(
        tr_ws_tracker::parse_signal(
            R"({"action":"announce","info_hash":"AAAAAAAAAAAAAAAAAAAA","peer_id":"short","offer_id":"id","offer":{"type":"offer","sdp":"v=0"}})"));
}

TEST_F(WsTrackerProtocolTest, WebSocketTrackerUrlParsing)
{
    auto const parsed = tr_urlParseTracker("wss://tracker.example:443/announce"sv);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->port, 443);
    EXPECT_TRUE(tr_urlIsValidTracker("ws://tracker.example/announce"sv));
    EXPECT_FALSE(tr_urlIsValidTracker("ftp://tracker.example/announce"sv));
}
