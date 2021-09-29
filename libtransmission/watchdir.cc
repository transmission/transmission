/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* strcmp() */

#include <event2/event.h>
#include <event2/util.h>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"
#include "error.h"
#include "error-types.h"
#include "file.h"
#include "log.h"
#include "ptrarray.h"
#include "tr-assert.h"
#include "utils.h"
#include "watchdir.h"
#include "watchdir-common.h"

/***
****
***/

#define log_debug(...) \
    (!tr_logLevelIsActive(TR_LOG_DEBUG) ? (void)0 : tr_logAddMessage(__FILE__, __LINE__, TR_LOG_DEBUG, "watchdir", __VA_ARGS__))

#define log_error(...) \
    (!tr_logLevelIsActive(TR_LOG_ERROR) ? (void)0 : tr_logAddMessage(__FILE__, __LINE__, TR_LOG_ERROR, "watchdir", __VA_ARGS__))

/***
****
***/

struct tr_watchdir
{
    char* path;
    tr_watchdir_cb callback;
    void* callback_user_data;
    struct event_base* event_base;
    tr_watchdir_backend* backend;
    tr_ptrArray active_retries;
};

/***
****
***/

static bool is_regular_file(char const* dir, char const* name)
{
    char* const path = tr_buildPath(dir, name, nullptr);
    auto path_info = tr_sys_path_info{};
    tr_error* error = nullptr;

    bool const ret = tr_sys_path_get_info(path, 0, &path_info, &error) && (path_info.type == TR_SYS_PATH_IS_FILE);

    if (error != nullptr)
    {
        if (!TR_ERROR_IS_ENOENT(error->code))
        {
            log_error("Failed to get type of \"%s\" (%d): %s", path, error->code, error->message);
        }

        tr_error_free(error);
    }

    tr_free(path);
    return ret;
}

static char const* watchdir_status_to_string(tr_watchdir_status status)
{
    switch (status)
    {
    case TR_WATCHDIR_ACCEPT:
        return "accept";

    case TR_WATCHDIR_IGNORE:
        return "ignore";

    case TR_WATCHDIR_RETRY:
        return "retry";

    default:
        return "???";
    }
}

static tr_watchdir_status tr_watchdir_process_impl(tr_watchdir_t handle, char const* name)
{
    /* File may be gone while we're retrying */
    if (!is_regular_file(tr_watchdir_get_path(handle), name))
    {
        return TR_WATCHDIR_IGNORE;
    }

    tr_watchdir_status const ret = (*handle->callback)(handle, name, handle->callback_user_data);

    TR_ASSERT(ret == TR_WATCHDIR_ACCEPT || ret == TR_WATCHDIR_IGNORE || ret == TR_WATCHDIR_RETRY);

    log_debug("Callback decided to %s file \"%s\"", watchdir_status_to_string(ret), name);

    return ret;
}

/***
****
***/

typedef struct tr_watchdir_retry
{
    tr_watchdir_t handle;
    char* name;
    size_t counter;
    struct event* timer;
    struct timeval interval;
} tr_watchdir_retry;

/* Non-static and mutable for unit tests */
auto tr_watchdir_retry_limit = size_t{ 3 };
auto tr_watchdir_retry_start_interval = timeval{ 1, 0 };
auto tr_watchdir_retry_max_interval = timeval{ 10, 0 };

#define tr_watchdir_retries_init(r) (void)0
#define tr_watchdir_retries_destroy(r) tr_ptrArrayDestruct((r), (PtrArrayForeachFunc)&tr_watchdir_retry_free)
#define tr_watchdir_retries_insert(r, v) tr_ptrArrayInsertSorted((r), (v), &compare_retry_names)
#define tr_watchdir_retries_remove(r, v) tr_ptrArrayRemoveSortedPointer((r), (v), &compare_retry_names)
#define tr_watchdir_retries_find(r, v) tr_ptrArrayFindSorted((r), (v), &compare_retry_names)

static int compare_retry_names(void const* a, void const* b)
{
    return strcmp(((tr_watchdir_retry const*)a)->name, ((tr_watchdir_retry const*)b)->name);
}

static void tr_watchdir_retry_free(tr_watchdir_retry* retry);

static void tr_watchdir_on_retry_timer(evutil_socket_t fd, short type, void* context)
{
    TR_UNUSED(fd);
    TR_UNUSED(type);

    TR_ASSERT(context != nullptr);

    auto* const retry = static_cast<tr_watchdir_retry*>(context);
    tr_watchdir_t const handle = retry->handle;

    if (tr_watchdir_process_impl(handle, retry->name) == TR_WATCHDIR_RETRY)
    {
        if (++retry->counter < tr_watchdir_retry_limit)
        {
            evutil_timeradd(&retry->interval, &retry->interval, &retry->interval);

            if (evutil_timercmp(&retry->interval, &tr_watchdir_retry_max_interval, >))
            {
                retry->interval = tr_watchdir_retry_max_interval;
            }

            evtimer_del(retry->timer);
            evtimer_add(retry->timer, &retry->interval);
            return;
        }

        log_error("Failed to add (corrupted?) torrent file: %s", retry->name);
    }

    tr_watchdir_retries_remove(&handle->active_retries, retry);
    tr_watchdir_retry_free(retry);
}

