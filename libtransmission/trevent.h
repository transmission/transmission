// This file Copyright (C) 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "tr-macros.h"

void tr_eventInit(tr_session*);

void tr_eventClose(tr_session*);

bool tr_amInEventThread(tr_session const*);

void tr_runInEventThread(tr_session*, void (*func)(void*), void* user_data);
