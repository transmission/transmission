// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "net.h"

enum tr_peer_socket_type
{
    TR_PEER_SOCKET_TYPE_NONE,
    TR_PEER_SOCKET_TYPE_TCP,
    TR_PEER_SOCKET_TYPE_UTP
};

union tr_peer_socket_handle
{
    tr_socket_t tcp;
    struct UTPSocket* utp;
};

struct tr_peer_socket
{
    enum tr_peer_socket_type type = TR_PEER_SOCKET_TYPE_NONE;
    union tr_peer_socket_handle handle;
};

struct tr_peer_socket tr_peer_socket_tcp_create(tr_socket_t const handle);

struct tr_peer_socket tr_peer_socket_utp_create(struct UTPSocket* const handle);

struct tr_session;
struct tr_address;

struct tr_peer_socket tr_netOpenPeerSocket(tr_session* session, tr_address const* addr, tr_port port, bool client_is_seed);

struct tr_peer_socket tr_netOpenPeerUTPSocket(tr_session* session, tr_address const* addr, tr_port port, bool client_is_seed);

void tr_netClosePeerSocket(tr_session* session, tr_peer_socket socket);
