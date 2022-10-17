// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstring> // for std::memcpy()
#include <ctime>
#include <list>
#include <map>
#include <memory>
#include <utility>

#include <event2/dns.h>
#include <event2/event.h>

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
        sockaddr_storage ss_ = {};
        socklen_t sslen_ = {};
        time_t expires_at_ = {};
    };

    struct CallbackArg
    {
        Key key;
        EvDns* self;
    };

    struct Request
    {
        evdns_getaddrinfo_request* request;

        struct CallbackInfo
        {
            CallbackInfo(Tag tag, Callback callback)
                : tag_{ tag }
                , callback_{ std::move(callback) }
            {
            }

            Tag tag_;
            Callback callback_;
        };

        std::list<CallbackInfo> callbacks;
    };

public:
    using TimeFunc = time_t (*)();

    EvDns(struct event_base* event_base, TimeFunc time_func)
        : time_func_{ time_func }
        , evdns_base_{ evdns_base_new(event_base, EVDNS_BASE_INITIALIZE_NAMESERVERS),
                       [](evdns_base* dns)
                       {
                           // if zero, active requests will be aborted
                           evdns_base_free(dns, 0);
                       } }
    {
    }

    ~EvDns() override
    {
        for (auto& [key, request] : requests_)
        {
            evdns_getaddrinfo_cancel(request.request);
        }
    }

    std::optional<std::pair<sockaddr const*, socklen_t>> cached(std::string_view address, Hints hints = {}) const override
    {
        if (auto const* entry = cached(makeKey(address, hints)); entry != nullptr)
        {
            return std::make_pair(reinterpret_cast<sockaddr const*>(&entry->ss_), entry->sslen_);
        }

        return {};
    }

    Tag lookup(std::string_view address, Callback&& callback, Hints hints = {}) override
    {
        auto const key = makeKey(address, hints);

        if (auto const* entry = cached(key); entry)
        {
            callback(reinterpret_cast<sockaddr const*>(&entry->ss_), entry->sslen_, entry->expires_at_);
            return {};
        }

        auto& request = requests_[key];
        auto const tag = next_tag_++;
        request.callbacks.emplace_back(tag, std::move(callback));
        if (request.request == nullptr)
        {
            auto evhints = evutil_addrinfo{};
            evhints.ai_family = hints.ai_family;
            evhints.ai_socktype = hints.ai_socktype;
            evhints.ai_protocol = hints.ai_protocol;
            void* const arg = new CallbackArg{ key, this };
            request.request = evdns_getaddrinfo(evdns_base_.get(), key.first.c_str(), nullptr, &evhints, evcallback, arg);
        }

        return tag;
    }

    void cancel(Tag tag) override
    {
        for (auto& [key, request] : requests_)
        {
            for (auto iter = std::begin(request.callbacks), end = std::end(request.callbacks); iter != end; ++iter)
            {
                if (iter->tag_ != tag)
                {
                    continue;
                }

                iter->callback_(nullptr, 0, 0);

                request.callbacks.erase(iter);

                // if this was the last pending request for `key`, cancel the evdns request
                if (std::empty(request.callbacks))
                {
                    evdns_getaddrinfo_cancel(request.request);
                    requests_.erase(key);
                }

                return;
            }
        }
    }

private:
    [[nodiscard]] static Key makeKey(std::string_view address, Hints hints)
    {
        return Key{ tr_strlower(address), hints };
    }

    [[nodiscard]] CacheEntry const* cached(Key const& key) const
    {
        if (auto iter = cache_.find(key); iter != std::end(cache_))
        {
            auto const& entry = iter->second;

            if (auto const now = time_func_(); entry.expires_at_ > now)
            {
                return &entry;
            }

            cache_.erase(iter); // expired
        }

        return nullptr;
    }

    static void evcallback(int /*result*/, struct evutil_addrinfo* res, void* varg)
    {
        auto* const arg = static_cast<CallbackArg*>(varg);
        auto [key, self] = *arg;
        delete arg;

        auto& cache_entry = self->cache_[key];

        if (res != nullptr)
        {
            cache_entry.expires_at_ = self->time_func_() + CacheTtlSecs;
            cache_entry.sslen_ = res->ai_addrlen;
            std::memcpy(&cache_entry.ss_, res->ai_addr, res->ai_addrlen);
            evutil_freeaddrinfo(res);
        }

        if (auto request_entry = self->requests_.extract(key); request_entry)
        {
            for (auto& callback : request_entry.mapped().callbacks)
            {
                callback.callback_(
                    reinterpret_cast<sockaddr const*>(&cache_entry.ss_),
                    cache_entry.sslen_,
                    cache_entry.expires_at_);
            }
        }
    }

    TimeFunc const time_func_;
    static time_t constexpr CacheTtlSecs = 3600U;
    std::unique_ptr<evdns_base, void (*)(evdns_base*)> const evdns_base_;
    mutable std::map<Key, CacheEntry> cache_;
    std::map<Key, Request> requests_;
    unsigned int next_tag_ = 1;
};

} // namespace libtransmission
