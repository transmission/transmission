// This file Copyright © 2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <chrono>
#include <cstring> // for memcpy
#include <ctime> // for time_t
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>

#include <fmt/core.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#endif

#include "libtransmission/net.h" // for tr_port

namespace libtransmission
{

class DnsCache
{
public:
    enum class Family
    {
        IPv4,
        IPv6
    };

    enum class Protocol
    {
        TCP,
        UDP
    };

    enum class Result
    {
        Pending,
        Success,
        Failed
    };

    struct WalkEntry
    {
        std::string_view host;
        tr_port port;
        Family family;
        Protocol protocol;
        Result result;
        sockaddr_storage ss;
        socklen_t sslen;
    };

    virtual std::tuple<Result, sockaddr_storage, socklen_t> get(
        time_t now,
        std::string_view host,
        tr_port port,
        Family family,
        Protocol protocol) = 0;

    [[nodiscard]] virtual bool is_pending(std::string_view host, tr_port port, Family family, Protocol protocol) const = 0;

    // Note: walk() locks the DnsCache, so `func` should have a limited scope.
    // For example, it should not invoke other DnsCache methods.
    virtual void walk(
        time_t now,
        std::function<void(WalkEntry const&)> const& func,
        std::optional<Family> family_wanted = {},
        std::optional<Protocol> protocol_wanted = {}) const = 0;
};

class DefaultDnsCache : public DnsCache
{
public:
    std::tuple<Result, sockaddr_storage, socklen_t> get(
        time_t now,
        std::string_view host,
        tr_port port,
        Family family,
        Protocol protocol) override
    {
        auto key = Key{ host, port, family, protocol };

        // do we already have it?
        auto const cache_lock = std::unique_lock{ cache_mutex_ };
        if (auto const iter = cache_.find(key); iter != std::end(cache_))
        {
            if (auto const& [addr, created_at, result] = iter->second; now - created_at < CacheTtlSecs)
            {
                return { result, addr.first, addr.second };
            }

            cache_.erase(iter); // we did have it, but the TTL has expired
        }

        // did we already request it?
        auto const pending_lock = std::unique_lock{ pending_mutex_ };
        if (auto const iter = pending_.find(key); iter != std::end(pending_))
        {
            if (auto& fut = iter->second; fut.wait_for(std::chrono::milliseconds{ 0 }) == std::future_status::ready)
            {
                auto const addr = fut.get();
                auto& perm = cache_[key];
                perm = addr ? Entry{ *addr, now, Result::Success } : Entry{ {}, now, Result::Failed };
                pending_.erase(iter);
                return { perm.result, perm.addr.first, perm.addr.second };
            }

            return { Result::Pending, {}, {} };
        }

        pending_.try_emplace(
            std::move(key),
            std::async(std::launch::async, lookup, std::string{ host }, port, family, protocol));
        return { Result::Pending, {}, {} };
    }

    [[nodiscard]] bool is_pending(std::string_view host, tr_port port, Family family, Protocol protocol) const override
    {
        auto const lock = std::unique_lock{ pending_mutex_ };
        return pending_.count(Key{ host, port, family, protocol }) != 0U;
    }

    void walk(
        time_t now,
        std::function<void(WalkEntry const&)> const& func,
        std::optional<Family> family_wanted = {},
        std::optional<Protocol> protocol_wanted = {}) const override
    {
        check_pending(now);

        auto lock = std::unique_lock{ cache_mutex_ };
        for (auto const& [key, cache] : cache_)
        {
            auto const& [host, port, family, protocol] = key;

            if (family_wanted && *family_wanted != family)
            {
                continue;
            }

            if (protocol_wanted && *protocol_wanted != protocol)
            {
                continue;
            }

            func({ host, port, family, protocol, cache.result, cache.addr.first, cache.addr.second });
        }
    }

private:
    using Key = std::tuple<std::string, tr_port, Family, Protocol>;
    using Sockaddr = std::pair<sockaddr_storage, socklen_t>;
    using MaybeSockaddr = std::optional<Sockaddr>;

    struct Entry
    {
        Sockaddr addr = {};
        time_t created_at = {};
        Result result = Result::Failed;
    };

    [[nodiscard]] static MaybeSockaddr lookup(std::string host, tr_port port, Family family, Protocol protocol)
    {
        auto szport = std::array<char, 16>{};
        *fmt::format_to(std::data(szport), FMT_STRING("{:d}"), port.host()) = '\0';

        auto hints = addrinfo{};
        hints.ai_family = family == Family::IPv4 ? AF_INET : AF_INET6;
        hints.ai_protocol = protocol == Protocol::TCP ? IPPROTO_TCP : IPPROTO_UDP;
        hints.ai_socktype = protocol == Protocol::TCP ? SOCK_STREAM : SOCK_DGRAM;

        addrinfo* info = nullptr;
        if (int const rc = getaddrinfo(host.c_str(), std::data(szport), &hints, &info); rc != 0)
        {
            return {};
        }

        auto ss = sockaddr_storage{};
        auto const len = info->ai_addrlen;
        memcpy(&ss, info->ai_addr, len);
        freeaddrinfo(info);

        return std::make_pair(ss, len);
    }

    void check_pending(time_t now) const
    {
        auto const pending_lock = std::unique_lock{ pending_mutex_ };
        auto cache_lock = std::unique_lock{ cache_mutex_, std::defer_lock };

        for (auto iter = std::begin(pending_); iter != std::end(pending_);)
        {
            if (auto& [key, fut] = *iter; fut.wait_for(std::chrono::milliseconds{ 0 }) == std::future_status::ready)
            {
                if (!cache_lock)
                {
                    cache_lock.lock();
                }

                auto const addr = fut.get();
                cache_[key] = addr ? Entry{ *addr, now, Result::Success } : Entry{ {}, now, Result::Failed };
                iter = pending_.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
    }

    static inline constexpr auto CacheTtlSecs = time_t{ 3600 };

    mutable std::mutex pending_mutex_;
    mutable std::map<Key, std::future<MaybeSockaddr>> pending_;

    mutable std::mutex cache_mutex_;
    mutable std::map<Key, Entry> cache_;
};

} // namespace libtransmission
