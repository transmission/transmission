// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>

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
    tr_shared(tr_session* session_in)
        : session{ session_in }
    {
    }

    tr_session* const session = nullptr;

    bool isEnabled = false;
    bool isShuttingDown = false;
    bool doPortCheck = false;

    tr_port_forwarding natpmpStatus = TR_PORT_UNMAPPED;
    tr_port_forwarding upnpStatus = TR_PORT_UNMAPPED;

    tr_upnp* upnp = nullptr;
    tr_natpmp* natpmp = nullptr;

    std::unique_ptr<libtransmission::Timer> timer;
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
    auto* session = s->session;
    tr_port const private_peer_port = session->private_peer_port;
    bool const is_enabled = s->isEnabled && !s->isShuttingDown;

    if (s->natpmp == nullptr)
    {
        s->natpmp = tr_natpmpInit();
    }

    if (s->upnp == nullptr)
    {
        s->upnp = tr_upnpInit();
    }

    auto const old_status = tr_sharedTraversalStatus(s);

    auto public_peer_port = tr_port{};
    auto received_private_port = tr_port{};
    s->natpmpStatus = tr_natpmpPulse(s->natpmp, private_peer_port, is_enabled, &public_peer_port, &received_private_port);

    if (s->natpmpStatus == TR_PORT_MAPPED)
    {
        session->public_peer_port = public_peer_port;
        session->private_peer_port = received_private_port;
        tr_logAddInfo(fmt::format(
            _("Mapped private port {private_port} to public port {public_port}"),
            fmt::arg("public_port", session->public_peer_port.host()),
            fmt::arg("private_port", session->private_peer_port.host())));
    }

    s->upnpStatus = tr_upnpPulse(s->upnp, private_peer_port, is_enabled, do_check, session->bind_ipv4.readable());

    auto const new_status = tr_sharedTraversalStatus(s);

    if (new_status != old_status)
    {
        tr_logAddInfo(fmt::format(
            _("State changed from '{old_state}' to '{state}'"),
            fmt::arg("old_state", getNatStateStr(old_status)),
            fmt::arg("state", getNatStateStr(new_status))));
    }
}

static void restartTimer(tr_shared* s)
{
    auto& timer = s->timer;
    if (!timer)
    {
        return;
    }

    // when to wake up again
    switch (tr_sharedTraversalStatus(s))
    {
    case TR_PORT_MAPPED:
        // if we're mapped, everything is fine... check back at `renew_time`
        // to renew the port forwarding if it's expired
        s->doPortCheck = true;
        if (auto const now = tr_time(); s->natpmp->renew_time > now)
        {
            timer->startSingleShot(std::chrono::seconds{ s->natpmp->renew_time - now });
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
    TR_ASSERT(s->timer);

    /* do something */
    natPulse(s, s->doPortCheck);
    s->doPortCheck = false;

    /* set up the timer for the next pulse */
    restartTimer(s);
}

/***
****
***/

tr_shared* tr_sharedInit(tr_session* session)
{
    return new tr_shared{ session };
}

static void stop_timer(tr_shared* s)
{
    s->timer.reset();
}

static void stop_forwarding(tr_shared* s)
{
    tr_logAddTrace("stopped");
    natPulse(s, false);

    tr_natpmpClose(s->natpmp);
    s->natpmp = nullptr;
    s->natpmpStatus = TR_PORT_UNMAPPED;

    tr_upnpClose(s->upnp);
    s->upnp = nullptr;
    s->upnpStatus = TR_PORT_UNMAPPED;

    stop_timer(s);
}

void tr_sharedClose(tr_session* session)
{
    tr_shared* shared = session->shared;

    shared->isShuttingDown = true;
    stop_forwarding(shared);
    shared->session->shared = nullptr;
    delete shared;
}

static void start_timer(tr_shared* s)
{
    s->timer = s->session->timerMaker().create(onTimer, s);
    restartTimer(s);
}

void tr_sharedTraversalEnable(tr_shared* s, bool is_enable)
{
    if (is_enable)
    {
        s->isEnabled = true;
        start_timer(s);
    }
    else
    {
        s->isEnabled = false;
        stop_forwarding(s);
    }
}

void tr_sharedPortChanged(tr_session* session)
{
    tr_shared* s = session->shared;

    if (s->isEnabled)
    {
        stop_timer(s);
        natPulse(s, false);
        start_timer(s);
    }
}

bool tr_sharedTraversalIsEnabled(tr_shared const* s)
{
    return s->isEnabled;
}

int tr_sharedTraversalStatus(tr_shared const* s)
{
    return std::max(s->natpmpStatus, s->upnpStatus);
}
