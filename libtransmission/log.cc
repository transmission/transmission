// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>

#include <event2/buffer.h>

#include <fmt/core.h>
#include <fmt/chrono.h>

#include "transmission.h"
#include "file.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

using namespace std::literals;

static tr_log_level tr_message_level = TR_LOG_ERROR;
static bool myQueueEnabled = false;
static tr_log_message* myQueue = nullptr;
static tr_log_message** myQueueTail = &myQueue;
static int myQueueLength = 0;

/***
****
***/

tr_log_level tr_logGetLevel()
{
    return tr_message_level;
}

/***
****
***/

static std::recursive_mutex message_mutex_;

#if !defined(_WIN32)

static tr_sys_file_t tr_logGetFile()
{
    static bool initialized = false;
    static tr_sys_file_t file = TR_BAD_SYS_FILE;

    if (!initialized)
    {
        switch (tr_env_get_int("TR_DEBUG_FD", 0))
        {
        case 1:
            file = tr_sys_file_get_std(TR_STD_SYS_FILE_OUT, nullptr);
            break;

        case 2:
            file = tr_sys_file_get_std(TR_STD_SYS_FILE_ERR, nullptr);
            break;

        default:
            file = TR_BAD_SYS_FILE;
            break;
        }

        initialized = true;
    }

    return file;
}

#endif

void tr_logSetLevel(tr_log_level level)
{
    tr_message_level = level;
}

void tr_logSetQueueEnabled(bool isEnabled)
{
    myQueueEnabled = isEnabled;
}

bool tr_logGetQueueEnabled()
{
    return myQueueEnabled;
}

tr_log_message* tr_logGetQueue()
{
    auto const lock = std::lock_guard(message_mutex_);

    auto* const ret = myQueue;
    myQueue = nullptr;
    myQueueTail = &myQueue;
    myQueueLength = 0;

    return ret;
}

void tr_logFreeQueue(tr_log_message* list)
{
    while (list != nullptr)
    {
        tr_log_message* next = list->next;
        tr_free(list->message);
        tr_free(list->name);
        tr_free(list);
        list = next;
    }
}

/***
****
***/

static void logAddMessageImpl(
    [[maybe_unused]] char const* file,
    [[maybe_unused]] int line,
    tr_log_level level,
    std::string_view msg,
    [[maybe_unused]] std::string_view name)
{
    auto const lock = std::lock_guard(message_mutex_);
    int const err = errno; // message logging shouldn't affect errno

#if defined(_WIN32)

    OutputDebugStringA(tr_strvJoin(msg, "\r\n").c_str());

#elif defined(__ANDROID__)

    int prio;

    switch (level)
    {
    case TR_LOG_ERROR:
        prio = ANDROID_LOG_ERROR;
        break;
    case TR_LOG_INFO:
        prio = ANDROID_LOG_INFO;
        break;
    case TR_LOG_DEBUG:
        prio = ANDROID_LOG_DEBUG;
        break;
    default:
        prio = ANDROID_LOG_VERBOSE;
    }

#ifdef NDEBUG
    __android_log_print(prio, "transmission", "%s", msg.c_str());
#else
    __android_log_print(prio, "transmission", "[%s:%d] %s", file, line, msg.c_str());
#endif

#else

    if (tr_logGetQueueEnabled())
    {
        auto* const newmsg = tr_new0(tr_log_message, 1);
        newmsg->level = level;
        newmsg->when = tr_time();
        newmsg->message = tr_strvDup(msg);
        newmsg->file = file;
        newmsg->line = line;
        newmsg->name = tr_strvDup(name);

        *myQueueTail = newmsg;
        myQueueTail = &newmsg->next;
        ++myQueueLength;

        if (myQueueLength > TR_LOG_MAX_QUEUE_LENGTH)
        {
            tr_log_message* old = myQueue;
            myQueue = old->next;
            old->next = nullptr;
            tr_logFreeQueue(old);
            --myQueueLength;
            TR_ASSERT(myQueueLength == TR_LOG_MAX_QUEUE_LENGTH);
        }
    }
    else
    {
        tr_sys_file_t fp = tr_logGetFile();
        if (fp == TR_BAD_SYS_FILE)
        {
            fp = tr_sys_file_get_std(TR_STD_SYS_FILE_ERR, nullptr);
        }

        auto const now = std::chrono::system_clock::now();
        auto const out = std::empty(name) ? fmt::format("[{:%F %H:%M}:{:%S}] {}", now, now.time_since_epoch(), msg) :
                                            fmt::format("[{:%F %H:%M}:{:%S}] {}: {}", now, now.time_since_epoch(), name, msg);
        tr_sys_file_write_line(fp, out, nullptr);
        tr_sys_file_flush(fp, nullptr);
    }

#endif

    errno = err;
}

void tr_logAddMessage(char const* file, int line, tr_log_level level, std::string_view msg, std::string_view name)
{
    if (std::empty(msg) || !tr_logLevelIsActive(level))
    {
        return;
    }

    static auto constexpr MaxRepeat = 25;
    static auto* error_counts = new std::map<std::pair<std::string_view, int>, int>{};

    switch (level)
    {
    case TR_LOG_CRITICAL:
    case TR_LOG_ERROR:
    case TR_LOG_WARN:
        if (auto const count = (*error_counts)[std::make_pair(file, line)]++; count < MaxRepeat)
        {
            logAddMessageImpl(file, line, level, msg, name);
        }
        else if (count == MaxRepeat)
        {
            logAddMessageImpl(file, line, level, msg, name);
            auto const enough_msg = fmt::format(
                _("That last message has appeared {count} times; I won't log it anymore."),
                fmt::arg("count", MaxRepeat));
            logAddMessageImpl(file, line, level, enough_msg, name);
        }
        break;

    default:
        logAddMessageImpl(file, line, level, msg, name);
        return;
    }
}
