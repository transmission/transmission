// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include <event2/event.h>
#include <event2/util.h>

#include <fmt/core.h>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"

#include "error.h"
#include "error-types.h"
#include "file.h"
#include "log.h"
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h"
#include "watchdir.h"
#include "watchdir-common.h"

using namespace std::literals;

/***
****
***/

/* Non-static and mutable for unit tests */
auto tr_watchdir_retry_limit = size_t{ 3 };
auto tr_watchdir_retry_start_interval = timeval{ 1, 0 };
auto tr_watchdir_retry_max_interval = timeval{ 10, 0 };

class tr_watchdir_retry
{
public:
    tr_watchdir_retry(tr_watchdir_retry const&) = delete;
    tr_watchdir_retry& operator=(tr_watchdir_retry const&) = delete;

    tr_watchdir_retry(tr_watchdir_t handle_in, struct event_base* base, std::string_view name_in)
        : handle_{ handle_in }
        , name_{ name_in }
        , timer_{ evtimer_new(base, onRetryTimer, this) }
    {
        restart();
    }

    ~tr_watchdir_retry()
    {
        evtimer_del(timer_);
        event_free(timer_);
    }

    void restart()
    {
        evtimer_del(timer_);

        counter_ = 0U;
        interval_ = tr_watchdir_retry_start_interval;

        evtimer_add(timer_, &interval_);
    }

    bool bump()
    {
        evtimer_del(timer_);

        if (++counter_ >= tr_watchdir_retry_limit)
        {
            return false;
        }

        // keep doubling the interval, but clamp at max_interval
        evutil_timeradd(&interval_, &interval_, &interval_);
        if (evutil_timercmp(&interval_, &tr_watchdir_retry_max_interval, >))
        {
            interval_ = tr_watchdir_retry_max_interval;
        }

        evtimer_add(timer_, &interval_);
        return true;
    }

    [[nodiscard]] auto const& name() const noexcept
    {
        return name_;
    }

private:
    static void onRetryTimer(evutil_socket_t /*fd*/, short /*type*/, void* self);

    tr_watchdir_t handle_ = nullptr;
    std::string name_;
    size_t counter_ = 0U;
    struct event* const timer_;
    struct timeval interval_ = tr_watchdir_retry_start_interval;
};

// TODO: notify / kqueue / win32 / generic should subclass from tr_watchdir

struct tr_watchdir
{
public:
    tr_watchdir(
        std::string_view path,
        event_base* event_base,
        tr_watchdir_cb callback,
        void* callback_user_data,
        bool force_generic = false)
        : path_{ path }
        , event_base_{ event_base }
        , callback_{ callback }
        , callback_user_data_{ callback_user_data }
    {
        // TODO: backends should be subclasses
        if (!force_generic && (backend_ == nullptr))
        {
#if defined(WITH_INOTIFY)
            backend_ = tr_watchdir_inotify_new(this);
#elif defined(WITH_KQUEUE)
            backend_ = tr_watchdir_kqueue_new(this);
#elif defined(_WIN32)
            backend_ = tr_watchdir_win32_new(this);
#endif
        }

        if (backend_ == nullptr)
        {
            backend_ = tr_watchdir_generic_new(this);
        }
    }

    ~tr_watchdir()
    {
        if (backend_ != nullptr)
        {
            backend_->free_func(backend_);
        }
    }

    tr_watchdir(tr_watchdir const&) = delete;
    tr_watchdir& operator=(tr_watchdir const&) = delete;

    [[nodiscard]] constexpr auto const& path() const noexcept
    {
        return path_;
    }

    [[nodiscard]] constexpr auto* backend() noexcept
    {
        return backend_;
    }

    [[nodiscard]] constexpr auto* eventBase() noexcept
    {
        return event_base_;
    }

    tr_watchdir_status invoke(char const* name)
    {
        /* File may be gone while we're retrying */
        if (!is_regular_file(path(), name))
        {
            return TR_WATCHDIR_IGNORE;
        }

        auto const ret = (*callback_)(this, name, callback_user_data_);
        TR_ASSERT(ret == TR_WATCHDIR_ACCEPT || ret == TR_WATCHDIR_IGNORE || ret == TR_WATCHDIR_RETRY);
        tr_logAddDebug(fmt::format("Callback decided to {:s} file '{:s}'", statusToString(ret), name));
        return ret;
    }

    void erase(std::string_view name)
    {
        active_retries_.erase(std::string{ name });
    }

