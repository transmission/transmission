// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint8_t, uint64_t
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "libtransmission/transmission.h" // tr_block_span_t (ptr only)

#include "libtransmission/net.h" /* tr_address */
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h" /* tr_compare_3way */

/**
 * @addtogroup peers Peers
 * @{
 */

class tr_peer;
class tr_peer_socket;
struct tr_peerMgr;
struct tr_peer_stat;
struct tr_session;
struct tr_torrent;

/* added_f's bitwise-or'ed flags */
enum
{
    /* true if the peer supports encryption */
    ADDED_F_ENCRYPTION_FLAG = 1,
    /* true if the peer is a seed or partial seed */
    ADDED_F_SEED_FLAG = 2,
    /* true if the peer supports µTP */
    ADDED_F_UTP_FLAGS = 4,
    /* true if the peer has holepunch support */
    ADDED_F_HOLEPUNCH = 8,
    /* true if the peer telling us about this peer
     * initiated the connection (implying that it is connectible) */
    ADDED_F_CONNECTABLE = 16
};

/**
 * Peer information that should be retained even when not connected,
 * e.g. to help us decide which peers to connect to.
 */
class tr_peer_info
{
public:
    tr_peer_info(tr_socket_address socket_address, uint8_t pex_flags, tr_peer_from from)
        : listen_socket_address_{ socket_address }
        , from_first_{ from }
        , from_best_{ from }
    {
        TR_ASSERT(!std::empty(socket_address.port()));
        ++n_known_connectable;
        set_pex_flags(pex_flags);
    }

    tr_peer_info(tr_address address, uint8_t pex_flags, tr_peer_from from)
        : listen_socket_address_{ address, tr_port{} }
        , from_first_{ from }
        , from_best_{ from }
    {
        set_pex_flags(pex_flags);
    }

    tr_peer_info(tr_peer_info&&) = delete;
    tr_peer_info(tr_peer_info const&) = delete;
    tr_peer_info& operator=(tr_peer_info&&) = delete;
    tr_peer_info& operator=(tr_peer_info const&) = delete;

    ~tr_peer_info()
    {
        if (!std::empty(listen_socket_address_.port()))
        {
            [[maybe_unused]] auto const n_prev = n_known_connectable--;
            TR_ASSERT(n_prev > 0U);
        }
    }

    [[nodiscard]] static auto known_connectable_count() noexcept
    {
        return n_known_connectable;
    }

    // ---

    [[nodiscard]] constexpr auto const& listen_socket_address() const noexcept
    {
        return listen_socket_address_;
    }

    [[nodiscard]] constexpr auto const& listen_address() const noexcept
    {
        return listen_socket_address_.address();
    }

    [[nodiscard]] constexpr auto listen_port() const noexcept
    {
        return listen_socket_address_.port();
    }

    void set_listen_port(tr_port port_in) noexcept
    {
        if (!std::empty(port_in))
        {
            auto& port = listen_socket_address_.port_;
            if (std::empty(port)) // increment known connectable peers if we did not know the listening port of this peer before
            {
                ++n_known_connectable;
            }
            port = port_in;
        }
    }

    [[nodiscard]] auto display_name() const
    {
        return listen_socket_address_.display_name();
    }

    // ---

    [[nodiscard]] constexpr auto from_first() const noexcept
    {
        return from_first_;
    }

    [[nodiscard]] constexpr auto from_best() const noexcept
    {
        return from_best_;
    }

    constexpr void found_at(tr_peer_from from) noexcept
    {
        from_best_ = std::min(from_best_, from);
    }

    // ---

    constexpr void set_seed(bool seed = true) noexcept
    {
        is_seed_ = seed;
    }

    [[nodiscard]] constexpr auto is_seed() const noexcept
    {
        return is_seed_;
    }

    // ---

    void set_connectable(bool value = true) noexcept
    {
        is_connectable_ = value;
    }

    [[nodiscard]] constexpr auto const& is_connectable() const noexcept
    {
        return is_connectable_;
    }

    // ---

    void set_utp_supported(bool value = true) noexcept
    {
        is_utp_supported_ = value;
    }

    [[nodiscard]] constexpr auto supports_utp() const noexcept
    {
        return is_utp_supported_;
    }

    // ---

    [[nodiscard]] constexpr auto compare_by_failure_count(tr_peer_info const& that) const noexcept
    {
        return tr_compare_3way(num_consecutive_fails_, that.num_consecutive_fails_);
    }

