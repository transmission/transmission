// Except where noted, this file Copyright © 2010-2023 Johannes Lieder.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <ctime>
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <netinet/in.h> /* sockaddr_in */
#endif

#include <event2/event.h>

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h" // for tr_rand_obj()
#include "log.h"
#include "net.h"
#include "timer.h"
#include "tr-assert.h"
#include "tr-lpd.h"
#include "utils.h" // for tr_net_init()
#include "utils-ev.h" // for tr_net_init()

using namespace std::literals;

// Code in this namespace Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only), MIT (SPDX: MIT),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.
namespace
{

// opaque value, allowing the sending client to filter out its
// own announces if it receives them via multicast loopback
auto makeCookie()
{
    static auto constexpr Pool = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"sv;

    auto buf = tr_rand_obj<std::array<char, 12>>();
    for (auto& ch : buf)
    {
        ch = Pool[static_cast<unsigned char>(ch) % std::size(Pool)];
    }

    return std::string{ std::data(buf), std::size(buf) };
}

constexpr char const* const McastGroup = "239.192.152.143"; /**<LPD multicast group */
auto constexpr McastPort = tr_port::fromHost(6771); /**<LPD source and destination UPD port */

/*
 * A LSD announce is formatted as follows:
 *
 * ```
 * BT-SEARCH * HTTP/1.1\r\n
 * Host: <host>\r\n
 * Port: <port>\r\n
 * Infohash: <ihash>\r\n
 * cookie: <cookie (optional)>\r\n
 * \r\n
 * \r\n
 * ```
 *
 * An announce may contain multiple, consecutive Infohash headers
 * to announce the participation in more than one torrent. This
 * may not be supported by older implementations. When sending
 * multiple infohashes the packet length should not exceed 1400
 * bytes to avoid MTU/fragmentation problems.
 */
auto makeAnnounceMsg(std::string_view cookie, tr_port port, std::vector<std::string_view> const& info_hash_strings)
{
    auto ret = fmt::format(
        "BT-SEARCH * HTTP/1.1\r\n"
        "Host: {:s}:{:d}\r\n"
        "Port: {:d}\r\n",
        McastGroup,
        McastPort.host(),
        port.host());

    for (auto const& info_hash : info_hash_strings)
    {
        ret += fmt::format("Infohash: {:s}\r\n", tr_strupper(info_hash));
    }

    if (!std::empty(cookie))
    {
        ret += fmt::format("cookie: {:s}\r\n", cookie);
    }

    return ret + "\r\n\r\n";
}

struct ParsedAnnounce
{
    int major;
    int minor;
    tr_port port;
    std::vector<std::string_view> info_hash_strings;
    std::string_view cookie;
};

std::optional<ParsedAnnounce> parseAnnounceMsg(std::string_view announce)
{
    static auto constexpr CrLf = "\r\n"sv;

    auto ret = ParsedAnnounce{};

    // get major, minor
    auto key = "BT-SEARCH * HTTP/"sv;
    if (auto const pos = announce.find(key); pos != std::string_view::npos)
    {
        // parse `${major}.${minor}`
        auto walk = announce.substr(pos + std::size(key));
        if (auto const major = tr_parseNum<int>(walk, &walk); major && tr_strvStartsWith(walk, '.'))
        {
            ret.major = *major;
        }
        else
        {
            return {};
        }

        walk.remove_prefix(1); // the '.' between major and minor
        if (auto const minor = tr_parseNum<int>(walk, &walk); minor && tr_strvStartsWith(walk, CrLf))
        {
            ret.minor = *minor;
        }
        else
        {
            return {};
        }
    }

    key = "Port: "sv;
    if (auto const pos = announce.find(key); pos != std::string_view::npos)
    {
        auto walk = announce.substr(pos + std::size(key));
        if (auto const port = tr_parseNum<uint16_t>(walk, &walk); port && tr_strvStartsWith(walk, CrLf))
        {
            ret.port = tr_port::fromHost(*port);
        }
        else
        {
            return {};
        }
    }

    key = "cookie: "sv;
    if (auto const pos = announce.find(key); pos != std::string_view::npos)
    {
        auto walk = announce.substr(pos + std::size(key));
        if (auto const end = walk.find(CrLf); end != std::string_view::npos)
        {
            ret.cookie = walk.substr(0, end);
        }
        else
        {
            return {};
        }
    }

    key = "Infohash: "sv;
    for (;;)
    {
        if (auto const pos = announce.find(key); pos != std::string_view::npos)
        {
            announce.remove_prefix(pos + std::size(key));
        }
        else
        {
            break;
        }

        if (auto const end = announce.find(CrLf); end != std::string_view::npos)
        {
            ret.info_hash_strings.push_back(announce.substr(0, end));
            announce.remove_prefix(end + std::size(CrLf));
        }
        else
        {
            return {};
        }
    }

    return ret;
}

} // namespace

