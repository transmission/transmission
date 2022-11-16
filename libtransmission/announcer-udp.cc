// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // for std::find_if()
#include <cerrno> // for errno, EAFNOSUPPORT
#include <cstring> // for memset()
#include <ctime>
#include <list>
#include <memory>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"

#include "announcer.h"
#include "announcer-common.h"
#include "crypto-utils.h" /* tr_rand_buffer() */
#include "log.h"
#include "peer-io.h"
#include "peer-mgr.h" // for tr_pex::fromCompact4()
#include "session.h"
#include "tr-assert.h"
#include "tr-buffer.h"
#include "utils.h"
#include "web-utils.h"

#define logwarn(interned, msg) tr_logAddWarn(msg, (interned).sv())
#define logdbg(interned, msg) tr_logAddDebug(msg, (interned).sv())
#define logtrace(interned, msg) tr_logAddTrace(msg, (interned).sv())

using namespace std::literals;

// size defined by bep15
using tau_connection_t = uint64_t;
using tau_transaction_t = uint32_t;

static auto constexpr TauConnectionTtlSecs = int{ 60 };

static tau_transaction_t tau_transaction_new()
{
    auto tmp = tau_transaction_t{};
    tr_rand_buffer(&tmp, sizeof(tau_transaction_t));
    return tmp;
}

// used in the "action" field of a request. Values defined in bep 15.
enum tau_action_t
{
    TAU_ACTION_CONNECT = 0,
    TAU_ACTION_ANNOUNCE = 1,
    TAU_ACTION_SCRAPE = 2,
    TAU_ACTION_ERROR = 3
};

/****
*****  SCRAPE
****/

struct tau_scrape_request
{
    tau_scrape_request(tr_scrape_request const& in, tr_scrape_response_func callback, void* user_data)
        : callback_{ callback }
        , user_data_{ user_data }
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
        buf.addUint32(TAU_ACTION_SCRAPE);
        buf.addUint32(transaction_id);
        for (int i = 0; i < in.info_hash_count; ++i)
        {
            buf.add(in.info_hash[i]);
        }
        this->payload.insert(std::end(this->payload), std::begin(buf), std::end(buf));
    }

    [[nodiscard]] constexpr auto hasCallback() const noexcept
    {
        return callback_ != nullptr;
    }

    void requestFinished()
    {
        if (callback_ != nullptr)
        {
            callback_(&response, user_data_);
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
                row.seeders = buf.toUint32();
                row.downloads = buf.toUint32();
                row.leechers = buf.toUint32();
            }

            requestFinished();
        }
        else
        {
            std::string const errmsg = action == TAU_ACTION_ERROR && !std::empty(buf) ? buf.toString() : _("Unknown error");
            fail(true, false, errmsg);
        }
    }

    std::vector<std::byte> payload;

    time_t sent_at = 0;
    time_t created_at = tr_time();
    tau_transaction_t transaction_id = tau_transaction_new();

    tr_scrape_response response = {};

private:
    tr_scrape_response_func callback_;
    void* user_data_;
};

/****
*****  ANNOUNCE
****/

struct tau_announce_request
{
    tau_announce_request(
        uint32_t announce_ip,
        tr_announce_request const& in,
        tr_announce_response_func callback,
        void* user_data)
        : callback_{ callback }
        , user_data_{ user_data }
    {
        response.seeders = -1;
        response.leechers = -1;
        response.downloads = -1;
        response.info_hash = in.info_hash;

        // build the payload
        auto buf = libtransmission::Buffer{};
        buf.addUint32(TAU_ACTION_ANNOUNCE);
        buf.addUint32(transaction_id);
        buf.add(in.info_hash);
        buf.add(in.peer_id);
        buf.addUint64(in.down);
        buf.addUint64(in.leftUntilComplete);
        buf.addUint64(in.up);
        buf.addUint32(get_tau_announce_event(in.event));
        buf.addUint32(announce_ip);
        buf.addUint32(in.key);
        buf.addUint32(in.numwant);
        buf.addPort(in.port);
        payload.insert(std::end(payload), std::begin(buf), std::end(buf));
    }

    [[nodiscard]] constexpr auto hasCallback() const noexcept
    {
        return callback_ != nullptr;
    }

    void requestFinished()
    {
        if (callback_ != nullptr)
        {
            callback_(&this->response, user_data_);
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
            response.interval = buf.toUint32();
            response.leechers = buf.toUint32();
            response.seeders = buf.toUint32();

            auto const contiguous = std::vector<std::byte>{ std::begin(buf), std::end(buf) };
            response.pex = tr_pex::fromCompact4(std::data(contiguous), std::size(contiguous), nullptr, 0);
            requestFinished();
        }
        else
        {
            std::string const errmsg = action == TAU_ACTION_ERROR && !std::empty(buf) ? buf.toString() : _("Unknown error");
            fail(true, false, errmsg);
        }
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

    time_t created_at = tr_time();
    time_t sent_at = 0;
    tau_transaction_t transaction_id = tau_transaction_new();

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

    tr_announce_response_func callback_ = nullptr;
    void* user_data_ = nullptr;
};

/****
*****  TRACKER
****/

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

