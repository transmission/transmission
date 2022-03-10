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

#define tr_logCriticalNamed(name, ...) tr_logAddNamed(TR_LOG_CRITICAL, (name), __VA_ARGS__)
#define tr_logErrorNamed(name, ...) tr_logAddNamed(TR_LOG_ERROR, (name), __VA_ARGS__)
#define tr_logWarnNamed(name, ...) tr_logAddNamed(TR_LOG_WARN, (name), __VA_ARGS__)
#define tr_logInfoNamed(name, ...) tr_logAddNamed(TR_LOG_INFO, (name), __VA_ARGS__)
#define tr_logDebugNamed(name, ...) tr_logAddNamed(TR_LOG_DEBUG, (name), __VA_ARGS__)
#define tr_logTraceNamed(name, ...) tr_logAddNamed(TR_LOG_TRACE, (name), __VA_ARGS__)

#define tr_logCritical(...) tr_logAddNamed(TR_LOG_CRITICAL, "", __VA_ARGS__)
#define tr_logError(...) tr_logAddNamed(TR_LOG_ERROR, "", __VA_ARGS__)
#define tr_logWarn(...) tr_logAddNamed(TR_LOG_WARN, "", __VA_ARGS__)
#define tr_logInfo(...) tr_logAddNamed(TR_LOG_INFO, "", __VA_ARGS__)
#define tr_logDebug(...) tr_logAddNamed(TR_LOG_DEBUG, "", __VA_ARGS__)
#define tr_logTrace(...) tr_logAddNamed(TR_LOG_TRACE, "", __VA_ARGS__)

char* tr_logGetTimeStr(char* buf, size_t buflen) TR_GNUC_NONNULL(1);

/** @} */