class tr_lpd_impl final : public tr_lpd
{
public:
    tr_lpd_impl(Mediator& mediator, struct event_base* event_base)
        : mediator_{ mediator }
        , announce_timer_{ mediator.timerMaker().create([this]() { announceUpkeep(); }) }
        , dos_timer_{ mediator.timerMaker().create([this]() { dosUpkeep(); }) }
    {
        if (!init(event_base))
        {
            return;
        }

        announce_timer_->startRepeating(AnnounceInterval);
        announceUpkeep();
        dos_timer_->startRepeating(DosInterval);
        dosUpkeep();
    }

    tr_lpd_impl(tr_lpd_impl&&) = delete;
    tr_lpd_impl(tr_lpd_impl const&) = delete;
    tr_lpd_impl& operator=(tr_lpd_impl&&) = delete;
    tr_lpd_impl& operator=(tr_lpd_impl const&) = delete;

    ~tr_lpd_impl() override
    {
        event_.reset();

        if (mcast_rcv_socket_ != TR_BAD_SOCKET)
        {
            evutil_closesocket(mcast_rcv_socket_);
        }

        if (mcast_snd_socket_ != TR_BAD_SOCKET)
        {
            evutil_closesocket(mcast_snd_socket_);
        }

        tr_logAddTrace("Done uninitialising Local Peer Discovery");
    }

private:
    bool init(struct event_base* event_base)
    {
        if (initImpl(event_base))
        {
            return true;
        }

        auto const err = sockerrno;
        evutil_closesocket(mcast_rcv_socket_);
        evutil_closesocket(mcast_snd_socket_);
        mcast_rcv_socket_ = TR_BAD_SOCKET;
        mcast_snd_socket_ = TR_BAD_SOCKET;
        tr_logAddWarn(fmt::format(
            _("Couldn't initialize LPD: {error} ({error_code})"),
            fmt::arg("error", tr_strerror(err)),
            fmt::arg("error_code", err)));
        return false;
    }

    /**
     * @brief Initializes Local Peer Discovery for this node
     *
     * For the most part, this means setting up an appropriately configured multicast socket
     * and event-based message handling.
     *
     * @remark Since the LPD service does not use another protocol family yet, this code is
     * IPv4 only for the time being.
     */
    bool initImpl(struct event_base* event_base)
    {
        tr_net_init();

        int const opt_on = 1;

        static_assert(AnnounceScope > 0);

        tr_logAddDebug("Initialising Local Peer Discovery");

        /* setup datagram socket (receive) */
        {
            mcast_rcv_socket_ = socket(PF_INET, SOCK_DGRAM, 0);

            if (mcast_rcv_socket_ == TR_BAD_SOCKET)
            {
                return false;
            }

            if (evutil_make_socket_nonblocking(mcast_rcv_socket_) == -1)
            {
                return false;
            }

            if (setsockopt(
                    mcast_rcv_socket_,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    reinterpret_cast<char const*>(&opt_on),
                    sizeof(opt_on)) == -1)
            {
                return false;
            }

#if HAVE_SO_REUSEPORT
            if (setsockopt(
                    mcast_rcv_socket_,
                    SOL_SOCKET,
                    SO_REUSEPORT,
                    reinterpret_cast<char const*>(&opt_on),
                    sizeof(opt_on)) == -1)
            {
                return false;
            }
#endif

            mcast_addr_ = {};
            mcast_addr_.sin_family = AF_INET;
            mcast_addr_.sin_port = McastPort.network();
            mcast_addr_.sin_addr.s_addr = INADDR_ANY;

            if (bind(mcast_rcv_socket_, reinterpret_cast<sockaddr*>(&mcast_addr_), sizeof(mcast_addr_)) == -1)
            {
                return false;
            }

            if (evutil_inet_pton(mcast_addr_.sin_family, McastGroup, &mcast_addr_.sin_addr) == -1)
            {
                return false;
            }

            /* we want to join that LPD multicast group */
            struct ip_mreq mcast_req = {};
            mcast_req.imr_multiaddr = mcast_addr_.sin_addr;
            mcast_req.imr_interface.s_addr = INADDR_ANY;

            if (setsockopt(
                    mcast_rcv_socket_,
                    IPPROTO_IP,
                    IP_ADD_MEMBERSHIP,
                    reinterpret_cast<char const*>(&mcast_req),
                    sizeof(struct ip_mreq)) == -1)
            {
                return false;
            }
        }

        /* setup datagram socket (send) */
        {
            unsigned char const scope = AnnounceScope;

            mcast_snd_socket_ = socket(PF_INET, SOCK_DGRAM, 0);

            if (mcast_snd_socket_ == TR_BAD_SOCKET)
            {
                return false;
            }

            if (evutil_make_socket_nonblocking(mcast_snd_socket_) == -1)
            {
                return false;
            }

            if (setsockopt(
                    mcast_snd_socket_,
                    SOL_SOCKET,
                    SO_REUSEADDR,
                    reinterpret_cast<char const*>(&opt_on),
                    sizeof(opt_on)) == -1)
            {
                return false;
            }

            if (auto [ss, sslen] = mediator_.bind_address(TR_AF_INET).to_sockaddr({});
                bind(mcast_snd_socket_, reinterpret_cast<sockaddr*>(&ss), sslen) == -1)
            {
                return false;
            }

            /* configure outbound multicast TTL */
            if (setsockopt(
                    mcast_snd_socket_,
                    IPPROTO_IP,
                    IP_MULTICAST_TTL,
                    reinterpret_cast<char const*>(&scope),
                    sizeof(scope)) == -1)
            {
                return false;
            }
        }

        /* Note: lpd_unsolicitedMsgCounter remains 0 until the first timeout event, thus
         * any announcement received during the initial interval will be discarded. */

        event_.reset(event_new(event_base, mcast_rcv_socket_, EV_READ | EV_PERSIST, event_callback, this));
        event_add(event_.get(), nullptr);

        tr_logAddDebug("Local Peer Discovery initialised");

        return true;
    }

