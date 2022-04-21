// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
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

struct event;
struct evhttp;
struct tr_variant;
struct tr_rpc_address;
struct libdeflate_compressor;

class tr_rpc_server
{
public:
    tr_rpc_server(tr_session* session, tr_variant* settings);
    ~tr_rpc_server();

    tr_rpc_server(tr_rpc_server&) = delete;
    tr_rpc_server(tr_rpc_server&&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&) = delete;
    tr_rpc_server& operator=(tr_rpc_server&&) = delete;

    [[nodiscard]] constexpr tr_port port() const noexcept
    {
        return port_;
    }

    void setPort(tr_port) noexcept;

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

    void setWhitelist(std::string_view whitelist) noexcept;

    [[nodiscard]] constexpr auto isPasswordEnabled() const noexcept
    {
        return is_password_enabled_;
    }

    void setPasswordEnabled(bool enabled) noexcept;

    std::shared_ptr<libdeflate_compressor> compressor;

    std::vector<std::string> hostWhitelist;
    std::vector<std::string> whitelist;
    std::string salted_password;
    std::string username;
    std::string whitelist_str_;
    std::string url;

    std::unique_ptr<struct tr_rpc_address> bindAddress;

    struct event* start_retry_timer = nullptr;
    struct evhttp* httpd = nullptr;
    tr_session* const session;

    int antiBruteForceThreshold = 0;
    int loginattempts = 0;
    int start_retry_counter = 0;
    static int constexpr DefaultRpcSocketMode = 0750;
    int rpc_socket_mode = DefaultRpcSocketMode;

    tr_port port_;

    bool isAntiBruteForceEnabled = false;
    bool is_enabled_ = false;
    bool isHostWhitelistEnabled = false;
    bool is_password_enabled_ = false;
    bool is_whitelist_enabled_ = false;
};

void tr_rpcSetUrl(tr_rpc_server* server, std::string_view url);

std::string const& tr_rpcGetUrl(tr_rpc_server const* server);

int tr_rpcSetTest(tr_rpc_server const* server, char const* whitelist, char** allocme_errmsg);

int tr_rpcGetRPCSocketMode(tr_rpc_server const* server);

void tr_rpcSetPassword(tr_rpc_server* server, std::string_view password);

std::string const& tr_rpcGetPassword(tr_rpc_server const* server);

void tr_rpcSetUsername(tr_rpc_server* server, std::string_view username);

std::string const& tr_rpcGetUsername(tr_rpc_server const* server);

bool tr_rpcGetAntiBruteForceEnabled(tr_rpc_server const* server);

void tr_rpcSetAntiBruteForceEnabled(tr_rpc_server* server, bool is_enabled);

int tr_rpcGetAntiBruteForceThreshold(tr_rpc_server const* server);

void tr_rpcSetAntiBruteForceThreshold(tr_rpc_server* server, int badRequests);

char const* tr_rpcGetBindAddress(tr_rpc_server const* server);
