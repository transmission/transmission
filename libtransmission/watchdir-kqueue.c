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
#include <string.h> /* strcmp () */

#include <fcntl.h> /* open () */
#include <unistd.h> /* close () */

#include <sys/types.h>
#include <sys/event.h>

#ifndef O_EVTONLY
 #define O_EVTONLY O_RDONLY
#endif

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
  tr_logAddMessage (__FILE__, __LINE__, TR_LOG_ERROR, "watchdir:kqueue", __VA_ARGS__))

/***
****
***/

typedef struct tr_watchdir_kqueue
{
  tr_watchdir_backend   base;

  int                   kq;
  int                   dirfd;
  struct event        * event;
  tr_ptrArray           dir_entries;
}
tr_watchdir_kqueue;

#define BACKEND_UPCAST(b) ((tr_watchdir_kqueue *) (b))

#define KQUEUE_WATCH_MASK (NOTE_WRITE | NOTE_EXTEND)

/***
****
***/

static void
tr_watchdir_kqueue_on_event (evutil_socket_t   fd UNUSED,
                             short             type UNUSED,
                             void            * context)
{
  const tr_watchdir_t handle = context;
  tr_watchdir_kqueue * const backend = BACKEND_UPCAST (tr_watchdir_get_backend (handle));
  struct kevent ke;
  const struct timespec ts = { 0, 0 };

  if (kevent (backend->kq, NULL, 0, &ke, 1, &ts) == -1)
    {
      log_error ("Failed to fetch kevent: %s", tr_strerror (errno));
      return;
    }

  /* Read directory with generic scan */
  tr_watchdir_scan (handle, &backend->dir_entries);
}

static void
tr_watchdir_kqueue_free (tr_watchdir_backend * backend_base)
{
  tr_watchdir_kqueue * const backend = BACKEND_UPCAST (backend_base);

  if (backend == NULL)
    return;

  assert (backend->base.free_func == &tr_watchdir_kqueue_free);

  if (backend->event != NULL)
    {
      event_del (backend->event);
      event_free (backend->event);
    }

  if (backend->kq != -1)
    close (backend->kq);
  if (backend->dirfd != -1)
    close (backend->dirfd);

  tr_ptrArrayDestruct (&backend->dir_entries, &tr_free);

  tr_free (backend);
}

tr_watchdir_backend *
tr_watchdir_kqueue_new (tr_watchdir_t handle)
{
  const char * const path = tr_watchdir_get_path (handle);
  struct kevent ke;
  tr_watchdir_kqueue * backend;

  backend = tr_new0 (tr_watchdir_kqueue, 1);
  backend->base.free_func = &tr_watchdir_kqueue_free;
  backend->kq = -1;
  backend->dirfd = -1;

  if ((backend->kq = kqueue ()) == -1)
    {
      log_error ("Failed to start kqueue");
      goto fail;
    }

  /* Open fd for watching */
  if ((backend->dirfd = open (path, O_RDONLY | O_EVTONLY)) == -1)
    {
      log_error ("Failed to passively watch directory \"%s\": %s", path,
                 tr_strerror (errno));
      goto fail;
    }

  /* Register kevent filter with kqueue descriptor */
  EV_SET (&ke, backend->dirfd, EVFILT_VNODE, EV_ADD | EV_ENABLE | EV_CLEAR,
          KQUEUE_WATCH_MASK, 0, NULL);
  if (kevent (backend->kq, &ke, 1, NULL, 0, NULL) == -1)
    {
      log_error ("Failed to set directory event filter with fd %d: %s", backend->kq,
                 tr_strerror (errno));
      goto fail;
    }

  /* Create libevent task for event descriptor */
  if ((backend->event = event_new (tr_watchdir_get_event_base (handle), backend->kq,
                                   EV_READ | EV_ET | EV_PERSIST,
                                   &tr_watchdir_kqueue_on_event, handle)) == NULL)
    {
      log_error ("Failed to create event: %s", tr_strerror (errno));
      goto fail;
    }

  if (event_add (backend->event, NULL) == -1)
    {
      log_error ("Failed to add event: %s", tr_strerror (errno));
      goto fail;
    }

  /* Trigger one event for the initial scan */
  event_active (backend->event, EV_READ, 0);

  return BACKEND_DOWNCAST (backend);

fail:
  tr_watchdir_kqueue_free (BACKEND_DOWNCAST (backend));
  return NULL;
}
