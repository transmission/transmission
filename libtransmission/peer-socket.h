// This file Copyright © 2017-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string>
#include <string_view>
#include <utility> // for std::make_pair()

#include "transmission.h"

#include "net.h"
#include "tr-assert.h"

struct UTPSocket;
struct tr_session;

class tr_peer_socket
{
public:
    tr_peer_socket() = default;
    tr_peer_socket(tr_session* session, tr_address const& address, tr_port port, tr_socket_t sock);
    tr_peer_socket(tr_address const& address, tr_port port, struct UTPSocket* const sock);
    tr_peer_socket(tr_peer_socket&&) = default;
    tr_peer_socket(tr_peer_socket const&) = delete;
    tr_peer_socket& operator=(tr_peer_socket&&) = default;
    tr_peer_socket& operator=(tr_peer_socket const&) = delete;
    ~tr_peer_socket() = default;

    void close(tr_session* session);

    [[nodiscard]] constexpr std::pair<tr_address, tr_port> socketAddress() const noexcept
    {
        return std::make_pair(address_, port_);
    }

    [[nodiscard]] constexpr auto const& address() const noexcept
    {
        return address_;
    }

    [[nodiscard]] constexpr auto const& port() const noexcept
    {
        return port_;
    }

    template<typename OutputIt>
    OutputIt display_name(OutputIt out)
    {
        return address_.display_name(out, port_);
    }

    [[nodiscard]] std::string_view display_name(char* out, size_t outlen) const
    {
        return address_.display_name(out, outlen, port_);
    }

    [[nodiscard]] std::string display_name() const
    {
        return address_.display_name(port_);
    }

    [[nodiscard]] constexpr auto is_utp() const noexcept
    {
        return type_ == Type::UTP;
    }

    [[nodiscard]] constexpr auto is_tcp() const noexcept
    {
        return type_ == Type::TCP;
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

    tr_address address_;
    tr_port port_;

    enum Type type_ = Type::None;
};

tr_peer_socket tr_netOpenPeerSocket(tr_session* session, tr_address const& addr, tr_port port, bool client_is_seed);
tr_peer_socket tr_netOpenPeerUTPSocket(tr_session* session, tr_address const& addr, tr_port port, bool client_is_seed);
