// This file Copyright © 2016-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <ctime>
#include <string_view>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <fmt/format.h>

#include "transmission.h"
#include "crypto-utils.h"
#include "error.h"
#include "error-types.h"
#include "file.h"
#include "log.h"
#include "platform.h"
#include "session-id.h"
#include "utils.h"

using namespace std::literals;

static auto constexpr SessionIdSize = size_t{ 48 };
static auto constexpr SessionIdDurationSec = time_t{ 60 * 60 }; /* expire in an hour */

struct tr_session_id
{
    char* current_value;
    char* previous_value;
    tr_sys_file_t current_lock_file;
    tr_sys_file_t previous_lock_file;
    time_t expires_at;
};

static char* generate_new_session_id_value()
{
    char const pool[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t const pool_size = sizeof(pool) - 1;

    auto* buf = tr_new(char, SessionIdSize + 1);

    tr_rand_buffer(buf, SessionIdSize);

    for (size_t i = 0; i < SessionIdSize; ++i)
    {
        buf[i] = pool[(unsigned char)buf[i] % pool_size];
    }

    buf[SessionIdSize] = '\0';

    return buf;
}

static std::string get_session_id_lock_file_path(std::string_view session_id)
{
    return fmt::format(FMT_STRING("{:s}/tr_session_id_{:s}"), tr_getSessionIdDir(), session_id);
}

static tr_sys_file_t create_session_id_lock_file(char const* session_id)
{
    if (session_id == nullptr)
    {
        return TR_BAD_SYS_FILE;
    }

    auto const lock_file_path = get_session_id_lock_file_path(session_id);
    tr_error* error = nullptr;
    auto lock_file = tr_sys_file_open(
        lock_file_path.c_str(),
        TR_SYS_FILE_READ | TR_SYS_FILE_WRITE | TR_SYS_FILE_CREATE,
        0600,
        &error);

    if (lock_file != TR_BAD_SYS_FILE)
    {
        if (tr_sys_file_lock(lock_file, TR_SYS_FILE_LOCK_EX | TR_SYS_FILE_LOCK_NB, &error))
        {
#ifndef _WIN32
            /* Allow any user to lock the file regardless of current umask */
            fchmod(lock_file, 0644);
#endif
        }
        else
        {
            tr_sys_file_close(lock_file);
            lock_file = TR_BAD_SYS_FILE;
        }
    }

    if (error != nullptr)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't create '{path}': {error} ({error_code})"),
            fmt::arg("path", lock_file_path),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
    }

    return lock_file;
}

static void destroy_session_id_lock_file(tr_sys_file_t lock_file, char const* session_id)
{
    if (lock_file != TR_BAD_SYS_FILE)
    {
        tr_sys_file_close(lock_file);
    }

    if (session_id != nullptr)
    {
        auto const lock_file_path = get_session_id_lock_file_path(session_id);
        tr_sys_path_remove(lock_file_path);
    }
}

tr_session_id_t tr_session_id_new()
{
    auto const session_id = tr_new0(struct tr_session_id, 1);

    session_id->current_lock_file = TR_BAD_SYS_FILE;
    session_id->previous_lock_file = TR_BAD_SYS_FILE;

    return session_id;
}

void tr_session_id_free(tr_session_id_t session_id)
{
    if (session_id == nullptr)
    {
        return;
    }

    destroy_session_id_lock_file(session_id->previous_lock_file, session_id->previous_value);
    destroy_session_id_lock_file(session_id->current_lock_file, session_id->current_value);

    tr_free(session_id->previous_value);
    tr_free(session_id->current_value);

    tr_free(session_id);
}

char const* tr_session_id_get_current(tr_session_id_t session_id)
{
    time_t const now = tr_time();

    if (session_id->current_value == nullptr || now >= session_id->expires_at)
    {
        destroy_session_id_lock_file(session_id->previous_lock_file, session_id->previous_value);
        tr_free(session_id->previous_value);

        session_id->previous_value = session_id->current_value;
        session_id->current_value = generate_new_session_id_value();

        session_id->previous_lock_file = session_id->current_lock_file;
        session_id->current_lock_file = create_session_id_lock_file(session_id->current_value);

        session_id->expires_at = now + SessionIdDurationSec;
    }

    return session_id->current_value;
}

bool tr_session_id_is_local(char const* session_id)
{
    bool ret = false;

    if (session_id != nullptr)
    {
        auto const lock_file_path = get_session_id_lock_file_path(session_id);
        tr_error* error = nullptr;
        auto lock_file = tr_sys_file_open(lock_file_path.c_str(), TR_SYS_FILE_READ, 0, &error);

        if (lock_file == TR_BAD_SYS_FILE)
        {
            if (TR_ERROR_IS_ENOENT(error->code))
            {
                tr_error_clear(&error);
            }
        }
        else
        {
            if (!tr_sys_file_lock(lock_file, TR_SYS_FILE_LOCK_SH | TR_SYS_FILE_LOCK_NB, &error) &&
#ifndef _WIN32
                (error->code == EWOULDBLOCK))
#else
                (error->code == ERROR_LOCK_VIOLATION))
#endif
            {
                ret = true;
                tr_error_clear(&error);
            }

            tr_sys_file_close(lock_file);
        }

        if (error != nullptr)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't open session lock file '{path}': {error} ({error_code})"),
                fmt::arg("path", lock_file_path),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_error_free(error);
        }
    }

    return ret;
}
