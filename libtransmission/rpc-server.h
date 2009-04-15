/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_RPC_SERVER_H
#define TR_RPC_SERVER_H

typedef struct tr_rpc_server tr_rpc_server;

tr_rpc_server * tr_rpcInit( tr_session  * session,
                            tr_benc  * settings );

void            tr_rpcClose( tr_rpc_server ** freeme );

void            tr_rpcSetEnabled( tr_rpc_server * server,
                                  tr_bool         isEnabled );

tr_bool         tr_rpcIsEnabled( const tr_rpc_server * server );

void            tr_rpcSetPort( tr_rpc_server * server,
                               tr_port         port );

tr_port         tr_rpcGetPort( const tr_rpc_server * server );

int             tr_rpcSetTest( const tr_rpc_server   * server,
                               const char            * whitelist,
                               char                 ** allocme_errmsg );

void            tr_rpcSetWhitelistEnabled( tr_rpc_server  * server,
                                           tr_bool          isEnabled );

tr_bool         tr_rpcGetWhitelistEnabled( const tr_rpc_server * server );

void            tr_rpcSetWhitelist( tr_rpc_server * server,
                                    const char *    whitelist );

char*           tr_rpcGetWhitelist( const tr_rpc_server * server );

void            tr_rpcSetPassword( tr_rpc_server * server,
                                   const char *    password );

char*           tr_rpcGetPassword( const tr_rpc_server * server );

void            tr_rpcSetUsername( tr_rpc_server * server,
                                   const char *    username );

char*           tr_rpcGetUsername( const tr_rpc_server * server );

void            tr_rpcSetPasswordEnabled( tr_rpc_server * server,
                                          tr_bool         isEnabled );

tr_bool         tr_rpcIsPasswordEnabled( const tr_rpc_server * session );

const char*     tr_rpcGetBindAddress( const tr_rpc_server * server );

#endif
