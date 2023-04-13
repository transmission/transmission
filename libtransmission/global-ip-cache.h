// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono> // operator ""min, operator ""s, std::chrono::milliseconds
#include <memory> // std::unique_ptr
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string_view> // operator ""sv

#include "net.h"
#include "timer.h"
#include "tr-assert.h"
#include "web.h"

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

using namespace std::literals;

struct tr_session;

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
 * Note: This class isn't meant to be accessed by anyone other than tr_session,
 * so it has no public methods.
 */
class tr_global_ip_cache
{
public:
    explicit tr_global_ip_cache(tr_session& session_in);
    ~tr_global_ip_cache();
    tr_global_ip_cache(tr_global_ip_cache const&) = delete;
    tr_global_ip_cache(tr_global_ip_cache&&) = delete;
    tr_global_ip_cache& operator=(tr_global_ip_cache const&) = delete;
    tr_global_ip_cache& operator=(tr_global_ip_cache&&) = delete;
    bool try_shutdown() noexcept;

    [[nodiscard]] bool has_ip_protocol(tr_address_type type) const noexcept
    {
        TR_ASSERT(type == TR_AF_INET || type == TR_AF_INET6);
        return has_ip_protocol_[type];
    }

    [[nodiscard]] std::optional<tr_address> global_addr(tr_address_type type) const noexcept;
    [[nodiscard]] std::optional<tr_address> global_source_addr(tr_address_type type) const noexcept;

    [[nodiscard]] tr_address bind_addr(tr_address_type type) const noexcept;

    void update_addr(tr_address_type type) noexcept;

private:
    void set_global_addr(tr_address const& addr) noexcept;
    void unset_global_addr(tr_address_type type) noexcept;
    void set_source_addr(tr_address const& addr) noexcept;
    void unset_addr(tr_address_type type) noexcept;

    void start_timer(tr_address_type type, std::chrono::milliseconds msec) noexcept;
    void stop_timer(tr_address_type type) noexcept;
    [[nodiscard]] bool set_is_updating(tr_address_type type) noexcept;
    void unset_is_updating(tr_address_type type) noexcept;

    void update_global_addr(tr_address_type type) noexcept;
    void update_source_addr(tr_address_type type) noexcept;

    // Only use as a callback for web_->fetch()
    void on_response_ip_query(tr_address_type type, tr_web::FetchResponse const& response) noexcept;

    [[nodiscard]] static std::optional<tr_address> get_global_source_address(tr_address const& bind_addr, int& err_out);
    [[nodiscard]] static std::optional<tr_address> get_source_address(
        tr_address const& dst_addr,
        tr_port dst_port,
        tr_address const& bind_addr,
        int& err_out);

    template<typename T>
    using array_ip_t = std::array<T, NUM_TR_AF_INET_TYPES>;

    tr_session const& session_;

    enum class is_updating_t
    {
        NO,
        YES,
        ABORT
    };
    array_ip_t<is_updating_t> is_updating_ = {};
    array_ip_t<std::mutex> is_updating_mutex_;
    array_ip_t<std::condition_variable> is_updating_cv_;

    // Whether this machine supports this IP protocol
    array_ip_t<std::atomic_bool> has_ip_protocol_ = { true, true };

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

    array_ip_t<std::atomic_size_t> ix_service_ = {};
    static auto constexpr IPQueryServices = std::array{ "https://icanhazip.com"sv, "https://api64.ipify.org"sv };
    static auto constexpr UpkeepInterval = 30min;
    static auto constexpr RetryUpkeepInterval = 30s;
};
