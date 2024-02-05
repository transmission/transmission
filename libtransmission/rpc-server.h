// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/net.h"
#include "libtransmission/quark.h"
#include "libtransmission/settings.h"

class tr_rpc_address;
struct tr_session;
struct tr_variant;
struct libdeflate_compressor;

namespace libtransmission
{
class Timer;
}

class tr_rpc_settings final : public libtransmission::Settings
{
public:
    tr_rpc_settings() = default;

    explicit tr_rpc_settings(tr_variant const& src)
    {
        load(src);
    }

    // NB: When adding a new field here, you must also add it to
    // fields() if you want it to be in session-settings.json
    size_t anti_brute_force_limit = 100U;
    bool authentication_required = false;
    std::string bind_address_str = "0.0.0.0";
    std::string host_whitelist_str = "";
    bool is_anti_brute_force_enabled = false;
    bool is_enabled = false;
    bool is_host_whitelist_enabled = true;
    bool is_whitelist_enabled = true;
    tr_port port = tr_port::from_host(TR_DEFAULT_RPC_PORT);
    std::string salted_password = "";
    tr_mode_t socket_mode = 0750;
    std::string url = TR_DEFAULT_RPC_URL_STR;
    std::string username = "";
    std::string whitelist_str = TR_DEFAULT_RPC_WHITELIST;

private:
    [[nodiscard]] Fields fields() override
    {
        return {
            { TR_KEY_anti_brute_force_enabled, &is_anti_brute_force_enabled },
            { TR_KEY_anti_brute_force_threshold, &anti_brute_force_limit },
            { TR_KEY_rpc_authentication_required, &authentication_required },
            { TR_KEY_rpc_bind_address, &bind_address_str },
            { TR_KEY_rpc_enabled, &is_enabled },
            { TR_KEY_rpc_host_whitelist, &host_whitelist_str },
            { TR_KEY_rpc_host_whitelist_enabled, &is_host_whitelist_enabled },
            { TR_KEY_rpc_port, &port },
            { TR_KEY_rpc_password, &salted_password },
            { TR_KEY_rpc_socket_mode, &socket_mode },
            { TR_KEY_rpc_url, &url },
            { TR_KEY_rpc_username, &username },
            { TR_KEY_rpc_whitelist, &whitelist_str },
            { TR_KEY_rpc_whitelist_enabled, &is_whitelist_enabled },
        };
    }
};

class tr_rpc_server
{
public:
    tr_rpc_server(tr_session* session, tr_rpc_settings settings);
    ~tr_rpc_server();

    tr_rpc_server(tr_rpc_server&) = delete;
    tr_rpc_server(tr_rpc_server&&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&&) = delete;

    void load(tr_rpc_settings settings);

    [[nodiscard]] constexpr tr_rpc_settings const& settings() const
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

    constexpr void set_anti_brute_force_limit(int limit) noexcept
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

    tr_rpc_settings settings_;

    std::vector<std::string> host_whitelist_;
    std::vector<std::string> whitelist_;
    std::string const web_client_dir_;

    std::unique_ptr<tr_rpc_address> bind_address_;

    std::unique_ptr<libtransmission::Timer> start_retry_timer;
    libtransmission::evhelpers::evhttp_unique_ptr httpd;
    tr_session* const session;

    size_t login_attempts_ = 0U;
    int start_retry_counter = 0;
};
