// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint>
#include <ctime>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h> // htonl(), htons()
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <event2/util.h>

#include <gtest/gtest.h>

#define LIBTRANSMISSION_PORT_FORWARDING_MODULE

#include <libtransmission/net.h>
#include <libtransmission/port-forwarding-natpmp.h>
#include <libtransmission/utils.h> // tr_time(), tr_timeUpdate()

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{
namespace
{

// Answers every NAT-PMP (RFC 6886) request on 127.0.0.1:5351 with success,
// granting public port = requested port + `public_port_offset`.
class FakeNatpmpGateway
{
public:
    struct Request
    {
        uint8_t opcode = 0;
        uint16_t internal_port = 0;
        uint32_t lifetime = 0;
    };

    explicit FakeNatpmpGateway(uint16_t public_port_offset = 0U)
        : public_port_offset_{ public_port_offset }
        , sock_{ socket(AF_INET, SOCK_DGRAM, 0) }
    {
        if (!is_valid_socket(sock_))
        {
            return;
        }

        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(NATPMP_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(sock_, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) != 0 ||
            evutil_make_socket_nonblocking(sock_) != 0)
        {
            tr_net_close_socket(sock_);
            sock_ = TR_BAD_SOCKET;
        }
    }

    ~FakeNatpmpGateway()
    {
        if (is_valid_socket(sock_))
        {
            tr_net_close_socket(sock_);
        }
    }

    FakeNatpmpGateway(FakeNatpmpGateway const&) = delete;
    FakeNatpmpGateway(FakeNatpmpGateway&&) = delete;
    FakeNatpmpGateway& operator=(FakeNatpmpGateway const&) = delete;
    FakeNatpmpGateway& operator=(FakeNatpmpGateway&&) = delete;

    [[nodiscard]] bool is_listening() const noexcept
    {
        return is_valid_socket(sock_);
    }

    [[nodiscard]] auto const& requests() const noexcept
    {
        return requests_;
    }

    // answers every request waiting on the socket
    void serve()
    {
        for (;;)
        {
            auto buf = std::array<uint8_t, 16>{};
            auto peer = sockaddr_in{};
            auto peer_len = socklen_t{ sizeof(peer) };
            auto const len = recvfrom(
                sock_,
                reinterpret_cast<char*>(std::data(buf)),
                std::size(buf),
                0,
                reinterpret_cast<sockaddr*>(&peer),
                &peer_len);
            if (len < 2 || buf[0] != 0U)
            {
                return;
            }

            auto request = Request{ .opcode = buf[1] };
            auto reply = std::vector<uint8_t>{ 0U, static_cast<uint8_t>(128U + request.opcode), 0U, 0U };
            put32(reply, Epoch);

            if (request.opcode == 0U) // public address
            {
                reply.insert(std::end(reply), std::begin(PublicAddress), std::end(PublicAddress));
            }
            else if (len >= 12) // port mapping; lifetime 0 means unmap
            {
                request.internal_port = get16(&buf[4]);
                request.lifetime = get32(&buf[8]);
                put16(reply, request.internal_port);
                put16(reply, static_cast<uint16_t>(request.internal_port + public_port_offset_));
                put32(reply, request.lifetime);
            }
            else
            {
                continue;
            }

            requests_.push_back(request);
            sendto(
                sock_,
                reinterpret_cast<char const*>(std::data(reply)),
                static_cast<int>(std::size(reply)),
                0,
                reinterpret_cast<sockaddr const*>(&peer),
                peer_len);
        }
    }

private:
    static constexpr auto Epoch = uint32_t{ 1 };
    static constexpr auto PublicAddress = std::array<uint8_t, 4>{ 203U, 0U, 113U, 7U };

    static void put16(std::vector<uint8_t>& out, uint16_t val)
    {
        out.push_back(static_cast<uint8_t>(val >> 8U));
        out.push_back(static_cast<uint8_t>(val));
    }

    static void put32(std::vector<uint8_t>& out, uint32_t val)
    {
        put16(out, static_cast<uint16_t>(val >> 16U));
        put16(out, static_cast<uint16_t>(val));
    }

    [[nodiscard]] static uint16_t get16(uint8_t const* in)
    {
        return static_cast<uint16_t>((in[0] << 8U) | in[1]);
    }

    [[nodiscard]] static uint32_t get32(uint8_t const* in)
    {
        return (static_cast<uint32_t>(get16(in)) << 16U) | get16(in + 2);
    }

    uint16_t public_port_offset_;
    tr_socket_t sock_;
    std::vector<Request> requests_;
};

class PortForwardingNatpmpTest : public TransmissionTest
{
protected:
    void SetUp() override
    {
        TransmissionTest::SetUp();
        saved_time_ = tr_time();
        tr_timeUpdate(std::time(nullptr));
    }

    void TearDown() override
    {
        tr_timeUpdate(saved_time_);
        TransmissionTest::TearDown();
    }

    // Pulses until `natpmp` is no longer mapping or unmapping, letting the
    // gateway answer in between. tr_natpmp spaces its commands by
    // tr_time() seconds, so the clock is stepped instead of slept.
    static tr_natpmp::PulseResult pulse_until_settled(
        tr_natpmp& natpmp,
        FakeNatpmpGateway& gateway,
        tr_port port,
        bool is_enabled)
    {
        auto result = tr_natpmp::PulseResult{};
        for (int i = 0; i < 100; ++i)
        {
            result = natpmp.pulse(port, is_enabled);
            gateway.serve();
            if (result.state != TR_PORT_MAPPING && result.state != TR_PORT_UNMAPPING)
            {
                break;
            }

            std::this_thread::sleep_for(10ms); // let the reply reach natpmp's socket
            tr_timeUpdate(tr_time() + 10);
        }
        return result;
    }

private:
    time_t saved_time_ = {};
};

} // namespace

TEST_F(PortForwardingNatpmpTest, mapsAndUnmapsThroughForcedGateway)
{
    auto gateway = FakeNatpmpGateway{};
    if (!gateway.is_listening())
    {
        GTEST_SKIP() << "could not bind UDP 127.0.0.1:" << NATPMP_PORT;
    }

    static auto constexpr Port = tr_port::from_host(51413);
    auto natpmp = tr_natpmp{ tr_address::from_string("127.0.0.1") };

    auto result = pulse_until_settled(natpmp, gateway, Port, true);
    EXPECT_EQ(TR_PORT_MAPPED, result.state);
    EXPECT_EQ(Port, result.local_port);
    EXPECT_EQ(Port, result.advertised_port);
    ASSERT_FALSE(std::empty(gateway.requests()));
    EXPECT_EQ(0U, gateway.requests().front().opcode);
    EXPECT_EQ(NATPMP_PROTOCOL_TCP, gateway.requests().back().opcode);
    EXPECT_EQ(Port.host(), gateway.requests().back().internal_port);
    EXPECT_NE(0U, gateway.requests().back().lifetime);

    result = pulse_until_settled(natpmp, gateway, Port, false);
    EXPECT_EQ(TR_PORT_UNMAPPED, result.state);
    EXPECT_EQ(NATPMP_PROTOCOL_TCP, gateway.requests().back().opcode);
    EXPECT_EQ(0U, gateway.requests().back().lifetime);
}

TEST_F(PortForwardingNatpmpTest, reportsPublicPortGrantedByGateway)
{
    static auto constexpr Offset = uint16_t{ 1000 };
    auto gateway = FakeNatpmpGateway{ Offset };
    if (!gateway.is_listening())
    {
        GTEST_SKIP() << "could not bind UDP 127.0.0.1:" << NATPMP_PORT;
    }

    static auto constexpr Port = tr_port::from_host(51413);
    auto natpmp = tr_natpmp{ tr_address::from_string("127.0.0.1") };

    auto const result = pulse_until_settled(natpmp, gateway, Port, true);
    EXPECT_EQ(TR_PORT_MAPPED, result.state);
    EXPECT_EQ(Port, result.local_port);
    EXPECT_EQ(tr_port::from_host(Port.host() + Offset), result.advertised_port);
}

} // namespace tr::test
