/*
 * This file Copyright (C) 2017 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "net.h"
#include "tr-assert.h"

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
    enum tr_peer_socket_type type;
    union tr_peer_socket_handle handle;
};

#define TR_PEER_SOCKET_INIT ((struct tr_peer_socket){ .type = TR_PEER_SOCKET_TYPE_NONE })

static inline struct tr_peer_socket tr_peer_socket_tcp_create(tr_socket_t const handle)
{
    TR_ASSERT(handle != TR_BAD_SOCKET);
    return (struct tr_peer_socket){ .type = TR_PEER_SOCKET_TYPE_TCP, .handle.tcp = handle };
}

static inline struct tr_peer_socket tr_peer_socket_utp_create(struct UTPSocket* const handle)
{
    TR_ASSERT(handle != NULL);
    return (struct tr_peer_socket){ .type = TR_PEER_SOCKET_TYPE_UTP, .handle.utp = handle };
}