static tr_watchdir_retry* tr_watchdir_retry_new(tr_watchdir_t handle, char const* name)
{
    tr_watchdir_retry* retry;

    retry = tr_new0(tr_watchdir_retry, 1);
    retry->handle = handle;
    retry->name = tr_strdup(name);
    retry->timer = evtimer_new(handle->event_base, &tr_watchdir_on_retry_timer, retry);
    retry->interval = tr_watchdir_retry_start_interval;

    evtimer_add(retry->timer, &retry->interval);

    return retry;
}

static void tr_watchdir_retry_free(tr_watchdir_retry* retry)
{
    if (retry == nullptr)
    {
        return;
    }

    if (retry->timer != nullptr)
    {
        evtimer_del(retry->timer);
        event_free(retry->timer);
    }

    tr_free(retry->name);
    tr_free(retry);
}

static void tr_watchdir_retry_restart(tr_watchdir_retry* retry)
{
    TR_ASSERT(retry != nullptr);

    evtimer_del(retry->timer);

    retry->counter = 0;
    retry->interval = tr_watchdir_retry_start_interval;

    evtimer_add(retry->timer, &retry->interval);
}

/***
****
***/

tr_watchdir_t tr_watchdir_new(
    char const* path,
    tr_watchdir_cb callback,
    void* callback_user_data,
    struct event_base* event_base,
    bool force_generic)
{
    tr_watchdir_t handle;

    handle = tr_new0(struct tr_watchdir, 1);
    handle->path = tr_strdup(path);
    handle->callback = callback;
    handle->callback_user_data = callback_user_data;
    handle->event_base = event_base;
    tr_watchdir_retries_init(&handle->active_retries);

    if (!force_generic && (handle->backend == nullptr))
    {
#if defined(WITH_INOTIFY)
        handle->backend = tr_watchdir_inotify_new(handle);
#elif defined(WITH_KQUEUE)
        handle->backend = tr_watchdir_kqueue_new(handle);
#elif defined(_WIN32)
        handle->backend = tr_watchdir_win32_new(handle);
#endif
    }

    if (handle->backend == nullptr)
    {
        handle->backend = tr_watchdir_generic_new(handle);
    }

    if (handle->backend == nullptr)
    {
        tr_watchdir_free(handle);
        handle = nullptr;
    }
    else
    {
        TR_ASSERT(handle->backend->free_func != nullptr);
    }

    return handle;
}

void tr_watchdir_free(tr_watchdir_t handle)
{
    if (handle == nullptr)
    {
        return;
    }

    tr_watchdir_retries_destroy(&handle->active_retries);

    if (handle->backend != nullptr)
    {
        handle->backend->free_func(handle->backend);
    }

    tr_free(handle->path);
    tr_free(handle);
}

char const* tr_watchdir_get_path(tr_watchdir_t handle)
{
    TR_ASSERT(handle != nullptr);

    return handle->path;
}

tr_watchdir_backend* tr_watchdir_get_backend(tr_watchdir_t handle)
{
    TR_ASSERT(handle != nullptr);

    return handle->backend;
}

struct event_base* tr_watchdir_get_event_base(tr_watchdir_t handle)
{
    TR_ASSERT(handle != nullptr);

    return handle->event_base;
}

/***
****
***/

void tr_watchdir_process(tr_watchdir_t handle, char const* name)
{
    TR_ASSERT(handle != nullptr);

    auto const search_key = tr_watchdir_retry{ {}, const_cast<char*>(name), {}, {}, {} };
    auto* existing_retry = static_cast<tr_watchdir_retry*>(tr_watchdir_retries_find(&handle->active_retries, &search_key));
    if (existing_retry != nullptr)
    {
        tr_watchdir_retry_restart(existing_retry);
        return;
    }

    if (tr_watchdir_process_impl(handle, name) == TR_WATCHDIR_RETRY)
    {
        tr_watchdir_retry* retry = tr_watchdir_retry_new(handle, name);
        tr_watchdir_retries_insert(&handle->active_retries, retry);
    }
}

void tr_watchdir_scan(tr_watchdir_t handle, std::unordered_set<std::string>* dir_entries)
{
    tr_sys_dir_t dir;
    char const* name;
    auto new_dir_entries = std::unordered_set<std::string>{};
    tr_error* error = nullptr;

    if ((dir = tr_sys_dir_open(handle->path, &error)) == TR_BAD_SYS_DIR)
    {
        log_error("Failed to open directory \"%s\" (%d): %s", handle->path, error->code, error->message);
        tr_error_free(error);
        return;
    }

    while ((name = tr_sys_dir_read_name(dir, &error)) != nullptr)
    {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        {
            continue;
        }

        if (dir_entries != nullptr)
        {
            auto const namestr = std::string(name);
            new_dir_entries.insert(namestr);

            if (dir_entries->count(namestr) != 0)
            {
                continue;
            }
        }

        tr_watchdir_process(handle, name);
    }

    if (error != nullptr)
    {
        log_error("Failed to read directory \"%s\" (%d): %s", handle->path, error->code, error->message);
        tr_error_free(error);
    }

    tr_sys_dir_close(dir, nullptr);

    if (dir_entries != nullptr)
    {
        *dir_entries = new_dir_entries;
    }
}
