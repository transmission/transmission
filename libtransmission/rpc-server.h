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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/constants.h" // TrDefaultHttpServerBasePath
#include "libtransmission/net.h"
#include "libtransmission/quark.h"
#include "libtransmission/session-settings.h"
#include "libtransmission/types.h"
#include "libtransmission/utils-ev.h"

class tr_rpc_address;
struct tr_session;
struct tr_variant;
struct libdeflate_compressor;

namespace tr
{
class Timer;
}

class tr_rpc_server
{
public:
    using Settings = tr::RpcServerSettings;

    tr_rpc_server(tr_session* session, Settings&& settings);
    ~tr_rpc_server();

    tr_rpc_server(tr_rpc_server&) = delete;
    tr_rpc_server(tr_rpc_server&&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&&) = delete;

    void load(Settings&& settings);

    [[nodiscard]] constexpr Settings const& settings() const
    {
        return settings_;
    }

    [[nodiscard]] constexpr tr_port port() const noexcept
    {
        return settings_.port;
    }

    void set_port(tr_port port) noexcept;

    [[nodiscard]] constexpr auto is_enabled() const noexcept
    {
        return settings_.is_enabled;
    }

    void set_enabled(bool is_enabled);

    [[nodiscard]] constexpr auto is_whitelist_enabled() const noexcept
    {
        return settings_.is_whitelist_enabled;
    }

    constexpr void set_whitelist_enabled(bool is_whitelist_enabled) noexcept
    {
        settings_.is_whitelist_enabled = is_whitelist_enabled;
    }

    [[nodiscard]] constexpr auto const& whitelist() const noexcept
    {
        return settings_.whitelist_str;
    }

    void set_whitelist(std::string_view whitelist);

    [[nodiscard]] constexpr auto const& username() const noexcept
    {
        return settings_.username;
    }

    void set_username(std::string_view username);

    [[nodiscard]] constexpr auto is_password_enabled() const noexcept
    {
        return settings_.authentication_required;
    }

    void set_password_enabled(bool enabled);

    [[nodiscard]] constexpr auto const& get_salted_password() const noexcept
    {
        return settings_.salted_password;
    }

    void set_password(std::string_view password) noexcept;

    [[nodiscard]] constexpr auto is_anti_brute_force_enabled() const noexcept
    {
        return settings_.is_anti_brute_force_enabled;
    }

    void set_anti_brute_force_enabled(bool enabled) noexcept;

    [[nodiscard]] constexpr auto get_anti_brute_force_limit() const noexcept
    {
        return settings_.anti_brute_force_limit;
    }

    constexpr void set_anti_brute_force_limit(size_t limit) noexcept
    {
        settings_.anti_brute_force_limit = limit;
    }

    std::unique_ptr<libdeflate_compressor, void (*)(libdeflate_compressor*)> compressor;

    [[nodiscard]] constexpr auto const& url() const noexcept
    {
        return settings_.url;
    }

    void set_url(std::string_view url);

    [[nodiscard]] std::string get_bind_address() const;

    [[nodiscard]] constexpr auto socket_mode() const noexcept
    {
        return settings_.socket_mode;
    }

    Settings settings_;

    std::vector<std::string> host_whitelist_;
    std::vector<std::string> whitelist_;
    std::string const web_client_dir_;

    std::unique_ptr<tr_rpc_address> bind_address_;

    std::unique_ptr<tr::Timer> start_retry_timer;
    tr::evhelpers::evhttp_unique_ptr httpd;
    tr_session* const session;

    size_t login_attempts_ = 0U;
    int start_retry_counter = 0;
};
