// This file Copyright © 2010 Johannes Lieder.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>
#include <string_view>

#include "transmission.h"

#include "net.h" // for tr_address
#include "timer.h"

class tr_torrents;
struct tr_session;
struct event_base;

class tr_lpd
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;
        [[nodiscard]] virtual tr_port port() const = 0;
        [[nodiscard]] virtual bool allowsLPD() const = 0;
        [[nodiscard]] virtual tr_torrents const& torrents() const = 0;

        // returns true if info was used
        virtual bool onPeerFound(std::string_view info_hash_str, tr_address address, tr_port port) = 0;
    };

    virtual ~tr_lpd() = default;
    static std::unique_ptr<tr_lpd> create(Mediator& mediator, libtransmission::TimerMaker&, event_base* event_base);

protected:
    tr_lpd() = default;
};
