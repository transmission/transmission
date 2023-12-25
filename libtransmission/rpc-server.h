// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <chrono>
#include <cstddef> // size_t
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/net.h"
#include "libtransmission/quark.h"
#include "libtransmission/utils-ev.h"

class tr_rpc_address;
struct tr_session;
struct tr_variant;
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
    V(TR_KEY_rpc_port, port_, tr_port, tr_port::from_host(TR_DEFAULT_RPC_PORT), "") \
    V(TR_KEY_rpc_password, salted_password_, std::string, "", "") \
    V(TR_KEY_rpc_socket_mode, socket_mode_, tr_mode_t, 0750, "") \
    V(TR_KEY_rpc_url, url_, std::string, TR_DEFAULT_RPC_URL_STR, "") \
    V(TR_KEY_rpc_username, username_, std::string, "", "") \
    V(TR_KEY_rpc_whitelist, whitelist_str_, std::string, TR_DEFAULT_RPC_WHITELIST, "") \
    V(TR_KEY_rpc_whitelist_enabled, is_whitelist_enabled_, bool, true, "")

class tr_rpc_server
{
public:
    tr_rpc_server(tr_session* session, tr_variant const& settings);
    ~tr_rpc_server();

    tr_rpc_server(tr_rpc_server&) = delete;
    tr_rpc_server(tr_rpc_server&&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&&) = delete;

    void load(tr_variant const& src);
    [[nodiscard]] tr_variant settings() const;
    [[nodiscard]] static tr_variant default_settings();

    [[nodiscard]] constexpr tr_port port() const noexcept
    {
        return port_;
    }

    void set_port(tr_port port) noexcept;

    [[nodiscard]] constexpr auto is_enabled() const noexcept
    {
        return is_enabled_;
    }

    void set_enabled(bool is_enabled);

    [[nodiscard]] constexpr auto is_whitelist_enabled() const noexcept
    {
        return is_whitelist_enabled_;
    }

    constexpr void set_whitelist_enabled(bool is_whitelist_enabled) noexcept
    {
        is_whitelist_enabled_ = is_whitelist_enabled;
    }

    [[nodiscard]] constexpr auto const& whitelist() const noexcept
    {
        return whitelist_str_;
    }

    void set_whitelist(std::string_view whitelist);

    [[nodiscard]] constexpr auto const& username() const noexcept
    {
        return username_;
    }

    void set_username(std::string_view username);

    [[nodiscard]] constexpr auto is_password_enabled() const noexcept
    {
        return is_password_enabled_;
    }

    void set_password_enabled(bool enabled);

    [[nodiscard]] constexpr auto const& get_salted_password() const noexcept
    {
        return salted_password_;
    }

    void set_password(std::string_view password) noexcept;

    [[nodiscard]] constexpr auto is_anti_brute_force_enabled() const noexcept
    {
        return is_anti_brute_force_enabled_;
    }

    void set_anti_brute_force_enabled(bool enabled) noexcept;

    [[nodiscard]] constexpr auto get_anti_brute_force_limit() const noexcept
    {
        return anti_brute_force_limit_;
    }

    constexpr void set_anti_brute_force_limit(int limit) noexcept
    {
        anti_brute_force_limit_ = limit;
    }

    [[nodiscard]] constexpr auto const& url() const noexcept
    {
        return url_;
    }

    void set_url(std::string_view url);

    [[nodiscard]] std::string get_bind_address() const;

    [[nodiscard]] constexpr auto socket_mode() const noexcept
    {
        return socket_mode_;
    }

#define V(key, name, type, default_value, comment) type name = type{ default_value };
    RPC_SETTINGS_FIELDS(V)
#undef V

private:
    static void handle_request(struct evhttp_request* req, void* arg);
    static void rpc_response_func(tr_session* /*session*/, tr_variant* content, void* user_data);

    void handle_web_client(struct evhttp_request* req);
    void handle_rpc_from_json(struct evhttp_request* req, std::string_view json);
    void handle_rpc(struct evhttp_request* req);
    [[nodiscard]] bool test_session_id(struct evhttp_request const* req);

    void serve_file(struct evhttp_request* req, std::string_view filename);
    [[nodiscard]] struct evbuffer* make_response(struct evhttp_request* req, std::string_view content);

    [[nodiscard]] bool is_address_allowed(std::string_view address) const noexcept;
    [[nodiscard]] bool is_hostname_allowed(evhttp_request const* req) const noexcept;

    void start();
    [[nodiscard]] std::chrono::seconds start_retry();
    void start_retry_cancel();
    void stop();
    void restart();

    std::unique_ptr<libdeflate_compressor, void (*)(libdeflate_compressor*)> compressor_;

    std::vector<std::string> host_whitelist_;
    std::vector<std::string> whitelist_;
    std::string const web_client_dir_;

    std::unique_ptr<tr_rpc_address> bind_address_;

    std::unique_ptr<libtransmission::Timer> start_retry_timer_;
    libtransmission::evhelpers::evhttp_unique_ptr httpd_;
    tr_session* const session_;

    size_t login_attempts_ = 0U;
    int start_retry_counter_ = 0;

    bool is_password_enabled_ = false;
};
