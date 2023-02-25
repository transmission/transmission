// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PORT_FORWARDING_MODULE
#error only the libtransmission port forwarding module should #include this header.
#endif

/**
 * @addtogroup port_forwarding Port Forwarding
 * @{
 */

#include <string>

#include "transmission.h"

#include "net.h" // tr_port

struct tr_upnp;

tr_upnp* tr_upnpInit();

void tr_upnpClose(tr_upnp* handle);

tr_port_forwarding_state tr_upnpPulse(tr_upnp*, tr_port port, bool is_enabled, bool do_port_check, std::string bindaddr);

/* @} */
