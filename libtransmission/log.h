// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include "transmission.h"

#define TR_LOG_MAX_QUEUE_LENGTH 10000

tr_log_level tr_logGetLevel();

inline bool tr_logLevelIsActive(tr_log_level level)
{
    return tr_logGetLevel() >= level;
}

void tr_logAddMessage(char const* file, int line, tr_log_level level, std::string_view msg, std::string_view name);

#define TR_LOC __FILE__, __LINE__

namespace tr_log
{
using namespace std::literals;

namespace critical
{
inline void add(char const* file, int line, std::string_view msg, std::string_view name = ""sv)
{
    tr_logAddMessage(file, line, TR_LOG_CRITICAL, msg, name);
}

inline bool enabled()
{
    return tr_logLevelIsActive(TR_LOG_CRITICAL);
}
} // namespace critical

namespace error
{
inline void add(char const* file, int line, std::string_view msg, std::string_view name = ""sv)
{
    tr_logAddMessage(file, line, TR_LOG_ERROR, msg, name);
}

inline bool enabled()
{
    return tr_logLevelIsActive(TR_LOG_ERROR);
}
} // namespace error

namespace warn
{
inline void add(char const* file, int line, std::string_view msg, std::string_view name = ""sv)
{
    tr_logAddMessage(file, line, TR_LOG_WARN, msg, name);
}

inline bool enabled()
{
    return tr_logLevelIsActive(TR_LOG_WARN);
}
} // namespace warn

namespace info
{
inline void add(char const* file, int line, std::string_view msg, std::string_view name = ""sv)
{
    tr_logAddMessage(file, line, TR_LOG_INFO, msg, name);
}

inline bool enabled()
{
    return tr_logLevelIsActive(TR_LOG_INFO);
}
} // namespace info

namespace debug
{
inline void add(char const* file, int line, std::string_view msg, std::string_view name = ""sv)
{
    tr_logAddMessage(file, line, TR_LOG_DEBUG, msg, name);
}

inline bool enabled()
{
    return tr_logLevelIsActive(TR_LOG_DEBUG);
}
} // namespace debug

namespace trace
{
inline void add(char const* file, int line, std::string_view msg, std::string_view name = ""sv)
{
    tr_logAddMessage(file, line, TR_LOG_TRACE, msg, name);
}

inline bool enabled()
{
    return tr_logLevelIsActive(TR_LOG_TRACE);
}
} // namespace trace

} // namespace tr_log

/** @} */
