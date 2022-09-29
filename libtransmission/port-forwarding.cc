// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <memory>

#include <fmt/core.h>

#include "transmission.h"
#include "natpmp_local.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h"
#include "port-forwarding.h"
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "upnp.h"
#include "utils.h" // for _()

using namespace std::literals;

struct tr_shared
{
    explicit tr_shared(tr_session& session_in)
        : session_{ session_in }
    {
    }

    tr_session& session_;

    bool is_enabled_ = false;
    bool is_shutting_down_ = false;
    bool do_port_check_ = false;

    tr_port_forwarding_state natpmp_state_ = TR_PORT_UNMAPPED;
    tr_port_forwarding_state upnp_state_ = TR_PORT_UNMAPPED;

    tr_upnp* upnp_ = nullptr;
    std::unique_ptr<tr_natpmp> natpmp_;

    std::unique_ptr<libtransmission::Timer> timer_;
};

/***
****
***/

static char const* getNatStateStr(int state)
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

static void natPulse(tr_shared* s, bool do_check)
{
    auto& session = s->session_;
    tr_port const private_peer_port = session.private_peer_port;
    bool const is_enabled = s->is_enabled_ && !s->is_shutting_down_;

    if (!s->natpmp_)
    {
        s->natpmp_ = std::make_unique<tr_natpmp>();
    }

    if (s->upnp_ == nullptr)
    {
        s->upnp_ = tr_upnpInit();
    }

    auto const old_state = tr_sharedTraversalState(s);

    auto public_peer_port = tr_port{};
    auto received_private_port = tr_port{};
    s->natpmp_state_ = s->natpmp_->pulse(private_peer_port, is_enabled, &public_peer_port, &received_private_port);

    if (s->natpmp_state_ == TR_PORT_MAPPED)
    {
        session.public_peer_port = public_peer_port;
        session.private_peer_port = received_private_port;
        tr_logAddInfo(fmt::format(
            _("Mapped private port {private_port} to public port {public_port}"),
            fmt::arg("public_port", session.public_peer_port.host()),
            fmt::arg("private_port", session.private_peer_port.host())));
    }

    s->upnp_state_ = tr_upnpPulse(s->upnp_, private_peer_port, is_enabled, do_check, session.bind_ipv4.readable());

    if (auto const new_state = tr_sharedTraversalState(s); new_state != old_state)
    {
        tr_logAddInfo(fmt::format(
            _("State changed from '{old_state}' to '{state}'"),
            fmt::arg("old_state", getNatStateStr(old_state)),
            fmt::arg("state", getNatStateStr(new_state))));
    }
}

static void restartTimer(tr_shared* s)
{
    auto& timer = s->timer_;
    if (!timer)
    {
        return;
    }

    // when to wake up again
    switch (tr_sharedTraversalState(s))
    {
    case TR_PORT_MAPPED:
        // if we're mapped, everything is fine... check back at `renew_time`
        // to renew the port forwarding if it's expired
        s->do_port_check_ = true;
        if (auto const now = tr_time(); s->natpmp_->renewTime() > now)
        {
            timer->startSingleShot(std::chrono::seconds{ s->natpmp_->renewTime() - now });
        }
        else // ???
        {
            timer->startSingleShot(1min);
        }
        break;

    case TR_PORT_ERROR:
        // some kind of an error. wait a minute and retry
        timer->startSingleShot(1min);
        break;

    default:
        // in progress. pulse frequently.
        timer->startSingleShot(333ms);
        break;
    }
}

static void onTimer(void* vshared)
{
    auto* s = static_cast<tr_shared*>(vshared);

    TR_ASSERT(s != nullptr);
    TR_ASSERT(s->timer_);

    /* do something */
    natPulse(s, s->do_port_check_);
    s->do_port_check_ = false;

    /* set up the timer for the next pulse */
    restartTimer(s);
}

/***
****
***/

tr_shared* tr_sharedInit(tr_session& session)
{
    return new tr_shared{ session };
}

static void stop_timer(tr_shared* s)
{
    s->timer_.reset();
}

static void stop_forwarding(tr_shared* s)
{
    tr_logAddTrace("stopped");
    natPulse(s, false);

    s->natpmp_.reset();
    s->natpmp_state_ = TR_PORT_UNMAPPED;

    tr_upnpClose(s->upnp_);
    s->upnp_ = nullptr;
    s->upnp_state_ = TR_PORT_UNMAPPED;

    stop_timer(s);
}

void tr_sharedClose(tr_session& session)
{
    tr_shared* shared = session.shared;

    shared->is_shutting_down_ = true;
    stop_forwarding(shared);
    shared->session_.shared = nullptr;
    delete shared;
}

static void start_timer(tr_shared* s)
{
    s->timer_ = s->session_.timerMaker().create(onTimer, s);
    restartTimer(s);
}

void tr_sharedTraversalEnable(tr_shared* s, bool is_enable)
{
    if (is_enable)
    {
        s->is_enabled_ = true;
        start_timer(s);
    }
    else
    {
        s->is_enabled_ = false;
        stop_forwarding(s);
    }
}

void tr_sharedPortChanged(tr_session& session)
{
    auto* const s = session.shared;

    if (s->is_enabled_)
    {
        stop_timer(s);
        natPulse(s, false);
        start_timer(s);
    }
}

bool tr_sharedTraversalIsEnabled(tr_shared const* s)
{
    return s->is_enabled_;
}

tr_port_forwarding_state tr_sharedTraversalState(tr_shared const* s)
{
    return std::max(s->natpmp_state_, s->upnp_state_);
}
