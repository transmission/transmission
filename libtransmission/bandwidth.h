// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // size_t
#include <cstdint> // uint64_t
#include <memory>
#include <utility> // for std::move()
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/tr-assert.h"
#include "libtransmission/values.h"

class tr_peerIo;

/**
 * @addtogroup networked_io Networked IO
 * @{
 */

struct tr_bandwidth_limits
{
    libtransmission::Values::Speed up_limit;
    libtransmission::Values::Speed down_limit;
    bool up_limited = false;
    bool down_limited = false;
};

/**
 * `tr_bandwidth` is an object for measuring and constraining bandwidth speeds.
 *
 * `tr_bandwidth` objects can be "stacked" so that a peer can be made to obey
 * multiple constraints (for example, obeying the global speed limit and a
 * per-torrent speed limit).
 *
 * HIERARCHY
 *
 *   Transmission's bandwidth hierarchy is a tree.
 *   At the top is the global bandwidth object owned by `tr_session`.
 *   Its children are per-torrent bandwidth objects owned by `tr_torrent`.
 *   Underneath those are per-peer bandwidth objects owned by `tr_peer`.
 *
 *   `tr_session` also owns a `tr_handshake`'s bandwidths, so that the handshake
 *   I/O can be counted in the global raw totals. When the handshake is done,
 *   the bandwidth's ownership passes to a `tr_peer`.
 *
 * MEASURING
 *
 *   When you ask a bandwidth object for its speed, it gives the speed of the
 *   subtree underneath it as well. So you can get Transmission's overall
 *   speed by querying `tr_session`'s bandwidth, per-torrent speeds by asking
 *   `tr_torrent`'s bandwidth, and per-peer speeds by asking `tr_peer`'s bandwidth.
 *
 * CONSTRAINING
 *
 *   Call `tr_bandwidth::allocate()` periodically. `tr_bandwidth` knows its current
 *   speed and will decide how many bytes to make available over the
 *   user-specified period to reach the user-specified desired speed.
 *   If appropriate, it notifies its peer-ios that new bandwidth is available.
 *
 *   `tr_bandwidth::allocate()` operates on the `tr_bandwidth` subtree, so usually
 *   you'll only need to invoke it for the top-level `tr_session` bandwidth.
 *
 *   The peer-ios all have a pointer to their associated `tr_bandwidth` object,
 *   and call `tr_bandwidth::clamp()` before performing I/O to see how much
 *   bandwidth they can safely use.
 */
struct tr_bandwidth
{
private:
    using Speed = libtransmission::Values::Speed;

    static constexpr auto HistoryMSec = 2000U;
    static constexpr auto GranularityMSec = 250U;
    static constexpr auto HistorySize = HistoryMSec / GranularityMSec;

public:
    explicit tr_bandwidth(tr_bandwidth* parent, bool is_group = false);

    explicit tr_bandwidth(bool is_group = false)
        : tr_bandwidth{ nullptr, is_group }
    {
    }

    ~tr_bandwidth() noexcept
    {
        deparent();
    }

    tr_bandwidth& operator=(tr_bandwidth&&) = delete;
    tr_bandwidth& operator=(tr_bandwidth) = delete;
    tr_bandwidth(tr_bandwidth&&) = delete;
    tr_bandwidth(tr_bandwidth&) = delete;

    /**
     * @brief Sets the peer. nullptr is allowed.
     */
    void set_peer(std::weak_ptr<tr_peerIo> peer) noexcept
    {
        peer_ = std::move(peer);
    }

    /**
     * @brief Notify the bandwidth object that some of its allocated bandwidth has been consumed.
     * This is is usually invoked by the peer-io after a read or write.
     */
    void notify_bandwidth_consumed(tr_direction dir, size_t byte_count, bool is_piece_data, uint64_t now);

    /**
     * @brief allocate the next `period_msec`'s worth of bandwidth for the peer-ios to consume
     */
    void allocate(uint64_t period_msec);

    void set_parent(tr_bandwidth* new_parent);

    [[nodiscard]] constexpr auto get_priority() const noexcept
    {
        return priority_;
    }

    constexpr void set_priority(tr_priority_t prio) noexcept
    {
        priority_ = prio;
    }

    /**
     * @brief clamps `byte_count` down to a number that this bandwidth will allow to be consumed
     */
    [[nodiscard]] size_t clamp(tr_direction dir, size_t byte_count) const noexcept;

