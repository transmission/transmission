// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "net.h"
#include "utils-ev.h"

struct evhttp;
struct tr_variant;
struct tr_rpc_address;
struct libdeflate_compressor;

namespace libtransmission
{
class Timer;
}

#define RPC_SETTINGS_FIELDS(V) \
    V(TR_KEY_anti_brute_force_enabled, is_anti_brute_force_enabled_, bool, false, "") \
    V(TR_KEY_anti_brute_force_threshold, anti_brute_force_limit_, size_t, 100U, "") \
    V(TR_KEY_rpc_authentication_required, authentication_required_, bool, false, "") \
    V(TR_KEY_rpc_bind_address, bind_address_str_, std::string, "0.0.0.0", "") \
    V(TR_KEY_rpc_enabled, is_enabled_, bool, false, "") \
    V(TR_KEY_rpc_host_whitelist, host_whitelist_str_, std::string, "", "") \
    V(TR_KEY_rpc_host_whitelist_enabled, is_host_whitelist_enabled_, bool, true, "") \
    V(TR_KEY_rpc_port, port_, tr_port, tr_port::fromHost(TR_DEFAULT_RPC_PORT), "") \
    V(TR_KEY_rpc_password, salted_password_, std::string, "", "") \
    V(TR_KEY_rpc_socket_mode, socket_mode_, tr_mode_t, 0750, "") \
    V(TR_KEY_rpc_url, url_, std::string, TR_DEFAULT_RPC_URL_STR, "") \
    V(TR_KEY_rpc_username, username_, std::string, "", "") \
    V(TR_KEY_rpc_whitelist, whitelist_str_, std::string, TR_DEFAULT_RPC_WHITELIST, "") \
    V(TR_KEY_rpc_whitelist_enabled, is_whitelist_enabled_, bool, true, "")

class tr_rpc_server
{
public:
    tr_rpc_server(tr_session* session, tr_variant* settings);
    ~tr_rpc_server();

    tr_rpc_server(tr_rpc_server&) = delete;
    tr_rpc_server(tr_rpc_server&&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&&) = delete;

    void load(tr_variant* src);
    void save(tr_variant* tgt) const;
    static void defaultSettings(tr_variant* tgt);

    [[nodiscard]] constexpr tr_port port() const noexcept
    {
        return port_;
    }

    void setPort(tr_port port) noexcept;

    [[nodiscard]] constexpr auto isEnabled() const noexcept
    {
        return is_enabled_;
    }

    void setEnabled(bool is_enabled);

    [[nodiscard]] constexpr auto isWhitelistEnabled() const noexcept
    {
        return is_whitelist_enabled_;
    }

    constexpr void setWhitelistEnabled(bool is_whitelist_enabled) noexcept
    {
        is_whitelist_enabled_ = is_whitelist_enabled;
    }

    [[nodiscard]] constexpr auto const& whitelist() const noexcept
    {
        return whitelist_str_;
    }

    void setWhitelist(std::string_view whitelist);

    [[nodiscard]] constexpr auto const& username() const noexcept
    {
        return username_;
    }

    void setUsername(std::string_view username);

    [[nodiscard]] constexpr auto isPasswordEnabled() const noexcept
    {
        return is_password_enabled_;
    }

    void setPasswordEnabled(bool enabled);

    [[nodiscard]] constexpr auto const& getSaltedPassword() const noexcept
    {
        return salted_password_;
    }

    void setPassword(std::string_view password) noexcept;

    [[nodiscard]] constexpr auto isAntiBruteForceEnabled() const noexcept
    {
        return is_anti_brute_force_enabled_;
    }

    void setAntiBruteForceEnabled(bool enabled) noexcept;

    [[nodiscard]] constexpr auto getAntiBruteForceLimit() const noexcept
    {
        return anti_brute_force_limit_;
    }

    constexpr void setAntiBruteForceLimit(int limit) noexcept
    {
        anti_brute_force_limit_ = limit;
    }

    std::unique_ptr<libdeflate_compressor, void (*)(libdeflate_compressor*)> compressor;

    [[nodiscard]] constexpr auto const& url() const noexcept
    {
        return url_;
    }

    void setUrl(std::string_view url);

    [[nodiscard]] std::string getBindAddress() const;

    [[nodiscard]] constexpr auto socketMode() const noexcept
    {
        return socket_mode_;
    }

#define V(key, name, type, default_value, comment) type name = type{ default_value };
    RPC_SETTINGS_FIELDS(V)
#undef V

    std::vector<std::string> host_whitelist_;
    std::vector<std::string> whitelist_;
    std::string const web_client_dir_;

    std::unique_ptr<struct tr_rpc_address> bind_address_;

    std::unique_ptr<libtransmission::Timer> start_retry_timer;
    libtransmission::evhelpers::evhttp_unique_ptr httpd;
    tr_session* const session;

    size_t login_attempts_ = 0U;
    int start_retry_counter = 0;

    bool is_password_enabled_ = false;
};
