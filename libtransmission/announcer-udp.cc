// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // for std::find_if()
#include <cerrno> // for errno, EAFNOSUPPORT
#include <climits> // for CHAR_BIT
#include <cstring> // for memset()
#include <ctime>
#include <future>
#include <list>
#include <memory>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#undef gai_strerror
#define gai_strerror gai_strerrorA
#endif

#include <fmt/core.h>
#include <fmt/format.h>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"

#include "announcer.h"
#include "announcer-common.h"
#include "crypto-utils.h" // for tr_rand_obj()
#include "log.h"
#include "peer-io.h"
#include "peer-mgr.h" // for tr_pex::fromCompact4()
#include "tr-assert.h"
#include "tr-buffer.h"
#include "utils.h"
#include "web-utils.h"

#define logwarn(interned, msg) tr_logAddWarn(msg, (interned).sv())
#define logdbg(interned, msg) tr_logAddDebug(msg, (interned).sv())
#define logtrace(interned, msg) tr_logAddTrace(msg, (interned).sv())

namespace
{
using namespace std::literals;

// size defined by bep15
using tau_connection_t = uint64_t;
using tau_transaction_t = uint32_t;

constexpr auto TauConnectionTtlSecs = time_t{ 45 };

auto tau_transaction_new()
{
    return tr_rand_obj<tau_transaction_t>();
}

// used in the "action" field of a request. Values defined in bep 15.
enum tau_action_t
{
    TAU_ACTION_CONNECT = 0,
    TAU_ACTION_ANNOUNCE = 1,
    TAU_ACTION_SCRAPE = 2,
    TAU_ACTION_ERROR = 3
};

// --- SCRAPE

struct tau_scrape_request
{
    tau_scrape_request(tr_scrape_request const& in, tr_scrape_response_func on_response)
        : on_response_{ std::move(on_response) }
    {
        this->response.scrape_url = in.scrape_url;
        this->response.row_count = in.info_hash_count;
        for (int i = 0; i < this->response.row_count; ++i)
        {
            this->response.rows[i].seeders = -1;
            this->response.rows[i].leechers = -1;
            this->response.rows[i].downloads = -1;
            this->response.rows[i].info_hash = in.info_hash[i];
        }

        // build the payload
        auto buf = libtransmission::Buffer{};
        buf.add_uint32(TAU_ACTION_SCRAPE);
        buf.add_uint32(transaction_id);
        for (int i = 0; i < in.info_hash_count; ++i)
        {
            buf.add(in.info_hash[i]);
        }
        this->payload.insert(std::end(this->payload), std::begin(buf), std::end(buf));
    }

    [[nodiscard]] auto has_callback() const noexcept
    {
        return !!on_response_;
    }

    void requestFinished() const
    {
        if (on_response_)
        {
            on_response_(response);
        }
    }

    void fail(bool did_connect, bool did_timeout, std::string_view errmsg)
    {
        response.did_connect = did_connect;
        response.did_timeout = did_timeout;
        response.errmsg = errmsg;
        requestFinished();
    }

    void onResponse(tau_action_t action, libtransmission::Buffer& buf)
    {
        response.did_connect = true;
        response.did_timeout = false;

        if (action == TAU_ACTION_SCRAPE)
        {
            for (int i = 0; i < response.row_count; ++i)
            {
                if (std::size(buf) < sizeof(uint32_t) * 3)
                {
                    break;
                }

                auto& row = response.rows[i];
                row.seeders = buf.to_uint32();
                row.downloads = buf.to_uint32();
                row.leechers = buf.to_uint32();
            }

            requestFinished();
        }
        else
        {
            std::string const errmsg = action == TAU_ACTION_ERROR && !std::empty(buf) ? buf.to_string() : _("Unknown error");
            fail(true, false, errmsg);
        }
    }

    [[nodiscard]] constexpr auto expiresAt() const noexcept
    {
        return created_at_ + TR_SCRAPE_TIMEOUT_SEC.count();
    }

    std::vector<std::byte> payload;

    time_t sent_at = 0;
    tau_transaction_t const transaction_id = tau_transaction_new();

    tr_scrape_response response = {};

private:
    time_t const created_at_ = tr_time();

