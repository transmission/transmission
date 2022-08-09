// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#define LIBTRANSMISSION_WATCHDIR_MODULE

#include "transmission.h"

#include "watchdir-base.h"
#include "utils.h" // for tr_timerAddMsec()

class tr_watchdir_generic final : public tr_watchdir_base
{
public:
    tr_watchdir_generic(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        std::chrono::milliseconds rescan_interval)
        : tr_watchdir_base{ dirname, std::move(callback), timer_maker }
        , rescan_timer_{ timer_maker.create() }
    {
        rescan_timer_->setCallback([this]() { scan(); });
        rescan_timer_->startRepeating(rescan_interval);
        scan();
    }

private:
    std::unique_ptr<libtransmission::Timer> rescan_timer_;
};

std::unique_ptr<tr_watchdir> tr_watchdir::createGeneric(
    std::string_view dirname,
    Callback callback,
    libtransmission::TimerMaker& timer_maker,
    std::chrono::milliseconds rescan_interval)
{
    return std::make_unique<tr_watchdir_generic>(dirname, std::move(callback), timer_maker, rescan_interval);
}

#if !defined(WITH_INOTIFY) && !defined(WITH_KQUEUE) && !defined(_WIN32)
// no native impl, so use generic
std::unique_ptr<tr_watchdir> tr_watchdir::create(
    std::string_view dirname,
    Callback callback,
    libtransmission::TimerMaker& timer_maker)
{
    return std::make_unique<tr_watchdir_generic>(dirname, std::move(callback), timer_maker, genericRescanIntervalMsec());
}
#endif
