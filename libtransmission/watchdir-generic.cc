// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <memory>
#include <string_view>
#include <utility>

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "libtransmission/timer.h"
#include "libtransmission/watchdir.h"
#include "libtransmission/watchdir-base.h"

namespace libtransmission
{
namespace
{
class GenericWatchdir final : public impl::BaseWatchdir
{
public:
    GenericWatchdir(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        std::chrono::milliseconds rescan_interval)
        : BaseWatchdir{ dirname, std::move(callback), timer_maker }
        , rescan_timer_{ timer_maker.create() }
    {
        rescan_timer_->set_callback([this]() { scan(); });
        rescan_timer_->start_repeating(rescan_interval);
        scan();
    }

private:
    std::unique_ptr<Timer> rescan_timer_;
};

} // namespace

std::unique_ptr<Watchdir> Watchdir::create_generic(
    std::string_view dirname,
    Callback callback,
    libtransmission::TimerMaker& timer_maker,
    std::chrono::milliseconds rescan_interval)
{
    return std::make_unique<GenericWatchdir>(dirname, std::move(callback), timer_maker, rescan_interval);
}

#if !defined(WITH_INOTIFY) && !defined(WITH_KQUEUE) && !defined(_WIN32)
// no native impl, so use generic
std::unique_ptr<Watchdir> Watchdir::create(
    std::string_view dirname,
    Callback callback,
    libtransmission::TimerMaker& timer_maker,
    struct event_base* /*evbase*/)
{
    return std::make_unique<GenericWatchdir>(dirname, std::move(callback), timer_maker, generic_rescan_interval());
}
#endif

} // namespace libtransmission