    /**
    * @brief Processing of timeout notifications and incoming data on the socket
    * @note maximum rate of read events is limited according to @a lpd_maxAnnounceCap
    * @see DoS */
    static void event_callback(evutil_socket_t /*s*/, short type, void* vself)
    {
        if ((type & EV_READ) != 0)
        {
            static_cast<tr_lpd_impl*>(vself)->onCanRead();
        }
    }

    void onCanRead()
    {
        if (!mediator_.allowsLPD())
        {
            return;
        }

        // process announcement from foreign peer
        struct sockaddr_in foreign_addr = {};
        auto addr_len = socklen_t{ sizeof(foreign_addr) };
        auto foreign_msg = std::array<char, MaxDatagramLength>{};
        auto const res = recvfrom(
            mcast_rcv_socket_,
            std::data(foreign_msg),
            MaxDatagramLength,
            0,
            reinterpret_cast<sockaddr*>(&foreign_addr),
            &addr_len);

        // If we couldn't read it or it was too big, discard it
        if (res < 1 || static_cast<size_t>(res) > MaxDatagramLength)
        {
            return;
        }

        // If it doesn't look like a BEP14 message, discard it
        auto const msg = std::string_view{ std::data(foreign_msg), static_cast<size_t>(res) };
        if (static auto constexpr SearchKey = "BT-SEARCH * HTTP/"sv; msg.find(SearchKey) == std::string_view::npos)
        {
            return;
        }

        // If we're receiving too many, discard it
        if (++messages_received_since_upkeep_ > MaxIncomingPerUpkeep)
        {
            return;
        }

        // If it's an invalid message or the wrong protocol version, discard it.
        // Note this comes *after* incrementing the count since there is some
        // small CPU overhead in parsing, so don't do it for *every* message
        auto const parsed = parseAnnounceMsg(msg);
        if (!parsed || parsed->major != 1 || parsed->minor < 1 || parsed->cookie == cookie_)
        {
            tr_logAddTrace("Discarded invalid multicast message");
            return;
        }

        auto peer_addr = tr_address{};
        peer_addr.addr.addr4 = foreign_addr.sin_addr;
        for (auto const& hash_string : parsed->info_hash_strings)
        {
            if (!mediator_.onPeerFound(hash_string, peer_addr, parsed->port))
            {
                tr_logAddDebug(fmt::format(FMT_STRING("Cannot serve torrent #{:s}"), hash_string));
            }
        }
    }

