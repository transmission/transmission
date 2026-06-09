// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <cstddef> // size_t
#include <deque>
#include <optional>
#include <string>
#include <string_view>

#include <libtransmission/types.h>

// ---

std::optional<tr_log_level> tr_logGetLevelFromKey(std::string_view key);

struct tr_log_message
{
    tr_log_level level;

    // location in the source code
    std::string_view file;
    long line;

    // when the message was generated
    std::chrono::system_clock::time_point when;

    // torrent name or code module name associated with the message
    std::string name;

    // the message
    std::string message;
};

using tr_log_messages = std::deque<tr_log_message>;

// ---

void tr_logSetQueueEnabled(bool is_enabled);

void tr_logClearQueue();

[[nodiscard]] tr_log_messages tr_logGetQueue();

// ---

void tr_logSetLevel(tr_log_level level);

[[nodiscard]] tr_log_level tr_logGetLevel();

[[nodiscard]] bool tr_logLevelIsActive(tr_log_level level);

// ---

void tr_logAddMessage(
    char const* source_file,
    long source_line,
    tr_log_level level,
    std::string&& msg,
    std::string_view module_name = {});

#define tr_logAddLevel(level, ...) \
    do \
    { \
        if (tr_logLevelIsActive(level)) \
        { \
            tr_logAddMessage(__FILE__, __LINE__, level, __VA_ARGS__); \
        } \
    } while (0)

#define tr_logAddCritical(...) tr_logAddLevel(TR_LOG_CRITICAL, __VA_ARGS__)
#define tr_logAddError(...) tr_logAddLevel(TR_LOG_ERROR, __VA_ARGS__)
#define tr_logAddWarn(...) tr_logAddLevel(TR_LOG_WARN, __VA_ARGS__)
#define tr_logAddInfo(...) tr_logAddLevel(TR_LOG_INFO, __VA_ARGS__)
#define tr_logAddDebug(...) tr_logAddLevel(TR_LOG_DEBUG, __VA_ARGS__)
#define tr_logAddTrace(...) tr_logAddLevel(TR_LOG_TRACE, __VA_ARGS__)

// ---

std::string_view tr_logGetTimeStr(std::chrono::system_clock::time_point now, char* buf, size_t buflen);
std::string_view tr_logGetTimeStr(char* buf, size_t buflen);
