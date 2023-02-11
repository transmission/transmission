// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory> // for std::unique_ptr

#include "transmission.h" // for tr_port_forwarding_state

#include "net.h"

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

        [[nodiscard]] virtual tr_port localPeerPort() const = 0;
        [[nodiscard]] virtual tr_address incomingPeerAddress() const = 0;
        [[nodiscard]] virtual libtransmission::TimerMaker& timerMaker() = 0;
        virtual void onPortForwarded(tr_port advertised_port) = 0;
    };

    [[nodiscard]] static std::unique_ptr<tr_port_forwarding> create(Mediator&);
    virtual ~tr_port_forwarding() = default;

    [[nodiscard]] virtual bool isEnabled() const = 0;
    [[nodiscard]] virtual tr_port_forwarding_state state() const = 0;

    virtual void localPortChanged() = 0;
    virtual void setEnabled(bool enabled) = 0;
};
