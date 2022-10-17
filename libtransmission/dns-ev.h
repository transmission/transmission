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
    EvDns(struct event_base* event_base)
        : evdns_base_{ evdns_base_new(event_base, EVDNS_BASE_INITIALIZE_NAMESERVERS),
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

    Tag lookup(std::string_view address, time_t now, Callback&& callback, Hints hints = {}) override
    {
        auto const key = std::make_pair(tr_strlower(address), hints);

        if (auto iter = cache_.find(key); iter != std::end(cache_))
        {
            auto const& entry = iter->second;

            if (entry.expiration_time > now)
            {
                callback(reinterpret_cast<struct sockaddr const*>(&entry.ss), entry.sslen);
                return {};
            }

            cache_.erase(iter); // expired
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

                iter->callback_(nullptr, 0);

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
    static void evcallback(int /*result*/, struct evutil_addrinfo* res, void* varg)
    {
        auto* const arg = static_cast<CallbackArg*>(varg);
        auto [key, self] = *arg;
        delete arg;

        struct sockaddr const* sa = nullptr;
        socklen_t salen = 0;

        if (res != nullptr)
        {
            sa = res->ai_addr;
            salen = res->ai_addrlen;
        }

        if (auto entry = self->requests_.extract(key); entry)
        {
            for (auto& callback : entry.mapped().callbacks)
            {
                callback.callback_(sa, salen);
            }
        }

        if (res != nullptr)
        {
            evutil_freeaddrinfo(res);
        }
    }

    std::unique_ptr<evdns_base, void (*)(evdns_base*)> const evdns_base_;
    std::map<Key, CacheEntry> cache_;
    std::map<Key, Request> requests_;
    unsigned int next_tag_ = 1;
};

} // namespace libtransmission
