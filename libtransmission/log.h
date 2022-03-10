// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <stddef.h> /* size_t */

#include "file.h" /* tr_sys_file_t */
#include "transmission.h"

#define TR_LOG_MAX_QUEUE_LENGTH 10000

tr_log_level tr_logGetLevel(void);

static inline bool tr_logLevelIsActive(tr_log_level level)
{
    return tr_logGetLevel() >= level;
}

void tr_logAddMessage(char const* file, int line, tr_log_level level, char const* torrent, char const* fmt, ...)
    TR_GNUC_PRINTF(5, 6);

#define tr_logAddNamed(level, name, ...) \
    do \
    { \
        if (tr_logLevelIsActive(level)) \
        { \
            tr_logAddMessage(__FILE__, __LINE__, level, name, __VA_ARGS__); \
        } \
    } while (0)

#define tr_logAddNamedError(name, ...) tr_logAddNamed(TR_LOG_ERROR, name, __VA_ARGS__)
#define tr_logAddNamedInfo(name, ...) tr_logAddNamed(TR_LOG_INFO, name, __VA_ARGS__)
#define tr_logAddNamedDbg(name, ...) tr_logAddNamed(TR_LOG_DEBUG, name, __VA_ARGS__)

#define tr_logAddTor(level, tor, ...) tr_logAddNamed(level, tr_torrentName(tor), __VA_ARGS__)

#define tr_logAddTorErr(tor, ...) tr_logAddTor(TR_LOG_ERROR, tor, __VA_ARGS__)
#define tr_logAddTorInfo(tor, ...) tr_logAddTor(TR_LOG_INFO, tor, __VA_ARGS__)
#define tr_logAddTorDbg(tor, ...) tr_logAddTor(TR_LOG_DEBUG, tor, __VA_ARGS__)

#define tr_logAdd(level, ...) tr_logAddNamed(level, nullptr, __VA_ARGS__)

#define tr_logAddError(...) tr_logAdd(TR_LOG_ERROR, __VA_ARGS__)
#define tr_logAddInfo(...) tr_logAdd(TR_LOG_INFO, __VA_ARGS__)
#define tr_logAddDebug(...) tr_logAdd(TR_LOG_DEBUG, __VA_ARGS__)

tr_sys_file_t tr_logGetFile(void);

/** @brief set the buffer with the current time formatted for deep logging. */
char* tr_logGetTimeStr(char* buf, size_t buflen) TR_GNUC_NONNULL(1);

/** @} */
