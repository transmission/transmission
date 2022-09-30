// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory> // for std::unique_ptr

#include "transmission.h" // for tr_port_forwarding_state

struct tr_session;

class tr_port_forwarding
{
public:
    virtual ~tr_port_forwarding() = default;

    virtual void portChanged() = 0;

    virtual void setEnabled(bool enabled) = 0;

    [[nodiscard]] virtual bool isEnabled() const = 0;

    [[nodiscard]] virtual tr_port_forwarding_state state() const = 0;

    [[nodiscard]] static std::unique_ptr<tr_port_forwarding> create(tr_session&);
};