    [[nodiscard]] constexpr auto compare_by_piece_data_time(tr_peer_info const& that) const noexcept
    {
        return tr_compare_3way(piece_data_at_, that.piece_data_at_);
    }

    // ---

    constexpr auto set_connected(time_t now, bool is_connected = true) noexcept
    {
        connection_changed_at_ = now;

        is_connected_ = is_connected;

        if (is_connected_)
        {
            num_consecutive_fails_ = {};
            piece_data_at_ = {};
        }
    }

    [[nodiscard]] constexpr auto is_connected() const noexcept
    {
        return is_connected_;
    }

    // ---

    [[nodiscard]] bool is_blocklisted(tr_session const* session) const;

    void set_blocklisted_dirty()
    {
        blocklisted_.reset();
    }

    // ---

    constexpr void ban() noexcept
    {
        is_banned_ = true;
    }

    [[nodiscard]] constexpr auto is_banned() const noexcept
    {
        return is_banned_;
    }

    // ---

    [[nodiscard]] constexpr auto connection_attempt_time() const noexcept
    {
        return connection_attempted_at_;
    }

    constexpr void set_connection_attempt_time(time_t value) noexcept
    {
        connection_attempted_at_ = value;
    }

    constexpr void set_latest_piece_data_time(time_t value) noexcept
    {
        piece_data_at_ = value;
    }

    [[nodiscard]] constexpr bool has_transferred_piece_data() const noexcept
    {
        return piece_data_at_ != time_t{};
    }

    [[nodiscard]] constexpr auto reconnect_interval_has_passed(time_t const now) const noexcept
    {
        auto const interval = now - std::max(connection_attempted_at_, connection_changed_at_);
        return interval >= get_reconnect_interval_secs(now);
    }

    [[nodiscard]] constexpr std::optional<time_t> idle_secs(time_t now) const noexcept
    {
        if (!is_connected_)
        {
            return {};
        }

        return now - std::max(piece_data_at_, connection_changed_at_);
    }

    // ---

    constexpr void on_connection_failed() noexcept
    {
        if (num_consecutive_fails_ != std::numeric_limits<decltype(num_consecutive_fails_)>::max())
        {
            ++num_consecutive_fails_;
        }
    }

    [[nodiscard]] constexpr auto connection_failure_count() const noexcept
    {
        return num_consecutive_fails_;
    }

    // ---

    constexpr void set_pex_flags(uint8_t pex_flags) noexcept
    {
        pex_flags_ = pex_flags;

        if ((pex_flags & ADDED_F_CONNECTABLE) != 0U)
        {
            set_connectable();
        }

        if ((pex_flags & ADDED_F_UTP_FLAGS) != 0U)
        {
            set_utp_supported();
        }

        is_seed_ = (pex_flags & ADDED_F_SEED_FLAG) != 0U;
    }

    [[nodiscard]] constexpr uint8_t pex_flags() const noexcept
    {
        auto ret = pex_flags_;

        if (is_connectable_)
        {
            if (*is_connectable_)
            {
                ret |= ADDED_F_CONNECTABLE;
            }
            else
            {
                ret &= ~ADDED_F_CONNECTABLE;
            }
        }

        if (is_utp_supported_)
        {
            if (*is_utp_supported_)
            {
                ret |= ADDED_F_UTP_FLAGS;
            }
            else
            {
                ret &= ~ADDED_F_UTP_FLAGS;
            }
        }

        if (is_seed_)
        {
            ret |= ADDED_F_SEED_FLAG;
        }

        return ret;
    }

    // ---

