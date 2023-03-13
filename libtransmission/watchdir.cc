// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <set>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"

#include "error-types.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "tr-strbuf.h"
#include "utils.h" // for _()
#include "watchdir-base.h"

using namespace std::literals;

namespace libtransmission
{
namespace
{

[[nodiscard]] constexpr std::string_view actionToString(Watchdir::Action action)
{
    switch (action)
    {
    case Watchdir::Action::Retry:
        return "retry";

    case Watchdir::Action::Done:
        return "done";
    }

    return "???";
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

namespace impl
{

void BaseWatchdir::processFile(std::string_view basename)
{
    if (!isRegularFile(dirname_, basename) || handled_.count(basename) != 0)
    {
        return;
    }

    auto const action = callback_(dirname_, basename);
    tr_logAddDebug(fmt::format("Callback decided to {:s} file '{:s}'", actionToString(action), basename));
    if (action == Action::Retry)
    {
        auto const [iter, added] = pending_.try_emplace(std::string{ basename });

        auto const now = std::chrono::steady_clock::now();
        auto& info = iter->second;
        ++info.strikes;
        info.last_kick_at = now;

        if (info.first_kick_at == Timestamp{})
        {
            info.first_kick_at = now;
        }

        if (now - info.first_kick_at > timeoutDuration())
        {
            tr_logAddWarn(fmt::format(_("Couldn't add torrent file '{path}'"), fmt::arg("path", basename)));
            pending_.erase(iter);
        }
        else
        {
            setNextKickTime(info);
            restartTimerIfPending();
        }
    }
    else if (action == Action::Done)
    {
        handled_.emplace(basename);
    }
}

void BaseWatchdir::scan()
{
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

    for (;;)
    {
        char const* const name = tr_sys_dir_read_name(dir, &error);
        if (name == nullptr)
        {
            break;
        }

        if ("."sv == name || ".."sv == name)
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

} // namespace impl
} // namespace libtransmission