    /** @brief Get the raw total of bytes read or sent by this bandwidth subtree. */
    [[nodiscard]] auto get_raw_speed(uint64_t const now, tr_direction const dir) const
    {
        TR_ASSERT(tr_isDirection(dir));

        return get_speed(band_[dir].raw_, HistoryMSec, now);
    }

    /** @brief Get the number of piece data bytes read or sent by this bandwidth subtree. */
    [[nodiscard]] auto get_piece_speed(uint64_t const now, tr_direction const dir) const
    {
        TR_ASSERT(tr_isDirection(dir));

        return get_speed(band_[dir].piece_, HistoryMSec, now);
    }

    /**
     * @brief Set the desired speed for this bandwidth subtree.
     * @see `tr_bandwidth::allocate`
     * @see `tr_bandwidth::getDesiredSpeed`
     */
    constexpr bool set_desired_speed(tr_direction dir, Speed desired_speed)
    {
        auto& value = band_[dir].desired_speed_;
        auto const did_change = desired_speed != value;
        value = desired_speed;
        return did_change;
    }

    /**
     * @brief Get the desired speed for the bandwidth subtree.
     * @see `tr_bandwidth::setDesiredSpeed`
     */
    [[nodiscard]] constexpr auto get_desired_speed(tr_direction dir) const
    {
        return band_[dir].desired_speed_;
    }

    [[nodiscard]] bool is_maxed_out(tr_direction dir, uint64_t now_msec) const noexcept
    {
        if (!is_limited(dir))
        {
            return false;
        }

        auto const got = get_raw_speed(now_msec, dir);
        auto const want = get_desired_speed(dir);
        return got >= want;
    }

    /**
     * @brief Set whether or not this bandwidth should throttle its peer-io's speeds
     */
    constexpr bool set_limited(tr_direction dir, bool is_limited)
    {
        auto& value = band_[dir].is_limited_;
        auto const did_change = is_limited != value;
        value = is_limited;
        return did_change;
    }

    /**
     * @return nonzero if this bandwidth throttles its peer-ios speeds
     */
    [[nodiscard]] constexpr bool is_limited(tr_direction dir) const noexcept
    {
        return band_[dir].is_limited_;
    }

    /**
     * Almost all the time we do want to honor a parents' bandwidth cap, so that
     * (for example) a peer is constrained by a per-torrent cap and the global cap.
     * But when we set a torrent's speed mode to `TR_SPEEDLIMIT_UNLIMITED`, then
     * in that particular case we want to ignore the global speed limit...
     */
    constexpr bool honor_parent_limits(tr_direction direction, bool is_enabled)
    {
        auto& value = band_[direction].honor_parent_limits_;
        auto const did_change = is_enabled != value;
        value = is_enabled;
        return did_change;
    }

    [[nodiscard]] constexpr bool are_parent_limits_honored(tr_direction direction) const
    {
        return band_[direction].honor_parent_limits_;
    }

    [[nodiscard]] tr_bandwidth_limits get_limits() const;

    void set_limits(tr_bandwidth_limits const& limits);

private:
    struct RateControl
    {
        std::array<uint64_t, HistorySize> date_;
        std::array<size_t, HistorySize> size_;
        uint64_t cache_time_;
        Speed cache_val_;
        int newest_;
    };

    struct Band
    {
        RateControl raw_;
        RateControl piece_;
        size_t bytes_left_;
        Speed desired_speed_;
        bool is_limited_ = false;
        bool honor_parent_limits_ = true;
    };

    static Speed get_speed(RateControl& r, unsigned int interval_msec, uint64_t now);

    [[nodiscard]] constexpr auto* parent() noexcept
    {
        return parent_;
    }

    void deparent() noexcept;

    static void notify_bandwidth_consumed_bytes(uint64_t now, RateControl& r, size_t size);

    static void phase_one(std::vector<tr_peerIo*>& peers, tr_direction dir);

    void allocate_bandwidth(
        tr_priority_t parent_priority,
        uint64_t period_msec,
        std::vector<std::shared_ptr<tr_peerIo>>& peer_pool);

    mutable std::array<Band, 2> band_ = {};
    std::vector<tr_bandwidth*> children_;
    tr_bandwidth* parent_ = nullptr;
    std::weak_ptr<tr_peerIo> peer_;
    tr_priority_t priority_;
};

/* @} */

constexpr auto tr_isPriority(tr_priority_t p)
{
    return p == TR_PRI_LOW || p == TR_PRI_NORMAL || p == TR_PRI_HIGH;
}
