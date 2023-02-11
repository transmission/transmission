// This file Copyright © 2017-2022 Mnemosyne LLC.
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

tr_peer_socket::tr_peer_socket(tr_session* session, tr_address const& address, tr_port port, tr_socket_t sock)
    : handle{ sock }
    , address_{ address }
    , port_{ port }
    , type_{ Type::TCP }
{
    TR_ASSERT(sock != TR_BAD_SOCKET);

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
    handle.utp = sock;

    tr_logAddTraceIo(this, fmt::format("socket (µTP) is {}", fmt::ptr(handle.utp)));
}

void tr_peer_socket::close(tr_session* session)
{
    if (is_tcp())
    {
        tr_netClose(session, handle.tcp);
    }
#ifdef WITH_UTP
    else if (is_utp())
    {
        utp_set_userdata(handle.utp, nullptr);
        utp_close(handle.utp);
    }
#endif

    type_ = Type::None;
    handle = {};
}
