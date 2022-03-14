// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include "transmission.h"

#define TR_LOG_MAX_QUEUE_LENGTH 10000

[[nodiscard]] tr_log_level tr_logGetLevel(void);

[[nodiscard]] static inline bool tr_logLevelIsActive(tr_log_level level)
{
    return tr_logGetLevel() >= level;
}

void tr_logAddMessage(char const* file, int line, tr_log_level level, std::string_view name, std::string_view msg);

void tr_logAddMessage(char const* file, int line, tr_log_level level, std::string_view name, char const* fmt, ...)
    TR_GNUC_PRINTF(5, 6);

#define tr_logAddNamed(level, name, ...) \
    do \
    { \
        if (tr_logGetLevel() >= level) \
        { \
            tr_logAddMessage(__FILE__, __LINE__, level, name, __VA_ARGS__); \
        } \
    } while (0)

#define tr_logAddNamedCritical(name, ...) tr_logAddNamed(TR_LOG_CRITICAL, (name), __VA_ARGS__)
#define tr_logAddNamedError(name, ...) tr_logAddNamed(TR_LOG_ERROR, (name), __VA_ARGS__)
#define tr_logAddNamedWarn(name, ...) tr_logAddNamed(TR_LOG_WARN, (name), __VA_ARGS__)
#define tr_logAddNamedInfo(name, ...) tr_logAddNamed(TR_LOG_INFO, (name), __VA_ARGS__)
#define tr_logAddNamedDebug(name, ...) tr_logAddNamed(TR_LOG_DEBUG, (name), __VA_ARGS__)
#define tr_logAddNamedTrace(name, ...) tr_logAddNamed(TR_LOG_TRACE, (name), __VA_ARGS__)

#define tr_logAddCritical(...) tr_logAddNamed(TR_LOG_CRITICAL, "", __VA_ARGS__)
#define tr_logAddError(...) tr_logAddNamed(TR_LOG_ERROR, "", __VA_ARGS__)
#define tr_logAddWarn(...) tr_logAddNamed(TR_LOG_WARN, "", __VA_ARGS__)
#define tr_logAddInfo(...) tr_logAddNamed(TR_LOG_INFO, "", __VA_ARGS__)
#define tr_logAddDebug(...) tr_logAddNamed(TR_LOG_DEBUG, "", __VA_ARGS__)
#define tr_logAddTrace(...) tr_logAddNamed(TR_LOG_TRACE, "", __VA_ARGS__)

char* tr_logGetTimeStr(char* buf, size_t buflen) TR_GNUC_NONNULL(1);

/** @} */
