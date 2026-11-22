// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <event2/dns.h>
#include <event2/event.h>
#include <event2/util.h>

#include <fmt/format.h>

#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/socks5-udp.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/utils.h"

using namespace std::literals;

//  SOCKS5 protocol constants (RFC 1928)

namespace
{

constexpr uint8_t Socks5Version = 0x05;
constexpr uint8_t Socks5AuthNone = 0x00;
constexpr uint8_t Socks5CmdUdpAssociate = 0x03;
constexpr uint8_t Socks5AtypIpv4 = 0x01;
constexpr uint8_t Socks5AtypIpv6 = 0x04;
constexpr uint8_t Socks5Rsv = 0x00;
constexpr uint8_t Socks5ReplySuccess = 0x00;

} // namespace

// Construction / destruction

tr_socks5_udp::tr_socks5_udp(struct event_base* base, std::string_view proxy_host, uint16_t proxy_port, ReadyCallback on_ready)
    : base_{ base }
    , proxy_host_{ proxy_host }
    , proxy_port_{ proxy_port }
    , on_ready_{ std::move(on_ready) }
{
    start_connect();
}

tr_socks5_udp::~tr_socks5_udp()
{
    if (dns_req_ != nullptr)
    {
        evdns_getaddrinfo_cancel(dns_req_);
        dns_req_ = nullptr;
    }

    if (dns_base_ != nullptr)
    {
        evdns_base_free(dns_base_, 0);
        dns_base_ = nullptr;
    }

    tcp_event_.reset();
    udp_event_.reset();

    if (tcp_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(tcp_socket_);
    }

    if (udp_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(udp_socket_);
    }
}

// TCP connect to SOCKS5 proxy

