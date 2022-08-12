// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <ctime> // time_t

#include "transmission.h" // tr_port_forwarding

#include "natpmp.h"
#include "net.h" // tr_port

class tr_natpmp
{
public:
    tr_natpmp()
    {
        natpmp_.s = TR_BAD_SOCKET; /* socket */
    }

    ~tr_natpmp()
    {
        closenatpmp(&natpmp_);
    }

    [[nodiscard]] constexpr auto renewTime() const noexcept
    {
        return renew_time_;
    }

    tr_port_forwarding pulse(tr_port port, bool is_enabled, tr_port* public_port, tr_port* real_private_port);

private:
    enum tr_natpmp_state
    {
        TR_NATPMP_IDLE,
        TR_NATPMP_ERR,
        TR_NATPMP_DISCOVER,
        TR_NATPMP_RECV_PUB,
        TR_NATPMP_SEND_MAP,
        TR_NATPMP_RECV_MAP,
        TR_NATPMP_SEND_UNMAP,
        TR_NATPMP_RECV_UNMAP
    };

    static constexpr auto LifetimeSecs = uint32_t{ 3600 };
    static constexpr auto CommandWaitSecs = time_t{ 8 };

    [[nodiscard]] bool canSendCommand() const;

    void setCommandTime();

    natpmp_t natpmp_ = {};

    tr_port public_port_ = {};
    tr_port private_port_ = {};

    time_t renew_time_ = 0;
    time_t command_time_ = 0;
    tr_natpmp_state state_ = TR_NATPMP_DISCOVER;

    bool has_discovered_ = false;
    bool is_mapped_ = false;
};
