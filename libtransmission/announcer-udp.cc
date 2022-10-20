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
#include "peer-mgr.h" /* tr_peerMgrCompactToPex() */
#include "session.h"
#include "tr-assert.h"
#include "tr-buffer.h"
#include "utils.h"
#include "web-utils.h"

#define logwarn(interned, msg) tr_logAddWarn(msg, (interned).sv())
#define logdbg(interned, msg) tr_logAddDebug(msg, (interned).sv())
#define logtrace(interned, msg) tr_logAddTrace(msg, (interned).sv())

using namespace std::literals;

using tau_connection_t = uint64_t;
using tau_transaction_t = uint32_t;

static auto constexpr TauConnectionTtlSecs = int{ 60 };

static tau_transaction_t tau_transaction_new()
{
    auto tmp = tau_transaction_t{};
    tr_rand_buffer(&tmp, sizeof(tau_transaction_t));
    return tmp;
}

/* used in the "action" field of a request */
enum tau_action_t
{
    TAU_ACTION_CONNECT = 0,
    TAU_ACTION_ANNOUNCE = 1,
    TAU_ACTION_SCRAPE = 2,
    TAU_ACTION_ERROR = 3
};

static bool is_tau_response_message(tau_action_t action, size_t msglen)
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

static auto constexpr TauRequestTtl = int{ 60 };

/****
*****
*****  SCRAPE
*****
****/