void tr_socks5_udp::start_connect()
{
    if (auto const addr = tr_address::from_string(proxy_host_); addr)
    {
        // Numeric IP — skip DNS entirely
        proxy_addr_ = *addr;
        proxy_family_ = tr_ip_protocol_to_af(addr->type);
        auto const socket_address = tr_socket_address{ *addr, tr_port::from_host(proxy_port_) };
        auto const [ss, sslen] = socket_address.to_sockaddr();
        state_ = State::Connecting;
        do_connect(reinterpret_cast<sockaddr const*>(&ss), sslen);
        return;
    }

    // Hostname — resolve asynchronously via evdns
    state_ = State::Resolving;

    dns_base_ = evdns_base_new(base_, EVDNS_BASE_INITIALIZE_NAMESERVERS | EVDNS_BASE_DISABLE_WHEN_INACTIVE);
    if (dns_base_ == nullptr)
    {
        set_error("SOCKS5: couldn't create DNS resolver"sv);
        return;
    }

    auto hints = evutil_addrinfo{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    auto port_str = std::array<char, 16>{};
    *fmt::format_to(std::data(port_str), "{:d}", proxy_port_) = '\0';

    dns_req_ = evdns_getaddrinfo(dns_base_, proxy_host_.c_str(), std::data(port_str), &hints, on_dns_result, this);
    // dns_req_ is nullptr when the callback was already invoked inline (immediate error or cached result)
}

void tr_socks5_udp::on_dns_result(int result, evutil_addrinfo* res, void* arg)
{
    auto* self = static_cast<tr_socks5_udp*>(arg);
    self->dns_req_ = nullptr;

    if (result != 0)
    {
        self->set_error(
            fmt::format(
                "SOCKS5: DNS lookup failed for {}:{} — {}",
                self->proxy_host_,
                self->proxy_port_,
                evutil_gai_strerror(result)));
        return;
    }

    auto const res_guard = std::unique_ptr<evutil_addrinfo, decltype(&evutil_freeaddrinfo)>{ res, evutil_freeaddrinfo };

    if (res == nullptr)
    {
        self->set_error(fmt::format("SOCKS5: DNS lookup returned no address for {}:{}", self->proxy_host_, self->proxy_port_));
        return;
    }

    self->proxy_family_ = res->ai_family;
    if (auto const socket_address = tr_socket_address::from_sockaddr(res->ai_addr); socket_address)
    {
        self->proxy_addr_ = socket_address->address();
    }

    self->state_ = State::Connecting;
    self->do_connect(res->ai_addr, static_cast<socklen_t>(res->ai_addrlen));
}

void tr_socks5_udp::do_connect(sockaddr const* sa, socklen_t salen)
{
    tcp_socket_ = socket(proxy_family_, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_socket_ == TR_BAD_SOCKET)
    {
        set_error("SOCKS5: couldn't create TCP socket"sv);
        return;
    }

    if (evutil_make_socket_nonblocking(tcp_socket_) != 0)
    {
        set_error("SOCKS5: couldn't make TCP socket non-blocking"sv);
        return;
    }

    int const ret = connect(tcp_socket_, sa, salen);
    if (ret != 0 &&
#ifdef _WIN32
        sockerrno != WSAEWOULDBLOCK &&
#endif
        sockerrno != EINPROGRESS)
    {
        set_error(fmt::format("SOCKS5: connect() failed — {}", tr_strerror(sockerrno)));
        return;
    }

    // Wait for writable (connect completion) or readable
    tcp_event_.reset(event_new(base_, tcp_socket_, EV_WRITE | EV_PERSIST, on_tcp_event, this));
    event_add(tcp_event_.get(), nullptr);

    if (ret == 0)
    {
        // Already connected (e.g. loopback)
        send_greeting();
    }
}

//  libevent callback

void tr_socks5_udp::on_tcp_event(evutil_socket_t /*fd*/, short what, void* arg)
{
    auto* self = static_cast<tr_socks5_udp*>(arg);

    if ((what & EV_WRITE) != 0)
    {
        self->on_tcp_writable();
    }

    if ((what & EV_READ) != 0)
    {
        self->on_tcp_readable();
    }
}

void tr_socks5_udp::on_tcp_writable()
{
    if (!tcp_write_buf_.empty())
    {
        flush_pending_write();
        return;
    }

    if (state_ == State::Connecting)
    {
        // Check if connect succeeded
        int err = 0;
        socklen_t errlen = sizeof(err);
        if (getsockopt(tcp_socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &errlen) != 0 || err != 0)
        {
            set_error(fmt::format("SOCKS5: TCP connect failed — {}", tr_strerror(err != 0 ? err : sockerrno)));
            return;
        }

        send_greeting();
        return;
    }
}

void tr_socks5_udp::queue_tcp_write(uint8_t const* data, size_t len)
{
    tcp_write_buf_.assign(data, data + len);
    tcp_write_sent_ = 0;
    flush_pending_write();
}

void tr_socks5_udp::flush_pending_write()
{
    while (tcp_write_sent_ < std::size(tcp_write_buf_))
    {
        auto const n = send(
            tcp_socket_,
            reinterpret_cast<char const*>(std::data(tcp_write_buf_) + tcp_write_sent_),
            std::size(tcp_write_buf_) - tcp_write_sent_,
            0);

        if (n > 0)
        {
            tcp_write_sent_ += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && (sockerrno == EWOULDBLOCK || sockerrno == EAGAIN))
        {
            tcp_event_.reset(event_new(base_, tcp_socket_, EV_WRITE | EV_PERSIST, on_tcp_event, this));
            event_add(tcp_event_.get(), nullptr);
            return;
        }

        set_error(fmt::format("SOCKS5: TCP send failed — {}", tr_strerror(sockerrno)));
        return;
    }

    tcp_write_buf_.clear();
    tcp_write_sent_ = 0;
    state_ = pending_state_;
    tcp_buf_used_ = 0;

    tcp_event_.reset(event_new(base_, tcp_socket_, EV_READ | EV_PERSIST, on_tcp_event, this));
    event_add(tcp_event_.get(), nullptr);
}

void tr_socks5_udp::on_tcp_readable()
{
    if (tcp_buf_used_ >= kTcpBufSize)
    {
        set_error("SOCKS5: handshake response exceeded buffer size"sv);
        return;
    }

    // Read whatever is available into tcp_buf_
    auto const n = recv(tcp_socket_, reinterpret_cast<char*>(tcp_buf_ + tcp_buf_used_), kTcpBufSize - tcp_buf_used_, 0);

    if (n <= 0)
    {
        if (n == 0)
        {
            set_error("SOCKS5: proxy closed TCP connection"sv);
        }
        else if (sockerrno != EWOULDBLOCK && sockerrno != EAGAIN)
        {
            set_error(fmt::format("SOCKS5: TCP recv failed — {}", tr_strerror(sockerrno)));
        }
        return;
    }

    tcp_buf_used_ += static_cast<size_t>(n);

    switch (state_)
    {
    case State::SentGreeting:
        handle_greeting_response();
        break;
    case State::SentUdpAssociate:
        handle_udp_associate_response();
        break;
    default:
        break;
    }
}

//  SOCKS5 handshake steps

void tr_socks5_udp::send_greeting()
{
    // VER=5, NMETHODS=1, METHOD=NO_AUTH
    uint8_t const greeting[] = { Socks5Version, 1, Socks5AuthNone };

    pending_state_ = State::SentGreeting;
    queue_tcp_write(greeting, sizeof(greeting));
}

void tr_socks5_udp::handle_greeting_response()
{
    // Expect 2 bytes: VER(5) METHOD
    if (tcp_buf_used_ < 2)
    {
        return; // need more data
    }

    if (tcp_buf_[0] != Socks5Version || tcp_buf_[1] != Socks5AuthNone)
    {
        set_error(
            fmt::format("SOCKS5: unsupported auth method {:d} (only no-auth supported)", static_cast<unsigned>(tcp_buf_[1])));
        return;
    }

    tcp_buf_used_ = 0;
    send_udp_associate();
}

void tr_socks5_udp::send_udp_associate()
{
    // Create a local UDP socket that we'll use for relay communication
    auto const family = proxy_family_ == AF_INET6 ? AF_INET6 : AF_INET;
    auto const ip_protocol = family == AF_INET6 ? TR_AF_INET6 : TR_AF_INET;

    udp_socket_ = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_ == TR_BAD_SOCKET)
    {
        set_error("SOCKS5: couldn't create UDP socket for relay"sv);
        return;
    }

    if (evutil_make_socket_nonblocking(udp_socket_) != 0)
    {
        set_error("SOCKS5: couldn't make UDP socket non-blocking"sv);
        return;
    }

    // Bind to any port
    auto const bind_address = tr_socket_address{ tr_address::any(ip_protocol), tr_port{} };
    auto const bind_sockaddr = bind_address.to_sockaddr();
    if (bind(udp_socket_, reinterpret_cast<sockaddr const*>(&bind_sockaddr.first), bind_sockaddr.second) != 0)
    {
        set_error(fmt::format("SOCKS5: bind UDP socket failed — {}", tr_strerror(sockerrno)));
        return;
    }

    // Get the bound address/port
    auto local_addr = sockaddr_storage{};
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(udp_socket_, reinterpret_cast<sockaddr*>(&local_addr), &local_len) != 0)
    {
        set_error(fmt::format("SOCKS5: couldn't inspect UDP socket address - {}", tr_strerror(sockerrno)));
        return;
    }

    // Send: VER=5 CMD=UDP_ASSOCIATE RSV=0 ATYP DST.ADDR DST.PORT
    // DST.ADDR and DST.PORT tell the proxy where we'll send from. Since the
    // socket is bound to any address, keep DST.ADDR wildcard but include the
    // actual UDP source port.
    auto request = std::vector<uint8_t>{ Socks5Version, Socks5CmdUdpAssociate, Socks5Rsv };
    request.reserve(4 + 16 + 2);

    if (family == AF_INET6)
    {
        auto const& sin6 = reinterpret_cast<sockaddr_in6 const&>(local_addr);
        request.push_back(Socks5AtypIpv6);
        auto const* const addr = reinterpret_cast<uint8_t const*>(&sin6.sin6_addr);
        request.insert(std::end(request), addr, addr + 16);
        auto const* const port = reinterpret_cast<uint8_t const*>(&sin6.sin6_port);
        request.insert(std::end(request), port, port + 2);
    }
    else
    {
        auto const& sin = reinterpret_cast<sockaddr_in const&>(local_addr);
        request.push_back(Socks5AtypIpv4);
        auto const* const addr = reinterpret_cast<uint8_t const*>(&sin.sin_addr);
        request.insert(std::end(request), addr, addr + 4);
        auto const* const port = reinterpret_cast<uint8_t const*>(&sin.sin_port);
        request.insert(std::end(request), port, port + 2);
    }

    pending_state_ = State::SentUdpAssociate;
    queue_tcp_write(std::data(request), std::size(request));
}

