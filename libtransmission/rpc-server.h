/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string_view>

#include "transmission.h"
#include "tr-macros.h"

#include "net.h"

struct tr_rpc_server;
struct tr_variant;

tr_rpc_server* tr_rpcInit(tr_session* session, tr_variant* settings);

void tr_rpcClose(tr_rpc_server** freeme);

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
