// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <string>
#include <unordered_set>

#include <event2/event.h>

#include <fmt/core.h>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h"
#include "watchdir.h"
#include "watchdir-common.h"

/***
****
***/

struct tr_watchdir_generic
{
    tr_watchdir_backend base;

    struct event* event;
    std::unordered_set<std::string> dir_entries;
};

#define BACKEND_UPCAST(b) (reinterpret_cast<tr_watchdir_generic*>(b))

/* Non-static and mutable for unit tests. default to 10 sec. */
auto tr_watchdir_generic_interval = timeval{ 10, 0 };

/***
****
***/

static void tr_watchdir_generic_on_event(evutil_socket_t /*fd*/, short /*type*/, void* context)
{
    auto const handle = static_cast<tr_watchdir_t>(context);
    auto* const backend = BACKEND_UPCAST(tr_watchdir_get_backend(handle));

    tr_watchdir_scan(handle, &backend->dir_entries);
}

static void tr_watchdir_generic_free(tr_watchdir_backend* backend_base)
{
    auto* const backend = BACKEND_UPCAST(backend_base);

    if (backend == nullptr)
    {
        return;
    }

    TR_ASSERT(backend->base.free_func == &tr_watchdir_generic_free);

    if (backend->event != nullptr)
    {
        event_del(backend->event);
        event_free(backend->event);
    }

    delete backend;
}

tr_watchdir_backend* tr_watchdir_generic_new(tr_watchdir_t handle)
{
    auto* backend = new tr_watchdir_generic{};
    backend->base.free_func = &tr_watchdir_generic_free;
    backend->event = event_new(tr_watchdir_get_event_base(handle), -1, EV_PERSIST, &tr_watchdir_generic_on_event, handle);
    if (backend->event == nullptr)
    {
        auto const error_code = errno;
        tr_logAddError(fmt::format(
            _("Couldn't create event: {error} ({error_code})"),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code)));
        goto FAIL;
    }

    if (event_add(backend->event, &tr_watchdir_generic_interval) == -1)
    {
        auto const error_code = errno;
        tr_logAddError(fmt::format(
            _("Couldn't add event: {error} ({error_code})"),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code)));
        goto FAIL;
    }

    /* Run initial scan on startup */
    event_active(backend->event, EV_READ, 0);

    return BACKEND_DOWNCAST(backend);

FAIL:
    tr_watchdir_generic_free(BACKEND_DOWNCAST(backend));
    return nullptr;
}
