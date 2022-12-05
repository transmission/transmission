// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"

#include "net.h"
#include "tr-assert.h"

extern "C"
{
    struct UTPSocket;
}

struct tr_peer_socket
{
    tr_peer_socket() = default;

    tr_peer_socket(tr_socket_t sock)
        : handle{ sock }
        , type{ Type::TCP }
    {
        TR_ASSERT(sock != TR_BAD_SOCKET);
    }

    tr_peer_socket(struct UTPSocket* const sock)
        : type{ Type::UTP }
    {
        TR_ASSERT(sock != nullptr);
        handle.utp = sock;
    }

    [[nodiscard]] constexpr auto is_utp() const noexcept
    {
        return type == Type::UTP;
    }

    [[nodiscard]] constexpr auto is_tcp() const noexcept
    {
        return type == Type::TCP;
    }

    [[nodiscard]] constexpr auto is_valid() const noexcept
    {
#ifdef WITH_UTP
        return is_tcp() || is_utp();
#else
        return is_tcp();
#endif
    }

    union
    {
        tr_socket_t tcp;
        struct UTPSocket* utp;
    } handle = {};

private:
    enum class Type
    {
        None,
        TCP,
        UTP
    };

    enum Type type = Type::None;
};

struct tr_session;
struct tr_address;

struct tr_peer_socket tr_netOpenPeerSocket(tr_session* session, tr_address const* addr, tr_port port, bool client_is_seed);

struct tr_peer_socket tr_netOpenPeerUTPSocket(tr_session* session, tr_address const* addr, tr_port port, bool client_is_seed);

void tr_netClosePeerSocket(tr_session* session, tr_peer_socket socket);
