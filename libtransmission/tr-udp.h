// This file Copyright © 2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint8_t

struct tr_session;

void tr_udpInit(tr_session*);
void tr_udpUninit(tr_session*);
void tr_udpSetSocketBuffers(tr_session*);
void tr_udpSetSocketTOS(tr_session*);

bool tau_handle_message(tr_session* session, uint8_t const* msg, size_t msglen);
