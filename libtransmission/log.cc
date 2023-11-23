// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef> // size_t
#include <iterator> // back_insert_iterator, empty
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <fmt/chrono.h>
#include <fmt/core.h>

#include "libtransmission/file.h"
#include "libtransmission/log.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/utils.h"

using namespace std::literals;

namespace
{

class tr_log_state
{
public:
    [[nodiscard]] auto unique_lock()
    {
        return std::unique_lock(message_mutex_);
    }

    tr_log_level level = TR_LOG_ERROR;

    bool queue_enabled_ = false;

    tr_log_message* queue_ = nullptr;

    tr_log_message** queue_tail_ = &queue_;

    int queue_length_ = 0;

    std::recursive_mutex message_mutex_;
};

auto log_state = tr_log_state{};

// ---

void logAddImpl(
    [[maybe_unused]] std::string_view file,
    [[maybe_unused]] long line,
    [[maybe_unused]] tr_log_level level,
    std::string&& msg,
    [[maybe_unused]] std::string_view name)
{
    if (std::empty(msg))
    {
        return;
    }

    auto const lock = log_state.unique_lock();

#if defined(__ANDROID__)

    int prio;

    switch (level)
    {
    case TR_LOG_CRITICAL:
        prio = ANDROID_LOG_FATAL;
        break;
    case TR_LOG_ERROR:
        prio = ANDROID_LOG_ERROR;
        break;
    case TR_LOG_WARN:
        prio = ANDROID_LOG_WARN;
        break;
    case TR_LOG_INFO:
        prio = ANDROID_LOG_INFO;
        break;
    case TR_LOG_DEBUG:
        prio = ANDROID_LOG_DEBUG;
        break;
    case TR_LOG_TRACE:
        prio = ANDROID_LOG_VERBOSE;
    }

#ifdef NDEBUG
    auto const szmsg = fmt::format("{:s}", msg);
#else
    auto const szmsg = fmt::format("[{:s}:{:d}] {:s}", file, line, msg);
#endif
    __android_log_write(prio, "transmission", szmsg.c_str());

#else

    if (log_state.queue_enabled_)
    {
        auto* const newmsg = new tr_log_message{};
        newmsg->level = level;
        newmsg->when = tr_time();
        newmsg->message = std::move(msg);
        newmsg->file = file;
        newmsg->line = line;
        newmsg->name = name;

        *log_state.queue_tail_ = newmsg;
        log_state.queue_tail_ = &newmsg->next;
        ++log_state.queue_length_;

        if (log_state.queue_length_ > TR_LOG_MAX_QUEUE_LENGTH)
        {
            tr_log_message* old = log_state.queue_;
            log_state.queue_ = old->next;
            old->next = nullptr;
            tr_logFreeQueue(old);
            --log_state.queue_length_;
            TR_ASSERT(log_state.queue_length_ == TR_LOG_MAX_QUEUE_LENGTH);
        }
    }
    else
    {
        static auto const fp = tr_sys_file_get_std(TR_STD_SYS_FILE_ERR);
        if (fp == TR_BAD_SYS_FILE)
        {
            return;
        }

        auto timestr = std::array<char, 64U>{};
        tr_logGetTimeStr(std::data(timestr), std::size(timestr));

        auto buf = tr_strbuf<char, 2048U>{};
        if (std::empty(name))
        {
            fmt::format_to(std::back_inserter(buf), "[{:s}] {:s}", std::data(timestr), msg);
        }
        else
        {
            fmt::format_to(std::back_inserter(buf), "[{:s}] {:s}: {:s}", std::data(timestr), name, msg);
        }
        tr_sys_file_write_line(fp, buf);
        tr_sys_file_flush(fp);
    }
#endif
}

} // unnamed namespace

tr_log_level tr_logGetLevel()
{
    return log_state.level;
}

bool tr_logLevelIsActive(tr_log_level level)
{
    return tr_logGetLevel() >= level;
}

