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

#include "variant.h"

typedef struct tr_rpc_server tr_rpc_server;

tr_rpc_server* tr_rpcInit(tr_session* session, tr_variant* settings);

void tr_rpcClose(tr_rpc_server** freeme);

void tr_rpcSetEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcIsEnabled(tr_rpc_server const* server);

void tr_rpcSetPort(tr_rpc_server* server, tr_port port);

tr_port tr_rpcGetPort(tr_rpc_server const* server);

void tr_rpcSetUrl(tr_rpc_server* server, char const* url);

char const* tr_rpcGetUrl(tr_rpc_server const* server);

int tr_rpcSetTest(tr_rpc_server const* server, char const* whitelist, char** allocme_errmsg);

void tr_rpcSetWhitelistEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcGetWhitelistEnabled(tr_rpc_server const* server);

void tr_rpcSetWhitelist(tr_rpc_server* server, char const* whitelist);

char const* tr_rpcGetWhitelist(tr_rpc_server const* server);

void tr_rpcSetHostWhitelistEnabled(tr_rpc_server* server, bool isEnabled);

void tr_rpcSetHostWhitelist(tr_rpc_server* server, char const* whitelist);

void tr_rpcSetPassword(tr_rpc_server* server, char const* password);

char const* tr_rpcGetPassword(tr_rpc_server const* server);

void tr_rpcSetUsername(tr_rpc_server* server, char const* username);

char const* tr_rpcGetUsername(tr_rpc_server const* server);

void tr_rpcSetPasswordEnabled(tr_rpc_server* server, bool isEnabled);

bool tr_rpcIsPasswordEnabled(tr_rpc_server const* session);

char const* tr_rpcGetBindAddress(tr_rpc_server const* server);
