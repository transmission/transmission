// This file Copyright © 2010-2022 Johannes Lieder.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring> // for memset()
#include <memory>
#include <optional>
#include <sstream>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <ctime>
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <netinet/in.h> /* sockaddr_in */
#endif

#include <event2/event.h>
#include <event2/util.h>

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "log.h"
#include "net.h"
#include "tr-lpd.h"
#include "tr-strbuf.h"
#include "utils.h"

using namespace std::literals;

namespace
{

auto makeCookie()
{
    auto buf = std::array<char, 12>{};
    tr_rand_buffer(std::data(buf), std::size(buf));
    static auto constexpr Pool = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"sv;
    for (auto& ch : buf)
    {
        ch = Pool[static_cast<unsigned char>(ch) % std::size(Pool)];
    }
    return std::string{ std::data(buf), std::size(buf) };
}

} // namespace

class tr_lpd_impl final : public tr_lpd
{
public:
    tr_lpd_impl(Mediator& mediator, libtransmission::TimerMaker& timer_maker, struct event_base* event_base)
        : mediator_{ mediator }
        , upkeep_timer_{ timer_maker.create([this]() { onUpkeepTimer(); }) }
    {
        if (!init(event_base))
        {
            return;
        }

        upkeep_timer_->startRepeating(UpkeepInterval);
        onUpkeepTimer();
    }

    tr_lpd_impl(tr_lpd_impl&&) = delete;
    tr_lpd_impl(tr_lpd_impl const&) = delete;
    tr_lpd_impl& operator=(tr_lpd_impl&&) = delete;
    tr_lpd_impl& operator=(tr_lpd_impl const&) = delete;

    ~tr_lpd_impl() override
    {
        upkeep_timer_.reset();

        if (event_ != nullptr)
        {
            event_free(event_);
            event_ = nullptr;
        }

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

        auto const save = errno;
        evutil_closesocket(mcast_rcv_socket_);
        evutil_closesocket(mcast_snd_socket_);
        mcast_rcv_socket_ = TR_BAD_SOCKET;
        mcast_snd_socket_ = TR_BAD_SOCKET;
        tr_logAddWarn(fmt::format(
            _("Couldn't initialize LPD: {error} ({error_code})"),
            fmt::arg("error", tr_strerror(save)),
            fmt::arg("error_code", save)));
        errno = save;
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
        int const opt_on = 1;
        // int const opt_off = 0;

        static_assert(AnnounceInterval > 0);
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

            memset(&mcast_addr_, 0, sizeof(mcast_addr_));
            mcast_addr_.sin_family = AF_INET;
            mcast_addr_.sin_port = McastPort.network();

            if (evutil_inet_pton(mcast_addr_.sin_family, McastGroup, &mcast_addr_.sin_addr) == -1)
            {
                return false;
            }

            if (bind(mcast_rcv_socket_, (struct sockaddr*)&mcast_addr_, sizeof(mcast_addr_)) == -1)
            {
                return false;
            }

            /* we want to join that LPD multicast group */
            struct ip_mreq mcastReq;
            memset(&mcastReq, 0, sizeof(mcastReq));
            mcastReq.imr_multiaddr = mcast_addr_.sin_addr;
            mcastReq.imr_interface.s_addr = htonl(INADDR_ANY);

            if (setsockopt(
                    mcast_rcv_socket_,
                    IPPROTO_IP,
                    IP_ADD_MEMBERSHIP,
                    reinterpret_cast<char const*>(&mcastReq),
                    sizeof(struct ip_mreq)) == -1)
            {
                return false;
            }

#if 0
            if (setsockopt(
                    mcast_rcv_socket_,
                    IPPROTO_IP,
                    IP_MULTICAST_LOOP,
                    reinterpret_cast<char const*>(&opt_off),
                    sizeof(opt_off)) == -1)
            {
                return false;
            }
#endif
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

#if 0
            if (setsockopt(
                    mcast_snd_socket_,
                    IPPROTO_IP,
                    IP_MULTICAST_LOOP,
                    reinterpret_cast<char const*>(&opt_off),
                    sizeof(opt_off)) == -1)
            {
                return false;
            }
#endif
        }

        /* Note: lpd_unsolicitedMsgCounter remains 0 until the first timeout event, thus
         * any announcement received during the initial interval will be discarded. */

