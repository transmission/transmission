// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <climits> /* NAME_MAX */

#include <iostream> // NOCOMMIT

#include <unistd.h> /* close() */

#include <sys/inotify.h>

#include <event2/bufferevent.h>
#include <event2/event.h>

#include <fmt/core.h>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"

#include "log.h"
#include "tr-assert.h"
#include "tr-strbuf.h"
#include "utils.h"
#include "watchdir-base.h"

class tr_watchdir_inotify : public tr_watchdir_base
{
private:
    static auto constexpr InotifyWatchMask = uint32_t{ IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE };

public:
    tr_watchdir_inotify(std::string_view dirname, Callback callback, event_base* event_base, TimeFunc time_func)
        : tr_watchdir_base{ dirname, std::move(callback), event_base, time_func }
    {
        init();
        scan();
    }

    tr_watchdir_inotify(tr_watchdir_inotify&&) = delete;
    tr_watchdir_inotify(tr_watchdir_inotify const&) = delete;
    tr_watchdir_inotify& operator=(tr_watchdir_inotify&&) = delete;
    tr_watchdir_inotify& operator=(tr_watchdir_inotify const&) = delete;

    ~tr_watchdir_inotify() override
    {
        if (event_ != nullptr)
        {
            bufferevent_disable(event_, EV_READ);
            bufferevent_free(event_);
        }

        if (infd_ != -1)
        {
            if (inwd_ != -1)
            {
                inotify_rm_watch(infd_, inwd_);
            }

            close(infd_);
        }
    }

private:
    void init()
    {
        infd_ = inotify_init();
        if (infd_ == -1)
        {
            auto const error_code = errno;
            tr_logAddError(fmt::format(
                _("Couldn't watch '{path}': {error} ({error_code})"),
                fmt::arg("path", dirname()),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
            return;
        }

        inwd_ = inotify_add_watch(infd_, tr_pathbuf{ dirname() }, InotifyWatchMask | IN_ONLYDIR);
        if (inwd_ == -1)
        {
            auto const error_code = errno;
            tr_logAddError(fmt::format(
                _("Couldn't watch '{path}': {error} ({error_code})"),
                fmt::arg("path", dirname()),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
            return;
        }

        event_ = bufferevent_socket_new(eventBase(), infd_, 0);
        if (event_ == nullptr)
        {
            auto const error_code = errno;
            tr_logAddError(fmt::format(
                _("Couldn't watch '{path}': {error} ({error_code})"),
                fmt::arg("path", dirname()),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
            return;
        }

        // guarantees at least the sizeof an inotify event will be available in the event buffer
        bufferevent_setwatermark(event_, EV_READ, sizeof(struct inotify_event), 0);
        bufferevent_setcb(event_, onInotifyEvent, nullptr, nullptr, this);
        bufferevent_enable(event_, EV_READ);
    }

    static void onInotifyEvent(struct bufferevent* event, void* vself)
    {
        static_cast<tr_watchdir_inotify*>(vself)->handleInotifyEvent(event);
    }

    void handleInotifyEvent(struct bufferevent* event)
    {
        struct inotify_event ev;
        auto name = std::string{};

        // Read the size of the struct excluding name into buf.
        // Guaranteed to have at least sizeof(ev) available.
        auto nread = size_t{};
        while ((nread = bufferevent_read(event, &ev, sizeof(ev))) != 0)
        {
            if (nread == (size_t)-1)
            {
                auto const error_code = errno;
                tr_logAddError(fmt::format(
                    _("Couldn't read event: {error} ({error_code})"),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
                break;
            }

            if (nread != sizeof(ev))
            {
                tr_logAddError(fmt::format(
                    _("Couldn't read event: expected {expected_size}, got {actual_size}"),
                    fmt::arg("expected_size", sizeof(ev)),
                    fmt::arg("actual_size", nread)));
                break;
            }

            TR_ASSERT(ev.wd == inwd_);
            TR_ASSERT((ev.mask & InotifyWatchMask) != 0);
            TR_ASSERT(ev.len > 0);

            // consume entire name into buffer
            name.resize(ev.len);
            nread = bufferevent_read(event, std::data(name), ev.len);
            if (nread == static_cast<size_t>(-1))
            {
                auto const error_code = errno;
                tr_logAddError(fmt::format(
                    _("Couldn't read filename: {error} ({error_code})"),
                    fmt::arg("error", tr_strerror(error_code)),
                    fmt::arg("error_code", error_code)));
                break;
            }

            if (nread != ev.len)
            {
                tr_logAddError(fmt::format(
                    _("Couldn't read filename: expected {expected_size}, got {actual_size}"),
                    fmt::arg("expected_size", sizeof(ev)),
                    fmt::arg("actual_size", nread)));
                break;
            }

            // NB: `name` may have extra trailing zeroes from inotify;
            // pass the c_str() so that processFile gets the right strlen
            processFile(name.c_str());
        }
    }

private:
    int infd_ = -1;
    int inwd_ = -1;
    struct bufferevent* event_ = nullptr;
};

std::unique_ptr<tr_watchdir> tr_watchdir::create(
    std::string_view dirname,
    Callback callback,
    event_base* event_base,
    TimeFunc time_func)
{
    return std::make_unique<tr_watchdir_inotify>(dirname, std::move(callback), event_base, time_func);
}
