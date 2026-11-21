// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Validate selected WebRTC ICE endpoints independently of tracker signaling.
#include "libtransmission/rtc-endpoint.h"
#include "libtransmission/rtc-network.h"

#include <cerrno>
#include <optional>
#include <string>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra-semi"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <rtc/rtc.hpp>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include "test-fixtures.h"

using RtcEndpointTest = ::tr::test::TransmissionTest;

TEST_F(RtcEndpointTest, ParsesSelectedRealIceEndpointsForIpv4AndIpv6)
{
    using tr_rtc_endpoint::parse;

    auto const ipv4 = parse("203.0.113.19:53421");
    ASSERT_TRUE(ipv4);
    EXPECT_TRUE(ipv4->address().is_ipv4());
    EXPECT_EQ(ipv4->address().display_name(), "203.0.113.19");
    EXPECT_EQ(ipv4->port().host(), 53421U);

    // libjuice returns unbracketed IPv6; also accept the bracketed form.
    auto const ipv6 = parse("2001:db8::42:62000");
    ASSERT_TRUE(ipv6);
    EXPECT_TRUE(ipv6->address().is_ipv6());
    EXPECT_EQ(ipv6->address().display_name(), "2001:db8::42");
    EXPECT_EQ(ipv6->port().host(), 62000U);

    auto const bracketed = parse("[2001:db8::42]:62000");
    ASSERT_TRUE(bracketed);
    EXPECT_EQ(bracketed->address(), ipv6->address());
    EXPECT_EQ(bracketed->port(), ipv6->port());
}

TEST_F(RtcEndpointTest, RejectsMissingOrMalformedSelectedIceEndpoints)
{
    using tr_rtc_endpoint::parse;

    EXPECT_FALSE(parse(""));
    EXPECT_FALSE(parse("127.0.0.1"));
    EXPECT_FALSE(parse("0.0.0.0:1000"));
    EXPECT_FALSE(parse("203.0.113.10:0"));
    EXPECT_FALSE(parse("203.0.113.10:70000"));
    EXPECT_FALSE(parse("203.0.113.10:123notport"));
    EXPECT_FALSE(parse("203.0.113.10:-1"));
    EXPECT_FALSE(parse("not_an_ip:1000"));
}

TEST_F(RtcEndpointTest, UsesExplicitBindAddressForWebRtc)
{
    using tr_rtc_network::bind_address;

    auto address = std::optional<std::string>{};
    EXPECT_FALSE(bind_address("", "", 1, address));
    EXPECT_FALSE(address);
    EXPECT_FALSE(bind_address("0.0.0.0", "::", 1, address));
    EXPECT_FALSE(address);

    EXPECT_FALSE(bind_address("192.0.2.10", "", 1, address));
    EXPECT_EQ(address, "192.0.2.10");
    EXPECT_FALSE(bind_address("", "2001:db8::10", 1, address));
    EXPECT_EQ(address, "2001:db8::10");
    EXPECT_FALSE(bind_address("192.0.2.10", "2001:db8::10", 1, address));
    EXPECT_EQ(address, "192.0.2.10");
    EXPECT_FALSE(bind_address("192.0.2.10", "2001:db8::10", 2, address));
    EXPECT_EQ(address, "2001:db8::10");

    EXPECT_EQ(bind_address("2001:db8::10", "", 1, address).code(), EINVAL);
    EXPECT_EQ(bind_address("", "192.0.2.10", 1, address).code(), EINVAL);
    EXPECT_EQ(bind_address("not-an-ip", "", 1, address).code(), EINVAL);
}

TEST_F(RtcEndpointTest, FiltersBlocklistedRemoteIceCandidatesBeforeNegotiation)
{
    auto description = rtc::Description{
        "v=0\r\no=- 1 1 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\na=mid:0\r\n",
        rtc::Description::Type::Offer
    };
    description.addCandidate(rtc::Candidate{ "candidate:1 1 UDP 2114977791 203.0.113.15 54000 typ host" });
    description.addCandidate(rtc::Candidate{ "candidate:2 1 UDP 2114977790 198.51.100.17 55000 typ host" });
    description.endCandidates();

    auto const removed = tr_rtc_network::filter_blocklisted_candidates(
        description,
        [](tr_address const& address) { return address.display_name() == "203.0.113.15"; });

    EXPECT_EQ(removed, 1U);
    ASSERT_EQ(description.candidates().size(), 1U);
    EXPECT_NE(description.candidates().front().candidate().find("198.51.100.17"), std::string::npos);
    EXPECT_TRUE(description.ended());
}
