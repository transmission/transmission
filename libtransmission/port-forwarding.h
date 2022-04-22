// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "net.h" // tr_port

struct tr_bindsockets;
struct tr_session;
struct tr_shared;

tr_shared* tr_sharedInit(tr_session*);

void tr_sharedClose(tr_session*);

void tr_sharedPortChanged(tr_session*);

void tr_sharedTraversalEnable(tr_shared*, bool isEnabled);

tr_port tr_sharedGetPeerPort(tr_shared const* s);

bool tr_sharedTraversalIsEnabled(tr_shared const* s);

int tr_sharedTraversalStatus(tr_shared const*);