    // merge two peer info objects that supposedly describes the same peer
    void merge(tr_peer_info const& that) noexcept
    {
        TR_ASSERT(is_connectable_.value_or(true) || !is_connected());
        TR_ASSERT(that.is_connectable_.value_or(true) || !that.is_connected());

        connection_attempted_at_ = std::max(connection_attempted_at_, that.connection_attempted_at_);
        connection_changed_at_ = std::max(connection_changed_at_, that.connection_changed_at_);
        piece_data_at_ = std::max(piece_data_at_, that.piece_data_at_);

        /* no need to merge blocklist since it gets updated elsewhere */

        {
            // This part is frankly convoluted and confusing, but the idea is:
            // 1. If the two peer info objects agree that this peer is connectable/non-connectable,
            //    then the answer is straightforward: We keep the agreed value.
            // 2. If the two peer info objects disagrees as to whether this peer is connectable,
            //    then we reset the flag to an empty value, so that we can try for ourselves when
            //    initiating outgoing connections.
            // 3. If one object has knowledge and the other doesn't, then we take the word of the
            //    peer info object with knowledge with one exception:
            //    - If the object with knowledge says the peer is not connectable, but we are
            //      currently connected to the peer, then we give it the benefit of the doubt.
            //      The connectable flag will be reset to an empty value.
            // 4. In case both objects have no knowledge about whether this peer is connectable,
            //    we shall not make any assumptions: We keep the flag empty.
            //
            // Truth table:
            //   +-----------------+---------------+----------------------+--------------------+---------+
            //   | is_connectable_ | is_connected_ | that.is_connectable_ | that.is_connected_ | Result  |
            //   +=================+===============+======================+====================+=========+
            //   | T               | T             | T                    | T                  | T       |
            //   | T               | T             | T                    | F                  | T       |
            //   | T               | T             | F                    | F                  | ?       |
            //   | T               | T             | ?                    | T                  | T       |
            //   | T               | T             | ?                    | F                  | T       |
            //   | T               | F             | T                    | T                  | T       |
            //   | T               | F             | T                    | F                  | T       |
            //   | T               | F             | F                    | F                  | ?       |
            //   | T               | F             | ?                    | T                  | T       |
            //   | T               | F             | ?                    | F                  | T       |
            //   | F               | F             | T                    | T                  | ?       |
            //   | F               | F             | T                    | F                  | ?       |
            //   | F               | F             | F                    | F                  | F       |
            //   | F               | F             | ?                    | T                  | ?       |
            //   | F               | F             | ?                    | F                  | F       |
            //   | ?               | T             | T                    | T                  | T       |
            //   | ?               | T             | T                    | F                  | T       |
            //   | ?               | T             | F                    | F                  | ?       |
            //   | ?               | T             | ?                    | T                  | ?       |
            //   | ?               | T             | ?                    | F                  | ?       |
            //   | ?               | F             | T                    | T                  | T       |
            //   | ?               | F             | T                    | F                  | T       |
            //   | ?               | F             | F                    | F                  | F       |
            //   | ?               | F             | ?                    | T                  | ?       |
            //   | ?               | F             | ?                    | F                  | ?       |
            //   | N/A             | N/A           | F                    | T                  | Invalid |
            //   | F               | T             | N/A                  | N/A                | Invalid |
            //   +-----------------+---------------+----------------------+--------------------+---------+

            auto const conn_this = is_connectable_ && *is_connectable_;
            auto const conn_that = that.is_connectable_ && *that.is_connectable_;

            if ((!is_connectable_ && !that.is_connectable_) ||
                is_connectable_.value_or(conn_that || is_connected()) !=
                    that.is_connectable_.value_or(conn_this || that.is_connected()))
            {
                is_connectable_.reset();
            }
            else
            {
                set_connectable(conn_this || conn_that);
            }
        }

        set_utp_supported(supports_utp() || that.supports_utp());

        /* from_first_ should never be modified */
        found_at(that.from_best());

        /* num_consecutive_fails_ is already the latest */
        pex_flags_ |= that.pex_flags_;

        if (that.is_banned())
        {
            ban();
        }
        /* is_connected_ should already be set */
        set_seed(is_seed() || that.is_seed());
    }

private:
    [[nodiscard]] constexpr time_t get_reconnect_interval_secs(time_t const now) const noexcept
    {
        // if we were recently connected to this peer and transferring piece
        // data, try to reconnect to them sooner rather that later -- we don't
        // want network troubles to get in the way of a good peer.
        auto const unreachable = is_connectable_ && !*is_connectable_;
        if (!unreachable && now - piece_data_at_ <= MinimumReconnectIntervalSecs * 2)
        {
            return MinimumReconnectIntervalSecs;
        }

        // otherwise, the interval depends on how many times we've tried
        // and failed to connect to the peer. Penalize peers that were
        // unreachable the last time we tried
        auto step = this->num_consecutive_fails_;
        if (unreachable)
        {
            step += 2;
        }

        switch (step)
        {
        case 0:
            return 0U;
        case 1:
            return 10U;
        case 2:
            return 60U * 2U;
        case 3:
            return 60U * 15U;
        case 4:
            return 60U * 30U;
        case 5:
            return 60U * 60U;
        default:
            return 60U * 120U;
        }
    }

    // the minimum we'll wait before attempting to reconnect to a peer
    static auto constexpr MinimumReconnectIntervalSecs = time_t{ 5U };

    static auto inline n_known_connectable = size_t{};

    // if the port is 0, it SHOULD mean we don't know this peer's listen socket address
    tr_socket_address listen_socket_address_;

