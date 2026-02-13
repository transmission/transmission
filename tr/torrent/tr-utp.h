// This file Copyright © Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t

#ifndef _WIN32
#include <sys/socket.h>
#else
#include <ws2tcpip.h>
#endif

struct tr_session;

void tr_utp_init(tr_session* session);

bool tr_utp_packet(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen, tr_session* ss);

void tr_utp_issue_deferred_acks(tr_session* ss);

void tr_utp_close(tr_session* session);
