// This file Copyright © 2009-2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <optional>
#include <string_view>

#include "transmission.h"

#include "net.h" // tr_port

int tr_dhtInit(tr_session*, tr_socket_t udp4_socket, tr_socket_t udp6_socket);
void tr_dhtUninit();

bool tr_dhtEnabled();

std::optional<tr_port> tr_dhtPort();

bool tr_dhtAddNode(tr_address, tr_port, bool bootstrap);
void tr_dhtUpkeep();
void tr_dhtCallback(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen);
