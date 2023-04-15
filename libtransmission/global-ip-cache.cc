// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::all_of
#include <cstddef>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <sys/socket.h>
#endif

#include <fmt/core.h>

#include "log.h"
#include "global-ip-cache.h"
#include "session.h"
#include "tr-assert.h"
#include "utils.h"

tr_global_ip_cache::tr_global_ip_cache(tr_session& session_in)
    : session_{ session_in }
    , upkeep_timers_{ session_in.timerMaker().create(), session_in.timerMaker().create() }
{
    static_assert(TR_AF_INET == 0);
    static_assert(TR_AF_INET6 == 1);
    static_assert(NUM_TR_AF_INET_TYPES == 2);

    for (std::size_t i = 0; i < NUM_TR_AF_INET_TYPES; ++i)
    {
        auto const type = static_cast<tr_address_type>(i);
        auto const cb = [this, type]()
        {
            update_addr(type);
        };
        upkeep_timers_[i]->setCallback(cb);
        start_timer(type, UpkeepInterval);
    }
}

tr_global_ip_cache::~tr_global_ip_cache()
{
    TR_ASSERT(!session_.amInSessionThread());

    // Destroying mutex while someone owns it is undefined behaviour, so we acquire it first
    auto const locks = std::scoped_lock{ is_updating_mutex_[TR_AF_INET], is_updating_mutex_[TR_AF_INET6],
                                         global_addr_mutex_[TR_AF_INET], global_addr_mutex_[TR_AF_INET6],
                                         source_addr_mutex_[TR_AF_INET], source_addr_mutex_[TR_AF_INET6] };

    TR_ASSERT(std::all_of(
        std::begin(is_updating_),
        std::end(is_updating_),
        [](is_updating_t const& v) { return v == is_updating_t::ABORT; }));
}

bool tr_global_ip_cache::try_shutdown() noexcept
{
    for (auto& timer : upkeep_timers_)
    {
        timer->stop();
    }

    for (std::size_t i = 0; i < NUM_TR_AF_INET_TYPES; ++i)
    {
        auto const lock = std::unique_lock{ is_updating_mutex_[i], std::try_to_lock };
        if (!lock.owns_lock() || is_updating_[i] == is_updating_t::YES)
        {
            return false;
        }
        is_updating_[i] = is_updating_t::ABORT; // Abort any future updates
    }
    return true;
}

std::optional<tr_address> tr_global_ip_cache::global_addr(tr_address_type type) const noexcept
{
    auto const lock = std::shared_lock{ global_addr_mutex_[type] };
    return global_addr_[type];
}

std::optional<tr_address> tr_global_ip_cache::global_source_addr(tr_address_type type) const noexcept
{
    auto const lock = std::shared_lock{ source_addr_mutex_[type] };
    return source_addr_[type];
}

tr_address tr_global_ip_cache::bind_addr(tr_address_type type) const noexcept
{
    if (type == TR_AF_INET)
    {
        // if user provided an address, use it.
        // otherwise, use any_ipv4 (0.0.0.0).
        static auto constexpr DefaultAddr = tr_address::any_ipv4();
        return tr_address::from_string(session_.bindAddress(type)).value_or(DefaultAddr);
    }

    if (type == TR_AF_INET6)
    {
        // if user provided an address, use it.
        // otherwise, use any_ipv6 (::).
        static auto constexpr DefaultAddr = tr_address::any_ipv6();
        return tr_address::from_string(session_.bindAddress(type)).value_or(DefaultAddr);
    }

    TR_ASSERT_MSG(false, "invalid type");
    return {};
}

void tr_global_ip_cache::update_addr(tr_address_type type) noexcept
{
    update_source_addr(type);
    if (global_source_addr(type))
    {
        update_global_addr(type);
    }
}

void tr_global_ip_cache::set_global_addr(tr_address const& addr) noexcept
{
    TR_ASSERT(addr.is_global_unicast_address());
    auto const lock = std::lock_guard{ global_addr_mutex_[addr.type] };
    global_addr_[addr.type] = addr;
    tr_logAddTrace(fmt::format("Cached global address {}", addr.display_name()));
}

void tr_global_ip_cache::unset_global_addr(tr_address_type type) noexcept
{
    auto const lock = std::lock_guard{ global_addr_mutex_[type] };
    global_addr_[type].reset();
    tr_logAddTrace(fmt::format("Unset {} global address cache", type == TR_AF_INET ? "IPv4"sv : "IPv6"sv));
}

