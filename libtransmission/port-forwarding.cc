// This file Copyright © 2008-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <memory>

#include <fmt/core.h>

#define LIBTRANSMISSION_PORT_FORWARDING_MODULE

#include "transmission.h"

#include "log.h"
#include "net.h"
#include "port-forwarding-natpmp.h"
#include "port-forwarding-upnp.h"
#include "port-forwarding.h"
#include "timer.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h" // for _()

using namespace std::literals;

class tr_port_forwarding_impl final : public tr_port_forwarding
{
public:
    explicit tr_port_forwarding_impl(Mediator& mediator)
        : mediator_{ mediator }
    {
    }

    ~tr_port_forwarding_impl() override
    {
        is_shutting_down_ = true;
        stopForwarding();
    }

    tr_port_forwarding_impl(tr_port_forwarding_impl&&) = delete;
    tr_port_forwarding_impl(tr_port_forwarding_impl const&) = delete;
    tr_port_forwarding_impl& operator=(tr_port_forwarding_impl&&) = delete;
    tr_port_forwarding_impl& operator=(tr_port_forwarding_impl const&) = delete;

    void localPortChanged() override
    {
        if (!is_enabled_)
        {
            return;
        }

        stopTimer();
        natPulse(false);
        startTimer();
    }

    void setEnabled(bool enabled) override
    {
        if (enabled)
        {
            is_enabled_ = true;
            startTimer();
        }
        else
        {
            is_enabled_ = false;
            stopForwarding();
        }
    }

    [[nodiscard]] bool isEnabled() const noexcept override
    {
        return is_enabled_;
    }

    [[nodiscard]] tr_port_forwarding_state state() const noexcept override
    {
        return std::max(natpmp_state_, upnp_state_);
    }

private:
    void stopTimer()
    {
        timer_.reset();
    }

    void stopForwarding()
    {
        tr_logAddTrace("stopped");
        natPulse(false);

        natpmp_.reset();
        natpmp_state_ = TR_PORT_UNMAPPED;

        tr_upnpClose(upnp_);
        upnp_ = nullptr;
        upnp_state_ = TR_PORT_UNMAPPED;

        stopTimer();
    }

    void startTimer()
    {
        timer_ = mediator_.timerMaker().create([this]() { this->onTimer(); });
        restartTimer();
    }

    void restartTimer()
    {
        if (!timer_)
        {
            return;
        }

        // when to wake up again
        switch (state())
        {
        case TR_PORT_MAPPED:
            // if we're mapped, everything is fine... check back at `renew_time`
            // to renew the port forwarding if it's expired
            do_port_check_ = true;
            if (auto const now = tr_time(); natpmp_->renewTime() > now)
            {
                timer_->startSingleShot(std::chrono::seconds{ natpmp_->renewTime() - now });
            }
            else // ???
            {
                timer_->startSingleShot(1min);
            }
            break;

        case TR_PORT_ERROR:
            // some kind of an error. wait a minute and retry
            timer_->startSingleShot(1min);
            break;

        default:
            // in progress. pulse frequently.
            timer_->startSingleShot(333ms);
            break;
        }
    }

    void onTimer()
    {
        TR_ASSERT(timer_);

        // do something
        natPulse(do_port_check_);
        do_port_check_ = false;

        // set up the timer for the next pulse
        restartTimer();
    }

    static constexpr char const* getNatStateStr(int state)
    {
        switch (state)
        {
        case TR_PORT_MAPPING:
            return _("Starting");

        case TR_PORT_MAPPED:
            return _("Forwarded");

        case TR_PORT_UNMAPPING:
            return _("Stopping");

        case TR_PORT_UNMAPPED:
            return _("Not forwarded");

        default:
            return "???";
        }
    }

    void natPulse(bool do_check)
    {
        auto const is_enabled = is_enabled_ && !is_shutting_down_;

        if (!natpmp_)
        {
            natpmp_ = std::make_unique<tr_natpmp>();
        }

        if (upnp_ == nullptr)
        {
            upnp_ = tr_upnpInit();
        }

        auto const old_state = state();

        auto const result = natpmp_->pulse(mediator_.localPeerPort(), is_enabled);
        natpmp_state_ = result.state;
        if (!std::empty(result.local_port) && !std::empty(result.advertised_port))
        {
            mediator_.onPortForwarded(result.advertised_port);
            tr_logAddInfo(fmt::format(
                _("Mapped private port {private_port} to public port {public_port}"),
                fmt::arg("private_port", result.local_port.host()),
                fmt::arg("public_port", result.advertised_port.host())));
        }

        upnp_state_ = tr_upnpPulse(
            upnp_,
            mediator_.localPeerPort(),
            is_enabled,
            do_check,
            mediator_.incomingPeerAddress().display_name());

        if (auto const new_state = state(); new_state != old_state)
        {
            tr_logAddInfo(fmt::format(
                _("State changed from '{old_state}' to '{state}'"),
                fmt::arg("old_state", getNatStateStr(old_state)),
                fmt::arg("state", getNatStateStr(new_state))));
        }
    }

    Mediator& mediator_;

    bool is_enabled_ = false;
    bool is_shutting_down_ = false;
    bool do_port_check_ = false;

    tr_port_forwarding_state natpmp_state_ = TR_PORT_UNMAPPED;
    tr_port_forwarding_state upnp_state_ = TR_PORT_UNMAPPED;

    tr_upnp* upnp_ = nullptr;
    std::unique_ptr<tr_natpmp> natpmp_;

    std::unique_ptr<libtransmission::Timer> timer_;
};

std::unique_ptr<tr_port_forwarding> tr_port_forwarding::create(Mediator& mediator)
{
    return std::make_unique<tr_port_forwarding_impl>(mediator);
}