    tr_scrape_response_func on_response_;
};

// --- ANNOUNCE

struct tau_announce_request
{
    tau_announce_request(
        std::optional<tr_address> announce_ip,
        tr_announce_request const& in,
        tr_announce_response_func on_response)
        : on_response_{ std::move(on_response) }
    {
        // https://www.bittorrent.org/beps/bep_0015.html sets key size at 32 bits
        static_assert(sizeof(tr_announce_request::key) * CHAR_BIT == 32);

        response.seeders = -1;
        response.leechers = -1;
        response.downloads = -1;
        response.info_hash = in.info_hash;

        // build the payload
        auto buf = libtransmission::Buffer{};
        buf.add_uint32(TAU_ACTION_ANNOUNCE);
        buf.add_uint32(transaction_id);
        buf.add(in.info_hash);
        buf.add(in.peer_id);
        buf.add_uint64(in.down);
        buf.add_uint64(in.leftUntilComplete);
        buf.add_uint64(in.up);
        buf.add_uint32(get_tau_announce_event(in.event));
        if (announce_ip && announce_ip->is_ipv4())
        {
            buf.add_address(*announce_ip);
        }
        else
        {
            buf.add_uint32(0U);
        }
        buf.add_uint32(in.key);
        buf.add_uint32(in.numwant);
        buf.add_port(in.port);
        payload.insert(std::end(payload), std::begin(buf), std::end(buf));
    }

    [[nodiscard]] auto has_callback() const noexcept
    {
        return !!on_response_;
    }

    void requestFinished() const
    {
        if (on_response_)
        {
            on_response_(this->response);
        }
    }

    void fail(bool did_connect, bool did_timeout, std::string_view errmsg)
    {
        this->response.did_connect = did_connect;
        this->response.did_timeout = did_timeout;
        this->response.errmsg = errmsg;
        this->requestFinished();
    }

    void onResponse(tau_action_t action, libtransmission::Buffer& buf)
    {
        auto const buflen = std::size(buf);

        this->response.did_connect = true;
        this->response.did_timeout = false;

        if (action == TAU_ACTION_ANNOUNCE && buflen >= 3 * sizeof(uint32_t))
        {
            response.interval = buf.to_uint32();
            response.leechers = buf.to_uint32();
            response.seeders = buf.to_uint32();

            auto const [bytes, n_bytes] = buf.pullup();
            response.pex = tr_pex::from_compact_ipv4(bytes, n_bytes, nullptr, 0);
            requestFinished();
        }
        else
        {
            std::string const errmsg = action == TAU_ACTION_ERROR && !std::empty(buf) ? buf.to_string() : _("Unknown error");
            fail(true, false, errmsg);
        }
    }

    [[nodiscard]] constexpr auto expiresAt() const noexcept
    {
        return created_at_ + TR_ANNOUNCE_TIMEOUT_SEC.count();
    }

    enum tau_announce_event
    {
        // Used in the "event" field of an announce request.
        // These values come from BEP 15
        TAU_ANNOUNCE_EVENT_NONE = 0,
        TAU_ANNOUNCE_EVENT_COMPLETED = 1,
        TAU_ANNOUNCE_EVENT_STARTED = 2,
        TAU_ANNOUNCE_EVENT_STOPPED = 3
    };

    std::vector<std::byte> payload;

    time_t sent_at = 0;
    tau_transaction_t const transaction_id = tau_transaction_new();

    tr_announce_response response = {};

private:
    [[nodiscard]] static constexpr tau_announce_event get_tau_announce_event(tr_announce_event e)
    {
        switch (e)
        {
        case TR_ANNOUNCE_EVENT_COMPLETED:
            return TAU_ANNOUNCE_EVENT_COMPLETED;

        case TR_ANNOUNCE_EVENT_STARTED:
            return TAU_ANNOUNCE_EVENT_STARTED;

        case TR_ANNOUNCE_EVENT_STOPPED:
            return TAU_ANNOUNCE_EVENT_STOPPED;

        default:
            return TAU_ANNOUNCE_EVENT_NONE;
        }
    }

    time_t const created_at_ = tr_time();

    tr_announce_response_func on_response_;
};

// --- TRACKER

struct tau_tracker
{
    using Mediator = tr_announcer_udp::Mediator;

    tau_tracker(Mediator& mediator, tr_interned_string key_in, tr_interned_string host_in, tr_port port_in)
        : key{ key_in }
        , host{ host_in }
        , port{ port_in }
        , mediator_{ mediator }
    {
    }