void tr_global_ip_cache::set_source_addr(tr_address const& addr) noexcept
{
    auto const lock = std::lock_guard{ source_addr_mutex_[addr.type] };
    source_addr_[addr.type] = addr;
    tr_logAddTrace(fmt::format("Cached source address {}", addr.display_name()));
}

void tr_global_ip_cache::unset_addr(tr_address_type type) noexcept
{
    auto const lock = std::lock_guard{ source_addr_mutex_[type] };
    source_addr_[type].reset();
    tr_logAddTrace(fmt::format("Unset {} source address cache", type == TR_AF_INET ? "IPv4"sv : "IPv6"sv));

    // No public internet connectivity means no global IP address
    unset_global_addr(type);
}

void tr_global_ip_cache::start_timer(tr_address_type type, std::chrono::milliseconds msec) noexcept
{
    upkeep_timers_[type]->startRepeating(msec);
}

void tr_global_ip_cache::stop_timer(tr_address_type type) noexcept
{
    upkeep_timers_[type]->stop();
}

bool tr_global_ip_cache::set_is_updating(tr_address_type type) noexcept
{
    auto lock = std::unique_lock{ is_updating_mutex_[type] };
    is_updating_cv_[type].wait(
        lock,
        [this, type]() { return is_updating_[type] == is_updating_t::NO || is_updating_[type] == is_updating_t::ABORT; });
    if (is_updating_[type] != is_updating_t::NO)
    {
        return false;
    }
    is_updating_[type] = is_updating_t::YES;
    lock.unlock();
    is_updating_cv_[type].notify_one();
    return true;
}

void tr_global_ip_cache::unset_is_updating(tr_address_type type) noexcept
{
    TR_ASSERT(is_updating_[type] == is_updating_t::YES);
    auto lock = std::unique_lock{ is_updating_mutex_[type] };
    is_updating_[type] = is_updating_t::NO;
    lock.unlock();
    is_updating_cv_[type].notify_one();
}

void tr_global_ip_cache::update_global_addr(tr_address_type type) noexcept
{
    TR_ASSERT(has_ip_protocol_[type]);
    TR_ASSERT(global_source_addr(type));
    TR_ASSERT(ix_service_[type] < std::size(IPQueryServices));

    if (ix_service_[type] == 0U && !set_is_updating(type))
    {
        return;
    }
    TR_ASSERT(is_updating_[type] == is_updating_t::YES);

    // Update global address
    auto options = tr_web::FetchOptions{ IPQueryServices[ix_service_[type]],
                                         [this, type](tr_web::FetchResponse const& response)
                                         { this->on_response_ip_query(type, response); },
                                         nullptr };
    options.ip_proto = type == TR_AF_INET ? tr_web::FetchOptions::IPProtocol::V4 : tr_web::FetchOptions::IPProtocol::V6;
    options.sndbuf = 4096;
    options.rcvbuf = 4096;
    session_.fetch(std::move(options));
}

void tr_global_ip_cache::update_source_addr(tr_address_type type) noexcept
{
    TR_ASSERT(has_ip_protocol_[type]);

    if (!set_is_updating(type))
    {
        return;
    }
    TR_ASSERT(is_updating_[type] == is_updating_t::YES);

    auto const protocol = type == TR_AF_INET ? "IPv4"sv : "IPv6"sv;

    auto err = int{};
    auto const& source_addr = get_global_source_address(bind_addr(type), err);
    if (source_addr)
    {
        set_source_addr(*source_addr);
        tr_logAddInfo(fmt::format(
            _("Successfully updated source {protocol} address to {ip}"),
            fmt::arg("protocol", protocol),
            fmt::arg("ip", source_addr->display_name())));
    }
    else
    {
        // Stop the update process since we have no public internet connectivity
        unset_addr(type);
        upkeep_timers_[type]->setInterval(RetryUpkeepInterval);
        tr_logAddDebug(fmt::format(_("Couldn't obtain source {protocol} address"), fmt::arg("protocol", protocol)));
        if (err == EAFNOSUPPORT)
        {
            stop_timer(type); // No point in retrying
            has_ip_protocol_[type] = false;
            tr_logAddWarn(fmt::format(_("Your machine does not support {protocol}"), fmt::arg("protocol", protocol)));
        }
    }

    unset_is_updating(type);
}

