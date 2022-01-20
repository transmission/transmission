/* Copyright © 2010 by Juliusz Chroboczek. This file is licensed
 * under the MIT (SPDX: MIT) license, A copy is in licenses/ */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t

int tr_utpPacket(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen, tr_session* ss);

void tr_utpClose(tr_session*);

void tr_utpSendTo(void* closure, unsigned char const* buf, size_t buflen, struct sockaddr const* to, socklen_t tolen);
