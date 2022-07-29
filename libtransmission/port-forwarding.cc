// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>

#include <sys/types.h>

#include <event2/event.h>

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
#include "utils.h"

struct tr_shared
{
    bool isEnabled;
    bool isShuttingDown;
    bool doPortCheck;

    tr_port_forwarding natpmpStatus;
    tr_port_forwarding upnpStatus;

    tr_upnp* upnp;
    tr_natpmp* natpmp;
    tr_session* session;

    struct event* timer;
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

static void set_evtimer_from_status(tr_shared* s)
{
    int sec = 0;
    int msec = 0;

    /* when to wake up again */
    switch (tr_sharedTraversalStatus(s))
    {
    case TR_PORT_MAPPED:
        /* if we're mapped, everything is fine... check back at renew_time
         * to renew the port forwarding if it's expired */
        s->doPortCheck = true;
        sec = std::max(0, int(s->natpmp->renew_time - tr_time()));
        break;

    case TR_PORT_ERROR:
        /* some kind of an error. wait 60 seconds and retry */
        sec = 60;
        break;

    default:
        /* in progress. pulse frequently. */
        msec = 333000;
        break;
    }

    if (s->timer != nullptr)
    {
        tr_timerAdd(*s->timer, sec, msec);
    }
}

static void onTimer(evutil_socket_t /*fd*/, short /*what*/, void* vshared)
{
    auto* s = static_cast<tr_shared*>(vshared);

    TR_ASSERT(s != nullptr);
    TR_ASSERT(s->timer != nullptr);

    /* do something */
    natPulse(s, s->doPortCheck);
    s->doPortCheck = false;

    /* set up the timer for the next pulse */
    set_evtimer_from_status(s);
}

/***
****
***/

tr_shared* tr_sharedInit(tr_session* session)
{
    auto* const s = tr_new0(tr_shared, 1);

    s->session = session;
    s->isEnabled = false;
    s->upnpStatus = TR_PORT_UNMAPPED;
    s->natpmpStatus = TR_PORT_UNMAPPED;

#if 0

    if (isEnabled)
    {
        s->timer = tr_new0(struct event, 1);
        evtimer_set(s->timer, onTimer, s);
        tr_timerAdd(s->timer, 0, 333000);
    }

#endif

    return s;
}

static void stop_timer(tr_shared* s)
{
    if (s->timer != nullptr)
    {
        event_free(s->timer);
        s->timer = nullptr;
    }
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
    tr_shared* s = session->shared;

    s->isShuttingDown = true;
    stop_forwarding(s);
    s->session->shared = nullptr;
    tr_free(s);
}

static void start_timer(tr_shared* s)
{
    s->timer = evtimer_new(s->session->event_base, onTimer, s);
    set_evtimer_from_status(s);
}

void tr_sharedTraversalEnable(tr_shared* s, bool enable)
{
    if (enable)
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