    void sendto(std::byte const* buf, size_t buflen)
    {
        TR_ASSERT(addr_);
        if (!addr_)
        {
            return;
        }

        auto const& [ss, sslen] = *addr_;
        mediator_.sendto(buf, buflen, reinterpret_cast<sockaddr const*>(&ss), sslen);
    }

    void on_connection_response(tau_action_t action, libtransmission::Buffer& buf)
    {
        this->connecting_at = 0;
        this->connection_transaction_id = 0;

        if (action == TAU_ACTION_CONNECT)
        {
            this->connection_id = buf.to_uint64();
            this->connection_expiration_time = tr_time() + TauConnectionTtlSecs;
            logdbg(this->key, fmt::format("Got a new connection ID from tracker: {}", this->connection_id));
        }
        else if (action == TAU_ACTION_ERROR)
        {
            std::string const errmsg = !std::empty(buf) ? buf.to_string() : _("Connection failed");
            logdbg(this->key, errmsg);
            this->failAll(true, false, errmsg);
        }

        this->upkeep();
    }

    void upkeep(bool timeout_reqs = true)
    {
        time_t const now = tr_time();

        // do we have a DNS request that's ready?
        if (addr_pending_dns_ && addr_pending_dns_->wait_for(0ms) == std::future_status::ready)
        {
            addr_ = addr_pending_dns_->get();
            addr_pending_dns_.reset();
            addr_expires_at_ = now + DnsRetryIntervalSecs;
        }

        // are there any requests pending?
        if (this->isIdle())
        {
            return;
        }

        // update the addr if our lookup is past its shelf date
        if (!addr_pending_dns_ && addr_expires_at_ <= now)
        {
            addr_.reset();
            addr_pending_dns_ = std::async(std::launch::async, lookup, this->host, this->port, this->key);
            return;
        }

        logtrace(
            this->key,
            fmt::format(
                "connected {} ({} {}) -- connecting_at {}",
                is_connected(now),
                this->connection_expiration_time,
                now,
                this->connecting_at));

        /* also need a valid connection ID... */
        if (addr_ && !is_connected(now) && this->connecting_at == 0)
        {
            this->connecting_at = now;
            this->connection_transaction_id = tau_transaction_new();
            logtrace(this->key, fmt::format("Trying to connect. Transaction ID is {}", this->connection_transaction_id));

            auto buf = libtransmission::Buffer{};
            buf.add_uint64(0x41727101980LL);
            buf.add_uint32(TAU_ACTION_CONNECT);
            buf.add_uint32(this->connection_transaction_id);

            auto const [bytes, n_bytes] = buf.pullup();
            this->sendto(bytes, n_bytes);
        }

        if (timeout_reqs)
        {
            timeout_requests(now);
        }

        if (addr_ && is_connected(now))
        {
            send_requests();
        }
    }

private:
    using Sockaddr = std::pair<sockaddr_storage, socklen_t>;
    using MaybeSockaddr = std::optional<Sockaddr>;

    [[nodiscard]] constexpr bool is_connected(time_t now) const noexcept
    {
        return connection_id != tau_connection_t{} && now < connection_expiration_time;
    }

    [[nodiscard]] static MaybeSockaddr lookup(tr_interned_string host, tr_port port, tr_interned_string logname)
    {
        auto szport = std::array<char, 16>{};
        *fmt::format_to(std::data(szport), FMT_STRING("{:d}"), port.host()) = '\0';

        auto hints = addrinfo{};
        hints.ai_family = AF_INET; // https://github.com/transmission/transmission/issues/4719
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_socktype = SOCK_DGRAM;

        addrinfo* info = nullptr;
        if (int const rc = getaddrinfo(host.c_str(), std::data(szport), &hints, &info); rc != 0)
        {
            logwarn(
                logname,
                fmt::format(
                    _("Couldn't look up '{address}:{port}': {error} ({error_code})"),
                    fmt::arg("address", host.sv()),
                    fmt::arg("port", port.host()),
                    fmt::arg("error", gai_strerror(rc)),
                    fmt::arg("error_code", static_cast<int>(rc))));
            return {};
        }

        auto ss = sockaddr_storage{};
        auto const len = info->ai_addrlen;
        memcpy(&ss, info->ai_addr, len);
        freeaddrinfo(info);

        logdbg(logname, "DNS lookup succeeded");
        return std::make_pair(ss, len);
    }

