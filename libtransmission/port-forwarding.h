// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory> // for std::unique_ptr

#include "libtransmission/transmission.h" // for tr_port_forwarding_state

#include "libtransmission/net.h"

namespace libtransmission
{
class TimerMaker;
}

class tr_port_forwarding
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() = default;

        [[nodiscard]] virtual tr_port advertised_peer_port() const = 0;
        [[nodiscard]] virtual tr_port local_peer_port() const = 0;
        [[nodiscard]] virtual tr_address incoming_peer_address() const = 0;
        [[nodiscard]] virtual libtransmission::TimerMaker& timer_maker() = 0;
        virtual void on_port_forwarded(tr_port advertised_port) = 0;
    };

    [[nodiscard]] static std::unique_ptr<tr_port_forwarding> create(Mediator&);
    virtual ~tr_port_forwarding() = default;

    [[nodiscard]] virtual bool is_enabled() const = 0;
    [[nodiscard]] virtual tr_port_forwarding_state state() const = 0;

    virtual void local_port_changed() = 0;
    virtual void set_enabled(bool enabled) = 0;
};