    [[nodiscard]] auto isIdle() const noexcept
    {
        return std::empty(announces) && std::empty(scrapes) && (dns_request_ == 0U);
    }

    void sendto(void const* buf, size_t buflen)
    {
        TR_ASSERT(addr_);
        if (!addr_)
        {
            return;
        }

        auto [ss, sslen] = *addr_;

        if (ss.ss_family == AF_INET)
        {
            reinterpret_cast<sockaddr_in*>(&ss)->sin_port = port.network();
        }
        else if (ss.ss_family == AF_INET6)
        {
            reinterpret_cast<sockaddr_in6*>(&ss)->sin6_port = port.network();
        }

        mediator_.sendto(buf, buflen, reinterpret_cast<sockaddr*>(&ss), sslen);
    }

    void on_connection_response(tau_action_t action, libtransmission::Buffer& buf)
    {
        this->connecting_at = 0;
        this->connection_transaction_id = 0;

        if (action == TAU_ACTION_CONNECT)
        {
            this->connection_id = buf.toUint64();
            this->connection_expiration_time = tr_time() + TauConnectionTtlSecs;
            logdbg(this->key, fmt::format("Got a new connection ID from tracker: {}", this->connection_id));
        }
        else if (action == TAU_ACTION_ERROR)
        {
            std::string const errmsg = !std::empty(buf) ? buf.toString() : _("Connection failed");
            logdbg(this->key, errmsg);
            this->failAll(true, false, errmsg);
        }

        this->upkeep();
    }

    void upkeep(bool timeout_reqs = true)
    {
        time_t const now = tr_time();
        bool const closing = this->close_at != 0;

        /* if the address info is too old, expire it */
        if (this->addr_ && (closing || this->addr_expires_at_ <= now))
        {
            logtrace(this->host, "Expiring old DNS result");
            this->addr_.reset();
            this->addr_expires_at_ = 0;
        }

        /* are there any requests pending? */
        if (this->isIdle())
        {
            return;
        }

        // if DNS lookup *recently* failed for this host, do nothing
        if (!this->addr_ && now < this->addr_expires_at_)
        {
            return;
        }

        /* if we don't have an address yet, try & get one now. */
        if (!closing && !this->addr_ && (this->dns_request_ == 0U))
        {
            auto hints = libtransmission::Dns::Hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            logtrace(this->host, "Trying a new DNS lookup");
            this->dns_request_ = mediator_.dns().lookup(
                this->host.sv(),
                [this](sockaddr const* sa, socklen_t len, time_t expires_at) { this->on_dns(sa, len, expires_at); },
                hints);
            return;
        }

        logtrace(
            this->key,
            fmt::format(
                "connected {} ({} {}) -- connecting_at {}",
                this->connection_expiration_time > now,
                this->connection_expiration_time,
                now,
                this->connecting_at));

        /* also need a valid connection ID... */
        if (this->addr_ && this->connection_expiration_time <= now && this->connecting_at == 0)
        {
            this->connecting_at = now;
            this->connection_transaction_id = tau_transaction_new();
            logtrace(this->key, fmt::format("Trying to connect. Transaction ID is {}", this->connection_transaction_id));

            auto buf = libtransmission::Buffer{};
            buf.addUint64(0x41727101980LL);
            buf.addUint32(TAU_ACTION_CONNECT);
            buf.addUint32(this->connection_transaction_id);

            auto const contiguous = std::vector<std::byte>(std::begin(buf), std::end(buf));
            this->sendto(std::data(contiguous), std::size(contiguous));

            return;
        }

        if (timeout_reqs)
        {
            timeout_requests();
        }

        if (this->addr_ && this->connection_expiration_time > now)
        {
            send_requests();
        }
    }

private:
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

    void on_dns(sockaddr const* sa, socklen_t salen, time_t expires_at)
    {
        this->dns_request_ = {};

        if (sa == nullptr)
        {
            auto const errmsg = fmt::format(_("Couldn't find address of tracker '{host}'"), fmt::arg("host", this->host));
            logwarn(this->key, errmsg);
            this->failAll(false, false, errmsg.c_str());
            this->addr_expires_at_ = tr_time() + tau_tracker::DnsRetryIntervalSecs;
        }
        else
        {
            logdbg(this->key, "DNS lookup succeeded");
            auto ss = sockaddr_storage{};
            memcpy(&ss, sa, salen);
            this->addr_.emplace(ss, salen);
            this->addr_expires_at_ = expires_at;
            upkeep();
        }
    }