    [[nodiscard]] bool isIdle() const noexcept
    {
        return std::empty(announces) && std::empty(scrapes) && !addr_pending_dns_;
    }

    void failAll(bool did_connect, bool did_timeout, std::string_view errmsg)
    {
        for (auto& req : this->scrapes)
        {
            req.fail(did_connect, did_timeout, errmsg);
        }

        for (auto& req : this->announces)
        {
            req.fail(did_connect, did_timeout, errmsg);
        }

        this->scrapes.clear();
        this->announces.clear();
    }

    ///

    void timeout_requests(time_t now)
    {
        if (this->connecting_at != 0 && this->connecting_at + ConnectionRequestTtl < now)
        {
            auto empty_buf = libtransmission::Buffer{};
            on_connection_response(TAU_ACTION_ERROR, empty_buf);
        }

        timeout_requests(this->announces, now, "announce");
        timeout_requests(this->scrapes, now, "scrape");
    }

    template<typename T>
    void timeout_requests(std::list<T>& requests, time_t now, std::string_view name)
    {
        for (auto it = std::begin(requests); it != std::end(requests);)
        {
            if (auto& req = *it; req.expiresAt() <= now)
            {
                logtrace(this->key, fmt::format("timeout {} req {}", name, fmt::ptr(&req)));
                req.fail(false, true, "");
                it = requests.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    ///

    void send_requests()
    {
        TR_ASSERT(!addr_pending_dns_);
        TR_ASSERT(addr_);
        TR_ASSERT(this->connecting_at == 0);
        TR_ASSERT(this->connection_expiration_time > tr_time());

        send_requests(this->announces);
        send_requests(this->scrapes);
    }

    template<typename T>
    void send_requests(std::list<T>& reqs)
    {
        auto const now = tr_time();

        for (auto it = std::begin(reqs); it != std::end(reqs);)
        {
            auto& req = *it;

            if (req.sent_at != 0) // it's already been sent; we're awaiting a response
            {
                ++it;
                continue;
            }

            logdbg(this->key, fmt::format("sending req {}", fmt::ptr(&req)));
            req.sent_at = now;
            send_request(std::data(req.payload), std::size(req.payload));

            if (req.has_callback())
            {
                ++it;
                continue;
            }

            // no response needed, so we can remove it now
            it = reqs.erase(it);
        }
    }

    void send_request(std::byte const* payload, size_t payload_len)
    {
        logdbg(this->key, fmt::format("sending request w/connection id {}", this->connection_id));

        auto buf = libtransmission::Buffer{};
        buf.add_uint64(this->connection_id);
        buf.add(payload, payload_len);

        auto const [bytes, n_bytes] = buf.pullup();
        this->sendto(bytes, n_bytes);
    }

public:
    tr_interned_string const key;
    tr_interned_string const host;
    tr_port const port;

    time_t connecting_at = 0;
    time_t connection_expiration_time = 0;
    tau_connection_t connection_id = {};
    tau_transaction_t connection_transaction_id = {};

    std::list<tau_announce_request> announces;
    std::list<tau_scrape_request> scrapes;

private:
    Mediator& mediator_;

    std::optional<std::future<MaybeSockaddr>> addr_pending_dns_ = {};

    MaybeSockaddr addr_ = {};
    time_t addr_expires_at_ = 0;

    static inline constexpr auto DnsRetryIntervalSecs = time_t{ 3600 };
    static inline constexpr auto ConnectionRequestTtl = int{ 30 };
};

// --- SESSION

class tr_announcer_udp_impl final : public tr_announcer_udp
{
public:
    explicit tr_announcer_udp_impl(Mediator& mediator)
        : mediator_{ mediator }
    {
    }

    void announce(tr_announce_request const& request, tr_announce_response_func on_response) override
    {
        auto* const tracker = getTrackerFromUrl(request.announce_url);
        if (tracker == nullptr)
        {
            return;
        }

        // Since size of IP field is only 4 bytes long, we can only announce IPv4 addresses
        tracker->announces.emplace_back(mediator_.announceIP(), request, std::move(on_response));
        tracker->upkeep(false);
    }

    void scrape(tr_scrape_request const& request, tr_scrape_response_func on_response) override
    {
        auto* const tracker = getTrackerFromUrl(request.scrape_url);
        if (tracker == nullptr)
        {
            return;
        }

        tracker->scrapes.emplace_back(request, std::move(on_response));
        tracker->upkeep(false);
    }

    void upkeep() override
    {
        for (auto& tracker : trackers_)
        {
            tracker.upkeep();
        }
    }

    // @brief process an incoming udp message if it's a tracker response.
    // @return true if msg was a tracker response; false otherwise
    bool handleMessage(uint8_t const* msg, size_t msglen) override
    {
        if (msglen < sizeof(uint32_t) * 2)
        {
            return false;
        }

        // extract the action_id and see if it makes sense
        auto buf = libtransmission::Buffer{};
        buf.add(msg, msglen);
        auto const action_id = static_cast<tau_action_t>(buf.to_uint32());

        if (!isResponseMessage(action_id, msglen))
        {
            return false;
        }

        /* extract the transaction_id and look for a match */
        tau_transaction_t const transaction_id = buf.to_uint32();

        for (auto& tracker : trackers_)
        {
            // is it a connection response?
            if (tracker.connecting_at != 0 && transaction_id == tracker.connection_transaction_id)
            {
                logtrace(tracker.key, fmt::format("{} is my connection request!", transaction_id));
                tracker.on_connection_response(action_id, buf);
                return true;
            }

            // is it a response to one of this tracker's announces?
            if (auto& reqs = tracker.announces; !std::empty(reqs))
            {
                auto it = std::find_if(
                    std::begin(reqs),
                    std::end(reqs),
                    [&transaction_id](auto const& req) { return req.transaction_id == transaction_id; });
                if (it != std::end(reqs))
                {
                    logtrace(tracker.key, fmt::format("{} is an announce request!", transaction_id));
                    auto req = *it;
                    it = reqs.erase(it);
                    req.onResponse(action_id, buf);
                    return true;
                }
            }

            // is it a response to one of this tracker's scrapes?
            if (auto& reqs = tracker.scrapes; !std::empty(reqs))
            {
                auto it = std::find_if(
                    std::begin(reqs),
                    std::end(reqs),
                    [&transaction_id](auto const& req) { return req.transaction_id == transaction_id; });
                if (it != std::end(reqs))
                {
                    logtrace(tracker.key, fmt::format("{} is a scrape request!", transaction_id));
                    auto req = *it;
                    it = reqs.erase(it);
                    req.onResponse(action_id, buf);
                    return true;
                }
            }
        }

        /* no match... */
        return false;
    }

private:
    // Finds the tau_tracker struct that corresponds to this url.
    // If it doesn't exist yet, create one.
    tau_tracker* getTrackerFromUrl(tr_interned_string announce_url)
    {
        // build a lookup key for this tracker
        auto const parsed = tr_urlParseTracker(announce_url);
        TR_ASSERT(parsed);
        if (!parsed)
        {
            return nullptr;
        }

        // see if we already have it
        auto const key = tr_announcerGetKey(*parsed);
        for (auto& tracker : trackers_)
        {
            if (tracker.key == key)
            {
                return &tracker;
            }
        }

        // we don't have it -- build a new one
        trackers_.emplace_back(mediator_, key, tr_interned_string(parsed->host), tr_port::fromHost(parsed->port));
        auto* const tracker = &trackers_.back();
        logtrace(tracker->key, "New tau_tracker created");
        return tracker;
    }

    [[nodiscard]] static constexpr bool isResponseMessage(tau_action_t action, size_t msglen) noexcept
    {
        if (action == TAU_ACTION_CONNECT)
        {
            return msglen == 16;
        }

        if (action == TAU_ACTION_ANNOUNCE)
        {
            return msglen >= 20;
        }

        if (action == TAU_ACTION_SCRAPE)
        {
            return msglen >= 20;
        }

        if (action == TAU_ACTION_ERROR)
        {
            return msglen >= 8;
        }

        return false;
    }

    std::list<tau_tracker> trackers_;

    Mediator& mediator_;
};

} // namespace

std::unique_ptr<tr_announcer_udp> tr_announcer_udp::create(Mediator& mediator)
{
    return std::make_unique<tr_announcer_udp_impl>(mediator);
}
