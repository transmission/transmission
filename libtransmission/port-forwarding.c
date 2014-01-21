/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <stdio.h>

#include <sys/types.h>

#include <event2/event.h>

#include "transmission.h"
#include "natpmp_local.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h"
#include "port-forwarding.h"
#include "session.h"
#include "torrent.h"
#include "upnp.h"
#include "utils.h"

static const char *
getKey (void) { return _("Port Forwarding"); }

struct tr_shared
{
  bool isEnabled;
  bool isShuttingDown;
  bool doPortCheck;

  tr_port_forwarding natpmpStatus;
  tr_port_forwarding upnpStatus;

  tr_upnp * upnp;
  tr_natpmp * natpmp;
  tr_session * session;

  struct event * timer;
};

/***
****
***/

static const char*
getNatStateStr (int state)
{
  switch (state)
    {
      case TR_PORT_MAPPING:   return _("Starting");
      case TR_PORT_MAPPED:    return _("Forwarded");
      case TR_PORT_UNMAPPING: return _("Stopping");
      case TR_PORT_UNMAPPED:  return _("Not forwarded");
      default:                return "???";
    }
}

static void
natPulse (tr_shared * s, bool do_check)
{
  int oldStatus;
  int newStatus;
  tr_port public_peer_port;
  const tr_port private_peer_port = s->session->private_peer_port;
  const int is_enabled = s->isEnabled && !s->isShuttingDown;

  if (s->natpmp == NULL)
    s->natpmp = tr_natpmpInit ();

  if (s->upnp == NULL)
    s->upnp = tr_upnpInit ();

  oldStatus = tr_sharedTraversalStatus (s);

  s->natpmpStatus = tr_natpmpPulse (s->natpmp, private_peer_port, is_enabled, &public_peer_port);
  if (s->natpmpStatus == TR_PORT_MAPPED)
    s->session->public_peer_port = public_peer_port;

  s->upnpStatus = tr_upnpPulse (s->upnp, private_peer_port, is_enabled, do_check);

  newStatus = tr_sharedTraversalStatus (s);

  if (newStatus != oldStatus)
    tr_logAddNamedInfo (getKey (), _("State changed from \"%1$s\" to \"%2$s\""),
             getNatStateStr (oldStatus),
             getNatStateStr (newStatus));
}

static void
set_evtimer_from_status (tr_shared * s)
{
  int sec=0, msec=0;

  /* when to wake up again */
  switch (tr_sharedTraversalStatus (s))
    {
      case TR_PORT_MAPPED:
        /* if we're mapped, everything is fine... check back in 20 minutes
         * to renew the port forwarding if it's expired */
        s->doPortCheck = true;
        sec = 60 * 20;
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

  if (s->timer != NULL)
    tr_timerAdd (s->timer, sec, msec);
}

static void
onTimer (evutil_socket_t fd UNUSED, short what UNUSED, void * vshared)
{
  tr_shared * s = vshared;

  assert (s);
  assert (s->timer);

  /* do something */
  natPulse (s, s->doPortCheck);
  s->doPortCheck = false;

  /* set up the timer for the next pulse */
  set_evtimer_from_status (s);
}

/***
****
***/

tr_shared *
tr_sharedInit (tr_session  * session)
{
  tr_shared * s = tr_new0 (tr_shared, 1);

  s->session = session;
  s->isEnabled = false;
  s->upnpStatus = TR_PORT_UNMAPPED;
  s->natpmpStatus = TR_PORT_UNMAPPED;

#if 0
  if (isEnabled)
    {
      s->timer = tr_new0 (struct event, 1);
      evtimer_set (s->timer, onTimer, s);
      tr_timerAdd (s->timer, 0, 333000);
    }
#endif

  return s;
}

static void
stop_timer (tr_shared * s)
{
  if (s->timer != NULL)
    {
      event_free (s->timer);
      s->timer = NULL;
    }
}

static void
stop_forwarding (tr_shared * s)
{
  tr_logAddNamedInfo (getKey (), "%s", _("Stopped"));
  natPulse (s, false);

  tr_natpmpClose (s->natpmp);
  s->natpmp = NULL;
  s->natpmpStatus = TR_PORT_UNMAPPED;

  tr_upnpClose (s->upnp);
  s->upnp = NULL;
  s->upnpStatus = TR_PORT_UNMAPPED;

  stop_timer (s);
}

void
tr_sharedClose (tr_session * session)
{
  tr_shared * s = session->shared;

  s->isShuttingDown = true;
  stop_forwarding (s);
  s->session->shared = NULL;
  tr_free (s);
}

static void
start_timer (tr_shared * s)
{
  s->timer = evtimer_new (s->session->event_base, onTimer, s);
  set_evtimer_from_status (s);
}

void
tr_sharedTraversalEnable (tr_shared * s, bool isEnabled)
{
  if ((s->isEnabled = isEnabled))
    start_timer (s);
  else
    stop_forwarding (s);
}

void
tr_sharedPortChanged (tr_session * session)
{
  tr_shared * s = session->shared;

  if (s->isEnabled)
    {
      stop_timer (s);
      natPulse (s, false);
      start_timer (s);
    }
}

bool
tr_sharedTraversalIsEnabled (const tr_shared * s)
{
  return s->isEnabled;
}

int
tr_sharedTraversalStatus (const tr_shared * s)
{
  return MAX (s->natpmpStatus, s->upnpStatus);
}
