// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <cstddef> // size_t
#include <cstdint> // uint8_t, uint64_t
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "libtransmission/transmission.h" // tr_block_span_t (ptr only)

#include "libtransmission/net.h" /* tr_address */
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
 * Peer information that should be kept around even when not connected,
 * e.g. to help us decide which peers to connect to.
 */
class tr_peer_info
{
public:
    tr_peer_info(tr_socket_address socket_address_in, uint8_t pex_flags, tr_peer_from from)
        : socket_address_{ std::move(socket_address_in) }
        , from_first_{ from }
        , from_best_{ from }
    {
        ++n_known_peers;

        set_pex_flags(pex_flags);
    }

    tr_peer_info(tr_peer_info&&) = delete;
    tr_peer_info(tr_peer_info const&) = delete;
    tr_peer_info& operator=(tr_peer_info&&) = delete;
    tr_peer_info& operator=(tr_peer_info const&) = delete;

    ~tr_peer_info()
    {
        [[maybe_unused]] auto const n_prev = n_known_peers--;
        TR_ASSERT(n_prev > 0U);
    }

    [[nodiscard]] static auto known_peer_count() noexcept
    {
        return n_known_peers.load();
    }

    // ---

    [[nodiscard]] constexpr auto const& socket_address() const noexcept
    {
        return socket_address_;
    }

    [[nodiscard]] constexpr auto& port() noexcept
    {
        return socket_address_.second;
    }

