// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <climits> /* NAME_MAX */
#include <string_view>

#include <unistd.h> /* close() */

#include <sys/inotify.h>

#include <event2/bufferevent.h>
#include <event2/event.h>

#include <fmt/core.h>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"
#include "watchdir.h"
#include "watchdir-common.h"

static auto constexpr LogName = std::string_view{ "watchdir:inotify" };

/***
****
***/

struct tr_watchdir_inotify
{
    tr_watchdir_backend base;

    int infd;
    int inwd;
    struct bufferevent* event;
};

#define BACKEND_UPCAST(b) ((tr_watchdir_inotify*)(b))

#define INOTIFY_WATCH_MASK (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)

/***
****
***/

static void tr_watchdir_inotify_on_first_scan(evutil_socket_t /*fd*/, short /*type*/, void* context)
{
    auto const handle = static_cast<tr_watchdir_t>(context);

    tr_watchdir_scan(handle, nullptr);
}

static void tr_watchdir_inotify_on_event(struct bufferevent* event, void* context)
{
    TR_ASSERT(context != nullptr);

    auto const handle = static_cast<tr_watchdir_t>(context);
#ifdef TR_ENABLE_ASSERTS
    tr_watchdir_inotify const* const backend = BACKEND_UPCAST(tr_watchdir_get_backend(handle));
#endif
    struct inotify_event ev;
    size_t name_size = NAME_MAX + 1;
    auto* name = tr_new(char, name_size);

    /* Read the size of the struct excluding name into buf. Guaranteed to have at
       least sizeof(ev) available */
    auto nread = size_t{};
    while ((nread = bufferevent_read(event, &ev, sizeof(ev))) != 0)
    {
        if (nread == (size_t)-1)
        {
            auto const error_code = errno;
            tr_logAddNamedError(
                LogName,
                fmt::format(
                    _("Couldn't read event: {error} ({error_code})"),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
            break;
        }

        if (nread != sizeof(ev))
        {
            tr_logAddNamedError(
                LogName,
                fmt::format(
                    _("Couldn't read event: expected {expected_size}, got {actual_size}"),
                    fmt::arg("expected_size", sizeof(ev)),
                    fmt::arg("actual_size", nread)));
            break;
        }

        TR_ASSERT(ev.wd == backend->inwd);
        TR_ASSERT((ev.mask & INOTIFY_WATCH_MASK) != 0);
        TR_ASSERT(ev.len > 0);

        if (ev.len > name_size)
        {
            name_size = ev.len;
            name = tr_renew(char, name, name_size);
        }

        /* Consume entire name into buffer */
        if ((nread = bufferevent_read(event, name, ev.len)) == (size_t)-1)
        {
            auto const error_code = errno;
            tr_logAddNamedError(
                LogName,
                fmt::format(
                    _("Couldn't read filename: {error} ({error_code})"),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
            break;
        }

        if (nread != ev.len)
        {
            tr_logAddNamedError(
                LogName,
                fmt::format(
                    _("Couldn't read filename: expected {expected_size}, got {actual_size}"),
                    fmt::arg("expected_size", sizeof(ev)),
                    fmt::arg("actual_size", nread)));
            break;
        }

        tr_watchdir_process(handle, name);
    }

    tr_free(name);
}

static void tr_watchdir_inotify_free(tr_watchdir_backend* backend_base)
{
    auto* const backend = BACKEND_UPCAST(backend_base);

    if (backend == nullptr)
    {
        return;
    }

    TR_ASSERT(backend->base.free_func == &tr_watchdir_inotify_free);

    if (backend->event != nullptr)
    {
        bufferevent_disable(backend->event, EV_READ);
        bufferevent_free(backend->event);
    }

    if (backend->infd != -1)
    {
        if (backend->inwd != -1)
        {
            inotify_rm_watch(backend->infd, backend->inwd);
        }

        close(backend->infd);
    }

    tr_free(backend);
}

tr_watchdir_backend* tr_watchdir_inotify_new(tr_watchdir_t handle)
{
    char const* const path = tr_watchdir_get_path(handle);

    auto* const backend = tr_new0(tr_watchdir_inotify, 1);
    backend->base.free_func = &tr_watchdir_inotify_free;

    backend->infd = inotify_init();
    if (backend->infd == -1)
    {
        auto const error_code = errno;
        tr_logAddNamedError(
            LogName,
            fmt::format(
                _("Couldn't watch '{path}': {error} ({error_code})"),
                fmt::arg("path", path),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
        goto FAIL;
    }

    backend->inwd = inotify_add_watch(backend->infd, path, INOTIFY_WATCH_MASK | IN_ONLYDIR);
    if (backend->inwd == -1)
    {
        auto const error_code = errno;
        tr_logAddNamedError(
            LogName,
            fmt::format(
                _("Couldn't watch '{path}': {error} ({error_code})"),
                fmt::arg("path", path),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
        goto FAIL;
    }

    backend->event = bufferevent_socket_new(tr_watchdir_get_event_base(handle), backend->infd, 0);
    if (backend->event == nullptr)
    {
        auto const error_code = errno;
        tr_logAddNamedError(
            LogName,
            fmt::format(
                _("Couldn't watch '{path}': {error} ({error_code})"),
                fmt::arg("path", path),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
        goto FAIL;
    }

    /* Guarantees at least the sizeof an inotify event will be available in the
       event buffer */
    bufferevent_setwatermark(backend->event, EV_READ, sizeof(struct inotify_event), 0);
    bufferevent_setcb(backend->event, &tr_watchdir_inotify_on_event, nullptr, nullptr, handle);
    bufferevent_enable(backend->event, EV_READ);

    /* Perform an initial scan on the directory */
    if (event_base_once(
            tr_watchdir_get_event_base(handle),
            -1,
            EV_TIMEOUT,
            &tr_watchdir_inotify_on_first_scan,
            handle,
            nullptr) == -1)
    {
        auto const error_code = errno;
        tr_logAddNamedWarn(
            LogName,
            fmt::format(
                _("Couldn't scan '{path}': {error} ({error_code})"),
                fmt::arg("path", path),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
    }

    return BACKEND_DOWNCAST(backend);

FAIL:
    tr_watchdir_inotify_free(BACKEND_DOWNCAST(backend));
    return nullptr;
}
