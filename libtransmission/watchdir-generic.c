/*
 * This file Copyright (C) 2015-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>

#include <event2/event.h>

#define __LIBTRANSMISSION_WATCHDIR_MODULE__

#include "transmission.h"
#include "log.h"
#include "ptrarray.h"
#include "utils.h"
#include "watchdir.h"
#include "watchdir-common.h"

/***
****
***/

#define log_error(...) (!tr_logLevelIsActive (TR_LOG_ERROR) ? (void) 0 : \
  tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, "watchdir:generic", __VA_ARGS__))

/***
****
***/

typedef struct tr_watchdir_generic
{
  tr_watchdir_backend   base;

  struct event        * event;
  tr_ptrArray           dir_entries;
}
tr_watchdir_generic;

#define BACKEND_UPCAST(b) ((tr_watchdir_generic *) (b))

/* Non-static and mutable for unit tests */
struct timeval tr_watchdir_generic_interval = { 10, 0 };

/***
****
***/

static void
tr_watchdir_generic_on_event (evutil_socket_t   fd UNUSED,
                              short             type UNUSED,
                              void            * context)
{
  const tr_watchdir_t handle = context;
  tr_watchdir_generic * const backend = BACKEND_UPCAST (tr_watchdir_get_backend (handle));

  tr_watchdir_scan (handle, &backend->dir_entries);
}

static void
tr_watchdir_generic_free (tr_watchdir_backend * backend_base)
{
  tr_watchdir_generic * const backend = BACKEND_UPCAST (backend_base);

  if (backend == NULL)
    return;

  assert (backend->base.free_func == &tr_watchdir_generic_free);

  if (backend->event != NULL)
    {
      event_del (backend->event);
      event_free (backend->event);
    }

  tr_ptrArrayDestruct (&backend->dir_entries, &tr_free);

  tr_free (backend);
}

tr_watchdir_backend *
tr_watchdir_generic_new (tr_watchdir_t handle)
{
  tr_watchdir_generic * backend;

  backend = tr_new0 (tr_watchdir_generic, 1);
  backend->base.free_func = &tr_watchdir_generic_free;

  if ((backend->event = event_new (tr_watchdir_get_event_base (handle), -1, EV_PERSIST,
                                   &tr_watchdir_generic_on_event, handle)) == NULL)
    {
      log_error ("Failed to create event: %s", tr_strerror (errno));
      goto fail;
    }

  if (event_add (backend->event, &tr_watchdir_generic_interval) == -1)
    {
      log_error ("Failed to add event: %s", tr_strerror (errno));
      goto fail;
    }

  /* Run initial scan on startup */
  event_active (backend->event, EV_READ, 0);

  return BACKEND_DOWNCAST (backend);

fail:
  tr_watchdir_generic_free (BACKEND_DOWNCAST (backend));
  return NULL;
}