    [[nodiscard]] auto display_name() const
    {
        return addr().display_name(port());
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

    constexpr void set_connectable(bool value = true) noexcept
    {
        is_connectable_ = value;
    }

    [[nodiscard]] constexpr auto const& is_connectable() const noexcept
    {
        return is_connectable_;
    }

    // ---

    constexpr void set_utp_supported(bool value = true) noexcept
    {
        is_utp_supported_ = value;
    }

    [[nodiscard]] constexpr auto supports_utp() const noexcept
    {
        return is_utp_supported_;
    }

    // ---

    [[nodiscard]] constexpr auto compare_by_connection_time(tr_peer_info const& that) const noexcept
    {
        return tr_compare_3way(last_connection_at_, that.last_connection_at_);
    }

    [[nodiscard]] constexpr auto compare_by_failure_count(tr_peer_info const& that) const noexcept
    {
        return tr_compare_3way(num_fails_, that.num_fails_);
    }

    [[nodiscard]] constexpr auto compare_by_piece_data_time(tr_peer_info const& that) const noexcept
    {
        return tr_compare_3way(last_piece_data_time_, that.last_piece_data_time_);
    }

    // ---

    constexpr auto set_connected(bool value = true) noexcept
    {
        is_connected_ = value;
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

    [[nodiscard]] constexpr auto last_connection_attempt_succeeded() const noexcept
    {
        return last_connection_at_ >= last_connection_attempt_at_;
    }

    [[nodiscard]] constexpr auto connection_attempt_time() const noexcept
    {
        return last_connection_attempt_at_;
    }

    constexpr void set_connection_attempt_time(time_t value) noexcept
    {
        last_connection_attempt_at_ = value;
    }

    constexpr void set_connection_time(time_t value) noexcept
    {
        last_connection_at_ = value;

        last_piece_data_time_ = {};
    }

    constexpr void set_latest_piece_data_time(time_t value) noexcept
    {
        last_piece_data_time_ = value;
    }

    [[nodiscard]] constexpr bool has_transferred_piece_data() const noexcept
    {
        return last_piece_data_time_ != time_t{};
    }

    [[nodiscard]] constexpr auto reconnect_interval_has_passed(time_t const now) const noexcept
    {
        auto const time_since_last_connection_attempt = now - last_connection_attempt_at_;
        return time_since_last_connection_attempt >= get_reconnect_interval_secs(now);
    }

    [[nodiscard]] constexpr std::optional<time_t> idle_secs(time_t now) const noexcept
    {
        if (!last_connection_attempt_succeeded())
        {
            return {};
        }

        return now - std::max(last_piece_data_time_, last_connection_at_);
    }

    // ---

    constexpr void on_connection_failed()
    {
        if (num_fails_ != std::numeric_limits<decltype(num_fails_)>::max())
        {
            ++num_fails_;
        }
    }

    [[nodiscard]] constexpr auto connection_failure_count() const noexcept
    {
        return num_fails_;
    }

    // ---

    constexpr void set_pex_flags(uint8_t pex_flags)
    {
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
        auto ret = uint8_t{};

        if (is_connectable_ && *is_connectable_)
        {
            ret |= ADDED_F_CONNECTABLE;
        }

        if (is_seed_)
        {
            ret |= ADDED_F_SEED_FLAG;
        }

        if (is_utp_supported_ && *is_utp_supported_)
        {
            ret |= ADDED_F_UTP_FLAGS;
        }

        return ret;
    }

private:
    [[nodiscard]] constexpr tr_address const& addr() const noexcept
    {
        return socket_address_.first;
    }

    [[nodiscard]] constexpr tr_port const& port() const noexcept
    {
        return socket_address_.second;
    }

    [[nodiscard]] constexpr int get_reconnect_interval_secs(time_t const now) const noexcept
    {
        auto sec = int{};
        auto const unreachable = is_connectable_ && !*is_connectable_;

        /* if we were recently connected to this peer and transferring piece
         * data, try to reconnect to them sooner rather that later -- we don't
         * want network troubles to get in the way of a good peer. */
        if (!unreachable && now - last_piece_data_time_ <= MinimumReconnectIntervalSecs * 2)
        {
            sec = MinimumReconnectIntervalSecs;
        }
        /* otherwise, the interval depends on how many times we've tried
         * and failed to connect to the peer */
        else
        {
            auto step = this->num_fails_;

            /* penalize peers that were unreachable the last time we tried */
            if (unreachable)
            {
                step += 2;
            }

            switch (step)
            {
            case 0:
                sec = 0;
                break;

            case 1:
                sec = 10;
                break;

            case 2:
                sec = 60 * 2;
                break;

            case 3:
                sec = 60 * 15;
                break;

            case 4:
                sec = 60 * 30;
                break;

            case 5:
                sec = 60 * 60;
                break;

            default:
                sec = 60 * 120;
                break;
            }
        }

        return sec;
    }

    // the minimum we'll wait before attempting to reconnect to a peer
    static auto constexpr MinimumReconnectIntervalSecs = int{ 5 };

    tr_socket_address socket_address_;

    static auto inline n_known_peers = std::atomic<size_t>{};

    time_t last_connection_attempt_at_ = {};
    time_t last_connection_at_ = {};
    time_t last_piece_data_time_ = {};

    mutable std::optional<bool> blocklisted_;
    std::optional<bool> is_connectable_;
    std::optional<bool> is_utp_supported_;

    tr_peer_from const from_first_; // where the peer was first found
    tr_peer_from from_best_; // the "best" place where this peer was found

    uint8_t num_fails_ = {};

    bool is_banned_ = false;
    bool is_connected_ = false;
    bool is_seed_ = false;
};

struct tr_pex
{
    tr_pex() = default;

    tr_pex(tr_address addr_in, tr_port port_in, uint8_t flags_in = {})
        : addr{ addr_in }
        , port{ port_in }
        , flags{ flags_in }
    {
    }

    template<typename OutputIt>
    OutputIt to_compact_ipv4(OutputIt out) const
    {
        return this->addr.to_compact_ipv4(out, this->port);
    }

    template<typename OutputIt>
    OutputIt to_compact_ipv6(OutputIt out) const
    {
        return this->addr.to_compact_ipv6(out, this->port);
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv4(OutputIt out, tr_pex const* pex, size_t n_pex)
    {
        for (size_t i = 0; i < n_pex; ++i)
        {
            out = pex[i].to_compact_ipv4(out);
        }
        return out;
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv6(OutputIt out, tr_pex const* pex, size_t n_pex)
    {
        for (size_t i = 0; i < n_pex; ++i)
        {
            out = pex[i].to_compact_ipv6(out);
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

    template<typename OutputIt>
    [[nodiscard]] OutputIt display_name(OutputIt out) const
    {
        return addr.display_name(out, port);
    }

    [[nodiscard]] std::string display_name() const
    {
        return addr.display_name(port);
    }

    [[nodiscard]] int compare(tr_pex const& that) const noexcept // <=>
    {
        if (auto const i = addr.compare(that.addr); i != 0)
        {
            return i;
        }

        if (port != that.port)
        {
            return port < that.port ? -1 : 1;
        }

        return 0;
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
        return addr.is_valid_for_peers(port);
    }

    tr_address addr = {};
    tr_port port = {}; /* this field is in network byte order */
    uint8_t flags = 0;
};

constexpr bool tr_isPex(tr_pex const* pex)
{
    return pex != nullptr && pex->addr.is_valid();
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