    ///

    void timeout_requests()
    {
        time_t const now = time(nullptr);
        bool const cancel_all = this->close_at != 0 && (this->close_at <= now);

        if (this->connecting_at != 0 && this->connecting_at + TauRequestTtl < now)
        {
            auto empty_buf = libtransmission::Buffer{};
            on_connection_response(TAU_ACTION_ERROR, empty_buf);
        }

        timeout_requests(this->announces, now, cancel_all, "announce");
        timeout_requests(this->scrapes, now, cancel_all, "scrape");
    }

    template<typename T>
    void timeout_requests(std::list<T>& requests, time_t now, bool cancel_all, std::string_view name)
    {
        for (auto it = std::begin(requests); it != std::end(requests);)
        {
            auto& req = *it;
            if (cancel_all || req.created_at + TauRequestTtl < now)
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
        TR_ASSERT(!this->dns_request_);
        TR_ASSERT(this->addr_);
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

            if (req.hasCallback())
            {
                ++it;
                continue;
            }

            // no response needed, so we can remove it now
            it = reqs.erase(it);
        }
    }

    void send_request(void const* payload, size_t payload_len)
    {
        logdbg(this->key, fmt::format("sending request w/connection id {}", this->connection_id));

        auto buf = libtransmission::Buffer{};
        buf.addUint64(this->connection_id);
        buf.add(payload, payload_len);

        auto const contiguous = std::vector<std::byte>(std::begin(buf), std::end(buf));
        this->sendto(std::data(contiguous), std::size(contiguous));
    }

public:
    tr_interned_string const key;
    tr_interned_string const host;
    tr_port const port;

    libtransmission::Dns::Tag dns_request_ = {};

    time_t connecting_at = 0;
    time_t connection_expiration_time = 0;
    tau_connection_t connection_id = {};
    tau_transaction_t connection_transaction_id = {};

    time_t close_at = 0;

    std::list<tau_announce_request> announces;
    std::list<tau_scrape_request> scrapes;

private:
    Mediator& mediator_;

    std::optional<std::pair<sockaddr_storage, socklen_t>> addr_;
    time_t addr_expires_at_ = 0;

    static time_t constexpr DnsRetryIntervalSecs = 60 * 60;
    static auto constexpr TauRequestTtl = int{ 60 };
};

/****
*****  SESSION
****/

class tr_announcer_udp_impl final : public tr_announcer_udp
{
public:
    explicit tr_announcer_udp_impl(Mediator& mediator)
        : mediator_{ mediator }
    {
    }

    void announce(tr_announce_request const& request, tr_announce_response_func response_func, void* user_data) override
    {
        auto* const tracker = getTrackerFromUrl(request.announce_url);
        if (tracker == nullptr)
        {
            return;
        }

        // Since size of IP field is only 4 bytes long, we can only announce IPv4 addresses
        auto const addr = mediator_.announceIP();
        uint32_t const announce_ip = addr && addr->isIPv4() ? addr->addr.addr4.s_addr : 0;
        tracker->announces.emplace_back(announce_ip, request, response_func, user_data);
        tracker->upkeep(false);
    }

    void scrape(tr_scrape_request const& request, tr_scrape_response_func response_func, void* user_data) override
    {
        auto* const tracker = getTrackerFromUrl(request.scrape_url);
        if (tracker == nullptr)
        {
            return;
        }

        tracker->scrapes.emplace_back(request, response_func, user_data);
        tracker->upkeep(false);
    }

    void upkeep() override
    {
        for (auto& tracker : trackers_)
        {
            tracker.upkeep();
        }
    }

    [[nodiscard]] bool isIdle() const noexcept override
    {
        return std::all_of(std::begin(trackers_), std::end(trackers_), [](auto const& tracker) { return tracker.isIdle(); });
    }

    // Start shutting down.
    // This doesn't destroy everything if there are requests,
    // but sets a deadline on how much longer to wait for the remaining ones.
    void startShutdown() override
    {
        auto const now = time(nullptr);

        for (auto& tracker : trackers_)
        {
            // if there's a pending DNS request, cancel it
            if (tracker.dns_request_ != 0U)
            {
                mediator_.dns().cancel(tracker.dns_request_);
            }

            tracker.close_at = now + 3;
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
        auto const action_id = static_cast<tau_action_t>(buf.toUint32());

        if (!isResponseMessage(action_id, msglen))
        {
            return false;
        }

        /* extract the transaction_id and look for a match */
        tau_transaction_t const transaction_id = buf.toUint32();

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

std::unique_ptr<tr_announcer_udp> tr_announcer_udp::create(Mediator& mediator)
{
    return std::make_unique<tr_announcer_udp_impl>(mediator);
}
