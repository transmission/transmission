// This file Copyright © 2010 Johannes Lieder.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring> /* strlen(), strncpy(), strstr(), memset() */
#include <memory>

#ifdef _WIN32
#include <ws2tcpip.h>
using in_port_t = uint16_t; /* all missing */
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

#include "log.h"
#include "net.h"
#include "tr-lpd.h"
#include "tr-strbuf.h"
#include "utils.h"

using namespace std::literals;

/**
* @def CRLF
* @brief a line-feed, as understood by the LPD protocol */
#define CRLF "\r\n"

class tr_lpd_impl final : public tr_lpd
{
public:
    tr_lpd_impl(Mediator& mediator, libtransmission::TimerMaker& timer_maker, struct event_base* event_base)
        : mediator_{ mediator }
        , upkeep_timer_{ timer_maker.create([this]() { onUpkeepTimer(); }) }
    {
        if (init(event_base))
        {
            upkeep_timer_->startRepeating(UpkeepInterval);
        }
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
        struct ip_mreq mcastReq;
        int const opt_on = 1;
        int const opt_off = 0;

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
            memset(&mcastReq, 0, sizeof(mcastReq));
            mcastReq.imr_multiaddr = mcast_addr_.sin_addr;
            mcastReq.imr_interface.s_addr = htonl(INADDR_ANY);

            if (setsockopt(
                    mcast_rcv_socket_,
                    IPPROTO_IP,
                    IP_ADD_MEMBERSHIP,
                    reinterpret_cast<char const*>(&mcastReq),
                    sizeof(mcastReq)) == -1)
            {
                return false;
            }

            if (setsockopt(
                    mcast_rcv_socket_,
                    IPPROTO_IP,
                    IP_MULTICAST_LOOP,
                    reinterpret_cast<char const*>(&opt_off),
                    sizeof(opt_off)) == -1)
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

            if (setsockopt(
                    mcast_snd_socket_,
                    IPPROTO_IP,
                    IP_MULTICAST_LOOP,
                    reinterpret_cast<char const*>(&opt_off),
                    sizeof(opt_off)) == -1)
            {
                return false;
            }
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
        /* do not allow announces to be processed if LPD is disabled */
        if (!mediator_.allowsLPD())
        {
            return;
        }

        struct sockaddr_in foreignAddr;
        int addrLen = sizeof(foreignAddr);
        char foreignMsg[lpd_maxDatagramLength + 1];

        /* process local announcement from foreign peer */
        int const res = recvfrom(
            mcast_rcv_socket_,
            foreignMsg,
            lpd_maxDatagramLength,
            0,
            (struct sockaddr*)&foreignAddr,
            (socklen_t*)&addrLen);

        /* besides, do we get flooded? then bail out! */
        if (--unsolicited_msg_counter_ < 0)
        {
            return;
        }

        if (res > 0 && res <= lpd_maxDatagramLength)
        {
            auto foreign_peer = tr_address{};
            foreign_peer.addr.addr4 = foreignAddr.sin_addr;

            /* be paranoid enough about zero terminating the foreign string */
            foreignMsg[res] = '\0';

            if (considerAnnounce(foreign_peer, foreignMsg) != 0)
            {
                return; /* OK so far, no log message */
            }
        }

        tr_logAddTrace("Discarded invalid multicast message");
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

                if (sendAnnounce(info_hash_str))
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
     * @param[in] t Torrent to announce
     * @return Returns true on success
     *
     * Send a query for torrent t out to the LPD multicast group (or the LAN, for that
     * matter). A listening client on the same network might react by adding us to his
     * peer pool for torrent t.
     */
    bool sendAnnounce(std::string_view info_hash_str)
    {
        auto const query = fmt::format(
            FMT_STRING("BT-SEARCH * HTTP/{:d}.{:d}" CRLF "Host: {:s}:{:d}" CRLF "Port: {:d}" CRLF
                       "Infohash: {:s}" CRLF CRLF CRLF),
            1,
            1,
            McastGroup,
            McastPort.host(),
            mediator_.port().host(),
            tr_strupper(info_hash_str));

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

    struct lpd_protocolVersion
    {
        int major;
        int minor;
    };

    /**
    * @brief Checks for BT-SEARCH method and separates the parameter section
    * @param[in] s The request string
    * @param[out] ver If non-nullptr, gets filled with protocol info from the request
    * @return Returns a relative pointer to the beginning of the parameter section.
    *         If result is nullptr, s was invalid and no information will be returned
    * @remark Note that the returned pointer is only usable as long as the given
    *         pointer s is valid; that is, return storage is temporary.
    *
    * Determines whether the given string checks out to be a valid BT-SEARCH message.
    * If so, the return value points to the beginning of the parameter section (note:
    * in this case the function returns a character sequence beginning with CRLF).
    * If parameter is not nullptr, the declared protocol version is returned as part
    * of the lpd_protocolVersion structure.
    */
    static char const* extractHeader(char const* s, struct lpd_protocolVersion* const ver)
    {
        int major = -1;
        int minor = -1;
        size_t const len = strlen(s);

        /* something might be rotten with this chunk of data */
        if (len == 0 || len > lpd_maxDatagramLength)
        {
            return nullptr;
        }

        /* now we can attempt to look up the BT-SEARCH header */
        if (sscanf(s, "BT-SEARCH * HTTP/%d.%d" CRLF, &major, &minor) != 2)
        {
            return nullptr;
        }

        if (major < 0 || minor < 0)
        {
            return nullptr;
        }

        {
            /* a pair of blank lines at the end of the string, no place else */
            char const* const two_blank = CRLF CRLF CRLF;
            char const* const end = strstr(s, two_blank);

            if (end == nullptr || strlen(end) > strlen(two_blank))
            {
                return nullptr;
            }
        }

        if (ver != nullptr)
        {
            ver->major = major;
            ver->minor = minor;
        }

        /* separate the header, begins with CRLF */
        return strstr(s, CRLF);
    }

    /**
    * @brief Return the value of a named parameter
    *
    * @param[in] str Input string of "\r\nName: Value" pairs without HTTP-style method part
    * @param[in] name Name of parameter to extract
    * @param[in] n Maximum available storage for value to return
    * @param[out] val Output parameter for the actual value
    * @return Returns 1 if value could be copied successfully
    *
    * Extracts the associated value of a named parameter from a HTTP-style header by
    * performing the following steps:
    *   - assemble search string "\r\nName: " and locate position
    *   - copy back value from end to next "\r\n"
    */
    // TODO: string_view
    static bool extractParam(char const* const str, char const* const name, int n, char* const val)
    {
        auto const key = tr_strbuf<char, 30>{ CRLF, name, ": "sv };

        char const* const pos = strstr(str, key);
        if (pos == nullptr)
        {
            return false; /* search was not successful */
        }

        {
            char const* const beg = pos + std::size(key);
            char const* const new_line = strstr(beg, CRLF);

            /* the value is delimited by the next CRLF */
            int const len = new_line - beg;

            /* if value string hits the length limit n,
             * leave space for a trailing '\0' character */
            n = std::min(len, n - 1);
            strncpy(val, beg, n);
            val[n] = 0;
        }

        /* we successfully returned the value string */
        return true;
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
    int considerAnnounce(tr_address peer_addr, char const* const msg)
    {
        auto constexpr MaxValueLen = int{ 25 };
        auto constexpr MaxHashLen = TR_SHA1_DIGEST_STRLEN;

        auto ver = lpd_protocolVersion{ -1, -1 };
        char value[MaxValueLen] = { 0 };
        char hash_string[MaxHashLen] = { 0 };
        int res = 0;

        if (msg != nullptr)
        {
            char const* params = extractHeader(msg, &ver);

            if (params == nullptr || ver.major != 1) /* allow messages of protocol v1 */
            {
                return 0;
            }

            /* save the effort to check Host, which seems to be optional anyway */

            if (!extractParam(params, "Port", MaxValueLen, value))
            {
                return 0;
            }

            /* determine announced peer port, refuse if value too large */
            int peer_port = 0;
            if (sscanf(value, "%d", &peer_port) != 1 || peer_port > (in_port_t)-1)
            {
                return 0;
            }
            auto const port = tr_port::fromHost(peer_port);

            res = -1; /* signal caller side-effect to peer->port via return != 0 */

            if (!extractParam(params, "Infohash", MaxHashLen, hash_string))
            {
                return res;
            }

            if (mediator_.onPeerFound(hash_string, peer_addr, port))
            {
                /* periodic reconnectPulse() deals with the rest... */
                return 1;
            }

            tr_logAddDebug(fmt::format(FMT_STRING("Cannot serve torrent #{:s}"), hash_string));
        }

        return res;
    }

    Mediator& mediator_;
    tr_socket_t mcast_rcv_socket_ = TR_BAD_SOCKET; /**<separate multicast receive socket */
    tr_socket_t mcast_snd_socket_ = TR_BAD_SOCKET; /**<and multicast send socket */
    event* event_ = nullptr;

    static auto constexpr lpd_maxDatagramLength = int{ 200 }; /**<the size an LPD datagram must not exceed */
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