    void announceUpkeep()
    {
        if (!mediator_.allowsLPD())
        {
            return;
        }

        auto torrents = mediator_.torrents();

        // remove torrents that don't need to be announced
        auto const now = tr_time();
        auto const needs_announce = [&now](auto& info)
        {
            return info.allows_lpd && (info.activity == TR_STATUS_DOWNLOAD || info.activity == TR_STATUS_SEED) &&
                (info.announce_after < now);
        };
        torrents.erase(
            std::remove_if(std::begin(torrents), std::end(torrents), std::not_fn(needs_announce)),
            std::end(torrents));

        if (std::empty(torrents))
        {
            return;
        }

        // prioritize the remaining torrents
        std::sort(
            std::begin(torrents),
            std::end(torrents),
            [](auto const& a, auto const& b)
            {
                if (a.activity != b.activity)
                {
                    return a.activity < b.activity;
                }

                if (a.announce_after != b.announce_after)
                {
                    return a.announce_after < b.announce_after;
                }
                return false;
            });

        // cram in as many as will fit in a message
        auto const baseline_size = std::size(makeAnnounceMsg(cookie_, mediator_.port(), {}));
        auto const size_with_one = std::size(makeAnnounceMsg(cookie_, mediator_.port(), { torrents.front().info_hash_str }));
        auto const size_per_hash = size_with_one - baseline_size;
        auto const max_torrents_per_announce = (MaxDatagramLength - baseline_size) / size_per_hash;
        auto info_hash_strings = std::vector<std::string_view>{};
        info_hash_strings.resize(std::min(std::size(torrents), max_torrents_per_announce));
        std::transform(
            std::begin(torrents),
            std::begin(torrents) + std::size(info_hash_strings),
            std::begin(info_hash_strings),
            [](auto const& tor) { return tor.info_hash_str; });

        if (!sendAnnounce(info_hash_strings))
        {
            return;
        }

        auto const next_announce_after = now + TorrentAnnounceIntervalSec;
        for (auto const& info_hash_string : info_hash_strings)
        {
            mediator_.setNextAnnounceTime(info_hash_string, next_announce_after);
        }
    }

    void dosUpkeep()
    {
        if (messages_received_since_upkeep_ > MaxIncomingPerUpkeep)
        {
            tr_logAddTrace(fmt::format(
                "Dropped {} announces in the last interval (max. {} allowed)",
                messages_received_since_upkeep_ - MaxIncomingPerUpkeep,
                MaxIncomingPerUpkeep));
        }

        messages_received_since_upkeep_ = 0;
    }

    /**
     * @brief Announce the given torrent on the local network
     *
     * @return Returns a success flag
     *
     * Send a query for torrent t out to the LPD multicast group (or the LAN, for that
     * matter). A listening client on the same network might react by adding us to his
     * peer pool for torrent t.
     */
    bool sendAnnounce(std::vector<std::string_view> const& info_hash_strings)
    {
        auto const announce = makeAnnounceMsg(cookie_, mediator_.port(), info_hash_strings);
        TR_ASSERT(std::size(announce) <= MaxDatagramLength);
        auto const res = sendto(
            mcast_snd_socket_,
            std::data(announce),
            std::size(announce),
            0,
            reinterpret_cast<sockaddr const*>(&mcast_addr_),
            sizeof(mcast_addr_));
        auto const sent = res == static_cast<int>(std::size(announce));
        return sent;
    }

    std::string const cookie_ = makeCookie();
    Mediator& mediator_;
    tr_socket_t mcast_rcv_socket_ = TR_BAD_SOCKET; /**<separate multicast receive socket */
    tr_socket_t mcast_snd_socket_ = TR_BAD_SOCKET; /**<and multicast send socket */
    libtransmission::evhelpers::event_unique_ptr event_;

    static auto constexpr MaxDatagramLength = size_t{ 1400 };
    sockaddr_in mcast_addr_ = {}; /**<initialized from the above constants in init() */

    // BEP14: "To avoid causing multicast storms on large networks a
    // client should send no more than 1 announce per minute."
    static auto constexpr AnnounceInterval = 1min;
    std::unique_ptr<libtransmission::Timer> announce_timer_;

    // Flood Protection:
    // To protect against message flooding, stop processing search messages
    // after processing N per upkeep. If we hit that limit, we're either
    // in a *very* crowded multicast group or a hostile host is sending us
    // bogus data. Better to drop a few packets than get DoS'ed.
    static auto constexpr DosInterval = 5s;
    std::unique_ptr<libtransmission::Timer> dos_timer_;
    static auto constexpr MaxIncomingPerSecond = int{ 10 };
    static auto constexpr MaxIncomingPerUpkeep = std::chrono::duration_cast<std::chrono::seconds>(DosInterval).count() *
        MaxIncomingPerSecond;
    // @brief throw away messages after this number exceeds MaxIncomingPerUpkeep
    size_t messages_received_since_upkeep_ = 0U;

    static auto constexpr TorrentAnnounceIntervalSec = time_t{ 240U }; // how frequently to reannounce the same torrent
    static auto constexpr TtlSameSubnet = int{ 1 };
    static auto constexpr AnnounceScope = int{ TtlSameSubnet }; /**<the maximum scope for LPD datagrams */
};

std::unique_ptr<tr_lpd> tr_lpd::create(Mediator& mediator, struct event_base* event_base)
{
    return std::make_unique<tr_lpd_impl>(mediator, event_base);
}