void tr_logSetLevel(tr_log_level level)
{
    log_state.level = level;
}

void tr_logSetQueueEnabled(bool is_enabled)
{
    log_state.queue_enabled_ = is_enabled;
}

tr_log_message* tr_logGetQueue()
{
    auto const lock = log_state.unique_lock();

    auto* const ret = log_state.queue_;
    log_state.queue_ = nullptr;
    log_state.queue_tail_ = &log_state.queue_;
    log_state.queue_length_ = 0;

    return ret;
}

void tr_logFreeQueue(tr_log_message* freeme)
{
    while (freeme != nullptr)
    {
        auto* const next = freeme->next;
        delete freeme;
        freeme = next;
    }
}

// ---

char* tr_logGetTimeStr(char* buf, size_t buflen)
{
    auto const a = std::chrono::system_clock::now();
    auto const [out, len] = fmt::format_to_n(
        buf,
        buflen - 1,
        "{0:%F %H:%M:}{1:%S}",
        a,
        std::chrono::duration_cast<std::chrono::milliseconds>(a.time_since_epoch()));
    *out = '\0';
    return buf;
}

void tr_logAddMessage(char const* file, long line, tr_log_level level, std::string&& msg, std::string_view name)
{
    TR_ASSERT(!std::empty(msg));

    // strip source path to only include the filename
    auto filename = tr_sys_path_basename(file);
    if (std::empty(filename))
    {
        filename = "?"sv;
    }

    auto name_fallback = std::string{};
    if (std::empty(name))
    {
        name_fallback = fmt::format("{:s}:{:d}", filename, line);
        name = name_fallback;
    }

    // message logging shouldn't affect errno
    int const err = errno;

    // skip unwanted messages
    if (!tr_logLevelIsActive(level))
    {
        errno = err;
        return;
    }

    auto const lock = log_state.unique_lock();

    // don't log the same warning ad infinitum.
    // it's not useful after some point.
    bool last_one = false;
    if (level == TR_LOG_CRITICAL || level == TR_LOG_ERROR || level == TR_LOG_WARN)
    {
        static auto constexpr MaxRepeat = size_t{ 30 };
        static auto counts = new std::map<std::pair<std::string_view, int>, size_t>{};

        auto& count = (*counts)[std::make_pair(filename, line)];
        ++count;
        last_one = count == MaxRepeat;
        if (count > MaxRepeat)
        {
            errno = err;
            return;
        }
    }

    // log the messages
    logAddImpl(filename, line, level, std::move(msg), name);
    if (last_one)
    {
        logAddImpl(
            filename,
            line,
            level,
            _("Too many messages like this! I won't log this message anymore this session."),
            name);
    }

    errno = err;
}

// ---

namespace
{

auto constexpr LogKeys = std::array<std::pair<std::string_view, tr_log_level>, 7>{ { { "off", TR_LOG_OFF },
                                                                                     { "critical", TR_LOG_CRITICAL },
                                                                                     { "error", TR_LOG_ERROR },
                                                                                     { "warn", TR_LOG_WARN },
                                                                                     { "info", TR_LOG_INFO },
                                                                                     { "debug", TR_LOG_DEBUG },
                                                                                     { "trace", TR_LOG_TRACE } } };

bool constexpr keysAreOrdered()
{
    for (size_t i = 0, n = std::size(LogKeys); i < n; ++i)
    {
        if (LogKeys[i].second != static_cast<tr_log_level>(i))
        {
            return false;
        }
    }

    return true;
}

static_assert(keysAreOrdered());

} // unnamed namespace

std::optional<tr_log_level> tr_logGetLevelFromKey(std::string_view key_in)
{
    auto const key = tr_strlower(tr_strv_strip(key_in));

    for (auto const& [name, level] : LogKeys)
    {
        if (key == name)
        {
            return level;
        }
    }

    return std::nullopt;
}