struct tau_scrape_request
{
    void requestFinished()
    {
        if (callback != nullptr)
        {
            callback(&response, user_data);
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

    time_t sent_at;
    time_t created_at;
    tau_transaction_t transaction_id;

    tr_scrape_response response;
    tr_scrape_response_func callback;
    void* user_data;
};

static tau_scrape_request make_tau_scrape_request(
    tr_scrape_request const& in,
    tr_scrape_response_func callback,
    void* user_data)
{
    tau_transaction_t const transaction_id = tau_transaction_new();

    /* build the payload */
    auto buf = libtransmission::Buffer{};
    buf.addUint32(TAU_ACTION_SCRAPE);
    buf.addUint32(transaction_id);
    for (int i = 0; i < in.info_hash_count; ++i)
    {
        buf.add(in.info_hash[i]);
    }

    // build the tau_scrape_request
    auto req = tau_scrape_request{};
    req.callback = callback;
    req.created_at = tr_time();
    req.transaction_id = transaction_id;
    req.callback = callback;
    req.user_data = user_data;
    req.response.scrape_url = in.scrape_url;
    req.response.row_count = in.info_hash_count;
    req.payload.insert(std::end(req.payload), std::begin(buf), std::end(buf));

    for (int i = 0; i < req.response.row_count; ++i)
    {
        req.response.rows[i].seeders = -1;
        req.response.rows[i].leechers = -1;
        req.response.rows[i].downloads = -1;
        req.response.rows[i].info_hash = in.info_hash[i];
    }

    /* cleanup */
    return req;
}

/****
*****
*****  ANNOUNCE
*****
****/

struct tau_announce_request
{
    void requestFinished()
    {
        if (this->callback != nullptr)
        {
            this->callback(&this->response, this->user_data);
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

            auto const compact_len = std::size(buf);
            auto contiguous = std::array<uint8_t, 576>{};
            buf.toBuf(std::data(contiguous), compact_len);
            response.pex = tr_peerMgrCompactToPex(std::data(contiguous), compact_len, nullptr, 0);
            requestFinished();
        }
        else
        {
            std::string const errmsg = action == TAU_ACTION_ERROR && !std::empty(buf) ? buf.toString() : _("Unknown error");
            fail(true, false, errmsg);
        }
    }

    std::vector<std::byte> payload;

    time_t created_at = 0;
    time_t sent_at = 0;
    tau_transaction_t transaction_id = 0;

    tr_announce_response response = {};

    tr_announce_response_func callback = nullptr;
    void* user_data = nullptr;
};

enum tau_announce_event
{
    /* used in the "event" field of an announce request */
    TAU_ANNOUNCE_EVENT_NONE = 0,
    TAU_ANNOUNCE_EVENT_COMPLETED = 1,
    TAU_ANNOUNCE_EVENT_STARTED = 2,
    TAU_ANNOUNCE_EVENT_STOPPED = 3
};

static tau_announce_event get_tau_announce_event(tr_announce_event e)
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

static tau_announce_request make_tau_announce_request(
    uint32_t announce_ip,
    tr_announce_request const& in,
    tr_announce_response_func callback,
    void* user_data)
{
    tau_transaction_t const transaction_id = tau_transaction_new();

    /* build the payload */
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
    buf.addUint16(in.port.host());

    /* build the tau_announce_request */
    auto req = tau_announce_request();
    req.created_at = tr_time();
    req.transaction_id = transaction_id;
    req.callback = callback;
    req.user_data = user_data;
    req.payload.insert(std::end(req.payload), std::begin(buf), std::end(buf));
    req.response.seeders = -1;
    req.response.leechers = -1;
    req.response.downloads = -1;
    req.response.info_hash = in.info_hash;

    return req;
}

/****
*****
*****  TRACKERS
*****
****/

struct tau_tracker
{
    using Mediator = tr_announcer_udp::Mediator;

    tau_tracker(Mediator& mediator, tr_interned_string key_in, tr_interned_string host_in, tr_port port_in)
        : mediator_{ mediator }
        , key{ key_in }
        , host{ host_in }
        , port{ port_in }
    {
    }

    [[nodiscard]] auto isIdle() const noexcept
    {
        return std::empty(announces) && std::empty(scrapes) && (dns_request_ == 0U);
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

    void sendto(void const* buf, size_t buflen)
    {
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

    Mediator& mediator_;

    tr_interned_string const key;
    tr_interned_string const host;
    tr_port const port;

    libtransmission::Dns::Tag dns_request_ = {};
    std::optional<std::pair<sockaddr_storage, socklen_t>> addr_;
    time_t addr_expires_at_ = 0;

    time_t connecting_at = 0;
    time_t connection_expiration_time = 0;
    tau_connection_t connection_id = 0;
    tau_transaction_t connection_transaction_id = 0;

    time_t close_at = 0;

    static time_t constexpr DnsRetryIntervalSecs = 60 * 60;

    std::list<tau_announce_request> announces;
    std::list<tau_scrape_request> scrapes;
};

static void tau_tracker_upkeep(struct tau_tracker* /*tracker*/);

static void tau_tracker_on_dns(tau_tracker* const tracker, sockaddr const* sa, socklen_t salen, time_t expires_at)
{
    tracker->dns_request_ = {};

    if (sa == nullptr)
    {
        auto const errmsg = fmt::format(_("Couldn't find address of tracker '{host}'"), fmt::arg("host", tracker->host));
        logwarn(tracker->key, errmsg);
        tracker->failAll(false, false, errmsg.c_str());
        tracker->addr_expires_at_ = tr_time() + tau_tracker::DnsRetryIntervalSecs;
    }
    else
    {
        logdbg(tracker->key, "DNS lookup succeeded");
        auto ss = sockaddr_storage{};
        memcpy(&ss, sa, salen);
        tracker->addr_.emplace(ss, salen);
        tracker->addr_expires_at_ = expires_at;
        tau_tracker_upkeep(tracker);
    }
}

static void tau_tracker_send_request(struct tau_tracker* tracker, void const* payload, size_t payload_len)
{
    logdbg(tracker->key, fmt::format("sending request w/connection id {}", tracker->connection_id));

    auto buf = libtransmission::Buffer{};
    buf.addUint64(tracker->connection_id);
    buf.add(payload, payload_len);

    auto const contiguous = std::vector<std::byte>(std::begin(buf), std::end(buf));
    tracker->sendto(std::data(contiguous), std::size(contiguous));
}

template<typename T>
static void tau_tracker_send_requests(tau_tracker* tracker, std::list<T>& reqs)
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

        logdbg(tracker->key, fmt::format("sending req {}", fmt::ptr(&req)));
        req.sent_at = now;
        tau_tracker_send_request(tracker, std::data(req.payload), std::size(req.payload));

        if (req.callback != nullptr)
        {
            ++it;
            continue;
        }

        // no response needed, so we can remove it now
        it = reqs.erase(it);
    }
}

static void tau_tracker_send_reqs(tau_tracker* tracker)
{
    TR_ASSERT(!tracker->dns_request_);
    TR_ASSERT(tracker->addr_);
    TR_ASSERT(tracker->connecting_at == 0);
    TR_ASSERT(tracker->connection_expiration_time > tr_time());

    tau_tracker_send_requests(tracker, tracker->announces);
    tau_tracker_send_requests(tracker, tracker->scrapes);
}

static void on_tracker_connection_response(struct tau_tracker& tracker, tau_action_t action, libtransmission::Buffer& buf)
{
    tracker.connecting_at = 0;
    tracker.connection_transaction_id = 0;

    if (action == TAU_ACTION_CONNECT)
    {
        tracker.connection_id = buf.toUint64();
        tracker.connection_expiration_time = tr_time() + TauConnectionTtlSecs;
        logdbg(tracker.key, fmt::format("Got a new connection ID from tracker: {}", tracker.connection_id));
    }
    else if (action == TAU_ACTION_ERROR)
    {
        std::string const errmsg = !std::empty(buf) ? buf.toString() : _("Connection failed");
        logdbg(tracker.key, errmsg);
        tracker.failAll(true, false, errmsg);
    }

    tau_tracker_upkeep(&tracker);
}

static void tau_tracker_timeout_reqs(struct tau_tracker* tracker)
{
    time_t const now = time(nullptr);
    bool const cancel_all = tracker->close_at != 0 && (tracker->close_at <= now);

    if (tracker->connecting_at != 0 && tracker->connecting_at + TauRequestTtl < now)
    {
        auto empty_buf = libtransmission::Buffer{};
        on_tracker_connection_response(*tracker, TAU_ACTION_ERROR, empty_buf);
    }

    if (auto& reqs = tracker->announces; !std::empty(reqs))
    {
        for (auto it = std::begin(reqs); it != std::end(reqs);)
        {
            auto& req = *it;
            if (cancel_all || req.created_at + TauRequestTtl < now)
            {
                logtrace(tracker->key, fmt::format("timeout announce req {}", fmt::ptr(&req)));
                req.fail(false, true, "");
                it = reqs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    if (auto& reqs = tracker->scrapes; !std::empty(reqs))
    {
        for (auto it = std::begin(reqs); it != std::end(reqs);)
        {
            auto& req = *it;
            if (cancel_all || req.created_at + TauRequestTtl < now)
            {
                logtrace(tracker->key, fmt::format("timeout scrape req {}", fmt::ptr(&req)));
                req.fail(false, true, "");
                it = reqs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

static void tau_tracker_upkeep_ex(struct tau_tracker* tracker, bool timeout_reqs)
{
    time_t const now = tr_time();
    bool const closing = tracker->close_at != 0;

    /* if the address info is too old, expire it */
    if (tracker->addr_ && (closing || tracker->addr_expires_at_ <= now))
    {
        logtrace(tracker->host, "Expiring old DNS result");
        tracker->addr_.reset();
        tracker->addr_expires_at_ = 0;
    }

    /* are there any requests pending? */
    if (tracker->isIdle())
    {
        return;
    }

    // if DNS lookup *recently* failed for this host, do nothing
    if (!tracker->addr_ && now < tracker->addr_expires_at_)
    {
        return;
    }

    /* if we don't have an address yet, try & get one now. */
    if (!closing && !tracker->addr_ && (tracker->dns_request_ == 0U))
    {
        auto hints = libtransmission::Dns::Hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        logtrace(tracker->host, "Trying a new DNS lookup");
        tracker->dns_request_ = tracker->mediator_.dns().lookup(
            tracker->host.sv(),
            [tracker](sockaddr const* sa, socklen_t len, time_t expires_at)
            { tau_tracker_on_dns(tracker, sa, len, expires_at); },
            hints);
        return;
    }

    logtrace(
        tracker->key,
        fmt::format(
            "connected {} ({} {}) -- connecting_at {}",
            tracker->connection_expiration_time > now,
            tracker->connection_expiration_time,
            now,
            tracker->connecting_at));

    /* also need a valid connection ID... */
    if (tracker->addr_ && tracker->connection_expiration_time <= now && tracker->connecting_at == 0)
    {
        tracker->connecting_at = now;
        tracker->connection_transaction_id = tau_transaction_new();
        logtrace(tracker->key, fmt::format("Trying to connect. Transaction ID is {}", tracker->connection_transaction_id));

        auto buf = libtransmission::Buffer{};
        buf.addUint64(0x41727101980LL);
        buf.addUint32(TAU_ACTION_CONNECT);
        buf.addUint32(tracker->connection_transaction_id);

        auto const contiguous = std::vector<std::byte>(std::begin(buf), std::end(buf));
        tracker->sendto(std::data(contiguous), std::size(contiguous));

        return;
    }

    if (timeout_reqs)
    {
        tau_tracker_timeout_reqs(tracker);
    }

    if (tracker->addr_ && tracker->connection_expiration_time > now)
    {
        tau_tracker_send_reqs(tracker);
    }
}

static void tau_tracker_upkeep(struct tau_tracker* tracker)
{
    tau_tracker_upkeep_ex(tracker, true);
}

/****
*****
*****  SESSION
*****
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
        tracker->announces.push_back(make_tau_announce_request(announce_ip, request, response_func, user_data));
        tau_tracker_upkeep_ex(tracker, false);
    }

    void scrape(tr_scrape_request const& request, tr_scrape_response_func response_func, void* user_data) override
    {
        auto* const tracker = getTrackerFromUrl(request.scrape_url);
        if (tracker == nullptr)
        {
            return;
        }

        tracker->scrapes.push_back(make_tau_scrape_request(request, response_func, user_data));
        tau_tracker_upkeep_ex(tracker, false);
    }

    void upkeep() override
    {
        for (auto& tracker : trackers_)
        {
            tau_tracker_upkeep(&tracker);
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
            tau_tracker_upkeep(&tracker);
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

        if (!is_tau_response_message(action_id, msglen))
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
                on_tracker_connection_response(tracker, action_id, buf);
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

    std::list<tau_tracker> trackers_;

    Mediator& mediator_;
};

std::unique_ptr<tr_announcer_udp> tr_announcer_udp::create(Mediator& mediator)
{
    return std::make_unique<tr_announcer_udp_impl>(mediator);
}
