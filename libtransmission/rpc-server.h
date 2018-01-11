/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#include "variant.h"

#ifndef TR_RPC_SERVER_H
#define TR_RPC_SERVER_H

typedef struct tr_rpc_server tr_rpc_server;

tr_rpc_server * tr_rpcInit (tr_session  * session,
                            tr_variant  * settings);

void            tr_rpcClose (tr_rpc_server ** freeme);

void            tr_rpcSetEnabled (tr_rpc_server * server, bool isEnabled);

bool            tr_rpcIsEnabled (const tr_rpc_server * server);

void            tr_rpcSetPort (tr_rpc_server * server, tr_port port);

tr_port         tr_rpcGetPort (const tr_rpc_server * server);

void            tr_rpcSetUrl (tr_rpc_server * server, const char * url);

const char *    tr_rpcGetUrl (const tr_rpc_server * server);

int             tr_rpcSetTest (const tr_rpc_server   * server,
                               const char            * whitelist,
                               char                 ** allocme_errmsg);

void            tr_rpcSetWhitelistEnabled (tr_rpc_server  * server,
                                           bool             isEnabled);

bool            tr_rpcGetWhitelistEnabled (const tr_rpc_server * server);

void            tr_rpcSetWhitelist (tr_rpc_server * server,
                                    const char *    whitelist);

const char*     tr_rpcGetWhitelist (const tr_rpc_server * server);

void            tr_rpcSetHostWhitelistEnabled (tr_rpc_server * server,
                                               bool            isEnabled);

void            tr_rpcSetHostWhitelist (tr_rpc_server * server,
                                        const char *    whitelist);

void            tr_rpcSetPassword (tr_rpc_server * server,
                                   const char *    password);

const char*     tr_rpcGetPassword (const tr_rpc_server * server);

void            tr_rpcSetUsername (tr_rpc_server * server,
                                   const char *    username);

const char*     tr_rpcGetUsername (const tr_rpc_server * server);

void            tr_rpcSetPasswordEnabled (tr_rpc_server * server, bool isEnabled);

bool            tr_rpcIsPasswordEnabled (const tr_rpc_server * session);

const char*     tr_rpcGetBindAddress (const tr_rpc_server * server);

#endif