void tr_socks5_udp::handle_udp_associate_response()
{
    // Minimum response: VER(1) REP(1) RSV(1) ATYP(1) + addr + port
    // IPv4: 4 + 4 + 2 = 10
    // IPv6: 4 + 16 + 2 = 22
    if (tcp_buf_used_ < 4)
    {
        return; // need more data
    }

    if (tcp_buf_[0] != Socks5Version)
    {
        set_error("SOCKS5: invalid version in UDP ASSOCIATE response"sv);
        return;
    }

    if (tcp_buf_[1] != Socks5ReplySuccess)
    {
        set_error(fmt::format("SOCKS5: UDP ASSOCIATE failed with reply code {:d}", static_cast<unsigned>(tcp_buf_[1])));
        return;
    }

    if (tcp_buf_[2] != Socks5Rsv)
    {
        set_error(fmt::format("SOCKS5: invalid RSV byte {:d} in UDP ASSOCIATE response", static_cast<unsigned>(tcp_buf_[2])));
        return;
    }

    uint8_t const atyp = tcp_buf_[3];
    size_t needed = 0;
    if (atyp == Socks5AtypIpv4)
    {
        needed = 4 + 4 + 2; // header + IPv4 + port
    }
    else if (atyp == Socks5AtypIpv6)
    {
        needed = 4 + 16 + 2; // header + IPv6 + port
    }
    else
    {
        set_error(fmt::format("SOCKS5: unsupported ATYP {:d} in UDP ASSOCIATE response", static_cast<unsigned>(atyp)));
        return;
    }

    if (tcp_buf_used_ < needed)
    {
        return; // need more data
    }

    // Parse the relay address
    auto relay_address = tr_address{};
    uint16_t relay_port = 0;

    if (atyp == Socks5AtypIpv4)
    {
        relay_address.type = TR_AF_INET;
        std::memcpy(&relay_address.addr.addr4, tcp_buf_ + 4, 4);
        std::memcpy(&relay_port, tcp_buf_ + 8, 2);
        relay_port = ntohs(relay_port);
    }
    else // IPv6
    {
        relay_address.type = TR_AF_INET6;
        std::memcpy(&relay_address.addr.addr6, tcp_buf_ + 4, 16);
        std::memcpy(&relay_port, tcp_buf_ + 20, 2);
        relay_port = ntohs(relay_port);
    }

    // If the relay address is 0.0.0.0 or ::, use the proxy host address instead
    if (relay_address.is_any())
    {
        if (proxy_addr_)
        {
            relay_address = *proxy_addr_;
        }
        else
        {
            set_error("SOCKS5: proxy returned wildcard UDP relay address and proxy address is unavailable"sv);
            return;
        }
    }

    relay_addr_ = tr_socket_address{ relay_address, tr_port::from_host(relay_port) };

    state_ = State::Ready;

    tr_logAddInfo(fmt::format("SOCKS5: UDP relay ready at {}", relay_addr_->display_name()));

    // Keep TCP connection alive (closing it terminates the association per RFC 1928).
    // Switch to read-only to detect proxy-initiated close.
    tcp_event_.reset(event_new(base_, tcp_socket_, EV_READ | EV_PERSIST, on_tcp_event, this));
    event_add(tcp_event_.get(), nullptr);

    // Set up UDP read event for relay responses
    setup_udp_read_event();

    if (on_ready_)
    {
        on_ready_();
    }
}

