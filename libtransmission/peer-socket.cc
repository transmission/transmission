// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // std::byte

#include "libtransmission/error.h"
#include "libtransmission/peer-socket.h"
#include "libtransmission/session.h"

tr_peer_socket::tr_peer_socket(tr_socket_address const& socket_address)
    : socket_address_{ socket_address }
{
    TR_ASSERT(socket_address_.is_valid());
    ++n_open_sockets;
}

tr_peer_socket::~tr_peer_socket()
{
    --n_open_sockets;
}

size_t tr_peer_socket::try_read(InBuf& buf, size_t max, tr_error* error)
{
    if (max == size_t{})
    {
        return {};
    }

    return try_read_impl(buf, max, error);
}

size_t tr_peer_socket::try_write(OutBuf& buf, size_t max, tr_error* error)
{
    if (max == size_t{})
    {
        return {};
    }

    return try_write_impl(buf, max, error);
}

bool tr_peer_socket::limit_reached(tr_session const* const session) noexcept
{
    return n_open_sockets.load() >= session->peerLimit();
}
