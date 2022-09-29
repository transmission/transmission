// This file Copyright © 2009-2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <optional>

#include "transmission.h"

#include "net.h" // tr_port

int tr_dhtInit(tr_session*);
void tr_dhtUninit(tr_session const*);
bool tr_dhtEnabled(tr_session const*);
std::optional<tr_port> tr_dhtPort(tr_session const*);
bool tr_dhtAddNode(tr_session*, tr_address const&, tr_port, bool bootstrap);
void tr_dhtUpkeep(tr_session*);
void tr_dhtCallback(tr_session*, unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen);
