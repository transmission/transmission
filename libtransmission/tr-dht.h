// This file Copyright © 2009-2023 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>
#include <string_view>
#include <vector>

#include <dht/dht.h>

#include "transmission.h"

#include "net.h" // tr_port

struct tr_pex;

namespace libtransmission
{
class TimerMaker;
} // namespace libtransmission

class tr_dht
{
public:
    // Wrapper around DHT library.
    // This calls `jech/dht` in production, but makes it possible for tests to inject a mock.
    struct API
    {
        virtual ~API() = default;

        virtual int get_nodes(struct sockaddr_in* sin, int* num, struct sockaddr_in6* sin6, int* num6)
        {
            return ::dht_get_nodes(sin, num, sin6, num6);
        }

        virtual int nodes(int af, int* good_return, int* dubious_return, int* cached_return, int* incoming_return)
        {
            return ::dht_nodes(af, good_return, dubious_return, cached_return, incoming_return);
        }

        virtual int periodic(
            void const* buf,
            size_t buflen,
            struct sockaddr const* from,
            int fromlen,
            time_t* tosleep,
            dht_callback_t callback,
            void* closure)
        {
            return ::dht_periodic(buf, buflen, from, fromlen, tosleep, callback, closure);
        }

        virtual int ping_node(struct sockaddr const* sa, int salen)
        {
            return ::dht_ping_node(sa, salen);
        }

        virtual int search(unsigned char const* id, int port, int af, dht_callback_t callback, void* closure)
        {
            return ::dht_search(id, port, af, callback, closure);
        }

        virtual int init(int s, int s6, unsigned const char* id, unsigned const char* v)
        {
            return ::dht_init(s, s6, id, v);
        }

        virtual int uninit()
        {
            return ::dht_uninit();
        }
    };

    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual std::vector<tr_torrent_id_t> torrentsAllowingDHT() const = 0;
        [[nodiscard]] virtual tr_sha1_digest_t torrentInfoHash(tr_torrent_id_t) const = 0;

        [[nodiscard]] virtual std::string_view configDir() const = 0;
        [[nodiscard]] virtual libtransmission::TimerMaker& timerMaker() = 0;
        [[nodiscard]] virtual API& api()
        {
            return api_;
        }

        virtual void addPex(tr_sha1_digest_t const&, tr_pex const* pex, size_t n_pex) = 0;

    private:
        API api_;
    };

    [[nodiscard]] static std::unique_ptr<tr_dht> create(
        Mediator& mediator,
        tr_port peer_port,
        tr_socket_t udp4_socket,
        tr_socket_t udp6_socket);
    virtual ~tr_dht() = default;

    virtual void addNode(tr_address const& address, tr_port port) = 0;
    virtual void handleMessage(unsigned char const* msg, size_t msglen, struct sockaddr* from, socklen_t fromlen) = 0;
};
