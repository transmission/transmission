/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

extern struct tr_session_stats const TR_SESSION_STATS_INIT;

void tr_statsInit(tr_session* session);
void tr_statsClose(tr_session* session);
void tr_statsSaveDirty(tr_session* session);
void tr_statsAddUploaded(tr_session* session, uint32_t bytes);
void tr_statsAddDownloaded(tr_session* session, uint32_t bytes);
void tr_statsFileCreated(tr_session* session);
