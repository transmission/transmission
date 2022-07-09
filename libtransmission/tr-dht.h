// This file Copyright © 2009-2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"

#include "net.h" // tr_port

enum
{
    TR_DHT_STOPPED = 0,
    TR_DHT_BROKEN = 1,
    TR_DHT_POOR = 2,
    TR_DHT_FIREWALLED = 3,
    TR_DHT_GOOD = 4
};

int tr_dhtInit(tr_session*);
void tr_dhtUninit(tr_session*);
bool tr_dhtEnabled(tr_session const*);
tr_port tr_dhtPort(tr_session*);
int tr_dhtStatus(tr_session*, int af, int* setme_nodeCount);
char const* tr_dhtPrintableStatus(int status);
bool tr_dhtAddNode(tr_session*, tr_address const*, tr_port, bool bootstrap);
void tr_dhtUpkeep(tr_session*);
void tr_dhtCallback(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen, void* sv);