//  UDP datagram wrapping (RFC 1928 §7)

void tr_socks5_udp::sendto(void const* buf, size_t buflen, sockaddr const* dest, socklen_t /*destlen*/)
{
    if (!is_ready() || udp_socket_ == TR_BAD_SOCKET || !relay_addr_)
    {
        return;
    }

    auto const socket_addr = tr_socket_address::from_sockaddr(dest);
    if (!socket_addr)
    {
        return;
    }

    // Build SOCKS5 UDP request header:
    // RSV(2) FRAG(1) ATYP(1) DST.ADDR(4|16) DST.PORT(2) DATA(...)
    auto header = std::array<uint8_t, 2 + 1 + 1 + 16 + 2>{}; // max size for IPv6
    size_t hdr_len = 0;

    // RSV = 0x0000
    header[0] = 0;
    header[1] = 0;
    // FRAG = 0 (no fragmentation)
    header[2] = 0;

    auto const& addr = socket_addr->address();
    auto const port = socket_addr->port();

    if (addr.is_ipv4())
    {
        header[3] = Socks5AtypIpv4;
        std::memcpy(header.data() + 4, &addr.addr.addr4, 4);
        auto const nport = port.network();
        std::memcpy(header.data() + 8, &nport, 2);
        hdr_len = 10;
    }
    else
    {
        header[3] = Socks5AtypIpv6;
        std::memcpy(header.data() + 4, &addr.addr.addr6, 16);
        auto const nport = port.network();
        std::memcpy(header.data() + 20, &nport, 2);
        hdr_len = 22;
    }

    // Assemble full packet: header + payload
    auto packet = std::vector<uint8_t>(hdr_len + buflen);
    std::memcpy(packet.data(), header.data(), hdr_len);
    std::memcpy(packet.data() + hdr_len, buf, buflen);

    // Send to relay address
    auto const [ss, sslen] = relay_addr_->to_sockaddr();
    ::sendto(
        udp_socket_,
        reinterpret_cast<char const*>(packet.data()),
        packet.size(),
        0,
        reinterpret_cast<sockaddr const*>(&ss),
        sslen);
}