void tr_global_ip_cache::on_response_ip_query(tr_address_type type, tr_web::FetchResponse const& response) noexcept
{
    TR_ASSERT(session_.amInSessionThread());
    TR_ASSERT(is_updating_[type] == is_updating_t::YES);
    TR_ASSERT(ix_service_[type] < std::size(IPQueryServices));

    auto const protocol = type == TR_AF_INET ? "IPv4"sv : "IPv6"sv;
    auto success = bool{ false };

    if (response.status == 200 /* HTTP_OK */)
    {
        auto const ip = tr_strvStrip(response.body);

        // Update member
        if (auto addr = tr_address::from_string(ip); addr && addr->is_global_unicast_address() && addr->type == type)
        {
            set_global_addr(*addr);

            success = true;
            upkeep_timers_[type]->setInterval(UpkeepInterval);

            tr_logAddInfo(fmt::format(
                _("Successfully updated global {type} address to {ip} using {url}"),
                fmt::arg("type", protocol),
                fmt::arg("ip", addr->display_name()),
                fmt::arg("url", IPQueryServices[ix_service_[type]])));
        }
    }

    // Try next IP query URL
    if (!success && ++ix_service_[type] < std::size(IPQueryServices))
    {
        update_global_addr(type);
        return;
    }

    if (!success)
    {
        tr_logAddDebug(fmt::format("Couldn't obtain global {} address", protocol));
        unset_global_addr(type);
        upkeep_timers_[type]->setInterval(RetryUpkeepInterval);
    }

    ix_service_[type] = 0U;
    unset_is_updating(type);
}

// tr_global_ip_cache::get_global_source_address() and tr_global_ip_cache::get_source_address
// are modified from code by Juliusz Chroboczek and is covered under the same license as dht.cc.
// Please feel free to copy them into your software if it can help
// unbreaking the double-stack Internet.
std::optional<tr_address> tr_global_ip_cache::get_global_source_address(tr_address const& bind_addr, int& err_out)
{
    // Pick some destination address to pretend to send a packet to
    static auto constexpr DstIPv4 = "91.121.74.28"sv;
    static auto constexpr DstIPv6 = "2001:1890:1112:1::20"sv;
    auto const dst_addr = tr_address::from_string(bind_addr.is_ipv4() ? DstIPv4 : DstIPv6);
    auto const dst_port = tr_port::fromHost(6969);

    // In order for address selection to work right,
    // this should be a global unicast address, not Teredo or 6to4
    TR_ASSERT(dst_addr && dst_addr->is_global_unicast_address());

    if (dst_addr)
    {
        return get_source_address(*dst_addr, dst_port, bind_addr, err_out);
    }

    return {};
}

// Get the source address used for a given destination address.
// Since there is no official interface to get this information,
// we create a connected UDP socket (connected UDP... hmm...)
// and check its source address.
//
// Since it's a UDP socket, this doesn't actually send any packets
std::optional<tr_address> tr_global_ip_cache::get_source_address(
    tr_address const& dst_addr,
    tr_port dst_port,
    tr_address const& bind_addr,
    int& err_out)
{
    TR_ASSERT(dst_addr.type == bind_addr.type);

    auto const save = errno;

    auto const [dst_ss, dst_sslen] = dst_addr.to_sockaddr(dst_port);
    auto const [bind_ss, bind_sslen] = bind_addr.to_sockaddr(tr_port::fromHost(0));
    if (auto const sock = socket(dst_ss.ss_family, SOCK_DGRAM, 0); sock != TR_BAD_SOCKET)
    {
        if (bind(sock, reinterpret_cast<sockaddr const*>(&bind_ss), bind_sslen) == 0)
        {
            if (connect(sock, reinterpret_cast<sockaddr const*>(&dst_ss), dst_sslen) == 0)
            {
                auto src_ss = sockaddr_storage{};
                auto src_sslen = socklen_t{ sizeof(src_ss) };
                if (getsockname(sock, reinterpret_cast<sockaddr*>(&src_ss), &src_sslen) == 0)
                {
                    if (auto const addrport = tr_address::from_sockaddr(reinterpret_cast<sockaddr*>(&src_ss)); addrport)
                    {
                        tr_net_close_socket(sock);
                        errno = save;
                        return addrport->first;
                    }
                }
            }
        }

        tr_net_close_socket(sock);
    }

    err_out = errno;
    errno = save;
    return {};
}