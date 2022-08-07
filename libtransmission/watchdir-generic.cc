// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/event.h>

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
        event_base* event_base,
        TimeFunc time_func,
        size_t rescan_interval_msec)
        : tr_watchdir_base{ dirname, std::move(callback), event_base, time_func }
        , rescan_interval_msec_{ rescan_interval_msec }
    {
        rescan();
    }

private:
    static void onRescanTimer(evutil_socket_t /*unused*/, short /*unused*/, void* vself)
    {
        static_cast<tr_watchdir_generic*>(vself)->rescan();
    }
    void rescan()
    {
        scan();

        evtimer_del(rescan_timer_.get());
        tr_timerAddMsec(*rescan_timer_, rescan_interval_msec_);
    }

    size_t const rescan_interval_msec_;
    WrappedEvent const rescan_timer_{ evtimer_new(eventBase(), onRescanTimer, this) };
};

std::unique_ptr<tr_watchdir> tr_watchdir::createGeneric(
    std::string_view dirname,
    Callback callback,
    event_base* event_base,
    TimeFunc time_func,
    size_t rescan_interval_msec)
{
    return std::make_unique<tr_watchdir_generic>(dirname, std::move(callback), event_base, time_func, rescan_interval_msec);
}
