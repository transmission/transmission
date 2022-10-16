// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <utility>

#include <event2/dns.h>
#include <event2/event.h>

#include <fmt/format.h>

#include "dns.h"
#include "utils.h" // for tr_strlower()

using namespace std::literals;

namespace libtransmission
{

class EvDns final : public Dns
{
private:
    using Key = std::pair<std::string, Hints>;

    struct CacheEntry
    {
        time_t expiration_time;
        sockaddr_storage ss;
        size_t sslen;
    };

    struct CallbackArg
    {
        Key key;
        EvDns* self;
    };

    struct Pending
    {
        evdns_getaddrinfo_request* request;
        std::list<Callback> callbacks;
    };

public:
    EvDns(struct event_base* event_base)
        : evdns_base_{ evdns_base_new(event_base, EVDNS_BASE_INITIALIZE_NAMESERVERS),
                   [](evdns_base* dns)
                   {
                       // if zero, active requests will be aborted
                       evdns_base_free(dns, 0);
                   } }
    { }

    ~EvDns() override
    {
        for (auto& [key, pending] : pending_)
        {
            fmt::print("cancel {}\n", key.first);
            evdns_getaddrinfo_cancel(pending.request);
        }
    }

    void lookup(std::string_view address, time_t now, Callback&& callback, Hints hints = {}) override
    {
        auto const key = std::make_pair(tr_strlower(address), hints);

        if (auto iter = cache_.find(key); iter != std::end(cache_))
        {
            auto const& entry = iter->second;

            if (entry.expiration_time > now)
            {
                fmt::print("cached {}\n", key.first);
                callback(reinterpret_cast<struct sockaddr const*>(&entry.ss), entry.sslen);
                return;
            }

            fmt::print("expired {}\n", key.first);
            cache_.erase(iter); // expired
        }

        auto& pending = pending_[key];;
        pending.callbacks.push_back(std::move(callback));
        if (pending.request == nullptr)
        {
            fmt::print("requesting {}\n", key.first);
            auto evhints = evutil_addrinfo{};
            evhints.ai_family = hints.ai_family;
            evhints.ai_socktype = hints.ai_socktype;
            evhints.ai_protocol = hints.ai_protocol;
            void* const arg = new CallbackArg{ key, this };
            fmt::print("pending {}\n", key.first);
            pending.request = evdns_getaddrinfo(evdns_base_.get(), key.first.c_str(), nullptr, &evhints, evcallback, arg);
        }
    }

private:
    static void evcallback(int /*result*/, struct evutil_addrinfo *res, void *varg)
    {
        auto* const arg = static_cast<CallbackArg*>(varg);
        auto [key, self] = *arg;
        fmt::print("evcalback {}\n", key.first);
        delete arg;

        struct sockaddr const* sa = nullptr;
        socklen_t salen = 0;

        if (res != nullptr)
        {
            sa = res->ai_addr;
            salen = res->ai_addrlen;
        }

        fmt::print("salen {}\n", salen);
        if (auto entry = self->pending_.extract(key); entry)
        {
            for (auto& callback : entry.mapped().callbacks)
            {
                fmt::print("calling callback salen {}\n", salen);
                callback(sa, salen);
            }
        }

        if (res != nullptr)
        {
            evutil_freeaddrinfo(res);
        }
    }

    std::unique_ptr<evdns_base, void(*)(evdns_base*)> const evdns_base_;
    std::map<Key, CacheEntry> cache_;
    std::map<Key, Pending> pending_;
};

} // namespace libtransmission