//  Unwrap incoming relay packets

uint8_t const* tr_socks5_udp::unwrap_udp_packet(
    uint8_t const* data,
    size_t datalen,
    size_t& payload_len,
    sockaddr_storage& from,
    socklen_t& fromlen)
{
    // Minimum: RSV(2) + FRAG(1) + ATYP(1) + IPv4(4) + PORT(2) = 10
    if (datalen < 10)
    {
        return nullptr;
    }

    // RSV must be 0x0000
    if (data[0] != 0 || data[1] != 0)
    {
        return nullptr;
    }

    // FRAG — we don't support reassembly, only standalone (0) packets
    if (data[2] != 0)
    {
        return nullptr;
    }

    uint8_t const atyp = data[3];
    size_t hdr_len = 0;

    if (atyp == Socks5AtypIpv4)
    {
        if (datalen < 10)
        {
            return nullptr;
        }
        hdr_len = 10;

        auto sin = sockaddr_in{};
        sin.sin_family = AF_INET;
        std::memcpy(&sin.sin_addr, data + 4, 4);
        std::memcpy(&sin.sin_port, data + 8, 2); // already network order
        std::memcpy(&from, &sin, sizeof(sin));
        fromlen = sizeof(sin);
    }
    else if (atyp == Socks5AtypIpv6)
    {
        if (datalen < 22)
        {
            return nullptr;
        }
        hdr_len = 22;

        auto sin6 = sockaddr_in6{};
        sin6.sin6_family = AF_INET6;
        std::memcpy(&sin6.sin6_addr, data + 4, 16);
        std::memcpy(&sin6.sin6_port, data + 20, 2); // already network order
        std::memcpy(&from, &sin6, sizeof(sin6));
        fromlen = sizeof(sin6);
    }
    else
    {
        return nullptr;
    }

    payload_len = datalen - hdr_len;
    return data + hdr_len;
}

//  UDP relay read event

void tr_socks5_udp::setup_udp_read_event()
{
    if (udp_socket_ == TR_BAD_SOCKET)
    {
        return;
    }

    udp_event_.reset(event_new(base_, udp_socket_, EV_READ | EV_PERSIST, on_udp_readable, this));
    event_add(udp_event_.get(), nullptr);
}

void tr_socks5_udp::on_udp_readable(evutil_socket_t fd, short /*what*/, void* arg)
{
    auto* self = static_cast<tr_socks5_udp*>(arg);

    auto buf = std::array<uint8_t, 8192>{};
    for (;;)
    {
        auto from = sockaddr_storage{};
        auto fromlen = socklen_t{ sizeof(from) };

        auto const n = recvfrom(
            fd,
            reinterpret_cast<char*>(std::data(buf)),
            std::size(buf),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromlen);

        if (n <= 0)
        {
            return;
        }

        // Unwrap the SOCKS5 UDP header to get the real source and payload
        auto orig_from = sockaddr_storage{};
        auto orig_fromlen = socklen_t{};
        size_t payload_len = 0;

        auto const* payload = unwrap_udp_packet(std::data(buf), static_cast<size_t>(n), payload_len, orig_from, orig_fromlen);

        if (payload != nullptr && self->on_incoming_)
        {
            self->on_incoming_(payload, payload_len, reinterpret_cast<sockaddr const*>(&orig_from), orig_fromlen);
        }
    }
}

//  Error handling

void tr_socks5_udp::set_error(std::string_view msg)
{
    tr_logAddWarn(std::string{ msg });
    state_ = State::Error;

    tcp_event_.reset();
    udp_event_.reset();

    if (tcp_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(tcp_socket_);
        tcp_socket_ = TR_BAD_SOCKET;
    }

    if (udp_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(udp_socket_);
        udp_socket_ = TR_BAD_SOCKET;
    }
}

//  Helper

std::optional<tr_socket_address> tr_socks5_udp::relay_address() const noexcept
{
    return relay_addr_;
}