    time_t connection_attempted_at_ = {};
    time_t connection_changed_at_ = {};
    time_t piece_data_at_ = {};

    mutable std::optional<bool> blocklisted_;
    std::optional<bool> is_connectable_;
    std::optional<bool> is_utp_supported_;

    tr_peer_from from_first_; // where the peer was first found
    tr_peer_from from_best_; // the "best" place where this peer was found

    uint8_t num_consecutive_fails_ = {};
    uint8_t pex_flags_ = {};

    bool is_banned_ = false;
    bool is_connected_ = false;
    bool is_seed_ = false;
};

struct tr_pex
{
    tr_pex() = default;

    explicit tr_pex(tr_socket_address socket_address_in, uint8_t flags_in = {})
        : socket_address{ socket_address_in }
        , flags{ flags_in }
    {
    }

    template<typename OutputIt>
    OutputIt to_compact(OutputIt out) const
    {
        return socket_address.to_compact(out);
    }

    template<typename OutputIt>
    static OutputIt to_compact(OutputIt out, tr_pex const* pex, size_t n_pex)
    {
        for (size_t i = 0; i < n_pex; ++i)
        {
            out = pex[i].to_compact(out);
        }
        return out;
    }

    [[nodiscard]] static std::vector<tr_pex> from_compact_ipv4(
        void const* compact,
        size_t compact_len,
        uint8_t const* added_f,
        size_t added_f_len);

    [[nodiscard]] static std::vector<tr_pex> from_compact_ipv6(
        void const* compact,
        size_t compact_len,
        uint8_t const* added_f,
        size_t added_f_len);

    [[nodiscard]] std::string display_name() const
    {
        return socket_address.display_name();
    }

    [[nodiscard]] int compare(tr_pex const& that) const noexcept // <=>
    {
        return socket_address.compare(that.socket_address);
    }

    [[nodiscard]] bool operator==(tr_pex const& that) const noexcept
    {
        return compare(that) == 0;
    }

    [[nodiscard]] bool operator<(tr_pex const& that) const noexcept
    {
        return compare(that) < 0;
    }

    [[nodiscard]] bool is_valid_for_peers() const noexcept
    {
        return socket_address.is_valid_for_peers();
    }

    tr_socket_address socket_address;

    uint8_t flags = 0;
};

constexpr bool tr_isPex(tr_pex const* pex)
{
    return pex != nullptr && pex->socket_address.is_valid();
}

[[nodiscard]] tr_peerMgr* tr_peerMgrNew(tr_session* session);

void tr_peerMgrFree(tr_peerMgr* manager);

[[nodiscard]] std::vector<tr_block_span_t> tr_peerMgrGetNextRequests(tr_torrent* torrent, tr_peer const* peer, size_t numwant);

[[nodiscard]] bool tr_peerMgrDidPeerRequest(tr_torrent const* torrent, tr_peer const* peer, tr_block_index_t block);

void tr_peerMgrClientSentRequests(tr_torrent* torrent, tr_peer* peer, tr_block_span_t span);

[[nodiscard]] size_t tr_peerMgrCountActiveRequestsToPeer(tr_torrent const* torrent, tr_peer const* peer);

void tr_peerMgrAddIncoming(tr_peerMgr* manager, tr_peer_socket&& socket);

size_t tr_peerMgrAddPex(tr_torrent* tor, tr_peer_from from, tr_pex const* pex, size_t n_pex);

enum
{
    TR_PEERS_CONNECTED,
    TR_PEERS_INTERESTING
};

[[nodiscard]] std::vector<tr_pex> tr_peerMgrGetPeers(
    tr_torrent const* tor,
    uint8_t address_type,
    uint8_t peer_list_mode,
    size_t max_peer_count);

void tr_peerMgrAddTorrent(tr_peerMgr* manager, struct tr_torrent* tor);

// return the number of connected peers that have `piece`, or -1 if we already have it
[[nodiscard]] int8_t tr_peerMgrPieceAvailability(tr_torrent const* tor, tr_piece_index_t piece);

void tr_peerMgrTorrentAvailability(tr_torrent const* tor, int8_t* tab, unsigned int n_tabs);

[[nodiscard]] uint64_t tr_peerMgrGetDesiredAvailable(tr_torrent const* tor);

[[nodiscard]] struct tr_peer_stat* tr_peerMgrPeerStats(tr_torrent const* tor, size_t* setme_count);

[[nodiscard]] tr_webseed_view tr_peerMgrWebseed(tr_torrent const* tor, size_t i);

/* @} */
