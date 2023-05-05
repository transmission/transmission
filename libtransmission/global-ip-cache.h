// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono> // std::chrono::milliseconds
#include <memory> // std::unique_ptr
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "libtransmission/net.h"
#include "libtransmission/timer.h"
#include "libtransmission/web.h"

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

/**
 * Cache global IP addresses.
 *
 * This class caches 3 useful info:
 * 1. Whether your machine supports the IP protocol
 * 2. Source address used for global connections
 * 3. Global address
 *
 * The idea is, if this class successfully cached a source address, that means
 * you have connectivity to the public internet. And if the global address is
 * the same with the source address, then you are not behind an NAT.
 *
 */
class tr_global_ip_cache
{
public:
    tr_global_ip_cache(tr_web& web_in, libtransmission::TimerMaker& timer_maker_in);
    tr_global_ip_cache() = delete;
    ~tr_global_ip_cache();
    tr_global_ip_cache(tr_global_ip_cache const&) = delete;
    tr_global_ip_cache(tr_global_ip_cache&&) = delete;
    tr_global_ip_cache& operator=(tr_global_ip_cache const&) = delete;
    tr_global_ip_cache& operator=(tr_global_ip_cache&&) = delete;

    bool try_shutdown() noexcept;

    [[nodiscard]] std::optional<tr_address> global_addr(tr_address_type type) const noexcept
    {
        auto const lock = std::shared_lock{ global_addr_mutex_[type] };
        return global_addr_[type];
    }

    [[nodiscard]] std::optional<tr_address> global_source_addr(tr_address_type type) const noexcept
    {
        auto const lock = std::shared_lock{ source_addr_mutex_[type] };
        return source_addr_[type];
    }

    void set_settings_bind_addr(tr_address_type type, std::string_view bind_address) noexcept;
    [[nodiscard]] tr_address bind_addr(tr_address_type type) const noexcept;

    void update_addr(tr_address_type type) noexcept;
    bool set_global_addr(tr_address_type type, tr_address const& addr) noexcept;

    [[nodiscard]] constexpr auto has_ip_protocol(tr_address_type type) const noexcept
    {
        return has_ip_protocol_[type];
    }

private:
    template<typename T>
    using array_ip_t = std::array<T, NUM_TR_AF_INET_TYPES>;

    void unset_global_addr(tr_address_type type) noexcept;
    void set_source_addr(tr_address const& addr) noexcept;
    void unset_addr(tr_address_type type) noexcept;

    void start_timer(tr_address_type type, std::chrono::milliseconds msec) noexcept
    {
        upkeep_timers_[type]->startRepeating(msec);
    }

    void stop_timer(tr_address_type type) noexcept
    {
        upkeep_timers_[type]->stop();
    }

    [[nodiscard]] bool set_is_updating(tr_address_type type) noexcept;
    void unset_is_updating(tr_address_type type) noexcept;

    void update_global_addr(tr_address_type type) noexcept;
    void update_source_addr(tr_address_type type) noexcept;

    // Only use as a callback for web_->fetch()
    void on_response_ip_query(tr_address_type type, tr_web::FetchResponse const& response) noexcept;

    tr_web& web_;

    array_ip_t<std::optional<tr_address>> settings_bind_addr_;

    enum class is_updating_t
    {
        NO,
        YES,
        ABORT
    };
    array_ip_t<is_updating_t> is_updating_ = {};
    array_ip_t<std::mutex> is_updating_mutex_;
    array_ip_t<std::condition_variable> is_updating_cv_;

    // Never directly read/write IP addresses for the sake of being thread safe
    // Use global_*_addr() for read, and set_*_addr()/unset_*_addr() for write instead
    mutable array_ip_t<std::shared_mutex> global_addr_mutex_;
    array_ip_t<std::optional<tr_address>> global_addr_;
    mutable array_ip_t<std::shared_mutex> source_addr_mutex_;
    array_ip_t<std::optional<tr_address>> source_addr_;

    // Keep the timer at the bottom of the class definition so that it will be destructed first
    // We don't want it to trigger after the IP addresses have been destroyed
    // (The destructor will acquire the IP address locks before proceeding, but still)
    array_ip_t<std::unique_ptr<libtransmission::Timer>> upkeep_timers_;

    // Whether this machine supports this IP protocol
    array_ip_t<bool> has_ip_protocol_ = { true, true };

    array_ip_t<std::atomic_size_t> ix_service_ = {};
};
