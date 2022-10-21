// This file Copyright © 2009-2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "net.h" // tr_port

struct tr_pex;

namespace libtransmission
{
class TimerMaker;
} // namespace libtransmission

// int tr_dhtInit(tr_session*, tr_socket_t udp4_socket, tr_socket_t udp6_socket);
// void tr_dhtUninit();
// bool tr_dhtEnabled();
// bool tr_dhtAddNode(tr_address, tr_port, bool bootstrap);
// void tr_dhtUpkeep();
// void tr_dhtCallback(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen);

class tr_dht
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual std::string_view configDir() const = 0;

        [[nodiscard]] virtual tr_port peerPort() const = 0;

        [[nodiscard]] virtual std::vector<tr_torrent_id_t> torrentsAllowingDHT() const = 0;
        [[nodiscard]] virtual tr_sha1_digest_t torrentInfoHash(tr_torrent_id_t) = 0;

        [[nodiscard]] virtual libtransmission::TimerMaker& timerMaker() = 0;

        virtual void addPex(tr_sha1_digest_t const&, tr_pex const* pex, size_t n_pex);
    };


    [[nodiscard]] static std::unique_ptr<tr_dht> create(Mediator& mediator, tr_socket_t udp4_socket, tr_socket_t udp6_socket);
    virtual ~tr_dht() = default;

    virtual void addNode(tr_address const& address, tr_port port) = 0;
    virtual void handleMessage(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen) = 0;
};
