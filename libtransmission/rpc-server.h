// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <list>
#include <memory>
#include <string>
#include <string_view>

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

    std::shared_ptr<libdeflate_compressor> compressor;

    std::list<std::string> hostWhitelist;
    std::list<std::string> whitelist;
    std::string salted_password;
    std::string username;
    std::string whitelistStr;
    std::string url;

    struct tr_rpc_address* bindAddress;

    struct event* start_retry_timer = nullptr;
    struct evhttp* httpd = nullptr;
    tr_session* const session;

    int antiBruteForceThreshold = 0;
    int loginattempts = 0;
    int start_retry_counter = 0;

    tr_port port = 0;

    bool isAntiBruteForceEnabled = false;
    bool isEnabled = false;
    bool isHostWhitelistEnabled = false;
    bool isPasswordEnabled = false;
    bool isWhitelistEnabled = false;
};

void tr_rpcSetEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcIsEnabled(tr_rpc_server const* server);

void tr_rpcSetPort(tr_rpc_server* server, tr_port port);

tr_port tr_rpcGetPort(tr_rpc_server const* server);

void tr_rpcSetUrl(tr_rpc_server* server, std::string_view url);

std::string const& tr_rpcGetUrl(tr_rpc_server const* server);

int tr_rpcSetTest(tr_rpc_server const* server, char const* whitelist, char** allocme_errmsg);

void tr_rpcSetWhitelistEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcGetWhitelistEnabled(tr_rpc_server const* server);

void tr_rpcSetWhitelist(tr_rpc_server* server, std::string_view whitelist);

std::string const& tr_rpcGetWhitelist(tr_rpc_server const* server);

void tr_rpcSetPassword(tr_rpc_server* server, std::string_view password);

std::string const& tr_rpcGetPassword(tr_rpc_server const* server);

void tr_rpcSetUsername(tr_rpc_server* server, std::string_view username);

std::string const& tr_rpcGetUsername(tr_rpc_server const* server);

void tr_rpcSetPasswordEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcIsPasswordEnabled(tr_rpc_server const* session);

bool tr_rpcGetAntiBruteForceEnabled(tr_rpc_server const* server);

void tr_rpcSetAntiBruteForceEnabled(tr_rpc_server* server, bool is_enabled);

int tr_rpcGetAntiBruteForceThreshold(tr_rpc_server const* server);

void tr_rpcSetAntiBruteForceThreshold(tr_rpc_server* server, int badRequests);

char const* tr_rpcGetBindAddress(tr_rpc_server const* server);