    void scan(std::unordered_set<std::string>* dir_entries)
    {
        auto new_dir_entries = std::unordered_set<std::string>{};
        tr_error* error = nullptr;

        auto const dir = tr_sys_dir_open(path().c_str(), &error);
        if (dir == TR_BAD_SYS_DIR)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't read '{path}': {error} ({error_code})"),
                fmt::arg("path", path()),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_error_free(error);
            return;
        }

        char const* name = nullptr;
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

            process(name);
        }

        if (error != nullptr)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't read '{path}': {error} ({error_code})"),
                fmt::arg("path", path()),
                fmt::arg("error", error->message),
                fmt::arg("error_code", error->code)));
            tr_error_free(error);
        }

        tr_sys_dir_close(dir);

        if (dir_entries != nullptr)
        {
            *dir_entries = new_dir_entries;
        }
    }

    void process(char const* name_cstr)
    {
        auto& retries = active_retries_;
        auto name = std::string{ name_cstr };
        auto it = retries.find(name);
        if (it != std::end(retries)) // if we already have it, restart it
        {
            it->second->restart();
            return;
        }

        if (invoke(name_cstr) != TR_WATCHDIR_RETRY)
        {
            return;
        }

        retries.try_emplace(name, std::make_unique<tr_watchdir_retry>(this, event_base_, name));
    }

private:
    static bool is_regular_file(std::string_view dir, std::string_view name)
    {
        auto const path = tr_pathbuf{ dir, '/', name };

        auto path_info = tr_sys_path_info{};
        tr_error* error = nullptr;
        bool const ret = tr_sys_path_get_info(path, 0, &path_info, &error) && (path_info.type == TR_SYS_PATH_IS_FILE);

        if (error != nullptr)
        {
            if (!TR_ERROR_IS_ENOENT(error->code))
            {
                tr_logAddWarn(fmt::format(
                    _("Skipping '{path}': {error} ({error_code})"),
                    fmt::arg("path", path),
                    fmt::arg("error", error->message),
                    fmt::arg("error_code", error->code)));
            }

            tr_error_free(error);
        }

        return ret;
    }

    static constexpr std::string_view statusToString(tr_watchdir_status status)
    {
        switch (status)
        {
        case TR_WATCHDIR_ACCEPT:
            return "accept"sv;

        case TR_WATCHDIR_IGNORE:
            return "ignore"sv;

        case TR_WATCHDIR_RETRY:
            return "retry"sv;

        default:
            return "???"sv;
        }
    }

    std::string const path_;
    struct event_base* const event_base_;
    tr_watchdir_backend* backend_ = nullptr;
    tr_watchdir_cb const callback_;
    void* const callback_user_data_;
    std::map<std::string /*name*/, std::unique_ptr<tr_watchdir_retry>> active_retries_;
};

/***
****
***/

void tr_watchdir_retry::onRetryTimer(evutil_socket_t /*fd*/, short /*type*/, void* vself)
{
    TR_ASSERT(vself != nullptr);

    auto* const retry = static_cast<tr_watchdir_retry*>(vself);
    auto const handle = retry->handle_;

    if (handle->invoke(retry->name_.c_str()) == TR_WATCHDIR_RETRY)
    {
        if (retry->bump())
        {
            return;
        }

        tr_logAddWarn(fmt::format(_("Couldn't add torrent file '{path}'"), fmt::arg("path", retry->name())));
    }

    handle->erase(retry->name());
}

/***
****
***/

tr_watchdir_t tr_watchdir_new(
    std::string_view path,
    tr_watchdir_cb callback,
    void* callback_user_data,
    struct event_base* event_base,
    bool force_generic)
{
    return new tr_watchdir{ path, event_base, callback, callback_user_data, force_generic };
}

void tr_watchdir_free(tr_watchdir_t handle)
{
    delete handle;
}

char const* tr_watchdir_get_path(tr_watchdir_t handle)
{
    TR_ASSERT(handle != nullptr);

    return handle->path().c_str();
}

tr_watchdir_backend* tr_watchdir_get_backend(tr_watchdir_t handle)
{
    TR_ASSERT(handle != nullptr);

    return handle->backend();
}

struct event_base* tr_watchdir_get_event_base(tr_watchdir_t handle)
{
    TR_ASSERT(handle != nullptr);

    return handle->eventBase();
}

void tr_watchdir_process(tr_watchdir_t handle, char const* name)
{
    handle->process(name);
}

void tr_watchdir_scan(tr_watchdir_t handle, std::unordered_set<std::string>* dir_entries)
{
    handle->scan(dir_entries);
}
