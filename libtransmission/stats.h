// This file Copyright 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

void tr_statsInit(tr_session* session);
void tr_statsClose(tr_session* session);
void tr_statsSaveDirty(tr_session* session);
void tr_statsAddUploaded(tr_session* session, uint32_t bytes);
void tr_statsAddDownloaded(tr_session* session, uint32_t bytes);
void tr_statsFileCreated(tr_session* session);
