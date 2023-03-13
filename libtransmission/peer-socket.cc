// This file Copyright © 2017-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/format.h>

#include <libutp/utp.h>

#include "transmission.h"

#include "peer-socket.h"
#include "net.h"
#include "session.h"

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->display_name())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

tr_peer_socket::tr_peer_socket(tr_session const* session, tr_address const& address, tr_port port, tr_socket_t sock)
    : handle{ sock }
    , address_{ address }
    , port_{ port }
    , type_{ Type::TCP }
{
    TR_ASSERT(sock != TR_BAD_SOCKET);

    ++n_open_sockets_;
    session->setSocketTOS(sock, address_.type);

    if (auto const& algo = session->peerCongestionAlgorithm(); !std::empty(algo))
    {
        tr_netSetCongestionControl(sock, algo.c_str());
    }

    tr_logAddTraceIo(this, fmt::format("socket (tcp) is {}", handle.tcp));
}

tr_peer_socket::tr_peer_socket(tr_address const& address, tr_port port, struct UTPSocket* const sock)
    : address_{ address }
    , port_{ port }
    , type_{ Type::UTP }
{
    TR_ASSERT(sock != nullptr);

    ++n_open_sockets_;
    handle.utp = sock;

    tr_logAddTraceIo(this, fmt::format("socket (µTP) is {}", fmt::ptr(handle.utp)));
}

void tr_peer_socket::close()
{
    if (is_tcp() && (handle.tcp != TR_BAD_SOCKET))
    {
        --n_open_sockets_;
        tr_net_close_socket(handle.tcp);
    }
#ifdef WITH_UTP
    else if (is_utp())
    {
        --n_open_sockets_;
        utp_set_userdata(handle.utp, nullptr);
        utp_close(handle.utp);
    }
#endif

    type_ = Type::None;
    handle = {};
}

size_t tr_peer_socket::try_write(Buffer& buf, size_t max, tr_error** error) const
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
        auto const [data, datalen] = buf.pullup();

        errno = 0;
        auto const n_written = utp_write(handle.utp, data, std::min(datalen, max));
        auto const error_code = errno;

        if (n_written > 0)
        {
            buf.drain(n_written);
            return static_cast<size_t>(n_written);
        }

        if (n_written < 0 && error_code != 0)
        {
            tr_error_set_from_errno(error, error_code);
        }
    }
#endif

    return {};
}

size_t tr_peer_socket::try_read(Buffer& buf, size_t max, tr_error** error) const
{
    if (max == size_t{})
    {
        return {};
    }

    if (is_tcp())
    {
        return buf.add_socket(handle.tcp, max, error);
    }

#ifdef WITH_UTP
    // utp_read_drained() notifies libutp that this read buffer is empty.
    // It opens up the congestion window by sending an ACK (soonish) if
    // one was not going to be sent.
    if (is_utp() && std::empty(buf))
    {
        utp_read_drained(handle.utp);
    }
#endif

    return {};
}

bool tr_peer_socket::limit_reached(tr_session* const session) noexcept
{
    return n_open_sockets_.load() >= session->peerLimit();
}
