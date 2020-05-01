/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <stddef.h> /* size_t */

#include "file.h" /* tr_sys_file_t */
#include "utils.h" /* TR_GNUC_PRINTF, TR_GNUC_NONNULL */

#ifdef __cplusplus
extern "C"
{
#endif

#define TR_LOG_MAX_QUEUE_LENGTH 10000

tr_log_level tr_logGetLevel(void);

static inline bool tr_logLevelIsActive(tr_log_level level)
{
    return tr_logGetLevel() >= level;
}

void tr_logAddMessage(char const* file, int line, tr_log_level level, char const* torrent, char const* fmt, ...) \
    TR_GNUC_PRINTF(5, 6);

#define tr_logAddNamed(level, name, ...) \
    do \
    { \
        if (tr_logLevelIsActive(level)) \
        { \
            tr_logAddMessage(__FILE__, __LINE__, level, name, __VA_ARGS__); \
        } \
    } \
    while (0)

#define tr_logAddNamedError(name, ...) tr_logAddNamed(TR_LOG_ERROR, name, __VA_ARGS__)
#define tr_logAddNamedInfo(name, ...) tr_logAddNamed(TR_LOG_INFO, name, __VA_ARGS__)
#define tr_logAddNamedDbg(name, ...) tr_logAddNamed(TR_LOG_DEBUG, name, __VA_ARGS__)

#define tr_logAddTor(level, tor, ...) tr_logAddNamed(level, tr_torrentName(tor), __VA_ARGS__)

#define tr_logAddTorErr(tor, ...) tr_logAddTor(TR_LOG_ERROR, tor, __VA_ARGS__)
#define tr_logAddTorInfo(tor, ...) tr_logAddTor(TR_LOG_INFO, tor, __VA_ARGS__)
#define tr_logAddTorDbg(tor, ...) tr_logAddTor(TR_LOG_DEBUG, tor, __VA_ARGS__)

#define tr_logAdd(level, ...) tr_logAddNamed(level, NULL, __VA_ARGS__)

#define tr_logAddError(...) tr_logAdd(TR_LOG_ERROR, __VA_ARGS__)
#define tr_logAddInfo(...) tr_logAdd(TR_LOG_INFO, __VA_ARGS__)
#define tr_logAddDebug(...) tr_logAdd(TR_LOG_DEBUG, __VA_ARGS__)

tr_sys_file_t tr_logGetFile(void);

/** @brief return true if deep logging has been enabled by the user; false otherwise */
bool tr_logGetDeepEnabled(void);

void tr_logAddDeep(char const* file, int line, char const* name, char const* fmt, ...) TR_GNUC_PRINTF(4, 5) \
    TR_GNUC_NONNULL(1, 4);

#define tr_logAddDeepNamed(name, ...) \
    do \
    { \
        if (tr_logGetDeepEnabled()) \
        { \
            tr_logAddDeep(__FILE__, __LINE__, name, __VA_ARGS__); \
        } \
    } \
    while (0)

/** @brief set the buffer with the current time formatted for deep logging. */
char* tr_logGetTimeStr(char* buf, size_t buflen) TR_GNUC_NONNULL(1);

#ifdef __cplusplus
}
#endif

/** @} */
