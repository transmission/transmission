// This file Copyright © 2008-2023 Mnemosyne LLC.
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

#include "transmission.h"

#include "tr-assert.h"

class tr_peerIo;

/**
 * @addtogroup networked_io Networked IO
 * @{
 */

struct tr_bandwidth_limits
{
    tr_kilobytes_per_second_t up_limit_KBps = 0;
    tr_kilobytes_per_second_t down_limit_KBps = 0;
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
    static constexpr size_t HistoryMSec = 2000U;
    static constexpr size_t IntervalMSec = HistoryMSec;
    static constexpr size_t GranularityMSec = 250;
    static constexpr size_t HistorySize = (IntervalMSec / GranularityMSec);

public:
    explicit tr_bandwidth(tr_bandwidth* newParent);

    tr_bandwidth()
        : tr_bandwidth(nullptr)
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

    // @brief Sets the peer. nullptr is allowed.
    void setPeer(std::weak_ptr<tr_peerIo> peer) noexcept
    {
        this->peer_ = std::move(peer);
    }

    /**
     * @brief Notify the bandwidth object that some of its allocated bandwidth has been consumed.
     * This is is usually invoked by the peer-io after a read or write.
     */
    void notifyBandwidthConsumed(tr_direction dir, size_t byte_count, bool is_piece_data, uint64_t now);

    /**
     * @brief allocate the next `period_msec`'s worth of bandwidth for the peer-ios to consume
     */
    void allocate(unsigned int period_msec);

    void setParent(tr_bandwidth* new_parent);

    [[nodiscard]] constexpr tr_priority_t getPriority() const noexcept
    {
        return this->priority_;
    }

    constexpr void setPriority(tr_priority_t prio) noexcept
    {
        this->priority_ = prio;
    }

    /**
     * @brief clamps `byte_count` down to a number that this bandwidth will allow to be consumed
     */
    [[nodiscard]] size_t clamp(tr_direction dir, size_t byte_count) const noexcept;

    /** @brief Get the raw total of bytes read or sent by this bandwidth subtree. */
    [[nodiscard]] auto getRawSpeedBytesPerSecond(uint64_t const now, tr_direction const dir) const
    {
        TR_ASSERT(tr_isDirection(dir));

        return getSpeedBytesPerSecond(this->band_[dir].raw_, HistoryMSec, now);
    }

    /** @brief Get the number of piece data bytes read or sent by this bandwidth subtree. */
    [[nodiscard]] auto getPieceSpeedBytesPerSecond(uint64_t const now, tr_direction const dir) const
    {
        TR_ASSERT(tr_isDirection(dir));

        return getSpeedBytesPerSecond(this->band_[dir].piece_, HistoryMSec, now);
    }

    /**
     * @brief Set the desired speed for this bandwidth subtree.
     * @see `tr_bandwidth::allocate`
     * @see `tr_bandwidth::getDesiredSpeed`
     */
    constexpr bool setDesiredSpeedBytesPerSecond(tr_direction dir, tr_bytes_per_second_t desired_speed)
    {
        auto& value = this->band_[dir].desired_speed_bps_;
        bool const did_change = desired_speed != value;
        value = desired_speed;
        return did_change;
    }

    /**
     * @brief Get the desired speed for the bandwidth subtree.
     * @see `tr_bandwidth::setDesiredSpeed`
     */
    [[nodiscard]] constexpr auto getDesiredSpeedBytesPerSecond(tr_direction dir) const
    {
        return this->band_[dir].desired_speed_bps_;
    }

    [[nodiscard]] bool is_maxed_out(tr_direction dir, uint64_t now_msec) const noexcept
    {
        if (!isLimited(dir))
        {
            return false;
        }

        auto const got = getPieceSpeedBytesPerSecond(now_msec, dir);
        auto const want = getDesiredSpeedBytesPerSecond(dir);
        return got >= want;
    }

    /**
     * @brief Set whether or not this bandwidth should throttle its peer-io's speeds
     */
    constexpr bool setLimited(tr_direction dir, bool is_limited)
    {
        bool* value = &this->band_[dir].is_limited_;
        bool const did_change = is_limited != *value;
        *value = is_limited;
        return did_change;
    }

    /**
     * @return nonzero if this bandwidth throttles its peer-ios speeds
     */
    [[nodiscard]] constexpr bool isLimited(tr_direction dir) const noexcept
    {
        return this->band_[dir].is_limited_;
    }

    /**
     * Almost all the time we do want to honor a parents' bandwidth cap, so that
     * (for example) a peer is constrained by a per-torrent cap and the global cap.
     * But when we set a torrent's speed mode to `TR_SPEEDLIMIT_UNLIMITED`, then
     * in that particular case we want to ignore the global speed limit...
     */
    constexpr bool honorParentLimits(tr_direction direction, bool is_enabled)
    {
        bool* value = &this->band_[direction].honor_parent_limits_;
        bool const did_change = is_enabled != *value;
        *value = is_enabled;
        return did_change;
    }

    [[nodiscard]] constexpr bool areParentLimitsHonored(tr_direction direction) const
    {
        TR_ASSERT(tr_isDirection(direction));

        return this->band_[direction].honor_parent_limits_;
    }

    [[nodiscard]] tr_bandwidth_limits getLimits() const;

    void setLimits(tr_bandwidth_limits const* limits);

private:
    struct RateControl
    {
        std::array<uint64_t, HistorySize> date_;
        std::array<size_t, HistorySize> size_;
        uint64_t cache_time_;
        tr_bytes_per_second_t cache_val_;
        int newest_;
    };

    struct Band
    {
        RateControl raw_;
        RateControl piece_;
        size_t bytes_left_;
        tr_bytes_per_second_t desired_speed_bps_;
        bool is_limited_ = false;
        bool honor_parent_limits_ = true;
    };

    static tr_bytes_per_second_t getSpeedBytesPerSecond(RateControl& r, unsigned int interval_msec, uint64_t now);

    [[nodiscard]] constexpr auto* parent() noexcept
    {
        return parent_;
    }

    void deparent() noexcept;

    static void notifyBandwidthConsumedBytes(uint64_t now, RateControl* r, size_t size);

    static void phaseOne(std::vector<tr_peerIo*>& peers, tr_direction dir);

    void allocateBandwidth(
        tr_priority_t parent_priority,
        unsigned int period_msec,
        std::vector<std::shared_ptr<tr_peerIo>>& peer_pool);

    mutable std::array<Band, 2> band_ = {};
    std::vector<tr_bandwidth*> children_;
    tr_bandwidth* parent_ = nullptr;
    std::weak_ptr<tr_peerIo> peer_;
    tr_priority_t priority_ = 0;
};

/* @} */
