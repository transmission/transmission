// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::min
#include <cerrno>
#include <cstddef> // std::byte

#include <fmt/format.h>

#include <libutp/utp.h>

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket.h"
#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->display_name())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

tr_peer_socket::tr_peer_socket(tr_session const* session, tr_socket_address const& socket_address, tr_socket_t sock)
    : handle{ sock }
    , socket_address_{ socket_address }
    , type_{ Type::TCP }
{
    TR_ASSERT(sock != TR_BAD_SOCKET);

    ++n_open_sockets;
    session->setSocketTOS(sock, address().type);

    if (auto const& algo = session->peerCongestionAlgorithm(); !std::empty(algo))
    {
        tr_netSetCongestionControl(sock, algo.c_str());
    }

    tr_logAddTraceIo(this, fmt::format("socket (tcp) is {}", handle.tcp));
}

tr_peer_socket::tr_peer_socket(tr_socket_address const& socket_address, struct UTPSocket* const sock)
    : socket_address_{ socket_address }
    , type_{ Type::UTP }
{
    TR_ASSERT(sock != nullptr);

    ++n_open_sockets;
    handle.utp = sock;

    tr_logAddTraceIo(this, fmt::format("socket (µTP) is {}", fmt::ptr(handle.utp)));
}

void tr_peer_socket::close()
{
    if (is_tcp() && (handle.tcp != TR_BAD_SOCKET))
    {
        --n_open_sockets;
        tr_net_close_socket(handle.tcp);
    }
#ifdef WITH_UTP
    else if (is_utp())
    {
        --n_open_sockets;
        utp_set_userdata(handle.utp, nullptr);
        utp_close(handle.utp);
    }
#endif

    type_ = Type::None;
    handle = {};
}

size_t tr_peer_socket::try_write(OutBuf& buf, size_t max, tr_error* error) const
{
    if (max == size_t{})
    {
        return {};
    }

    if (is_tcp())
    {
        return buf.to_socket(handle.tcp, max, error);
    }

#ifdef WITH_UTP
    if (is_utp())
    {
        errno = 0;
        // NB: utp_write() does not modify its 2nd arg, but a wart in
        // libutp's public API requires it to be non-const anyway :shrug:
        auto const n_written = utp_write(handle.utp, const_cast<std::byte*>(std::data(buf)), std::min(std::size(buf), max));
        auto const error_code = errno;

        if (n_written > 0)
        {
            buf.drain(n_written);
            return static_cast<size_t>(n_written);
        }

        if (error != nullptr && n_written < 0 && error_code != 0)
        {
            error->set_from_errno(error_code);
        }
    }
#endif

    return {};
}

size_t tr_peer_socket::try_read(InBuf& buf, size_t max, [[maybe_unused]] bool buf_is_empty, tr_error* error) const
{
    if (max == size_t{})
    {
        return {};
    }

    if (is_tcp())
    {
        return buf.add_socket(handle.tcp, max, error);
    }

    // Unlike conventional BSD-style socket API, libutp pushes incoming data to the
    // caller via a callback, instead of allowing the caller to pull data from
    // its buffers. Therefore, reading data from a uTP socket is not handled here.

    return {};
}

bool tr_peer_socket::limit_reached(tr_session const* const session) noexcept
{
    return n_open_sockets.load() >= session->peerLimit();
}
