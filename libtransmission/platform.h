// This file Copyright © 2009-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string>
#include <string_view>

struct tr_session;

/**
 * @addtogroup tr_session Session
 * @{
 */

/**
 * @brief invoked by tr_sessionInit() to set up the locations of the resume, torrent, and clutch directories.
 * @see tr_getResumeDir()
 * @see tr_getTorrentDir()
 * @see tr_getWebClientDir()
 */
void tr_setConfigDir(tr_session* session, std::string_view config_dir);

/** @brief return the directory where torrent files are stored */
char const* tr_getTorrentDir(tr_session const*);

/** @brief return the directory where the Web Client's web ui files are kept */
char const* tr_getWebClientDir(tr_session const*);

/** @brief return the directory where session id lock files are stored */
std::string tr_getSessionIdDir();

/** @} */

/* @} */
