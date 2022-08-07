// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include <set>

#include "transmission.h"

#include "error-types.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "tr-strbuf.h"
#include "utils.h" // for _()
#include "watchdir-common.h"
#include "watchdir.h"

namespace
{

[[nodiscard]] constexpr std::string_view actionToString(tr_watchdir::Action action)
{
    switch (action)
    {
    case tr_watchdir::Action::Retry:
        return "retry";

    case tr_watchdir::Action::Done:
        return "done";
    }
}

[[nodiscard]] bool isRegularFile(std::string_view dir, std::string_view name)
{
    auto const path = tr_pathbuf{ dir, '/', name };

    tr_error* error = nullptr;
    auto const info = tr_sys_path_get_info(path, 0, &error);
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

    return info && info->isFile();
}

} // namespace

void tr_watchdir_base::updateRetryTimer()
{
    evtimer_del(retry_timer_.get());

    if (auto const kick_at = nextKickTime(); kick_at)
    {
        auto const now = current_time_func_();
        auto const sec = static_cast<int>(*kick_at > now ? *kick_at - now : 1);
        auto const usec = 0;
        tr_timerAdd(*retry_timer_, sec, usec);
    }
}

void tr_watchdir_base::processFile(std::string_view basename)
{
    if (!isRegularFile(dirname_, basename) || handled_.count(basename) != 0)
    {
        return;
    }

    auto const action = callback_(dirname_, basename);
    auto const now = current_time_func_();
    tr_logAddDebug(fmt::format("Callback decided to {:s} file '{:s}'", actionToString(action), basename));
    if (action == Action::Retry)
    {
        auto const [iter, added] = pending_.try_emplace(std::string{ basename }, Pending{});

        auto& info = iter->second;
        ++info.strikes;
        info.last_kick_at = now;

        if (info.strikes < retry_limit_)
        {
            setNextKickTime(info);
        }
        else
        {
            tr_logAddWarn(fmt::format(_("Couldn't add torrent file '{path}'"), fmt::arg("path", basename)));
            pending_.erase(iter);
        }
    }
    else if (action == Action::Done)
    {
        handled_.insert(std::string{ basename });
    }
}

void tr_watchdir_base::scan()
{
    auto new_dir_entries = std::set<std::string>{};

    tr_error* error = nullptr;
    auto const dir = tr_sys_dir_open(dirname_.c_str(), &error);
    if (dir == TR_BAD_SYS_DIR)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", dirname()),
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

        processFile(name);
    }

    if (error != nullptr)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't read '{path}': {error} ({error_code})"),
            fmt::arg("path", dirname()),
            fmt::arg("error", error->message),
            fmt::arg("error_code", error->code)));
        tr_error_free(error);
    }

    tr_sys_dir_close(dir);
}

#if !defined(WITH_INOTIFY) && !defined(WITH_KQUEUE) && !defined(_WIN32)
// no native impl, so use generic
std::unique_ptr<tr_watchdir> tr_watchdir::create(
    std::string_view dirname,
    Callback callback,
    event_base* event_base,
    TimeFunc current_time_func)
{
#warning tr_watchdir::create creates generic
    return tr_watchdir_base::createGeneric(dirname, std::move(callback), event_base, current_time_func);
}
#endif