        event_ = event_new(event_base, mcast_rcv_socket_, EV_READ | EV_PERSIST, event_callback, this);
        event_add(event_, nullptr);

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
        struct sockaddr_in foreign_addr;
        int addr_len = sizeof(foreign_addr);
        auto foreign_msg = std::array<char, MaxDatagramLength>{};
        auto const res = recvfrom(
            mcast_rcv_socket_,
            std::data(foreign_msg),
            MaxDatagramLength,
            0,
            (struct sockaddr*)&foreign_addr,
            (socklen_t*)&addr_len);

        /* besides, do we get flooded? then bail out! */
        if (--unsolicited_msg_counter_ < 0)
        {
            return;
        }

        if (res > 0 && static_cast<size_t>(res) <= MaxDatagramLength)
        {
            auto foreign_peer = tr_address{};
            foreign_peer.addr.addr4 = foreign_addr.sin_addr;

            if (considerAnnounce(foreign_peer, { std::data(foreign_msg), static_cast<size_t>(res) }) == 0)
            {
                tr_logAddTrace("Discarded invalid multicast message");
            }
        }
    }

    void onUpkeepTimer()
    {
        time_t const now = tr_time();
        auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(UpkeepInterval).count();
        announceMore(now, seconds);
    }

    /**
     * @note Since it possible for tr_lpdAnnounceMore to get called from outside the LPD module,
     * the function needs to be informed of the externally employed housekeeping interval.
     * Further, by setting interval to zero (or negative) the caller may actually disable LPD
     * announces on a per-interval basis.
     *
     * TODO: since this function's been made private and is called by a periodic timer,
     * most of the previous paragraph isn't true anymore... we weren't using that functionality
     * before. are there cases where we should? if not, should we remove the bells & whistles?
     */
    void announceMore(time_t const now, int const interval)
    {
        if (mediator_.allowsLPD())
        {
            for (auto const& [info_hash_str, activity, allows_lpd, announce_at] : mediator_.torrents())
            {
                if (!allows_lpd)
                {
                    continue;
                }

                if (activity != TR_STATUS_DOWNLOAD && activity != TR_STATUS_SEED)
                {
                    continue;
                }

                if (announce_at > now)
                {
                    continue;
                }

                if (sendAnnounce(&info_hash_str, 1))
                {
                    // downloads re-announce twice as often as seeds
                    auto const next_announce_at = now +
                        (activity == TR_STATUS_DOWNLOAD ? AnnounceInterval : AnnounceInterval * 2U);
                    mediator_.setNextAnnounceTime(info_hash_str, next_announce_at);
                    break; /* that's enough for this interval */
                }
            }
        }

        /* perform housekeeping for the flood protection mechanism */
        {
            int const max_announce_cap = interval * AnnounceCapFactor;

            if (unsolicited_msg_counter_ < 0)
            {
                --unsolicited_msg_counter_;

                tr_logAddTrace(fmt::format(
                    "Dropped {} announces in the last interval (max. {} allowed)",
                    unsolicited_msg_counter_,
                    max_announce_cap));
            }

            unsolicited_msg_counter_ = max_announce_cap;
        }
    }

    /**
     * @brief Announce the given torrent on the local network
     *
     * @return Returns success
     *
     * Send a query for torrent t out to the LPD multicast group (or the LAN, for that
     * matter). A listening client on the same network might react by adding us to his
     * peer pool for torrent t.
     */
    bool sendAnnounce(std::string_view const* info_hash_strings, size_t n_strings)
    {
        static auto constexpr Major = 1;
        static auto constexpr Minor = 1;
        static auto constexpr CrLf = "\r\n"sv;

        auto ostr = std::ostringstream{};
        ostr << "BT-SEARCH * HTTP/" << Major << '.' << Minor << CrLf << "Host: " << McastGroup << ':' << McastPort.host()
             << CrLf << "Port: " << mediator_.port().host() << CrLf;
        for (size_t i = 0; i < n_strings; ++i)
        {
            ostr << "Infohash: " << tr_strupper(info_hash_strings[i]) << CrLf;
        }
        ostr << "cookie: " << cookie_ << CrLf << CrLf << CrLf;
        auto const query = ostr.str();

        // send the query out using [mcast_snd_socket_]
        // destination address info has already been set up in init(),
        // so we refrain from preparing another sockaddr_in here
        auto const res = sendto(
            mcast_snd_socket_,
            std::data(query),
            std::size(query),
            0,
            (struct sockaddr const*)&mcast_addr_,
            sizeof(mcast_addr_));
        auto const sent = res == static_cast<int>(std::size(query));
        return sent;
    }

    struct ParsedAnnounce
    {
        int major;
        int minor;
        tr_port port;
        std::vector<std::string_view> info_hash_strings;
        std::string_view cookie;
    };

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
    static std::optional<ParsedAnnounce> parseAnnounce(std::string_view announce)
    {
        static auto constexpr CrLf = "\r\n"sv;

        auto ret = ParsedAnnounce{};

        // get major, minor
        auto key = "BT-SEARCH * HTTP/"sv;
        if (auto const pos = announce.find(key); pos != std::string_view::npos)
        {
            // parse `${major}.${minor}`
            auto walk = announce.substr(pos + std::size(key));
            if (auto const major = tr_parseNum<int>(walk); major && tr_strvStartsWith(walk, '.'))
            {
                ret.major = *major;
            }
            else
            {
                return {};
            }

            walk.remove_prefix(1); // the '.' between major and minor
            if (auto const minor = tr_parseNum<int>(walk); minor && tr_strvStartsWith(walk, CrLf))
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
            if (auto const port = tr_parseNum<uint16_t>(walk); port && tr_strvStartsWith(walk, CrLf))
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

    /**
     * @brief Process incoming unsolicited messages and add the peer to the announced
     * torrent if all checks are passed.
     *
     * @param[in,out] peer Address information of the peer to add
     * @param[in] msg The announcement message to consider
     * @return Returns 0 if any input parameter or the announce was invalid, 1 if the peer
     * was successfully added, -1 if not.
     */
    int considerAnnounce(tr_address peer_addr, std::string_view msg)
    {
        auto const parsed = parseAnnounce(msg);
        if (!parsed || parsed->major != 1 || parsed->minor < 1)
        {
            return 0;
        }
        if (parsed->cookie == cookie_)
        {
            return -1; // it's our own message
        }

        auto any_added = false;

        for (auto const& hash_string : parsed->info_hash_strings)
        {
            if (mediator_.onPeerFound(hash_string, peer_addr, parsed->port))
            {
                any_added = true;
            }
            else
            {
                tr_logAddDebug(fmt::format(FMT_STRING("Cannot serve torrent #{:s}"), hash_string));
            }
        }

        return any_added ? 1 : -1;
    }

    std::string const cookie_ = makeCookie();
    Mediator& mediator_;
    tr_socket_t mcast_rcv_socket_ = TR_BAD_SOCKET; /**<separate multicast receive socket */
    tr_socket_t mcast_snd_socket_ = TR_BAD_SOCKET; /**<and multicast send socket */
    event* event_ = nullptr;

    static auto constexpr MaxDatagramLength = size_t{ 1400 };
    static constexpr char const* const McastGroup = "239.192.152.143"; /**<LPD multicast group */
    static auto constexpr McastPort = tr_port::fromHost(6771); /**<LPD source and destination UPD port */
    sockaddr_in mcast_addr_ = {}; /**<initialized from the above constants in init() */

    /// DoS Message Flood Protection
    // To protect against message flooding, cap event processing after ten messages
    // per second (that is, taking the average over one of our housekeeping intervals)
    // got into our processing handler.
    // If we'd really hit the limit and start discarding events, we either joined an
    // extremely crowded multicast group or a malevolent host is sending bogus data to
    // our socket. In this situation, we rather miss some announcements than blocking
    // the actual task.
    //
    // @brief allow at most ten messages per second (interval average)
    // @note this constraint is only enforced once per housekeeping interval
    static auto constexpr AnnounceCapFactor = int{ 10 };

    // @ingroup DoS
    // @brief number of unsolicited messages during the last HK interval
    // @remark counts downwards
    int unsolicited_msg_counter_;

    static auto constexpr UpkeepInterval = 5s;
    std::unique_ptr<libtransmission::Timer> upkeep_timer_;

    static auto constexpr AnnounceInterval = time_t{ 4 * 60 }; /**<4 min announce interval per torrent */
    static auto constexpr TtlSameSubnet = int{ 1 };
    static auto constexpr AnnounceScope = int{ TtlSameSubnet }; /**<the maximum scope for LPD datagrams */

    // static auto constexpr TtlSameSite = int{ 32 };
    // static auto constexpr TtlSameRegion = int{ 64 };
    // static auto constexpr TtlSameContinent = int{ 128 };
    // static auto constexpr TtlUnrestricted = int{ 255 };
};

std::unique_ptr<tr_lpd> tr_lpd::create(
    Mediator& mediator,
    libtransmission::TimerMaker& timer_maker,
    struct event_base* event_base)
{
    return std::make_unique<tr_lpd_impl>(mediator, timer_maker, event_base);
}
